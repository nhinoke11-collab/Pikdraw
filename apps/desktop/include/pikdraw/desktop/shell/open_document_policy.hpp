#pragma once

#include "pikdraw/document/snapshots/document_snapshot.hpp"

#include <algorithm>

namespace pikdraw::desktop::detail {

/// Opening another source keeps a populated or multi-page document in its tab.
[[nodiscard]] inline bool snapshot_has_openable_content(
    const document::DocumentSnapshot& snapshot) noexcept {
  if (snapshot.page_count() > 1) return true;
  return std::any_of(snapshot.pages().begin(), snapshot.pages().end(),
      [](const document::DocumentPage& page) {
        return !page.objects.empty() || page.pdf_source.has_value();
      });
}

} // namespace pikdraw::desktop::detail
