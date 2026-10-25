#include "pikdraw/desktop/shell/native_window.hpp"

namespace pikdraw::desktop {

void NativeWindow::handle_raster_histogram_worker_message() {
#ifdef _WIN32
  if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

}  // namespace pikdraw::desktop
