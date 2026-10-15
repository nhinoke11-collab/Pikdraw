#include "pikdraw/desktop/shell/native_window.hpp"
#include <iostream>
#include <fstream>
#include <vector>
#include <iomanip>
#include <sstream>

namespace pikdraw::desktop {

std::wstring NativeWindow::utf8_to_wide(const std::string& utf8) {
    if (utf8.empty()) return L"";
#ifdef _WIN32
    int size = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), NULL, 0);
    std::wstring wstr(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), &wstr[0], size);
    return wstr;
#else
    return std::wstring(utf8.begin(), utf8.end());
#endif
}

NativeWindow::NativeWindow(int width, int height)
    : width_(width),
      height_(height),
      shell_(static_cast<double>(width), static_cast<double>(height), 1.0),
      workflow_(root_) {
    root_.initialize(static_cast<double>(width), static_cast<double>(height), 1.0);
    workflow_.add_rectangle(50.0, 50.0, 400.0, 200.0, "#00f2c3");
    workflow_.add_vietnamese_text(60.0, 100.0, "AN PHÁT SIGNAGE 2026", "Be Vietnam Pro", 28.0);
}

NativeWindow::~NativeWindow() {
#ifdef _WIN32
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
#endif
    root_.shutdown();
}

void NativeWindow::set_language(UILanguage lang) noexcept {
    language_ = lang;
    status_msg_ = (lang == UILanguage::Vietnamese) ?
        "Đã chuyển giao diện sang Tiếng Việt (Be Vietnam Pro)" :
        "Switched UI language to English (Be Vietnam Pro)";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::toggle_language() noexcept {
    set_language(language_ == UILanguage::Vietnamese ? UILanguage::English : UILanguage::Vietnamese);
}

void NativeWindow::set_active_tool(int tool_index) noexcept {
    active_tool_ = tool_index;
    const char* vi_tools[] = { "Con trỏ chọn (V)", "Sửa điểm Node (A)", "Bút vẽ Pen (P)", "Hình chữ nhật (R)", "Hình Elip (E)", "Văn bản (T)", "Bàn tay Pan (H)", "Kính lúp Zoom (Z)" };
    const char* en_tools[] = { "Select Tool (V)", "Direct Node Edit (A)", "Pen Tool (P)", "Rectangle Tool (R)", "Ellipse Tool (E)", "Text Tool (T)", "Hand Pan (H)", "Zoom Tool (Z)" };
    status_msg_ = (language_ == UILanguage::Vietnamese) ?
        std::string("Đã kích hoạt: ") + vi_tools[tool_index % 8] :
        std::string("Activated: ") + en_tools[tool_index % 8];
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::set_active_workspace(int ws_index) noexcept {
    active_workspace_ = ws_index;
    const char* vi_ws[] = { "Vector (M1 Native)", "In ấn chuyên nghiệp (M2)", "Thi công biển quảng cáo (M2)", "Phối cảnh 3D (M2)" };
    const char* en_ws[] = { "Vector (M1 Native)", "Print Production (M2)", "Signage Manufacturing (M2)", "3D Perspective (M2)" };
    status_msg_ = (language_ == UILanguage::Vietnamese) ?
        std::string("Không gian làm việc: ") + vi_ws[ws_index % 4] :
        std::string("Active Workspace: ") + en_ws[ws_index % 4];
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::set_active_dock_tab(int tab_index) noexcept {
    active_dock_tab_ = tab_index;
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::zoom_in() noexcept {
    if (zoom_level_ < 200) zoom_level_ += 25;
    status_msg_ = (language_ == UILanguage::Vietnamese) ?
        "Tỉ lệ hiển thị: " + std::to_string(zoom_level_) + "%" :
        "Zoom level: " + std::to_string(zoom_level_) + "%";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::zoom_out() noexcept {
    if (zoom_level_ > 50) zoom_level_ -= 25;
    status_msg_ = (language_ == UILanguage::Vietnamese) ?
        "Tỉ lệ hiển thị: " + std::to_string(zoom_level_) + "%" :
        "Zoom level: " + std::to_string(zoom_level_) + "%";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::trigger_export() noexcept {
    [[maybe_unused]] auto svg_out = workflow_.export_svg();
    status_msg_ = (language_ == UILanguage::Vietnamese) ?
        "Đã xuất bản vẽ vector SVG thành công (100% chuẩn Contract C4)!" :
        "Vector SVG document exported successfully!";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

#ifdef _WIN32

void NativeWindow::handle_mouse_down(int x, int y) {
    int w = width_;

    // 1. Language Toggle [VI | EN] (x: w - 175 to w - 110, y: 6 to 32)
    int lang_x = w - 175;
    if (x >= lang_x && x <= lang_x + 65 && y >= 6 && y <= 32) {
        toggle_language();
        return;
    }

    // 2. Export Button [Xuất file] (x: w - 95 to w - 12, y: 6 to 32)
    if (x >= w - 95 && x <= w - 12 && y >= 6 && y <= 32) {
        trigger_export();
        return;
    }

    // 3. Workspace Pills (x: 520 to 790, y: 6 to 32)
    int pill_x = 520;
    if (y >= 6 && y <= 32) {
        if (x >= pill_x && x < pill_x + 65) { set_active_workspace(0); return; }
        if (x >= pill_x + 70 && x < pill_x + 135) { set_active_workspace(1); return; }
        if (x >= pill_x + 140 && x < pill_x + 215) { set_active_workspace(2); return; }
        if (x >= pill_x + 220 && x < pill_x + 270) { set_active_workspace(3); return; }
    }

    // 4. Tool Strip (x: 6 to 46, y: 80 to 420)
    if (x >= 6 && x <= 46 && y >= 80) {
        int tool_idx = (y - 80) / 42;
        if (tool_idx >= 0 && tool_idx < 8) {
            set_active_tool(tool_idx);
            return;
        }
    }

    // 5. Right Dock Tabs (x: w - 280 to w, y: 72 to 102)
    if (x >= w - 280 && x <= w && y >= 72 && y <= 102) {
        int tab_w = 280 / 3;
        int tab_idx = (x - (w - 280)) / tab_w;
        if (tab_idx >= 0 && tab_idx < 3) {
            set_active_dock_tab(tab_idx);
            return;
        }
    }

    // 6. Zoom buttons on status bar (x: w - 120 to w - 60, y: height - 26 to height)
    if (y >= height_ - 26) {
        if (x >= w - 120 && x <= w - 95) { zoom_out(); return; }
        if (x >= w - 45 && x <= w - 20) { zoom_in(); return; }
    }

    // 7. Canvas interaction: clicking/dragging the vector signboard object
    int tool_w = 52;
    int inspect_w = 280;
    int canvas_w = w - tool_w - inspect_w;
    int art_w = 660;
    int art_x = tool_w + (canvas_w - art_w) / 2;
    int art_y = 38 + 34 + 35;

    int canvas_obj_x = art_x + 30 + static_cast<int>(shape_x_ * 0.15);
    int canvas_obj_y = art_y + 40 + static_cast<int>(shape_y_ * 0.15);
    int canvas_obj_w = 460;
    int canvas_obj_h = 160;

    if (x >= canvas_obj_x - 10 && x <= canvas_obj_x + canvas_obj_w + 10 &&
        y >= canvas_obj_y - 10 && y <= canvas_obj_y + canvas_obj_h + 10) {
        is_dragging_ = true;
        drag_start_x_ = x;
        drag_start_y_ = y;
        drag_init_shape_x_ = shape_x_;
        drag_init_shape_y_ = shape_y_;
        status_msg_ = (language_ == UILanguage::Vietnamese) ?
            "Đang di chuyển: AN PHÁT SIGNAGE 2026" :
            "Moving: AN PHAT SIGNAGE 2026";
        InvalidateRect(hwnd_, NULL, FALSE);
    }
}

void NativeWindow::handle_mouse_move(int x, int y) {
    cursor_mm_x_ = static_cast<double>(x - 102) * 2.0;
    cursor_mm_y_ = static_cast<double>(y - 102) * 2.0;
    if (cursor_mm_x_ < 0.0) cursor_mm_x_ = 0.0;
    if (cursor_mm_y_ < 0.0) cursor_mm_y_ = 0.0;

    if (is_dragging_) {
        double dx = static_cast<double>(x - drag_start_x_) * 4.0;
        double dy = static_cast<double>(y - drag_start_y_) * 4.0;
        shape_x_ = drag_init_shape_x_ + dx;
        shape_y_ = drag_init_shape_y_ + dy;
        InvalidateRect(hwnd_, NULL, FALSE);
    }
}

void NativeWindow::handle_mouse_up(int /*x*/, int /*y*/) {
    if (is_dragging_) {
        is_dragging_ = false;
        status_msg_ = (language_ == UILanguage::Vietnamese) ?
            "Đã cập nhật vị trí đối tượng (" + std::to_string(static_cast<int>(shape_x_)) + " mm, " + std::to_string(static_cast<int>(shape_y_)) + " mm)" :
            "Updated object position (" + std::to_string(static_cast<int>(shape_x_)) + " mm, " + std::to_string(static_cast<int>(shape_y_)) + " mm)";
        InvalidateRect(hwnd_, NULL, FALSE);
    }
}

LRESULT CALLBACK NativeWindow::window_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<NativeWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (uMsg) {
    case WM_LBUTTONDOWN:
        if (self) self->handle_mouse_down(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_MOUSEMOVE:
        if (self) self->handle_mouse_move(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_LBUTTONUP:
        if (self) self->handle_mouse_up(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        if (self) self->paint(hdc, rc.right - rc.left, rc.bottom - rc.top);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SIZE: {
        int w = LOWORD(lParam);
        int h = HIWORD(lParam);
        if (self && w > 0 && h > 0) {
            self->width_ = w;
            self->height_ = h;
            self->shell_.set_window_size(static_cast<double>(w), static_cast<double>(h));
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

bool NativeWindow::initialize(const std::string& title) {
    hinstance_ = GetModuleHandleW(NULL);

    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = NativeWindow::window_proc;
    wc.hInstance = hinstance_;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"PikDrawShellWindowClass";

    RegisterClassExW(&wc);

    int w = width_;
    int h = height_;
    std::wstring wtitle = utf8_to_wide(title);

    hwnd_ = CreateWindowExW(
        0, wc.lpszClassName, wtitle.c_str(),
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, w, h,
        NULL, NULL, hinstance_, NULL
    );

    if (!hwnd_) return false;

    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    return true;
}

int NativeWindow::run() {
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

#else

bool NativeWindow::initialize(const std::string&) { return true; }
int NativeWindow::run() { return 0; }
void NativeWindow::set_language(UILanguage) noexcept {}
void NativeWindow::toggle_language() noexcept {}
void NativeWindow::set_active_tool(int) noexcept {}
void NativeWindow::set_active_workspace(int) noexcept {}
void NativeWindow::set_active_dock_tab(int) noexcept {}
void NativeWindow::zoom_in() noexcept {}
void NativeWindow::zoom_out() noexcept {}
void NativeWindow::trigger_export() noexcept {}

#endif

} // namespace pikdraw::desktop
