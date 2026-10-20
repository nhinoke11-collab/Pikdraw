#include "svg_path_parser.hpp"

#include <array>
#include <charconv>
#include <cctype>
#include <cmath>
#include <utility>

namespace pikdraw::interop {

bool ParseSvgNumber(std::string_view input, double& value, size_t& consumed) {
    if (input.empty()) return false;
    const auto result = std::from_chars(input.data(), input.data() + input.size(), value, std::chars_format::general);
    if (result.ec != std::errc{} || result.ptr == input.data() || !std::isfinite(value)) return false;
    consumed = static_cast<size_t>(result.ptr - input.data());
    return true;
}

namespace {
// Path parsing helpers remain private to this translation unit.
class NumberCursor final {
public:
    explicit NumberCursor(std::string_view source) : source_(source) {}
    bool Next(double& value) {
        SkipSeparators();
        if (position_ >= source_.size() || IsCommand(source_[position_])) return false;
        size_t consumed = 0;
        if (!ParseSvgNumber(source_.substr(position_), value, consumed)) return false;
        position_ += consumed;
        return true;
    }
    bool MoreNumbers() {
        SkipSeparators();
        return position_ < source_.size() && !IsCommand(source_[position_]);
    }
    bool Done() { SkipSeparators(); return position_ == source_.size(); }
    char TakeCommand() {
        SkipSeparators();
        return position_ < source_.size() && IsCommand(source_[position_]) ? source_[position_++] : 0;
    }
    bool IsCommandAhead() {
        SkipSeparators();
        return position_ < source_.size() && IsCommand(source_[position_]);
    }

private:
    static bool IsCommand(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; }
    void SkipSeparators() {
        while (position_ < source_.size() &&
               (std::isspace(static_cast<unsigned char>(source_[position_])) != 0 || source_[position_] == ',')) ++position_;
    }
    std::string_view source_;
    size_t position_{0};
};

enum class SvgPathVerb : uint8_t { Move, Line, Quad, Cubic, Arc, Close };

bool AddPathElement(geometry::Path2D& path, SvgPathVerb verb,
                    const std::array<double, 9>& values, uint8_t flags,
                    size_t maximum) {
    if (path.size() >= maximum) return false;
    switch (verb) {
        case SvgPathVerb::Move: path.move_to(values[0], values[1]); return true;
        case SvgPathVerb::Line: path.line_to(values[0], values[1]); return true;
        case SvgPathVerb::Quad: {
            path.quad_to(values[2], values[3], values[0], values[1]);
            return true;
        }
        case SvgPathVerb::Cubic: {
            path.cubic_to(values[2], values[3], values[4], values[5], values[0], values[1]);
            return true;
        }
        case SvgPathVerb::Close: path.close(); return true;
        case SvgPathVerb::Arc: {
            path.arc_to(values[2], values[3], values[4],
                        (flags & 1) != 0, (flags & 2) != 0,
                        values[0], values[1]);
            return true;
        }
    }
    return false;
}

} // namespace

bool ParseSvgNumberList(std::string_view input, std::span<double> values) {
    NumberCursor cursor(input);
    for (double& value : values) {
        if (!cursor.Next(value) || !std::isfinite(value)) return false;
    }
    return cursor.Done();
}

bool ParseSvgPath(std::string_view data, const geometry::Transform2D& transform,
               geometry::Path2D& output, size_t maximum) {
    // Build in source space first. Applying an affine map to each endpoint
    // alone breaks elliptical arcs under rotation, shear, or reflection.
    geometry::Path2D source_path;
    NumberCursor cursor(data);
    char command = 0;
    char previous = 0;
    double x = 0, y = 0, start_x = 0, start_y = 0, previous_cx = 0, previous_cy = 0;
    bool has_move = false;
    size_t elements = 0;
    while (!cursor.Done()) {
        if (cursor.IsCommandAhead()) command = cursor.TakeCommand();
        else if (command == 0) return false;
        const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(command)));
        const bool relative = std::islower(static_cast<unsigned char>(command)) != 0;
        if (upper == 'Z') {
            if (!has_move || !AddPathElement(source_path, SvgPathVerb::Close, {}, 0, maximum)) return false;
            x = start_x; y = start_y; previous = 'Z'; command = 0;
            continue;
        }
        if (upper != 'A' && upper != 'M' && upper != 'L' && upper != 'H' && upper != 'V' &&
            upper != 'C' && upper != 'S' && upper != 'Q' && upper != 'T') return false;
        size_t sets = 0;
        while (cursor.MoreNumbers()) {
            std::array<double, 9> values{};
            SvgPathVerb verb = SvgPathVerb::Line;
            uint8_t flags = 0;
            double nx = x, ny = y;
            if (upper == 'M' || upper == 'L' || upper == 'T') {
                if (!cursor.Next(nx) || !cursor.Next(ny)) return false;
                if (relative) { nx += x; ny += y; }
                values[0] = nx; values[1] = ny;
                if (upper == 'M' && sets == 0) {
                    verb = SvgPathVerb::Move; start_x = nx; start_y = ny; has_move = true;
                } else if (upper == 'T') {
                    const bool reflect = previous == 'Q' || previous == 'T';
                    const double cx = reflect ? 2 * x - previous_cx : x;
                    const double cy = reflect ? 2 * y - previous_cy : y;
                    values[2] = cx; values[3] = cy; values[0] = nx; values[1] = ny;
                    verb = SvgPathVerb::Quad; previous_cx = cx; previous_cy = cy;
                } else { verb = upper == 'M' && sets == 0 ? SvgPathVerb::Move : SvgPathVerb::Line; }
            } else if (upper == 'H') {
                if (!cursor.Next(nx)) return false;
                if (relative) nx += x;
                values[0] = nx; values[1] = y; verb = SvgPathVerb::Line;
            } else if (upper == 'V') {
                if (!cursor.Next(ny)) return false;
                if (relative) ny += y;
                values[0] = x; values[1] = ny; verb = SvgPathVerb::Line;
            } else if (upper == 'Q' || upper == 'T') {
                double cx = 0, cy = 0;
                if (!cursor.Next(cx) || !cursor.Next(cy) || !cursor.Next(nx) || !cursor.Next(ny)) return false;
                if (relative) { cx += x; cy += y; nx += x; ny += y; }
                values[0] = nx; values[1] = ny; values[2] = cx; values[3] = cy;
                verb = SvgPathVerb::Quad; previous_cx = cx; previous_cy = cy;
            } else if (upper == 'C' || upper == 'S') {
                double c1x = x, c1y = y, c2x = 0, c2y = 0;
                if (upper == 'C') {
                    if (!cursor.Next(c1x) || !cursor.Next(c1y) || !cursor.Next(c2x) || !cursor.Next(c2y) ||
                        !cursor.Next(nx) || !cursor.Next(ny)) return false;
                    if (relative) { c1x += x; c1y += y; c2x += x; c2y += y; nx += x; ny += y; }
                } else {
                    const bool reflect = previous == 'C' || previous == 'S';
                    c1x = reflect ? 2 * x - previous_cx : x;
                    c1y = reflect ? 2 * y - previous_cy : y;
                    if (!cursor.Next(c2x) || !cursor.Next(c2y) || !cursor.Next(nx) || !cursor.Next(ny)) return false;
                    if (relative) { c2x += x; c2y += y; nx += x; ny += y; }
                }
                values[0] = nx; values[1] = ny; values[2] = c1x; values[3] = c1y;
                values[4] = c2x; values[5] = c2y; verb = SvgPathVerb::Cubic;
                previous_cx = c2x; previous_cy = c2y;
            } else if (upper == 'A') {
                double rx = 0, ry = 0, rot = 0, large = 0, sweep = 0;
                if (!cursor.Next(rx) || !cursor.Next(ry) || !cursor.Next(rot) ||
                    !cursor.Next(large) || !cursor.Next(sweep) ||
                    !cursor.Next(nx) || !cursor.Next(ny)) return false;
                if (relative) { nx += x; ny += y; }
                values[0] = nx; values[1] = ny;
                values[2] = std::abs(rx); values[3] = std::abs(ry);
                values[4] = rot;
                flags = static_cast<uint8_t>((large != 0 ? 1 : 0) | (sweep != 0 ? 2 : 0));
                verb = SvgPathVerb::Arc;
            }
            for (double value : values) if (!std::isfinite(value) || std::abs(value) > 1.0e9) return false;
            if (!has_move && verb != SvgPathVerb::Move) return false;
            if (!AddPathElement(source_path, verb, values, flags, maximum)) return false;
            x = nx; y = ny; previous = upper; ++sets; ++elements;
            if (upper == 'M' && sets == 1) command = relative ? 'l' : 'L';
        }
        if (sets == 0) return false;
    }
    if (!has_move || elements == 0) return false;
    auto transformed = source_path.transformed_checked(transform);
    if (transformed.status != geometry::GeometryOperationStatus::Success) return false;
    output = std::move(transformed.path);
    return true;
}


} // namespace pikdraw::interop
