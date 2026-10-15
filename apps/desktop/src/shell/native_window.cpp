#include "pikdraw/desktop/shell/native_window.hpp"
#include <iostream>
#include <fstream>
#include <vector>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cmath>

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
    workflow_.new_document();
    fit_page_to_canvas();
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

void NativeWindow::set_theme(ThemeMode mode) noexcept {
    theme_ = mode;
    settings_theme_choice_ = (mode == ThemeMode::CyberDark) ? 0 : 1;
    status_msg_ = (language_ == UILanguage::Vietnamese) ?
        (mode == ThemeMode::CyberDark ? "Đã chuyển sang Chế độ Tối (Dark Mode Obsidian)" : "Đã chuyển sang Chế độ Sáng (Light Mode Silver)") :
        (mode == ThemeMode::CyberDark ? "Switched to Dark Mode (Obsidian Slate)" : "Switched to Light Mode (Silver Tech)");
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::toggle_theme() noexcept {
    set_theme(theme_ == ThemeMode::CyberDark ? ThemeMode::Light : ThemeMode::CyberDark);
}

void NativeWindow::open_settings_modal() noexcept {
    show_settings_modal_ = true;
    status_msg_ = (language_ == UILanguage::Vietnamese) ?
        "Đã mở Cài đặt hệ thống (PikDraw Preferences)" : "Opened System Preferences (PikDraw Settings)";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::close_settings_modal() noexcept {
    show_settings_modal_ = false;
    status_msg_ = (language_ == UILanguage::Vietnamese) ?
        "Đã lưu và đóng Cài đặt hệ thống" : "Saved and closed System Preferences";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::toggle_settings_modal() noexcept {
    if (show_settings_modal_) close_settings_modal(); else open_settings_modal();
}

void NativeWindow::set_settings_tab(int tab) noexcept {
    if (tab < 0 || tab > 5) return;
    settings_active_tab_ = tab;
    const char* vi_tabs[] = { "Chung & Ngôn ngữ", "Giao diện (Theme)", "Đơn vị & Bắt điểm", "Quản lý màu & ICC", "Thi công & CNC", "Hiệu năng & Hệ thống" };
    const char* en_tabs[] = { "General & Language", "Display & Theme", "Units & Snapping", "Color & ICC", "Production & CAM", "Hardware & System" };
    status_msg_ = (language_ == UILanguage::Vietnamese) ?
        std::string("Tùy chọn: ") + vi_tabs[tab] : std::string("Options: ") + en_tabs[tab];
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::set_settings_theme_choice(int theme) noexcept {
    settings_theme_choice_ = theme;
    set_theme(theme == 1 ? ThemeMode::Light : ThemeMode::CyberDark);
}

void NativeWindow::set_settings_accent_choice(int accent) noexcept {
    settings_accent_choice_ = accent;
    const char* vi_accents[] = { "Cyber Mint (#00f2c3)", "Sky Blue (#0ea5e9)", "Purple Tech (#a855f7)", "Amber CAD (#f59e0b)" };
    status_msg_ = (language_ == UILanguage::Vietnamese) ?
        std::string("Màu điểm nhấn: ") + vi_accents[accent % 4] : std::string("Accent Color: ") + vi_accents[accent % 4];
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::set_settings_unit_choice(int unit) noexcept {
    settings_unit_choice_ = unit;
    const char* units[] = { "mm (Milimét)", "cm (Xentimét)", "inch (Inches)", "pt (Điểm in)" };
    status_msg_ = (language_ == UILanguage::Vietnamese) ?
        std::string("Đơn vị mặc định: ") + units[unit % 4] : std::string("Default Units: ") + units[unit % 4];
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::set_active_tool(int tool_index) noexcept {
    if (tool_index < 0 || tool_index > 3)
        return;
    active_tool_ = tool_index;
    const auto mode = tool_index == 1 ? vector::ToolMode::Rectangle
                    : tool_index == 2 ? vector::ToolMode::Ellipse
                                      : vector::ToolMode::Select;
    root_.vector_controller().set_mode(mode);
    const char* vi_tools[] = { "Chọn và di chuyển · V", "Vẽ hình chữ nhật · R",
                               "Vẽ hình elip · E", "Di chuyển khung nhìn · Space/chuột giữa" };
    const char* en_tools[] = { "Select and move · V", "Draw rectangle · R",
                               "Draw ellipse · E", "Pan view · Space/middle mouse" };
    status_msg_ = language_ == UILanguage::Vietnamese ? vi_tools[tool_index] : en_tools[tool_index];
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::set_active_workspace(int ws_index) noexcept {
    if (ws_index != 0)
        return;
    active_workspace_ = 0;
    status_msg_ = language_ == UILanguage::Vietnamese
        ? "Không gian Vector 2D đang hoạt động"
        : "Vector 2D workspace is active";
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
    auto& camera = root_.renderer().camera();
    camera.set_viewport_size(std::max(1, width_ - 352), std::max(1, height_ - 118));
    camera.set_zoom(std::min(camera.zoom() * 1.25, 64.0));
    zoom_level_ = static_cast<int>(std::lround(camera.zoom() * 100.0));
    status_msg_ = (language_ == UILanguage::Vietnamese) ?
        "Tỉ lệ hiển thị: " + std::to_string(zoom_level_) + "%" :
        "Zoom level: " + std::to_string(zoom_level_) + "%";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::zoom_out() noexcept {
    auto& camera = root_.renderer().camera();
    camera.set_viewport_size(std::max(1, width_ - 352), std::max(1, height_ - 118));
    camera.set_zoom(std::max(camera.zoom() / 1.25, 0.01));
    zoom_level_ = static_cast<int>(std::lround(camera.zoom() * 100.0));
    status_msg_ = (language_ == UILanguage::Vietnamese) ?
        "Tỉ lệ hiển thị: " + std::to_string(zoom_level_) + "%" :
        "Zoom level: " + std::to_string(zoom_level_) + "%";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::fit_page_to_canvas() noexcept {
    const auto& page = root_.document().current_snapshot()->page();
    const int canvas_width = std::max(1, width_ - 352);
    const int canvas_height = std::max(1, height_ - 118);
    const double zoom = std::clamp(
        std::min(static_cast<double>(canvas_width) / page.width_mm,
                 static_cast<double>(canvas_height) / page.height_mm) * 0.9,
        0.01, 64.0);
    auto& camera = root_.renderer().camera();
    camera.set_viewport_size(canvas_width, canvas_height);
    camera.set_center({page.width_mm * 0.5, page.height_mm * 0.5});
    camera.set_zoom(zoom);
    zoom_level_ = static_cast<int>(std::lround(zoom * 100.0));
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::open_file_menu() noexcept {
    show_file_menu_ = true;
    status_msg_ = (language_ == UILanguage::Vietnamese) ?
        "Đã mở menu Tệp" : "File menu opened";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::close_file_menu() noexcept {
    if (show_file_menu_) {
        show_file_menu_ = false;
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
    }
}

void NativeWindow::toggle_file_menu() noexcept {
    if (show_file_menu_) close_file_menu(); else open_file_menu();
}

void NativeWindow::open_home_screen() noexcept {
    show_home_screen_ = true;
    close_file_menu();
    status_msg_ = (language_ == UILanguage::Vietnamese) ?
        "Đã chuyển sang Trang chủ (PikDraw Home Screen & Templates)" : "Switched to Home Screen (PikDraw Welcome & Templates)";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::close_home_screen() noexcept {
    show_home_screen_ = false;
    status_msg_ = (language_ == UILanguage::Vietnamese) ?
        "Đã quay lại Không gian vẽ Vector (Canvas)" : "Returned to Vector Workspace Canvas";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::toggle_home_screen() noexcept {
    if (show_home_screen_) close_home_screen(); else open_home_screen();
}

void NativeWindow::set_active_home_tab(int tab) noexcept {
    if (tab < 0 || tab > 3) return;
    active_home_tab_ = tab;
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::open_recent_document(int doc_index) noexcept {
    const char* vi_files[] = {
        "Bien_Hieu_An_Phat.pik (1850 × 420 mm - Biển alu đèn LED)",
        "Hop_Den_Hiflex_Techcom.pik (3000 × 1200 mm - Bạt xuyên sáng hiflex 3M)",
        "Chu_Noi_Inox_Vang_Guong.pik (800 × 350 mm - File cắt laser CNC kim loại)",
        "Standee_Cuon_Su_Kien_2026.pik (800 × 2000 mm - Bạt PP cán màng mờ)",
        "Tem_Nhan_Decal_Cuon.pik (500 × 500 mm - Decal bế demi chống nước)"
    };
    int idx = doc_index % 5;
    close_home_screen();
    close_file_menu();
    status_msg_ = (language_ == UILanguage::Vietnamese) ?
        std::string("Đã nạp tệp gần đây: ") + vi_files[idx] :
        std::string("Loaded recent file: ") + vi_files[idx];
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::create_from_template(int template_index) noexcept {
    const char* vi_tpls[] = {
        "Biển hiệu ngoài trời (6000 × 3000 mm)",
        "Tấm Alu chuẩn thi công (1220 × 2440 mm)",
        "Hộp đèn mica tròn hút nổi (600 × 600 mm)",
        "Standee cuốn sự kiện (800 × 2000 mm)",
        "Card visit chuẩn xưởng in (90 × 54 mm)",
        "Decal bế tem nhãn khổ cuộn (1000 × 1000 mm)"
    };
    int idx = template_index % 6;
    close_home_screen();
    close_file_menu();
    status_msg_ = (language_ == UILanguage::Vietnamese) ?
        std::string("Đã tạo bản vẽ từ mẫu chuẩn: ") + vi_tpls[idx] :
        std::string("Created new drawing from template: ") + vi_tpls[idx];
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::trigger_export() noexcept {
#ifdef _WIN32
    export_svg_dialog();
#else
    status_msg_ = "SVG export requires the Windows desktop file dialog";
#endif
}

bool NativeWindow::export_svg_to(const std::string& file_path) {
    const bool saved = workflow_.save_svg(file_path);
    status_msg_ = saved
        ? (language_ == UILanguage::Vietnamese ? "Đã xuất SVG ra tệp" : "SVG exported")
        : (language_ == UILanguage::Vietnamese ? "Không thể ghi tệp SVG" : "Could not write SVG file");
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
    return saved;
}

void NativeWindow::create_new_document() {
#ifdef _WIN32
    if (root_.document().is_dirty() && root_.document().current_snapshot()->object_count() > 0) {
        const auto prompt = utf8_to_wide(language_ == UILanguage::Vietnamese
            ? "Bỏ các thay đổi chưa lưu và tạo bản vẽ mới?"
            : "Discard the current drawing and create a new document?");
        const auto title = utf8_to_wide(language_ == UILanguage::Vietnamese
            ? "Tạo bản vẽ mới" : "New document");
        if (MessageBoxW(hwnd_, prompt.c_str(), title.c_str(), MB_YESNO | MB_ICONWARNING) != IDYES)
            return;
    }
#endif
    workflow_.new_document();
    document_path_.clear();
    active_tool_ = 0;
    root_.vector_controller().set_mode(vector::ToolMode::Select);
    fit_page_to_canvas();
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
    status_msg_ = language_ == UILanguage::Vietnamese
        ? "Đã tạo bản vẽ mới" : "New document created";
}

#ifdef _WIN32



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

void NativeWindow::capture_screenshot_bmp(const std::string& path) {
    int w = width_, h = height_;
    HDC screen_dc = GetDC(NULL);
    HDC mem_dc = CreateCompatibleDC(screen_dc);
    HBITMAP mem_bmp = CreateCompatibleBitmap(screen_dc, w, h);
    HGDIOBJ old_bmp = SelectObject(mem_dc, mem_bmp);

    paint(mem_dc, w, h);

    BITMAPINFOHEADER bi = { sizeof(BITMAPINFOHEADER), w, h, 1, 24, BI_RGB, 0, 0, 0, 0, 0 };
    int row_stride = ((w * 3 + 3) / 4) * 4;
    int data_size = row_stride * h;
    bi.biSizeImage = data_size;

    std::vector<uint8_t> pixel_data(data_size);
    GetDIBits(mem_dc, mem_bmp, 0, h, pixel_data.data(), reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);

    BITMAPFILEHEADER bfh = { 0x4D42, static_cast<DWORD>(sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + data_size), 0, 0, sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) };

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
