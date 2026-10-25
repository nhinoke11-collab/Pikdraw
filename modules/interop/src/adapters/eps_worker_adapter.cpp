#include "pikdraw/interop/adapters/eps_worker_adapter.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cctype>
#include <functional>
#include <limits>
#include <locale>
#include <iterator>
#include <map>
#include <sstream>
#include <string_view>
#include <utility>

namespace pikdraw::interop {
namespace {

constexpr double kPointsToMillimetres = 25.4 / 72.0;
constexpr size_t kMaxTokens = 1'000'000;
constexpr size_t kMaxPathElements = 4000;
constexpr size_t kMaxOperators = 500'000;
constexpr size_t kMaxGraphicsDepth = 32;
constexpr size_t kMaxAliases = 256;

enum class TokenKind { Word, LiteralName, LeftBrace, RightBrace, String };

struct Token {
  TokenKind kind{TokenKind::Word};
  std::string_view text;
  double number{0.0};
  bool is_number{false};
};

struct Bounds {
  double left{0.0};
  double bottom{0.0};
  double right{0.0};
  double top{0.0};
};

struct Point {
  double x{0.0};
  double y{0.0};
};

struct Matrix {
  double a{1.0}, b{0.0}, c{0.0}, d{1.0}, e{0.0}, f{0.0};
};

struct Color {
  ImportColorRecord value;
};

struct PathState {
  std::vector<ImportPathElementRecord> elements;
  Point current{};
  Point subpath_start{};
  bool has_current{false};
  bool has_subpath{false};
};

struct GraphicsState {
  Matrix ctm{};
  Color color{};
  double line_width_pt{1.0};
  int line_cap{0};
  int line_join{0};
  double miter_limit{10.0};
  PathState path{};
};

bool Finite(double value) noexcept { return std::isfinite(value); }

uint32_t ReadU32LittleEndian(std::span<const uint8_t> bytes, size_t offset) noexcept {
  return static_cast<uint32_t>(bytes[offset]) |
      (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
      (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
      (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}


bool ParseNumber(std::string_view text, double& value) noexcept {
  if (text.empty()) return false;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value,
                                      std::chars_format::general);
  return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() &&
         Finite(value);
}

bool ParseBounds(std::string_view line, Bounds& bounds) {
  const size_t colon = line.find(':');
  if (colon == std::string_view::npos) return false;
  std::istringstream input{std::string(line.substr(colon + 1))};
  input.imbue(std::locale::classic());
  if (!(input >> bounds.left >> bounds.bottom >> bounds.right >> bounds.top)) return false;
  return Finite(bounds.left) && Finite(bounds.bottom) && Finite(bounds.right) &&
         Finite(bounds.top) && bounds.right > bounds.left && bounds.top > bounds.bottom;
}

bool ReadEpsBounds(std::string_view source, Bounds& bounds, std::string& diagnostic) {
  bool have_box = false;
  bool have_hires = false;
  Bounds box{};
  Bounds hires{};
  size_t offset = 0;
  while (offset < source.size()) {
    size_t end = source.find('\n', offset);
    if (end == std::string_view::npos) end = source.size();
    std::string_view line = source.substr(offset, end - offset);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (line.starts_with("%%BeginBinary") || line.starts_with("%%BeginData")) {
      diagnostic = "Binary EPS/PostScript data is outside eps-path-v1";
      return false;
    }
    if (line.starts_with("%%HiResBoundingBox:")) {
      Bounds parsed{};
      if (ParseBounds(line, parsed)) {
        hires = parsed;
        have_hires = true;
      }
    } else if (line.starts_with("%%BoundingBox:")) {
      Bounds parsed{};
      if (ParseBounds(line, parsed)) {
        box = parsed;
        have_box = true;
      }
    }
    offset = end + (end < source.size() ? 1 : 0);
  }
  if (have_hires) bounds = hires;
  else if (have_box) bounds = box;
  else {
    diagnostic = "EPS is missing a numeric BoundingBox or HiResBoundingBox";
    return false;
  }
  const double width = (bounds.right - bounds.left) * kPointsToMillimetres;
  const double height = (bounds.top - bounds.bottom) * kPointsToMillimetres;
  if (!(width > 0.0 && height > 0.0) || width > 100000.0 || height > 100000.0) {
    diagnostic = "EPS page bounds are outside the supported range";
    return false;
  }
  return true;
}

bool IsDelimiter(unsigned char value) noexcept {
  return std::isspace(value) != 0 || value == '%' || value == '{' || value == '}' ||
         value == '(' || value == ')' || value == '[' || value == ']' ||
         value == '<' || value == '>';
}

bool Lex(std::string_view source, std::vector<Token>& tokens, std::string& diagnostic) {
  tokens.clear();
  size_t offset = 0;
  while (offset < source.size()) {
    const unsigned char current = static_cast<unsigned char>(source[offset]);
    if (std::isspace(current) != 0) { ++offset; continue; }
    if (current == '%') {
      const size_t end = source.find('\n', offset);
      offset = end == std::string_view::npos ? source.size() : end + 1;
      continue;
    }
    if (current == '\0' || current >= 0x80) {
      diagnostic = "Binary or non-ASCII executable EPS data is outside eps-path-v1";
      return false;
    }
    Token token;
    if (current == '{' || current == '}') {
      token.kind = current == '{' ? TokenKind::LeftBrace : TokenKind::RightBrace;
      token.text = source.substr(offset, 1);
      ++offset;
    } else if (current == '(') {
      token.kind = TokenKind::String;
      size_t depth = 1;
      ++offset;
      while (offset < source.size() && depth != 0) {
        const char value = source[offset++];
        if (value == '\\' && offset < source.size()) { ++offset; continue; }
        if (value == '(') ++depth;
        else if (value == ')') --depth;
        if (depth > 64) {
          diagnostic = "EPS string nesting exceeds the bounded parser profile";
          return false;
        }
      }
      if (depth != 0) {
        diagnostic = "EPS contains an unterminated string";
        return false;
      }
      token.text = "<text>";
    } else if (current == '[' || current == ']' || current == '<' || current == '>') {
      diagnostic = "EPS arrays, dictionaries, and encoded strings are outside eps-path-v1";
      return false;
    } else {
      const size_t start = offset;
      if (current == '/') {
        token.kind = TokenKind::LiteralName;
        ++offset;
      }
      while (offset < source.size() &&
             !IsDelimiter(static_cast<unsigned char>(source[offset]))) ++offset;
      if (offset == start || (token.kind == TokenKind::LiteralName && offset == start + 1)) {
        diagnostic = "EPS contains an empty PostScript name";
        return false;
      }
      const size_t name_start = token.kind == TokenKind::LiteralName ? start + 1 : start;
      token.text = source.substr(name_start, offset - name_start);
      if (token.kind == TokenKind::Word) token.is_number = ParseNumber(token.text, token.number);
    }
    if (token.text.size() > 1024 * 1024 || tokens.size() >= kMaxTokens) {
      diagnostic = "EPS exceeds the bounded token profile";
      return false;
    }
    tokens.push_back(std::move(token));
  }
  return true;
}

Matrix Multiply(const Matrix& left, const Matrix& right) noexcept {
  return {
      left.a * right.a + left.c * right.b,
      left.b * right.a + left.d * right.b,
      left.a * right.c + left.c * right.d,
      left.b * right.c + left.d * right.d,
      left.a * right.e + left.c * right.f + left.e,
      left.b * right.e + left.d * right.f + left.f};
}

Point Transform(const Matrix& matrix, Point point) noexcept {
  return {matrix.a * point.x + matrix.c * point.y + matrix.e,
          matrix.b * point.x + matrix.d * point.y + matrix.f};
}

bool IsUniformScale(const Matrix& matrix, double& scale) noexcept {
  const double first = matrix.a * matrix.a + matrix.b * matrix.b;
  const double second = matrix.c * matrix.c + matrix.d * matrix.d;
  const double dot = matrix.a * matrix.c + matrix.b * matrix.d;
  const double magnitude = (std::max)({first, second, 1.0});
  if (std::abs(first - second) > magnitude * 1e-8 ||
      std::abs(dot) > magnitude * 1e-8 || first <= 1e-16) return false;
  scale = std::sqrt(first);
  return Finite(scale);
}

class Parser {
 public:
  Parser(std::span<const uint8_t> bytes, std::vector<CanonicalImportRecord>& records,
         std::string& diagnostic)
      : bytes_(bytes), records_(records), diagnostic_(diagnostic) {
    state_.color.value.model = ImportColorModel::Gray;
    state_.color.value.components = {0.0, 1.0, 0.0, 0.0, 0.0};
  }

  bool Run() {
    if (bytes_.empty()) {
      diagnostic_ = "EPS source is empty";
      return false;
    }
    const std::string_view source(reinterpret_cast<const char*>(bytes_.data()), bytes_.size());
    if (!source.starts_with("%!PS-Adobe") || source.substr(0, 256).find("EPSF-") == std::string_view::npos) {
      diagnostic_ = "The source is not a DSC EPSF PostScript document";
      return false;
    }
    if (!ReadEpsBounds(source, bounds_, diagnostic_) || !Lex(source, tokens_, diagnostic_)) return false;
    records_.clear();
    const auto fail = [&](std::string message) {
      records_.clear();
      diagnostic_ = std::move(message);
      return false;
    };
    for (size_t index = 0; index < tokens_.size(); ++index) {
      const Token& token = tokens_[index];
      if (token.kind == TokenKind::String)
        return fail("EPS text strings and text operators are outside eps-path-v1");
      if (token.kind == TokenKind::LiteralName) {
        if (!DefineAlias(index)) return fail("EPS procedure definitions are outside the safe one-operator alias profile");
        continue;
      }
      if (token.kind != TokenKind::Word) return fail("EPS contains an unbalanced procedure body");
      if (token.is_number) {
        if (operand_stack_.size() >= 64) return fail("EPS operand stack exceeds the bounded profile");
        operand_stack_.push_back(token.number);
        continue;
      }
      if (++operator_count_ > kMaxOperators) return fail("EPS operator count exceeds the bounded profile");
      std::string_view op = token.text;
      if (const auto alias = aliases_.find(op); alias != aliases_.end()) op = alias->second;
      if (!Execute(op, index)) {
        if (!diagnostic_.empty()) return fail(std::exchange(diagnostic_, {}));
        std::string message = "EPS contains an operator outside the safe editable path profile: ";
        message.append(op.data(), op.size());
        return fail(std::move(message));
      }
    }
    if (!graphics_stack_.empty()) return fail("EPS has an unmatched gsave operator");
    if (!operand_stack_.empty()) return fail("EPS leaves unused values on the PostScript operand stack");
    if (!state_.path.elements.empty()) return fail("EPS ends with an unpainted path outside eps-path-v1");
    if (path_count_ == 0) return fail("EPS contains no editable path objects in eps-path-v1");
    return Finish();
  }

 private:
  bool DefineAlias(size_t& index) {
    if (index + 4 >= tokens_.size() || tokens_[index + 1].kind != TokenKind::LeftBrace ||
        tokens_[index + 2].kind != TokenKind::Word || tokens_[index + 2].is_number ||
        tokens_[index + 3].kind != TokenKind::RightBrace) return false;
    const std::string_view alias = tokens_[index].text;
    const std::string_view command = tokens_[index + 2].text;
    if (!IsSafeOperator(command) || aliases_.size() >= kMaxAliases) return false;
    size_t end = index + 4;
    if (tokens_[end].kind == TokenKind::Word && tokens_[end].text == "bind") ++end;
    if (end >= tokens_.size() || tokens_[end].kind != TokenKind::Word || tokens_[end].text != "def")
      return false;
    aliases_[alias] = command;
    index = end;
    return true;
  }

  static bool IsSafeOperator(std::string_view op) noexcept {
    static constexpr std::string_view operators[]{
        "newpath", "moveto", "lineto", "curveto", "closepath", "fill", "eofill",
        "stroke", "setrgbcolor", "setgray", "setcmykcolor", "setlinewidth",
        "setlinecap", "setlinejoin", "setmiterlimit", "gsave", "grestore",
        "translate", "scale", "rotate", "showpage", "pop", "dup", "exch",
        "add", "sub", "mul", "div", "neg", "rmoveto", "rlineto", "re",
        "arc", "arcn", "v", "y", "rectfill", "rectstroke", "m", "l", "c",
        "h", "f", "f*", "S", "rg", "RG", "k", "K", "w", "J", "j",
        "q", "Q", "g"};
    return std::find(std::begin(operators), std::end(operators), op) != std::end(operators);
  }

  bool Pop(double& value) {
    if (operand_stack_.empty()) return false;
    value = operand_stack_.back();
    operand_stack_.pop_back();
    return Finite(value);
  }

  bool PopArguments(std::span<double> values) {
    for (size_t index = values.size(); index > 0; --index) {
      if (!Pop(values[index - 1])) return false;
    }
    return true;
  }

  bool ToPage(Point user, Point& page) const noexcept {
    const Point transformed = Transform(state_.ctm, user);
    page = {(transformed.x - bounds_.left) * kPointsToMillimetres,
            (bounds_.top - transformed.y) * kPointsToMillimetres};
    return Finite(page.x) && Finite(page.y) && std::abs(page.x) <= 1e7 && std::abs(page.y) <= 1e7;
  }

  bool Append(ImportPathVerb verb, Point end, Point control1 = {}, Point control2 = {}) {
    if (state_.path.elements.size() >= kMaxPathElements) return false;
    Point page_end{};
    if (!ToPage(end, page_end)) return false;
    ImportPathElementRecord element;
    element.verb = verb;
    element.values[0] = page_end.x;
    element.values[1] = page_end.y;
    if (verb == ImportPathVerb::Cubic) {
      Point page_control1{}, page_control2{};
      if (!ToPage(control1, page_control1) || !ToPage(control2, page_control2)) return false;
      element.values[2] = page_control1.x;
      element.values[3] = page_control1.y;
      element.values[4] = page_control2.x;
      element.values[5] = page_control2.y;
    }
    state_.path.elements.push_back(element);
    return true;
  }

  bool MoveTo(Point point) {
    if (!Append(ImportPathVerb::Move, point)) return false;
    state_.path.current = point;
    state_.path.subpath_start = point;
    state_.path.has_current = true;
    state_.path.has_subpath = true;
    return true;
  }

  bool LineTo(Point point) {
    if (!state_.path.has_current || !Append(ImportPathVerb::Line, point)) return false;
    state_.path.current = point;
    return true;
  }

  bool CurveTo(Point first, Point second, Point end) {
    if (!state_.path.has_current || !Append(ImportPathVerb::Cubic, end, first, second)) return false;
    state_.path.current = end;
    return true;
  }

  bool AddRectangle(double x, double y, double width, double height) {
    return MoveTo({x, y}) && LineTo({x + width, y}) && LineTo({x + width, y + height}) &&
           LineTo({x, y + height}) && ClosePath();
  }

  bool ClosePath() {
    if (!state_.path.has_subpath || state_.path.elements.size() >= kMaxPathElements) return false;
    ImportPathElementRecord element;
    element.verb = ImportPathVerb::Close;
    state_.path.elements.push_back(element);
    state_.path.current = state_.path.subpath_start;
    state_.path.has_current = true;
    return true;
  }

  bool AddIssue(std::string_view code, std::string_view source_id) {
    ImportFidelityIssueRecord issue;
    issue.severity = ImportFidelitySeverity::Warning;
    issue.source_locator.assign(source_id);
    issue.feature_code.assign(code);
    issue.diagnostic_code = "eps-editable-path-subset-loss";
    return AddRecord(issue);
  }

  bool Paint(bool fill, bool even_odd) {
    if (state_.path.elements.empty()) {
      state_.path = {};
      return true;
    }
    if (path_count_ >= kEpsImportMaxObjects) return false;
    ImportPathRecord path;
    path.local_id = static_cast<uint32_t>(path_count_ + 1);
    path.source_id = "eps-path-" + std::to_string(path_count_ + 1);
    path.name = "EPS Path " + std::to_string(path_count_ + 1);
    path.winding_rule = even_odd ? 1 : 0;
    path.has_fill = fill;
    path.has_stroke = !fill;
    path.fill = state_.color.value;
    path.stroke = state_.color.value;
    path.elements = state_.path.elements;
    if (!fill) {
      double scale = 0.0;
      if (!IsUniformScale(state_.ctm, scale)) {
        diagnostic_ = "EPS uses a non-uniformly transformed stroke outside eps-path-v1";
        return false;
      }
      path.stroke_width_mm = state_.line_width_pt * scale * kPointsToMillimetres;
      if (!Finite(path.stroke_width_mm) || path.stroke_width_mm <= 0.0) return false;
    }
    if (!AddRecord(path)) return false;
    layer_object_ids_.push_back(path.local_id);
    ++path_count_;
    state_.path = {};
    return true;
  }

  bool AddArc(double cx, double cy, double radius, double start_degrees,
              double end_degrees, bool counter_clockwise) {
    if (!Finite(radius) || radius <= 0.0) return false;
    double delta_degrees = end_degrees - start_degrees;
    if (std::abs(delta_degrees) > 360.0) return false;
    if (counter_clockwise) {
      while (delta_degrees < 0.0) delta_degrees += 360.0;
    } else {
      while (delta_degrees > 0.0) delta_degrees -= 360.0;
    }
    if (std::abs(delta_degrees) > 360.0) return false;
    const double start = start_degrees * (std::acos(-1.0) / 180.0);
    const double delta = delta_degrees * (std::acos(-1.0) / 180.0);
    const size_t pieces = static_cast<size_t>(std::max(1.0, std::ceil(std::abs(delta) / (std::acos(-1.0) / 2.0))));
    const Point first{cx + radius * std::cos(start), cy + radius * std::sin(start)};
    if (!state_.path.has_current) {
      if (!MoveTo(first)) return false;
    } else if (std::hypot(state_.path.current.x - first.x, state_.path.current.y - first.y) > 1e-9) {
      if (!LineTo(first)) return false;
    }
    for (size_t index = 0; index < pieces; ++index) {
      const double a0 = start + delta * static_cast<double>(index) / static_cast<double>(pieces);
      const double a1 = start + delta * static_cast<double>(index + 1) / static_cast<double>(pieces);
      const double factor = 4.0 / 3.0 * std::tan((a1 - a0) / 4.0);
      const Point p0{cx + radius * std::cos(a0), cy + radius * std::sin(a0)};
      const Point p3{cx + radius * std::cos(a1), cy + radius * std::sin(a1)};
      const Point p1{p0.x - radius * factor * std::sin(a0), p0.y + radius * factor * std::cos(a0)};
      const Point p2{p3.x + radius * factor * std::sin(a1), p3.y - radius * factor * std::cos(a1)};
      if (!CurveTo(p1, p2, p3)) return false;
    }
    return true;
  }

  bool Execute(std::string_view op, size_t) {
    if (op == "newpath") { state_.path = {}; return true; }
    if (op == "moveto" || op == "m") {
      std::array<double, 2> args{};
      return PopArguments(args) && MoveTo({args[0], args[1]});
    }
    if (op == "lineto" || op == "l") {
      std::array<double, 2> args{};
      return PopArguments(args) && LineTo({args[0], args[1]});
    }
    if (op == "rmoveto" || op == "rlineto") {
      std::array<double, 2> args{};
      if (!PopArguments(args) || !state_.path.has_current) return false;
      const Point point{state_.path.current.x + args[0], state_.path.current.y + args[1]};
      return op == "rmoveto" ? MoveTo(point) : LineTo(point);
    }
    if (op == "curveto" || op == "c") {
      std::array<double, 6> args{};
      return PopArguments(args) && CurveTo({args[0], args[1]}, {args[2], args[3]},
                                           {args[4], args[5]});
    }
    if (op == "v" || op == "y") {
      std::array<double, 4> args{};
      if (!PopArguments(args) || !state_.path.has_current) return false;
      if (op == "v") return CurveTo(state_.path.current, {args[0], args[1]}, {args[2], args[3]});
      return CurveTo({args[0], args[1]}, {args[2], args[3]}, {args[2], args[3]});
    }
    if (op == "closepath" || op == "h") return ClosePath();
    if (op == "re" || op == "rectfill" || op == "rectstroke") {
      std::array<double, 4> args{};
      if (!PopArguments(args)) return false;
      if (op != "re") state_.path = {};
      if (!AddRectangle(args[0], args[1], args[2], args[3])) return false;
      return op == "rectfill" ? Paint(true, false) : op == "rectstroke" ? Paint(false, false) : true;
    }
    if (op == "arc" || op == "arcn") {
      std::array<double, 5> args{};
      return PopArguments(args) && AddArc(args[0], args[1], args[2], args[3], args[4], op == "arc");
    }
    if (op == "fill" || op == "f" || op == "eofill" || op == "f*")
      return Paint(true, op == "eofill" || op == "f*");
    if (op == "stroke" || op == "S") return Paint(false, false);
    if (op == "setgray" || op == "g") {
      double gray = 0.0;
      if (!Pop(gray) || gray < 0.0 || gray > 1.0) return false;
      state_.color.value = {};
      state_.color.value.model = ImportColorModel::Gray;
      state_.color.value.components = {gray, 1.0, 0.0, 0.0, 0.0};
      return true;
    }
    if (op == "setrgbcolor" || op == "rg" || op == "RG") {
      std::array<double, 3> args{};
      if (!PopArguments(args) || std::any_of(args.begin(), args.end(), [](double c) { return c < 0 || c > 1; })) return false;
      state_.color.value = {};
      state_.color.value.model = ImportColorModel::Rgb;
      state_.color.value.components = {args[0], args[1], args[2], 1.0, 0.0};
      return true;
    }
    if (op == "setcmykcolor" || op == "k" || op == "K") {
      std::array<double, 4> args{};
      if (!PopArguments(args) || std::any_of(args.begin(), args.end(), [](double c) { return c < 0 || c > 1; })) return false;
      state_.color.value = {};
      state_.color.value.model = ImportColorModel::Cmyk;
      state_.color.value.components = {args[0], args[1], args[2], args[3], 1.0};
      return true;
    }
    if (op == "setlinewidth" || op == "w") {
      return Pop(state_.line_width_pt) && state_.line_width_pt > 0.0 && state_.line_width_pt <= 10000.0;
    }
    if (op == "setlinecap" || op == "J") {
      double value = 0.0;
      if (!Pop(value) || value < 0 || value > 2 || std::floor(value) != value) return false;
      state_.line_cap = static_cast<int>(value);
      if (state_.line_cap != 0) style_warning_ = true;
      return true;
    }
    if (op == "setlinejoin" || op == "j") {
      double value = 0.0;
      if (!Pop(value) || value < 0 || value > 2 || std::floor(value) != value) return false;
      state_.line_join = static_cast<int>(value);
      if (state_.line_join != 0) style_warning_ = true;
      return true;
    }
    if (op == "setmiterlimit") {
      if (!Pop(state_.miter_limit) || state_.miter_limit < 1.0 || state_.miter_limit > 1000.0) return false;
      if (state_.miter_limit != 10.0) style_warning_ = true;
      return true;
    }
    if (op == "gsave" || op == "q") {
      if (graphics_stack_.size() >= kMaxGraphicsDepth) return false;
      graphics_stack_.push_back(state_);
      return true;
    }
    if (op == "grestore" || op == "Q") {
      if (graphics_stack_.empty()) return false;
      state_ = std::move(graphics_stack_.back());
      graphics_stack_.pop_back();
      return true;
    }
    if (op == "translate") {
      std::array<double, 2> args{};
      if (!PopArguments(args)) return false;
      state_.ctm = Multiply(state_.ctm, Matrix{1, 0, 0, 1, args[0], args[1]});
      return true;
    }
    if (op == "scale") {
      std::array<double, 2> args{};
      if (!PopArguments(args) || args[0] == 0 || args[1] == 0) return false;
      state_.ctm = Multiply(state_.ctm, Matrix{args[0], 0, 0, args[1], 0, 0});
      return true;
    }
    if (op == "rotate") {
      double degrees = 0.0;
      if (!Pop(degrees) || std::abs(degrees) > 36000.0) return false;
      const double radians = degrees * (std::acos(-1.0) / 180.0);
      const double cosine = std::cos(radians), sine = std::sin(radians);
      state_.ctm = Multiply(state_.ctm, Matrix{cosine, sine, -sine, cosine, 0, 0});
      return true;
    }
    if (op == "pop") {
      if (operand_stack_.empty()) return false;
      operand_stack_.pop_back();
      return true;
    }
    if (op == "dup") {
      if (operand_stack_.empty() || operand_stack_.size() >= 64) return false;
      operand_stack_.push_back(operand_stack_.back());
      return true;
    }
    if (op == "exch") {
      if (operand_stack_.size() < 2) return false;
      std::iter_swap(operand_stack_.end() - 1, operand_stack_.end() - 2);
      return true;
    }
    if (op == "neg") {
      if (operand_stack_.empty()) return false;
      operand_stack_.back() = -operand_stack_.back();
      return true;
    }
    if (op == "add" || op == "sub" || op == "mul" || op == "div") {
      std::array<double, 2> args{};
      if (!PopArguments(args)) return false;
      double result = 0.0;
      if (op == "add") result = args[0] + args[1];
      else if (op == "sub") result = args[0] - args[1];
      else if (op == "mul") result = args[0] * args[1];
      else {
        if (args[1] == 0.0) return false;
        result = args[0] / args[1];
      }
      if (!Finite(result) || operand_stack_.size() >= 64) return false;
      operand_stack_.push_back(result);
      return true;
    }
    if (op == "showpage") return true;
    return false;
  }

  bool AddRecord(const ImportRecordValue& value) {
    auto record = SerializeImportRecord(value);
    if (!record || records_.size() >= kEpsImportMaxObjects * 2 + 16) return false;
    records_.push_back(std::move(*record));
    return true;
  }

  bool Finish() {
    if (path_count_ == 0 || layer_object_ids_.empty()) return false;
    const double width = (bounds_.right - bounds_.left) * kPointsToMillimetres;
    const double height = (bounds_.top - bounds_.bottom) * kPointsToMillimetres;
    std::vector<CanonicalImportRecord> output;
    const auto append = [&](const ImportRecordValue& value) {
      auto record = SerializeImportRecord(value);
      if (!record) return false;
      output.push_back(std::move(*record));
      return true;
    };
    if (!append(ImportPageRecord{width, height})) return false;
    ImportLayerRecord layer;
    layer.local_id = 1;
    layer.source_id = "eps-layer-1";
    layer.name = "EPS Artwork";
    layer.object_local_ids = layer_object_ids_;
    if (!append(layer)) return false;
    // Path records have already been serialized. Reorder them after the page and layer records.
    for (auto& record : records_) {
      if (record.type == ImportRecordType::Path) output.push_back(std::move(record));
    }
    uint32_t issues = 0;
    if (!AddIssue("eps-source-layers-and-live-effects-not-present-in-profile", layer.source_id)) return false;
    ++issues;
    if (!AddIssue("eps-render-and-print-appearance-not-verified", layer.source_id)) return false;
    ++issues;
    if (style_warning_) {
      if (!AddIssue("eps-stroke-cap-join-or-miter-style-approximated", layer.source_id)) return false;
      ++issues;
    }
    // AddIssue appends into records_; append only the issues, after all native objects.
    size_t first_issue = 0;
    for (const auto& record : records_) {
      if (record.type == ImportRecordType::FidelityIssue) {
        if (first_issue++ >= issues) break;
        output.push_back(record);
      }
    }
    ImportFidelityReportRecord report;
    report.coverage_profile = "eps-path-v1";
    report.parse_outcome = ImportParseOutcome::CompleteWithDiagnostics;
    report.coverage = ImportCoverage::Partial;
    report.appearance = ImportAppearance::Partial;
    report.editability = ImportEditability::NativeEditable;
    report.production = ImportProduction::NotAssessed;
    report.issue_count = issues;
    auto report_record = SerializeImportRecord(report);
    if (!report_record) return false;
    output.push_back(std::move(*report_record));
    records_ = std::move(output);
    diagnostic_.clear();
    return true;
  }

  std::span<const uint8_t> bytes_;
  std::vector<CanonicalImportRecord>& records_;
  std::string& diagnostic_;
  std::vector<Token> tokens_;
  Bounds bounds_{};
  GraphicsState state_{};
  std::vector<GraphicsState> graphics_stack_;
  std::vector<double> operand_stack_;
  std::map<std::string_view, std::string_view, std::less<>> aliases_;
  std::vector<uint32_t> layer_object_ids_;
  size_t operator_count_{0};
  size_t path_count_{0};
  bool style_warning_{false};
};

} // namespace

bool BuildEpsImportRecords(std::span<const uint8_t> source,
                           std::vector<CanonicalImportRecord>& records,
                           std::string& diagnostic) {
  records.clear();
  diagnostic.clear();
  try {
    constexpr std::array<uint8_t, 4> kDosEpsMagic{0xC5, 0xD0, 0xD3, 0xC6};
    const bool dos_eps_wrapper = source.size() >= 30 &&
        std::equal(kDosEpsMagic.begin(), kDosEpsMagic.end(), source.begin());
    if (dos_eps_wrapper) {
      const uint32_t postscript_offset = ReadU32LittleEndian(source, 4);
      const uint32_t postscript_length = ReadU32LittleEndian(source, 8);
      if (postscript_offset < 30 || postscript_length == 0 ||
          postscript_offset > source.size() ||
          postscript_length > source.size() - postscript_offset) {
        diagnostic = "DOS EPS wrapper has an invalid PostScript section range";
        return false;
      }
      source = source.subspan(postscript_offset, postscript_length);
    }
    if (Parser(source, records, diagnostic).Run()) return true;
    return false;
  } catch (...) {
    records.clear();
    diagnostic = "EPS editable path conversion failed within the bounded worker";
    return false;
  }
}

} // namespace pikdraw::interop
