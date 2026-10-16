#pragma once

#include "pikdraw/geometry/primitives/transform.hpp"
#include <cstddef>
#include <optional>
#include <string_view>

namespace pikdraw::vector::precision::numeric {

inline constexpr std::size_t max_input_bytes = 256;

enum class Dimension {
    Scalar,
    Length,
    Angle
};

enum class ErrorCode {
    None,
    EmptyInput,
    InputTooLong,
    InvalidLocale,
    TooManyTokens,
    ExpressionTooDeep,
    InvalidSyntax,
    UnknownUnit,
    WrongDimension,
    InvalidOperation,
    DivisionByZero,
    NonFiniteValue,
    Overflow,
    NegativePolarLength,
    InvalidAnchor,
    InvalidFrame
};

struct Diagnostic {
    ErrorCode code{ErrorCode::None};
    std::size_t offset{0};
};

template <typename T>
struct ParseResult {
    std::optional<T> value;
    Diagnostic diagnostic;

    [[nodiscard]] explicit operator bool() const noexcept {
        return value.has_value();
    }
};

struct ParseOptions {
    char decimal_separator{'.'};
};

struct Quantity {
    double value{0.0};
    Dimension dimension{Dimension::Scalar};
};

enum class PointInputKind {
    RelativePolar,
    RelativeCartesian,
    AbsoluteWorld
};

// Length values are millimeters and angle values are degrees.
struct ParsedPointInput {
    PointInputKind kind{PointInputKind::RelativeCartesian};
    Quantity first;
    Quantity second;
    std::size_t first_offset{0};
    std::size_t second_offset{0};
};

// The local +X axis rotated counter-clockwise from world +X.
struct ConstructionFrame {
    double x_axis_angle_degrees{0.0};
};

[[nodiscard]] ParseResult<Quantity> parse_quantity(std::string_view input,
                                                   ParseOptions options = {});
[[nodiscard]] ParseResult<double> parse_length_mm(std::string_view input,
                                                  ParseOptions options = {});
[[nodiscard]] ParseResult<double> parse_angle_degrees(std::string_view input,
                                                      ParseOptions options = {});
[[nodiscard]] ParseResult<ParsedPointInput> parse_point_input(std::string_view input,
                                                              ParseOptions options = {});
[[nodiscard]] ParseResult<geometry::Point2D> evaluate_point_input(
    const ParsedPointInput& input,
    geometry::Point2D anchor_world,
    std::optional<ConstructionFrame> frame = std::nullopt);
[[nodiscard]] ParseResult<geometry::Point2D> evaluate_point_text(
    std::string_view input,
    geometry::Point2D anchor_world,
    std::optional<ConstructionFrame> frame = std::nullopt,
    ParseOptions options = {});

} // namespace pikdraw::vector::precision::numeric