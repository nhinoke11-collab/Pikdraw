#pragma once

#include "pikdraw/document/model/document_page.hpp"

#include <optional>

namespace pikdraw::rendering::detail {

[[nodiscard]] inline std::optional<document::ImageObject>
make_pdf_source_preview(const document::DocumentPage& page) {
  if (!page.pdf_source || !page.pdf_source->preview_rgba ||
      page.pdf_source->preview_width_px == 0 ||
      page.pdf_source->preview_height_px == 0)
    return std::nullopt;

  document::ImageObject image;
  image.name = "PDF source screen preview";
  image.bounds_rect = {0.0, 0.0, page.layout.width_mm, page.layout.height_mm};
  image.width_px = static_cast<int32_t>(page.pdf_source->preview_width_px);
  image.height_px = static_cast<int32_t>(page.pdf_source->preview_height_px);
  image.shared_pixels_rgba = page.pdf_source->preview_rgba;
  return image;
}

} // namespace pikdraw::rendering::detail
