#include "byte_codec.hpp"

namespace pikdraw::document::storage_detail {
namespace {
void write_rgb(Writer &writer, const color::RGBColor &value) {
  writer.f64(value.r);
  writer.f64(value.g);
  writer.f64(value.b);
  writer.f64(value.a);
}
color::RGBColor read_rgb(Reader &reader) {
  return {reader.f64(), reader.f64(), reader.f64(), reader.f64()};
}
} // namespace

void write_color(Writer &writer, const color::ColorValue &value) {
  if (!value.is_valid())
    throw std::invalid_argument("Invalid source color");
  writer.u8(static_cast<uint8_t>(value.payload().index()));
  writer.flag(value.overprint().fill);
  writer.flag(value.overprint().stroke);
  std::visit(
      [&](const auto &color) {
        using T = std::decay_t<decltype(color)>;
        if constexpr (std::is_same_v<T, color::RGBColor>)
          write_rgb(writer, color);
        else if constexpr (std::is_same_v<T, color::CMYKColor>) {
          writer.f64(color.c);
          writer.f64(color.m);
          writer.f64(color.y);
          writer.f64(color.k);
          writer.f64(color.a);
        } else if constexpr (std::is_same_v<T, color::SpotColor>) {
          writer.str(color.spot_name);
          writer.f64(color.tint);
          write_rgb(writer, color.alternate_rgb);
          writer.flag(color.overprint);
        } else {
          writer.f64(color.gray);
          writer.f64(color.a);
        }
      },
      value.payload());
}

color::ColorValue read_color(Reader &reader) {
  const auto tag = reader.u8();
  const bool fill = reader.flag();
  const bool stroke = reader.flag();
  color::ColorValue result;
  switch (tag) {
  case 0:
    result = read_rgb(reader);
    break;
  case 1:
    result = color::CMYKColor{reader.f64(), reader.f64(), reader.f64(),
                              reader.f64(), reader.f64()};
    break;
  case 2: {
    color::SpotColor spot;
    spot.spot_name = reader.str();
    spot.tint = reader.f64();
    spot.alternate_rgb = read_rgb(reader);
    spot.overprint = reader.flag();
    result = std::move(spot);
    break;
  }
  case 3:
    result = color::GrayscaleColor{reader.f64(), reader.f64()};
    break;
  default:
    throw std::invalid_argument("Unknown native source color tag");
  }
  result.set_overprint({fill, stroke});
  if (!result.is_valid())
    throw std::invalid_argument("Invalid native source color");
  return result;
}

} // namespace pikdraw::document::storage_detail
