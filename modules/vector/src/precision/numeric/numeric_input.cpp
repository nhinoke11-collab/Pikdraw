#include "pikdraw/vector/precision/numeric/numeric_input.hpp"

#include <charconv>
#include <cmath>
#include <string>
#include <system_error>
#include <utility>

namespace pikdraw::vector::precision::numeric {
namespace {

constexpr std::size_t max_tokens = 128;
constexpr unsigned max_expression_depth = 32;
constexpr double pi = 3.141592653589793238462643383279502884;
constexpr double radians_to_degrees = 180.0 / pi;

template <typename T>
ParseResult<T> failure(ErrorCode code, std::size_t offset) {
    ParseResult<T> result;
    result.diagnostic = {code, offset};
    return result;
}

template <typename T>
ParseResult<T> success(T value) {
    ParseResult<T> result;
    result.value = std::move(value);
    return result;
}

bool is_space(char value) {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

bool is_digit(char value) {
    return value >= '0' && value <= '9';
}

bool is_alpha(char value) {
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
}

char ascii_lower(char value) {
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
}

bool equals_ascii_case_insensitive(std::string_view actual, std::string_view expected) {
    if (actual.size() != expected.size())
        return false;
    for (std::size_t index = 0; index < actual.size(); ++index) {
        if (ascii_lower(actual[index]) != expected[index])
            return false;
    }
    return true;
}

bool is_non_finite_name(std::string_view name) {
    return equals_ascii_case_insensitive(name, "nan") ||
           equals_ascii_case_insensitive(name, "inf") ||
           equals_ascii_case_insensitive(name, "infinity");
}

ErrorCode result_error(double value) {
    return std::isnan(value) ? ErrorCode::NonFiniteValue : ErrorCode::Overflow;
}

Diagnostic validate_input(std::string_view input, ParseOptions options) {
    if (options.decimal_separator != '.' && options.decimal_separator != ',')
        return {ErrorCode::InvalidLocale, 0};
    if (input.size() > max_input_bytes)
        return {ErrorCode::InputTooLong, max_input_bytes};
    for (char value : input) {
        if (!is_space(value))
            return {};
    }
    return {ErrorCode::EmptyInput, 0};
}

class ExpressionParser {
  public:
    ExpressionParser(std::string_view input, ParseOptions options) : input_(input), options_(options) {}

    ParseResult<Quantity> parse_quantity() {
        skip_spaces();
        auto value = parse_expression(0);
        if (!value)
            return failure<Quantity>(diagnostic_.code, diagnostic_.offset);
        skip_spaces();
        if (!at_end())
            return failure<Quantity>(ErrorCode::InvalidSyntax, position_);
        return success(*value);
    }

    ParseResult<ParsedPointInput> parse_point() {
        skip_spaces();
        if (at_end())
            return failure<ParsedPointInput>(ErrorCode::EmptyInput, position_);
        const char marker = input_[position_];
        if (marker != '@' && marker != '#')
            return failure<ParsedPointInput>(ErrorCode::InvalidSyntax, position_);
        ++position_;
        skip_spaces();

        const std::size_t first_offset = position_;
        auto first = parse_expression(0);
        if (!first)
            return failure<ParsedPointInput>(diagnostic_.code, diagnostic_.offset);
        skip_spaces();

        ParsedPointInput result;
        result.first_offset = first_offset;
        if (marker == '@' && !at_end() && input_[position_] == '<') {
            if (!coerce(first, Dimension::Length, first_offset))
                return failure<ParsedPointInput>(diagnostic_.code, diagnostic_.offset);
            if (first->value < 0.0)
                return failure<ParsedPointInput>(ErrorCode::NegativePolarLength, first_offset);
            ++position_;
            skip_spaces();
            result.kind = PointInputKind::RelativePolar;
            const std::size_t second_offset = position_;
            auto second = parse_expression(0);
            if (!second)
                return failure<ParsedPointInput>(diagnostic_.code, diagnostic_.offset);
            if (!coerce(second, Dimension::Angle, second_offset))
                return failure<ParsedPointInput>(diagnostic_.code, diagnostic_.offset);
            if (!finish_point())
                return failure<ParsedPointInput>(diagnostic_.code, diagnostic_.offset);
            result.first = *first;
            result.second = *second;
            result.second_offset = second_offset;
            return success(result);
        }

        if (at_end() || input_[position_] != ';')
            return failure<ParsedPointInput>(ErrorCode::InvalidSyntax, position_);
        if (!coerce(first, Dimension::Length, first_offset))
            return failure<ParsedPointInput>(diagnostic_.code, diagnostic_.offset);
        ++position_;
        skip_spaces();
        result.kind = marker == '@' ? PointInputKind::RelativeCartesian : PointInputKind::AbsoluteWorld;
        const std::size_t second_offset = position_;
        auto second = parse_expression(0);
        if (!second)
            return failure<ParsedPointInput>(diagnostic_.code, diagnostic_.offset);
        if (!coerce(second, Dimension::Length, second_offset))
            return failure<ParsedPointInput>(diagnostic_.code, diagnostic_.offset);
        if (!finish_point())
            return failure<ParsedPointInput>(diagnostic_.code, diagnostic_.offset);
        result.first = *first;
        result.second = *second;
        result.second_offset = second_offset;
        return success(result);
    }

  private:
    std::string_view input_;
    ParseOptions options_;
    std::size_t position_{0};
    std::size_t tokens_{0};
    Diagnostic diagnostic_{};

    [[nodiscard]] bool at_end() const {
        return position_ >= input_.size();
    }

    void skip_spaces() {
        while (!at_end() && is_space(input_[position_]))
            ++position_;
    }

    bool count_token(std::size_t offset) {
        ++tokens_;
        if (tokens_ > max_tokens) {
            set_error(ErrorCode::TooManyTokens, offset);
            return false;
        }
        return true;
    }

    void set_error(ErrorCode code, std::size_t offset) {
        if (diagnostic_.code == ErrorCode::None)
            diagnostic_ = {code, offset};
    }

    bool coerce(std::optional<Quantity>& value, Dimension expected, std::size_t offset) {
        if (!value)
            return false;
        if (value->dimension == Dimension::Scalar) {
            value->dimension = expected;
            return true;
        }
        if (value->dimension != expected) {
            set_error(ErrorCode::WrongDimension, offset);
            return false;
        }
        return true;
    }

    bool finish_point() {
        skip_spaces();
        if (!at_end()) {
            set_error(ErrorCode::InvalidSyntax, position_);
            return false;
        }
        return true;
    }

    std::optional<Quantity> parse_expression(unsigned depth) {
        auto left = parse_term(depth);
        if (!left)
            return std::nullopt;
        while (true) {
            skip_spaces();
            if (at_end() || (input_[position_] != '+' && input_[position_] != '-'))
                return left;
            const std::size_t offset = position_;
            const char operation = input_[position_++];
            if (!count_token(offset))
                return std::nullopt;
            auto right = parse_term(depth);
            if (!right)
                return std::nullopt;
            if (left->dimension != right->dimension) {
                set_error(ErrorCode::WrongDimension, offset);
                return std::nullopt;
            }
            const double value = operation == '+' ? left->value + right->value : left->value - right->value;
            if (!std::isfinite(value)) {
                set_error(result_error(value), offset);
                return std::nullopt;
            }
            left->value = value;
        }
    }

    std::optional<Quantity> parse_term(unsigned depth) {
        auto left = parse_unary(depth);
        if (!left)
            return std::nullopt;
        while (true) {
            skip_spaces();
            if (at_end() || (input_[position_] != '*' && input_[position_] != '/'))
                return left;
            const std::size_t offset = position_;
            const char operation = input_[position_++];
            if (!count_token(offset))
                return std::nullopt;
            auto right = parse_unary(depth);
            if (!right)
                return std::nullopt;

            Dimension result_dimension = Dimension::Scalar;
            if (operation == '*') {
                if (left->dimension == Dimension::Scalar)
                    result_dimension = right->dimension;
                else if (right->dimension == Dimension::Scalar)
                    result_dimension = left->dimension;
                else {
                    set_error(ErrorCode::WrongDimension, offset);
                    return std::nullopt;
                }
            } else {
                if (right->value == 0.0) {
                    set_error(ErrorCode::DivisionByZero, offset);
                    return std::nullopt;
                }
                if (right->dimension == Dimension::Scalar)
                    result_dimension = left->dimension;
                else if (left->dimension == right->dimension)
                    result_dimension = Dimension::Scalar;
                else {
                    set_error(ErrorCode::WrongDimension, offset);
                    return std::nullopt;
                }
            }

            const double value = operation == '*' ? left->value * right->value : left->value / right->value;
            if (!std::isfinite(value)) {
                set_error(result_error(value), offset);
                return std::nullopt;
            }
            left = Quantity{value, result_dimension};
        }
    }

    std::optional<Quantity> parse_unary(unsigned depth) {
        skip_spaces();
        if (!at_end() && (input_[position_] == '+' || input_[position_] == '-')) {
            const std::size_t offset = position_;
            const char operation = input_[position_++];
            if (!count_token(offset))
                return std::nullopt;
            if (depth >= max_expression_depth) {
                set_error(ErrorCode::ExpressionTooDeep, offset);
                return std::nullopt;
            }
            auto value = parse_unary(depth + 1);
            if (!value)
                return std::nullopt;
            if (operation == '-')
                value->value = -value->value;
            return value;
        }
        return parse_primary(depth);
    }

    std::optional<Quantity> parse_primary(unsigned depth) {
        skip_spaces();
        if (at_end()) {
            set_error(ErrorCode::InvalidSyntax, position_);
            return std::nullopt;
        }
        if (input_[position_] == '(') {
            const std::size_t offset = position_++;
            if (!count_token(offset))
                return std::nullopt;
            if (depth >= max_expression_depth) {
                set_error(ErrorCode::ExpressionTooDeep, offset);
                return std::nullopt;
            }
            auto value = parse_expression(depth + 1);
            if (!value)
                return std::nullopt;
            skip_spaces();
            if (at_end() || input_[position_] != ')') {
                set_error(ErrorCode::InvalidSyntax, position_);
                return std::nullopt;
            }
            if (!count_token(position_))
                return std::nullopt;
            ++position_;
            return value;
        }
        return parse_number();
    }

    std::optional<Quantity> parse_number() {
        const std::size_t start = position_;
        std::size_t digits_before = 0;
        while (!at_end() && is_digit(input_[position_])) {
            ++position_;
            ++digits_before;
        }

        std::size_t digits_after = 0;
        if (!at_end() && input_[position_] == options_.decimal_separator) {
            ++position_;
            while (!at_end() && is_digit(input_[position_])) {
                ++position_;
                ++digits_after;
            }
        }
        if (digits_before == 0 && digits_after == 0) {
            if (is_alpha(input_[start])) {
                while (!at_end() && is_alpha(input_[position_]))
                    ++position_;
                const std::string_view name = input_.substr(start, position_ - start);
                set_error(is_non_finite_name(name) ? ErrorCode::NonFiniteValue : ErrorCode::InvalidSyntax, start);
            } else {
                set_error(ErrorCode::InvalidSyntax, start);
            }
            return std::nullopt;
        }

        if (!at_end() && (input_[position_] == 'e' || input_[position_] == 'E')) {
            ++position_;
            if (!at_end() && (input_[position_] == '+' || input_[position_] == '-'))
                ++position_;
            const std::size_t exponent_start = position_;
            while (!at_end() && is_digit(input_[position_]))
                ++position_;
            if (position_ == exponent_start) {
                set_error(ErrorCode::InvalidSyntax, position_);
                return std::nullopt;
            }
        }

        if (!count_token(start))
            return std::nullopt;
        std::string normalized;
        normalized.reserve(position_ - start);
        for (std::size_t index = start; index < position_; ++index)
            normalized.push_back(input_[index] == options_.decimal_separator ? '.' : input_[index]);

        double value = 0.0;
        const auto parsed = std::from_chars(normalized.data(), normalized.data() + normalized.size(), value,
                                            std::chars_format::general);
        if (parsed.ec == std::errc::result_out_of_range) {
            set_error(ErrorCode::Overflow, start);
            return std::nullopt;
        }
        if (parsed.ec != std::errc{} || parsed.ptr != normalized.data() + normalized.size()) {
            set_error(ErrorCode::InvalidSyntax, start);
            return std::nullopt;
        }
        if (!std::isfinite(value)) {
            set_error(ErrorCode::NonFiniteValue, start);
            return std::nullopt;
        }

        skip_spaces();
        const std::size_t unit_start = position_;
        while (!at_end() && is_alpha(input_[position_]))
            ++position_;
        const std::string_view unit = input_.substr(unit_start, position_ - unit_start);
        if (unit.empty())
            return Quantity{value, Dimension::Scalar};
        if (unit == "mm")
            return Quantity{value, Dimension::Length};
        if (unit == "cm")
            return scale(value, 10.0, Dimension::Length, unit_start);
        if (unit == "in")
            return scale(value, 25.4, Dimension::Length, unit_start);
        if (unit == "pt")
            return scale(value, 25.4 / 72.0, Dimension::Length, unit_start);
        if (unit == "deg")
            return Quantity{value, Dimension::Angle};
        if (unit == "rad")
            return scale(value, radians_to_degrees, Dimension::Angle, unit_start);
        set_error(ErrorCode::UnknownUnit, unit_start);
        return std::nullopt;
    }

    std::optional<Quantity> scale(double value, double factor, Dimension dimension, std::size_t offset) {
        const double converted = value * factor;
        if (!std::isfinite(converted)) {
            set_error(result_error(converted), offset);
            return std::nullopt;
        }
        return Quantity{converted, dimension};
    }
};


geometry::Point2D rotate_vector(geometry::Point2D vector, double angle_degrees) {
    const double reduced = std::remainder(angle_degrees, 360.0);
    const double radians = reduced * (pi / 180.0);
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    return {vector.x * cosine - vector.y * sine, vector.x * sine + vector.y * cosine};
}

} // namespace

ParseResult<Quantity> parse_quantity(std::string_view input, ParseOptions options) {
    const Diagnostic validation = validate_input(input, options);
    if (validation.code != ErrorCode::None)
        return failure<Quantity>(validation.code, validation.offset);
    return ExpressionParser(input, options).parse_quantity();
}

ParseResult<double> parse_length_mm(std::string_view input, ParseOptions options) {
    auto parsed = parse_quantity(input, options);
    if (!parsed)
        return failure<double>(parsed.diagnostic.code, parsed.diagnostic.offset);
    Quantity value = *parsed.value;
    if (value.dimension == Dimension::Scalar)
        value.dimension = Dimension::Length;
    if (value.dimension != Dimension::Length)
        return failure<double>(ErrorCode::WrongDimension, 0);
    return success(value.value);
}

ParseResult<double> parse_angle_degrees(std::string_view input, ParseOptions options) {
    auto parsed = parse_quantity(input, options);
    if (!parsed)
        return failure<double>(parsed.diagnostic.code, parsed.diagnostic.offset);
    Quantity value = *parsed.value;
    if (value.dimension == Dimension::Scalar)
        value.dimension = Dimension::Angle;
    if (value.dimension != Dimension::Angle)
        return failure<double>(ErrorCode::WrongDimension, 0);
    return success(value.value);
}

ParseResult<ParsedPointInput> parse_point_input(std::string_view input, ParseOptions options) {
    const Diagnostic validation = validate_input(input, options);
    if (validation.code != ErrorCode::None)
        return failure<ParsedPointInput>(validation.code, validation.offset);
    return ExpressionParser(input, options).parse_point();
}

ParseResult<geometry::Point2D> evaluate_point_input(const ParsedPointInput& input,
                                                    geometry::Point2D anchor_world,
                                                    std::optional<ConstructionFrame> frame) {
    if (!std::isfinite(input.first.value))
        return failure<geometry::Point2D>(result_error(input.first.value), input.first_offset);
    if (input.first.dimension != Dimension::Length)
        return failure<geometry::Point2D>(ErrorCode::WrongDimension, input.first_offset);
    const Dimension second_dimension = input.kind == PointInputKind::RelativePolar ? Dimension::Angle
                                                                                   : Dimension::Length;
    if (!std::isfinite(input.second.value))
        return failure<geometry::Point2D>(result_error(input.second.value), input.second_offset);
    if (input.second.dimension != second_dimension)
        return failure<geometry::Point2D>(ErrorCode::WrongDimension, input.second_offset);

    if (input.kind == PointInputKind::AbsoluteWorld)
        return success(geometry::Point2D{input.first.value, input.second.value});
    if (!anchor_world.is_finite())
        return failure<geometry::Point2D>(ErrorCode::InvalidAnchor, 0);

    const double frame_angle = frame ? frame->x_axis_angle_degrees : 0.0;
    if (!std::isfinite(frame_angle))
        return failure<geometry::Point2D>(ErrorCode::InvalidFrame, 0);

    geometry::Point2D local_delta;
    if (input.kind == PointInputKind::RelativePolar) {
        if (input.first.value < 0.0)
            return failure<geometry::Point2D>(ErrorCode::NegativePolarLength, input.first_offset);
        const double combined_angle = std::remainder(input.second.value, 360.0) +
                                      std::remainder(frame_angle, 360.0);
        const double radians = std::remainder(combined_angle, 360.0) * (pi / 180.0);
        local_delta = {input.first.value * std::cos(radians), input.first.value * std::sin(radians)};
    } else if (input.kind == PointInputKind::RelativeCartesian) {
        local_delta = rotate_vector({input.first.value, input.second.value}, frame_angle);
    } else {
        return failure<geometry::Point2D>(ErrorCode::InvalidSyntax, 0);
    }

    if (!local_delta.is_finite())
        return failure<geometry::Point2D>(ErrorCode::Overflow, 0);
    const geometry::Point2D world{anchor_world.x + local_delta.x, anchor_world.y + local_delta.y};
    if (!world.is_finite())
        return failure<geometry::Point2D>(ErrorCode::Overflow, 0);
    return success(world);
}

ParseResult<geometry::Point2D> evaluate_point_text(std::string_view input,
                                                   geometry::Point2D anchor_world,
                                                   std::optional<ConstructionFrame> frame,
                                                   ParseOptions options) {
    auto parsed = parse_point_input(input, options);
    if (!parsed)
        return failure<geometry::Point2D>(parsed.diagnostic.code, parsed.diagnostic.offset);
    return evaluate_point_input(*parsed.value, anchor_world, frame);
}

} // namespace pikdraw::vector::precision::numeric