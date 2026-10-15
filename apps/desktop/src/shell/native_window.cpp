#include "pikdraw/desktop/shell/native_window.hpp"
#include <iostream>
#include <fstream>
#include <vector>

namespace pikdraw::desktop {

NativeWindow::NativeWindow(int width, int height)
    : width_(width),
      height_(height),
      shell_(static_cast<double>(width), static_cast<double>(height), 1.0),
      workflow_(root_) {
    root_.initialize(static_cast<double>(width), static_cast<double>(height), 1.0);
    workflow_.add_rectangle(50.0, 50.0, 400.0, 200.0, "#00f2c3");
    workflow_.add_vietnamese_text(60.0, 100.0, "PikDraw 2026 - Đồ họa Vector Chuyên Nghiệp", "Plus Jakarta Sans", 24.0);
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

#ifdef _WIN32

LRESULT CALLBACK NativeWindow::window_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<NativeWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (uMsg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        if (self) {
            self->paint(hdc, rc.right - rc.left, rc.bottom - rc.top);
        }
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
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }
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
    std::wstring wtitle(title.begin(), title.end());

    hwnd_ = CreateWindowExW(
        0,
        wc.lpszClassName,
        wtitle.c_str(),
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        w, h,
        NULL, NULL, hinstance_, NULL
    );

    if (!hwnd_) {
        return false;
    }

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

void NativeWindow::paint(HDC hdc, int w, int h) {
    HDC mem_dc = CreateCompatibleDC(hdc);
    HBITMAP mem_bmp = CreateCompatibleBitmap(hdc, w, h);
    HGDIOBJ old_bmp = SelectObject(mem_dc, mem_bmp);

    // Color definitions (Cyber Dark 2026)
    COLORREF col_canvas = RGB(7, 12, 18);
    COLORREF col_panel = RGB(12, 19, 29);
    COLORREF col_ribbon = RGB(17, 26, 39);
    COLORREF col_statusbar = RGB(7, 11, 16);
    COLORREF col_border = RGB(30, 41, 59);
    COLORREF col_teal = RGB(0, 242, 195);
    COLORREF col_text_white = RGB(245, 247, 250);
    COLORREF col_text_dim = RGB(139, 155, 180);
    COLORREF col_paper = RGB(255, 255, 255);
    COLORREF col_active_tab = RGB(24, 38, 56);

    // 1. Clear background
    HBRUSH br_canvas = CreateSolidBrush(col_canvas);
    RECT rc_full = { 0, 0, w, h };
    FillRect(mem_dc, &rc_full, br_canvas);
    DeleteObject(br_canvas);

    SetBkMode(mem_dc, TRANSPARENT);

    // Dimensions
    int topbar_h = 38;
    int ribbon_h = 34;
    int status_h = 26;
    int tool_w = 52;
    int inspect_w = 280;

    // 2. Central Artboard (Paper)
    int art_x = tool_w + 60;
    int art_y = topbar_h + ribbon_h + 40;
    int art_w = 420;
    int art_h = 594;

    // Drop shadow
    HBRUSH br_shadow = CreateSolidBrush(RGB(2, 4, 8));
    RECT rc_shadow = { art_x + 6, art_y + 6, art_x + art_w + 6, art_y + art_h + 6 };
    FillRect(mem_dc, &rc_shadow, br_shadow);
    DeleteObject(br_shadow);

    // Paper sheet
    HBRUSH br_paper = CreateSolidBrush(col_paper);
    RECT rc_paper = { art_x, art_y, art_x + art_w, art_y + art_h };
    FillRect(mem_dc, &rc_paper, br_paper);
    DeleteObject(br_paper);

    // Vector Rectangle shape inside paper
    int rect_x = art_x + 50;
    int rect_y = art_y + 60;
    int rect_w = 280;
    int rect_h = 140;

    HBRUSH br_teal = CreateSolidBrush(col_teal);
    HPEN pen_stroke = CreatePen(PS_SOLID, 2, RGB(56, 239, 125));
    HGDIOBJ old_pen = SelectObject(mem_dc, pen_stroke);
    HGDIOBJ old_br = SelectObject(mem_dc, br_teal);

    RoundRect(mem_dc, rect_x, rect_y, rect_x + rect_w, rect_y + rect_h, 8, 8);

    SelectObject(mem_dc, old_pen);
    SelectObject(mem_dc, old_br);
    DeleteObject(pen_stroke);
    DeleteObject(br_teal);

    // Text on shape
    SetTextColor(mem_dc, RGB(12, 19, 29));
    RECT rc_shape_text = { rect_x + 16, rect_y + 50, rect_x + rect_w - 16, rect_y + 90 };
    DrawTextW(mem_dc, L"PikDraw 2026", -1, &rc_shape_text, DT_CENTER | DT_SINGLELINE);

    // Selection border with cyan outline and 8 handles
    HPEN pen_select = CreatePen(PS_SOLID, 1, col_teal);
    SelectObject(mem_dc, pen_select);
    SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
    Rectangle(mem_dc, rect_x - 4, rect_y - 4, rect_x + rect_w + 4, rect_y + rect_h + 4);

    HBRUSH br_handle = CreateSolidBrush(RGB(255, 255, 255));
    SelectObject(mem_dc, br_handle);
    int handles[8][2] = {
        {rect_x - 7, rect_y - 7}, {rect_x + rect_w / 2 - 3, rect_y - 7}, {rect_x + rect_w + 1, rect_y - 7},
        {rect_x - 7, rect_y + rect_h / 2 - 3}, {rect_x + rect_w + 1, rect_y + rect_h / 2 - 3},
        {rect_x - 7, rect_y + rect_h + 1}, {rect_x + rect_w / 2 - 3, rect_y + rect_h + 1}, {rect_x + rect_w + 1, rect_y + rect_h + 1}
    };
    for (int i = 0; i < 8; ++i) {
        Rectangle(mem_dc, handles[i][0], handles[i][1], handles[i][0] + 6, handles[i][1] + 6);
    }
    DeleteObject(br_handle);
    DeleteObject(pen_select);

    // 3. Topbar
    HBRUSH br_topbar = CreateSolidBrush(col_panel);
    RECT rc_topbar = { 0, 0, w, topbar_h };
    FillRect(mem_dc, &rc_topbar, br_topbar);
    DeleteObject(br_topbar);

    // Logo [PK]
    HBRUSH br_logo = CreateSolidBrush(col_teal);
    RECT rc_logo = { 10, 6, 36, 32 };
    FillRect(mem_dc, &rc_logo, br_logo);
    DeleteObject(br_logo);
    SetTextColor(mem_dc, RGB(10, 16, 24));
    DrawTextW(mem_dc, L"PK", -1, &rc_logo, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // Menus
    SetTextColor(mem_dc, col_text_white);
    RECT rc_menu = { 46, 0, 480, topbar_h };
    DrawTextW(mem_dc, L"Tệp   Chỉnh sửa   Đối tượng   Xem   Công cụ   Cửa sổ   Trợ giúp", -1, &rc_menu, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // Pill Workspace Switcher
    int pill_x = 520;
    HBRUSH br_active = CreateSolidBrush(col_active_tab);
    RECT rc_pill_vec = { pill_x, 6, pill_x + 68, 32 };
    FillRect(mem_dc, &rc_pill_vec, br_active);
    DeleteObject(br_active);
    SetTextColor(mem_dc, col_teal);
    DrawTextW(mem_dc, L"Vector", -1, &rc_pill_vec, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SetTextColor(mem_dc, col_text_dim);
    RECT rc_pill_oth = { pill_x + 72, 6, pill_x + 280, 32 };
    DrawTextW(mem_dc, L"In ấn (M2)    Thi công (M2)    3D (M2)", -1, &rc_pill_oth, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // Export Button
    HBRUSH br_export = CreateSolidBrush(col_teal);
    RECT rc_export = { w - 90, 6, w - 12, 32 };
    FillRect(mem_dc, &rc_export, br_export);
    DeleteObject(br_export);
    SetTextColor(mem_dc, RGB(10, 16, 24));
    DrawTextW(mem_dc, L"Xuất file", -1, &rc_export, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // 4. Context Ribbon
    HBRUSH br_ribbon = CreateSolidBrush(col_ribbon);
    RECT rc_ribbon = { 0, topbar_h, w, topbar_h + ribbon_h };
    FillRect(mem_dc, &rc_ribbon, br_ribbon);
    DeleteObject(br_ribbon);

    SetTextColor(mem_dc, col_text_white);
    RECT rc_ribbon_txt = { 16, topbar_h, w - 16, topbar_h + ribbon_h };
    DrawTextW(mem_dc, L"Con trỏ chọn (V)  |  X: 50.00 mm   Y: 50.00 mm   Rộng: 400.00 mm   Cao: 200.00 mm   Góc: 0°  |  Tô: #00f2c3   Viền: #38ef7d (2 mm)", -1, &rc_ribbon_txt, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // 5. Left Tool Strip
    HBRUSH br_tool = CreateSolidBrush(col_panel);
    RECT rc_tool = { 0, topbar_h + ribbon_h, tool_w, h - status_h };
    FillRect(mem_dc, &rc_tool, br_tool);
    DeleteObject(br_tool);

    const wchar_t* tools[] = { L"V", L"R", L"E", L"P", L"T", L"H", L"Z" };
    for (int i = 0; i < 7; ++i) {
        int ty = topbar_h + ribbon_h + 10 + i * 42;
        RECT rc_btn = { 6, ty, tool_w - 6, ty + 36 };
        if (i == 0) { // Active tool
            HBRUSH br_act = CreateSolidBrush(col_active_tab);
            FillRect(mem_dc, &rc_btn, br_act);
            DeleteObject(br_act);
            SetTextColor(mem_dc, col_teal);
        } else {
            SetTextColor(mem_dc, col_text_dim);
        }
        DrawTextW(mem_dc, tools[i], -1, &rc_btn, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // 6. Right Inspector & Layers Dock
    int insp_x = w - inspect_w;
    HBRUSH br_insp = CreateSolidBrush(col_panel);
    RECT rc_insp = { insp_x, topbar_h + ribbon_h, w, h - status_h };
    FillRect(mem_dc, &rc_insp, br_insp);
    DeleteObject(br_insp);

    SetTextColor(mem_dc, col_teal);
    RECT rc_insp_h1 = { insp_x + 14, topbar_h + ribbon_h + 12, w - 14, topbar_h + ribbon_h + 32 };
    DrawTextW(mem_dc, L"THUỘC TÍNH (INSPECTOR)", -1, &rc_insp_h1, DT_LEFT | DT_SINGLELINE);

    SetTextColor(mem_dc, col_text_white);
    RECT rc_insp_t1 = { insp_x + 14, topbar_h + ribbon_h + 42, w - 14, topbar_h + ribbon_h + 140 };
    DrawTextW(mem_dc, L"Đối tượng: Hình chữ nhật\nX: 50.00 mm\nY: 50.00 mm\nRộng: 400.00 mm\nCao: 200.00 mm", -1, &rc_insp_t1, DT_LEFT);

    SetTextColor(mem_dc, col_teal);
    RECT rc_insp_h2 = { insp_x + 14, topbar_h + ribbon_h + 160, w - 14, topbar_h + ribbon_h + 180 };
    DrawTextW(mem_dc, L"DANH SÁCH LỚP (LAYERS)", -1, &rc_insp_h2, DT_LEFT | DT_SINGLELINE);

    SetTextColor(mem_dc, col_text_white);
    RECT rc_insp_t2 = { insp_x + 14, topbar_h + ribbon_h + 190, w - 14, topbar_h + ribbon_h + 280 };
    DrawTextW(mem_dc, L"▼ Lớp 1 (Mặc định)\n   [T] PikDraw 2026\n   [R] Hcn #00f2c3 (Đang chọn)", -1, &rc_insp_t2, DT_LEFT);

    // 7. Bottom Status Bar
    HBRUSH br_status = CreateSolidBrush(col_statusbar);
    RECT rc_status = { 0, h - status_h, w, h };
    FillRect(mem_dc, &rc_status, br_status);
    DeleteObject(br_status);

    SetTextColor(mem_dc, col_text_dim);
    RECT rc_st_left = { 16, h - status_h, 300, h };
    DrawTextW(mem_dc, L"X: 50.00 mm   Y: 50.00 mm", -1, &rc_st_left, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT rc_st_right = { w - 420, h - status_h, w - 16, h };
    DrawTextW(mem_dc, L"Thu phóng: 100%   |   Không gian: Vector (M1 Native)   |   Sẵn sàng", -1, &rc_st_right, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    // Draw borders
    HPEN pen_bdr = CreatePen(PS_SOLID, 1, col_border);
    HGDIOBJ old_bdr = SelectObject(mem_dc, pen_bdr);

    // Topbar bottom line
    MoveToEx(mem_dc, 0, topbar_h, NULL);
    LineTo(mem_dc, w, topbar_h);

    // Ribbon bottom line
    MoveToEx(mem_dc, 0, topbar_h + ribbon_h, NULL);
    LineTo(mem_dc, w, topbar_h + ribbon_h);

    // Tool strip right line
    MoveToEx(mem_dc, tool_w, topbar_h + ribbon_h, NULL);
    LineTo(mem_dc, tool_w, h - status_h);

    // Inspector left line
    MoveToEx(mem_dc, insp_x, topbar_h + ribbon_h, NULL);
    LineTo(mem_dc, insp_x, h - status_h);

    // Status bar top line
    MoveToEx(mem_dc, 0, h - status_h, NULL);
    LineTo(mem_dc, w, h - status_h);

    SelectObject(mem_dc, old_bdr);
    DeleteObject(pen_bdr);

    // Blit buffer to screen
    BitBlt(hdc, 0, 0, w, h, mem_dc, 0, 0, SRCCOPY);

    SelectObject(mem_dc, old_bmp);
    DeleteObject(mem_bmp);
    DeleteDC(mem_dc);
}

void NativeWindow::capture_screenshot_bmp(const std::string& path) {
    int w = width_;
    int h = height_;

    HDC screen_dc = GetDC(NULL);
    HDC mem_dc = CreateCompatibleDC(screen_dc);
    HBITMAP mem_bmp = CreateCompatibleBitmap(screen_dc, w, h);
    HGDIOBJ old_bmp = SelectObject(mem_dc, mem_bmp);

    paint(mem_dc, w, h);

    BITMAPINFOHEADER bi = { sizeof(BITMAPINFOHEADER) };
    bi.biWidth = w;
    bi.biHeight = h;
    bi.biPlanes = 1;
    bi.biBitCount = 24;
    bi.biCompression = BI_RGB;
    int row_stride = ((w * 3 + 3) / 4) * 4;
    int data_size = row_stride * h;
    bi.biSizeImage = data_size;

    std::vector<uint8_t> pixel_data(data_size);
    GetDIBits(mem_dc, mem_bmp, 0, h, pixel_data.data(), reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);

    BITMAPFILEHEADER bfh = { 0 };
    bfh.bfType = 0x4D42; // "BM"
    bfh.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + data_size;
    bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

    std::ofstream out(path, std::ios::binary);
    if (out.is_open()) {
        out.write(reinterpret_cast<const char*>(&bfh), sizeof(bfh));
        out.write(reinterpret_cast<const char*>(&bi), sizeof(bi));
        out.write(reinterpret_cast<const char*>(pixel_data.data()), data_size);
    }

    SelectObject(mem_dc, old_bmp);
    DeleteObject(mem_bmp);
    DeleteDC(mem_dc);
    ReleaseDC(NULL, screen_dc);
}

#else

bool NativeWindow::initialize(const std::string&) { return true; }
int NativeWindow::run() { return 0; }
void NativeWindow::capture_screenshot_bmp(const std::string&) {}

#endif

} // namespace pikdraw::desktop
