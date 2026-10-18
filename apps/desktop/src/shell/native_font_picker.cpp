#include "pikdraw/desktop/shell/native_font_picker.hpp"
#include "pikdraw/desktop/shell/native_window.hpp"
#include "pikdraw/typography/shaping/font_resolver.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>

namespace pikdraw::desktop {
#ifdef _WIN32
namespace {
constexpr int kItemH = 46;
constexpr int kHeaderH = 82;
constexpr int kFooterH = 34;

std::wstring to_wide(const std::string& str) {
    if (str.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    if (len <= 1) return {};
    std::wstring result(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, result.data(), len);
    return result;
}

std::string to_lower_str(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

void fill_rect(HDC dc, RECT r, COLORREF col) {
    const HBRUSH b = CreateSolidBrush(col);
    FillRect(dc, &r, b);
    DeleteObject(b);
}

void draw_frame(HDC dc, RECT r, COLORREF col) {
    const HPEN p = CreatePen(PS_SOLID, 1, col);
    const HGDIOBJ op = SelectObject(dc, p);
    const HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, r.left, r.top, r.right, r.bottom);
    SelectObject(dc, op);
    SelectObject(dc, ob);
    DeleteObject(p);
}
} // namespace

NativeFontPicker::NativeFontPicker() {
    favorites_ = {"Be Vietnam Pro", "Arial", "Times New Roman", "Segoe UI", "Montserrat"};
    recent_fonts_ = {"Be Vietnam Pro", "Segoe UI", "Arial"};
}

NativeFontPicker::~NativeFontPicker() = default;

FontCategoryFilter NativeFontPicker::classify_font(const std::string& name) {
    const std::string lower = to_lower_str(name);
    for (const char* kw : {"script", "brush", "hand", "calligraph", "thuphap", "dancing", "pacifico",
                           "caveat", "lobster", "kristen", "comic", "corsiva", "chancery", "vladimir"}) {
        if (lower.find(kw) != std::string::npos) return FontCategoryFilter::Script;
    }
    for (const char* kw : {"impact", "bebas", "anton", "black", "stencil", "bauhaus", "broadway",
                           "showcard", "chiller", "jokerman", "algerian", "harrington", "bungee", "monoton"}) {
        if (lower.find(kw) != std::string::npos) return FontCategoryFilter::Display;
    }
    for (const char* kw : {"times", "georgia", "garamond", "bodoni", "baskerville", "cambria", "palatino",
                           "minion", "caslon", "didot", "merriweather", "playfair", "lora", "constantia"}) {
        if (lower.find(kw) != std::string::npos) return FontCategoryFilter::Serif;
    }
    return FontCategoryFilter::SansSerif;
}

bool NativeFontPicker::check_vietnamese_support(const std::string& name) {
    const std::string lower = to_lower_str(name);
    for (const char* kw : {"vietnam", "svn-", "utm ", "vni-", "uvn ", "hl ", "tcvn", "arial",
                           "times new roman", "segoe ui", "tahoma", "calibri", "verdana", "roboto",
                           "open sans", "montserrat", "noto", "helvetica", "inter", "lato", "consolas"}) {
        if (lower.find(kw) != std::string::npos) return true;
    }
    return false;
}

void NativeFontPicker::reload_fonts() {
    all_fonts_.clear();
    const auto families = typography::FontResolver::instance().available_font_families();
    all_fonts_.reserve(families.size());
    for (const auto& fam : families) {
        FontPickerItem item;
        item.family_name = fam;
        item.category = classify_font(fam);
        item.has_vietnamese = check_vietnamese_support(fam);
        item.is_favorite = favorites_.count(fam) > 0;
        item.is_recent = std::find(recent_fonts_.begin(), recent_fonts_.end(), fam) != recent_fonts_.end();
        all_fonts_.push_back(std::move(item));
    }
    apply_filter();
}

void NativeFontPicker::apply_filter() {
    filtered_indices_.clear();
    const std::string q = to_lower_str(search_query_);
    for (size_t i = 0; i < all_fonts_.size(); ++i) {
        const auto& item = all_fonts_[i];
        if (active_filter_ == FontCategoryFilter::Recent && !item.is_recent) continue;
        if (active_filter_ == FontCategoryFilter::Favorites && !item.is_favorite) continue;
        if (active_filter_ == FontCategoryFilter::SansSerif && item.category != FontCategoryFilter::SansSerif) continue;
        if (active_filter_ == FontCategoryFilter::Serif && item.category != FontCategoryFilter::Serif) continue;
        if (active_filter_ == FontCategoryFilter::Script && item.category != FontCategoryFilter::Script) continue;
        if (active_filter_ == FontCategoryFilter::Display && item.category != FontCategoryFilter::Display) continue;
        if (active_filter_ == FontCategoryFilter::Vietnamese && !item.has_vietnamese) continue;

        if (!q.empty() && to_lower_str(item.family_name).find(q) == std::string::npos) continue;
        filtered_indices_.push_back(static_cast<int>(i));
    }
    const int list_h = std::max(50, static_cast<int>(bounds_.bottom - bounds_.top) - kHeaderH - kFooterH);
    max_scroll_ = std::max(0, static_cast<int>(filtered_indices_.size()) * kItemH - list_h);
    scroll_offset_ = std::clamp(scroll_offset_, 0, max_scroll_);

    selected_filtered_idx_ = -1;
    for (size_t i = 0; i < filtered_indices_.size(); ++i) {
        if (all_fonts_[filtered_indices_[i]].family_name == current_font_) {
            selected_filtered_idx_ = static_cast<int>(i);
            break;
        }
    }
}

void NativeFontPicker::ensure_visible(int index) {
    if (index < 0 || index >= static_cast<int>(filtered_indices_.size())) return;
    const int list_h = std::max(50, static_cast<int>(bounds_.bottom - bounds_.top) - kHeaderH - kFooterH);
    const int item_top = index * kItemH;
    const int item_bot = item_top + kItemH;
    if (item_top < scroll_offset_) scroll_offset_ = item_top;
    else if (item_bot > scroll_offset_ + list_h) scroll_offset_ = item_bot - list_h;
    scroll_offset_ = std::clamp(scroll_offset_, 0, max_scroll_);
}

void NativeFontPicker::open(int anchor_x, int anchor_y, int max_w, int max_h,
                            std::string current_font, std::string sample_text) {
    baseline_font_ = current_font.empty() ? "Be Vietnam Pro" : current_font;
    current_font_ = baseline_font_;
    if (!sample_text.empty() && sample_text.size() <= 80) sample_text_ = std::move(sample_text);
    else sample_text_ = "PikDraw Tiếng Việt Ánh Sáng 123";

    constexpr int kPickerW = 440;
    constexpr int kPickerH = 520;
    int x = std::clamp(anchor_x, 8, std::max(8, max_w - kPickerW - 8));
    int y = anchor_y + 4;
    if (y + kPickerH > max_h - 24) y = std::max(8, anchor_y - kPickerH - 4);
    bounds_ = {x, y, x + kPickerW, y + kPickerH};

    search_query_.clear();
    scroll_offset_ = 0;
    hover_filtered_idx_ = -1;
    is_dragging_slider_ = false;
    is_dragging_scrollbar_ = false;

    if (all_fonts_.empty()) reload_fonts();
    else apply_filter();

    if (selected_filtered_idx_ >= 0) ensure_visible(selected_filtered_idx_);
    is_open_ = true;
}

void NativeFontPicker::close() {
    is_open_ = false;
    hover_filtered_idx_ = -1;
    is_dragging_slider_ = false;
    is_dragging_scrollbar_ = false;
}

void NativeFontPicker::set_active_filter(FontCategoryFilter filter) {
    if (active_filter_ == filter) return;
    active_filter_ = filter;
    scroll_offset_ = 0;
    apply_filter();
}

void NativeFontPicker::set_search_query(std::string query) {
    search_query_ = std::move(query);
    scroll_offset_ = 0;
    apply_filter();
}

void NativeFontPicker::select_index(int idx) {
    if (idx < 0 || idx >= static_cast<int>(filtered_indices_.size())) return;
    selected_filtered_idx_ = idx;
    current_font_ = all_fonts_[filtered_indices_[idx]].family_name;
    ensure_visible(idx);
    if (on_preview_) on_preview_(current_font_);
}

void NativeFontPicker::paint(HDC dc) {
    if (!is_open_) return;
    const int w = bounds_.right - bounds_.left;
    const int h = bounds_.bottom - bounds_.top;
    const int bx = bounds_.left, by = bounds_.top;

    fill_rect(dc, bounds_, RGB(30, 34, 43));
    draw_frame(dc, bounds_, RGB(59, 66, 82));

    // Header background & search bar
    const RECT header_rect{bx, by, bounds_.right, by + kHeaderH};
    fill_rect(dc, header_rect, RGB(37, 42, 54));

    const RECT search_box{bx + 12, by + 10, bounds_.right - 12, by + 38};
    fill_rect(dc, search_box, RGB(22, 25, 34));
    draw_frame(dc, search_box, RGB(51, 58, 76));

    SetBkMode(dc, TRANSPARENT);
    const HFONT ui_font = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        VARIABLE_PITCH, L"Segoe UI");
    const HFONT ui_bold = CreateFontW(-13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        VARIABLE_PITCH, L"Segoe UI");
    const HGDIOBJ old_font = SelectObject(dc, ui_font);

    // Search text / placeholder
    RECT search_text_rect{search_box.left + 28, search_box.top + 5, search_box.right - 28, search_box.bottom - 4};
    // Search icon
    SetTextColor(dc, RGB(130, 140, 160));
    RECT icon_rect{search_box.left + 8, search_box.top + 4, search_box.left + 24, search_box.bottom - 4};
    DrawTextW(dc, L"\x26B2", -1, &icon_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    if (search_query_.empty()) {
        SetTextColor(dc, RGB(110, 120, 138));
        DrawTextW(dc, L"Tìm kiếm phông chữ (Search fonts)...", -1, &search_text_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    } else {
        SetTextColor(dc, RGB(240, 244, 250));
        std::wstring qw = to_wide(search_query_ + "|");
        DrawTextW(dc, qw.c_str(), -1, &search_text_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // 'x' clear button
        RECT clear_r{search_box.right - 22, search_box.top + 4, search_box.right - 6, search_box.bottom - 4};
        SetTextColor(dc, RGB(180, 190, 205));
        DrawTextW(dc, L"\x2715", -1, &clear_r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // Filter Chips
    struct Chip { const wchar_t* label; FontCategoryFilter cat; int w; };
    const Chip chips[] = {
        {L"T\x1EA5t c\x1EA3", FontCategoryFilter::All, 54},
        {L"G\x1EA7n \x0111\xE2y", FontCategoryFilter::Recent, 58},
        {L"\x2605 Th\xEDch", FontCategoryFilter::Favorites, 52},
        {L"Sans", FontCategoryFilter::SansSerif, 44},
        {L"Serif", FontCategoryFilter::Serif, 42},
        {L"Script", FontCategoryFilter::Script, 46},
        {L"Display", FontCategoryFilter::Display, 52},
        {L"Vi\x1EC7t", FontCategoryFilter::Vietnamese, 42}
    };
    int chip_x = bx + 12;
    const int chip_y = by + 46;
    for (const auto& ch : chips) {
        const bool active = (active_filter_ == ch.cat);
        const RECT cr{chip_x, chip_y, chip_x + ch.w, chip_y + 24};
        fill_rect(dc, cr, active ? RGB(0, 120, 212) : RGB(44, 51, 67));
        draw_frame(dc, cr, active ? RGB(0, 160, 255) : RGB(60, 70, 90));
        SetTextColor(dc, active ? RGB(255, 255, 255) : RGB(170, 180, 195));
        RECT text_cr = cr;
        DrawTextW(dc, ch.label, -1, &text_cr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        chip_x += ch.w + 4;
        if (chip_x > bounds_.right - 40) break;
    }

    // List Area
    const int list_top = by + kHeaderH;
    const int list_bot = bounds_.bottom - kFooterH;
    const int list_w = w - (max_scroll_ > 0 ? 14 : 0);
    HRGN clip_rgn = CreateRectRgn(bx, list_top, bounds_.right, list_bot);
    SelectClipRgn(dc, clip_rgn);

    const int first_item = scroll_offset_ / kItemH;
    const int last_item = std::min(static_cast<int>(filtered_indices_.size()),
                                   (scroll_offset_ + (list_bot - list_top) + kItemH - 1) / kItemH);

    for (int i = first_item; i < last_item; ++i) {
        const int item_y = list_top + i * kItemH - scroll_offset_;
        const auto& item = all_fonts_[filtered_indices_[i]];
        const bool is_sel = (i == selected_filtered_idx_);
        const bool is_hov = (i == hover_filtered_idx_);

        const RECT ir{bx, item_y, bx + list_w, item_y + kItemH};
        if (is_sel) fill_rect(dc, ir, RGB(36, 51, 76));
        else if (is_hov) fill_rect(dc, ir, RGB(44, 52, 68));
        else if (i % 2 == 1) fill_rect(dc, ir, RGB(33, 38, 49));

        if (is_sel) {
            const RECT bar{bx, item_y, bx + 3, item_y + kItemH};
            fill_rect(dc, bar, RGB(0, 242, 195));
        }

        // Star button
        RECT star_r{bx + 8, item_y + 12, bx + 26, item_y + 32};
        SetTextColor(dc, item.is_favorite ? RGB(245, 166, 35) : RGB(85, 93, 110));
        DrawTextW(dc, item.is_favorite ? L"\x2605" : L"\x2606", -1, &star_r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // Font Family Name
        SelectObject(dc, ui_bold);
        SetTextColor(dc, is_sel ? RGB(0, 242, 195) : RGB(236, 239, 244));
        RECT name_r{bx + 32, item_y + 4, bx + list_w - 70, item_y + 22};
        std::wstring fam_w = to_wide(item.family_name);
        DrawTextW(dc, fam_w.c_str(), -1, &name_r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        // Category Tag
        SelectObject(dc, ui_font);
        const wchar_t* tag_w = item.category == FontCategoryFilter::Serif ? L"[Serif]" :
                               item.category == FontCategoryFilter::Script ? L"[Script]" :
                               item.category == FontCategoryFilter::Display ? L"[Disp]" : L"[Sans]";
        SetTextColor(dc, RGB(115, 125, 142));
        RECT tag_r{bx + list_w - 68, item_y + 4, bx + list_w - 6, item_y + 22};
        DrawTextW(dc, tag_w, -1, &tag_r, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

        // Live Font Sample line rendered in this font!
        const int font_px = static_cast<int>(std::round(sample_size_pt_ * 96.0 / 72.0));
        const HFONT sample_font = CreateFontW(-std::clamp(font_px, 12, 28), 0, 0, 0,
            FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, fam_w.c_str());
        SelectObject(dc, sample_font);
        SetTextColor(dc, RGB(190, 200, 218));
        RECT sample_r{bx + 32, item_y + 22, bx + list_w - 8, item_y + kItemH - 2};
        std::wstring s_w = to_wide(sample_text_);
        DrawTextW(dc, s_w.c_str(), -1, &sample_r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        DeleteObject(sample_font);
    }
    SelectClipRgn(dc, nullptr);
    DeleteObject(clip_rgn);

    // Scrollbar
    if (max_scroll_ > 0) {
        const int sb_x = bounds_.right - 10;
        const int sb_h = list_bot - list_top;
        const RECT s_track{sb_x, list_top, bounds_.right - 2, list_bot};
        fill_rect(dc, s_track, RGB(26, 29, 38));
        const int thumb_h = std::max(24, sb_h * sb_h / (sb_h + max_scroll_));
        const int thumb_y = list_top + (scroll_offset_ * (sb_h - thumb_h)) / max_scroll_;
        const RECT thumb_r{sb_x + 1, thumb_y, bounds_.right - 3, thumb_y + thumb_h};
        fill_rect(dc, thumb_r, is_dragging_scrollbar_ ? RGB(80, 92, 115) : RGB(62, 71, 89));
    }

    // Bottom Bar (Footer)
    const RECT footer_rect{bx, list_bot, bounds_.right, bounds_.bottom};
    fill_rect(dc, footer_rect, RGB(24, 27, 34));
    draw_frame(dc, footer_rect, RGB(44, 51, 67));

    SelectObject(dc, ui_font);
    SetTextColor(dc, RGB(120, 130, 150));
    std::wstring count_str = std::to_wstring(filtered_indices_.size()) + L" ph\xF4ng ch\x1EEF (fonts)";
    RECT count_r{bx + 12, list_bot + 6, bx + 160, bounds_.bottom - 6};
    DrawTextW(dc, count_str.c_str(), -1, &count_r, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // Sample Size Slider: 'A' [---●---] 'A'
    const int slider_right = bounds_.right - 14;
    const int slider_left = slider_right - 100;
    const int slider_y = list_bot + kFooterH / 2;

    RECT lbl_s{slider_left - 18, list_bot + 6, slider_left - 4, bounds_.bottom - 6};
    DrawTextW(dc, L"A", -1, &lbl_s, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    const RECT track_r{slider_left, slider_y - 2, slider_right, slider_y + 2};
    fill_rect(dc, track_r, RGB(55, 63, 80));

    const double t = (sample_size_pt_ - 12.0) / (36.0 - 12.0);
    const int thumb_cx = slider_left + static_cast<int>(std::round(std::clamp(t, 0.0, 1.0) * (slider_right - slider_left)));
    const RECT s_thumb{thumb_cx - 5, slider_y - 5, thumb_cx + 5, slider_y + 5};
    fill_rect(dc, s_thumb, RGB(0, 242, 195));

    SelectObject(dc, ui_bold);
    RECT lbl_l{slider_right + 6, list_bot + 6, slider_right + 20, bounds_.bottom - 6};
    DrawTextW(dc, L"A", -1, &lbl_l, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    SelectObject(dc, old_font);
    DeleteObject(ui_font);
    DeleteObject(ui_bold);
}

bool NativeFontPicker::handle_mouse_down(int x, int y) {
    if (!is_open_) return false;
    if (x < bounds_.left || x > bounds_.right || y < bounds_.top || y > bounds_.bottom) {
        if (on_cancel_) on_cancel_();
        close();
        return true;
    }
    const int bx = bounds_.left, by = bounds_.top;
    const int list_top = by + kHeaderH;
    const int list_bot = bounds_.bottom - kFooterH;

    // Search bar click or clear click
    const RECT search_box{bx + 12, by + 10, bounds_.right - 12, by + 38};
    if (x >= search_box.left && x <= search_box.right && y >= search_box.top && y <= search_box.bottom) {
        if (!search_query_.empty() && x >= search_box.right - 24) {
            set_search_query("");
        }
        return true;
    }

    // Filter Chips click
    if (y >= by + 46 && y <= by + 72) {
        struct Chip { FontCategoryFilter cat; int w; };
        const Chip chips[] = {
            {FontCategoryFilter::All, 54}, {FontCategoryFilter::Recent, 58},
            {FontCategoryFilter::Favorites, 52}, {FontCategoryFilter::SansSerif, 44},
            {FontCategoryFilter::Serif, 42}, {FontCategoryFilter::Script, 46},
            {FontCategoryFilter::Display, 52}, {FontCategoryFilter::Vietnamese, 42}
        };
        int chip_x = bx + 12;
        for (const auto& ch : chips) {
            if (x >= chip_x && x <= chip_x + ch.w) {
                set_active_filter(ch.cat);
                return true;
            }
            chip_x += ch.w + 4;
        }
    }

    // Scrollbar track/thumb click
    if (max_scroll_ > 0 && x >= bounds_.right - 14 && y >= list_top && y <= list_bot) {
        is_dragging_scrollbar_ = true;
        drag_start_y_ = y;
        drag_start_scroll_ = scroll_offset_;
        return true;
    }

    // Slider click / drag
    const int slider_right = bounds_.right - 14;
    const int slider_left = slider_right - 100;
    if (y >= list_bot && x >= slider_left - 8 && x <= slider_right + 8) {
        is_dragging_slider_ = true;
        const double frac = std::clamp(static_cast<double>(x - slider_left) / (slider_right - slider_left), 0.0, 1.0);
        sample_size_pt_ = std::round(12.0 + frac * (36.0 - 12.0));
        return true;
    }

    // Item click
    if (y >= list_top && y <= list_bot) {
        const int idx = (y - list_top + scroll_offset_) / kItemH;
        if (idx >= 0 && idx < static_cast<int>(filtered_indices_.size())) {
            // Check star toggle
            if (x <= bx + 30) {
                auto& item = all_fonts_[filtered_indices_[idx]];
                item.is_favorite = !item.is_favorite;
                if (item.is_favorite) favorites_.insert(item.family_name);
                else favorites_.erase(item.family_name);
                return true;
            }
            select_index(idx);
            if (on_select_) on_select_(current_font_);
            close();
            return true;
        }
    }
    return true;
}

bool NativeFontPicker::handle_mouse_move(int x, int y) {
    if (!is_open_) return false;
    if (is_dragging_slider_) {
        const int slider_right = bounds_.right - 14;
        const int slider_left = slider_right - 100;
        const double frac = std::clamp(static_cast<double>(x - slider_left) / (slider_right - slider_left), 0.0, 1.0);
        sample_size_pt_ = std::round(12.0 + frac * (36.0 - 12.0));
        return true;
    }
    if (is_dragging_scrollbar_ && max_scroll_ > 0) {
        const int list_top = bounds_.top + kHeaderH;
        const int list_bot = bounds_.bottom - kFooterH;
        const int sb_h = list_bot - list_top;
        const int delta_y = y - drag_start_y_;
        scroll_offset_ = std::clamp(drag_start_scroll_ + (delta_y * max_scroll_) / std::max(1, sb_h - 24), 0, max_scroll_);
        return true;
    }

    const int list_top = bounds_.top + kHeaderH;
    const int list_bot = bounds_.bottom - kFooterH;
    if (x >= bounds_.left && x <= bounds_.right && y >= list_top && y <= list_bot) {
        const int idx = (y - list_top + scroll_offset_) / kItemH;
        if (idx >= 0 && idx < static_cast<int>(filtered_indices_.size()) && idx != hover_filtered_idx_) {
            hover_filtered_idx_ = idx;
            const auto& fam = all_fonts_[filtered_indices_[idx]].family_name;
            if (on_preview_) on_preview_(fam);
            return true;
        }
    } else {
        if (hover_filtered_idx_ != -1) {
            hover_filtered_idx_ = -1;
            return true;
        }
    }
    return x >= bounds_.left && x <= bounds_.right && y >= bounds_.top && y <= bounds_.bottom;
}

bool NativeFontPicker::handle_mouse_up(int /*x*/, int /*y*/) {
    if (!is_open_) return false;
    is_dragging_slider_ = false;
    is_dragging_scrollbar_ = false;
    return true;
}

bool NativeFontPicker::handle_mouse_wheel(int delta, int x, int y) {
    if (!is_open_) return false;
    if (x < bounds_.left || x > bounds_.right || y < bounds_.top || y > bounds_.bottom) return false;
    if (max_scroll_ <= 0) return true;
    scroll_offset_ = std::clamp(scroll_offset_ - (delta / 120) * kItemH * 2, 0, max_scroll_);
    return true;
}

bool NativeFontPicker::handle_key_down(uint32_t key, bool /*ctrl*/, bool /*shift*/) {
    if (!is_open_) return false;
    if (key == VK_ESCAPE) {
        if (on_cancel_) on_cancel_();
        close();
        return true;
    }
    if (key == VK_RETURN) {
        if (selected_filtered_idx_ >= 0 && selected_filtered_idx_ < static_cast<int>(filtered_indices_.size())) {
            if (on_select_) on_select_(current_font_);
        }
        close();
        return true;
    }
    if (key == VK_DOWN) {
        if (filtered_indices_.empty()) return true;
        select_index(std::min(selected_filtered_idx_ + 1, static_cast<int>(filtered_indices_.size()) - 1));
        return true;
    }
    if (key == VK_UP) {
        if (filtered_indices_.empty()) return true;
        select_index(std::max(selected_filtered_idx_ - 1, 0));
        return true;
    }
    if (key == VK_PRIOR) { // Page Up
        select_index(std::max(selected_filtered_idx_ - 6, 0));
        return true;
    }
    if (key == VK_NEXT) { // Page Down
        select_index(std::min(selected_filtered_idx_ + 6, static_cast<int>(filtered_indices_.size()) - 1));
        return true;
    }
    if (key == VK_BACK) {
        if (!search_query_.empty()) {
            search_query_.pop_back();
            apply_filter();
        }
        return true;
    }
    return true;
}

bool NativeFontPicker::handle_char_input(wchar_t ch) {
    if (!is_open_) return false;
    if (ch >= 32 && ch != 127) {
        int len = WideCharToMultiByte(CP_UTF8, 0, &ch, 1, nullptr, 0, nullptr, nullptr);
        if (len > 0) {
            std::string u8(len, '\0');
            WideCharToMultiByte(CP_UTF8, 0, &ch, 1, u8.data(), len, nullptr, nullptr);
            search_query_ += u8;
            apply_filter();
        }
        return true;
    }
    return false;
}

NativeFontPicker* NativeWindow::font_picker() noexcept {
    return font_picker_.get();
}
#endif
} // namespace pikdraw::desktop
