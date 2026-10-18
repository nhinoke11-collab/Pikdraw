#pragma once

#include "pikdraw/desktop/shell/native_window.hpp"
#include "pikdraw/desktop/tools/canvas_text_editor.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <cwctype>
#include <unordered_map>

#ifdef _WIN32
#include <commdlg.h>
#endif

namespace pikdraw::desktop {

namespace TextPropertiesLayout {
inline constexpr int title_top = 2;
inline constexpr int summary_top = 25;
inline constexpr int character_top = 50;
inline constexpr int font_button_top = 72;
inline constexpr int font_button_height = 28;
inline constexpr int size_field_top = font_button_top;
inline constexpr int style_buttons_top = font_button_top;
inline constexpr int style_buttons_height = 28;
inline constexpr int paragraph_top = 112;
inline constexpr int alignment_buttons_top = 210;
inline constexpr int alignment_buttons_height = 28;
inline constexpr int spacing_field_top = 173;
inline constexpr int frame_field_top = 136;
inline constexpr int image_search_top = 246;
inline constexpr int image_search_height = 30;
inline constexpr int field_height = 28;
inline constexpr int hint_top = 284;
inline constexpr int font_row_gap = 4;
inline constexpr int font_size_width = 66;
inline constexpr int style_button_width = 20;
} // namespace TextPropertiesLayout

enum class TextPropertyField { FontSize, FrameWidth, LineSpacing };

inline NativeWindow::NumericFieldId text_numeric_field(TextPropertyField field) {
    switch (field) {
        case TextPropertyField::FontSize: return NativeWindow::NumericFieldId::TextFontSize;
        case TextPropertyField::FrameWidth: return NativeWindow::NumericFieldId::EffectSigmaX;
        case TextPropertyField::LineSpacing: return NativeWindow::NumericFieldId::EffectSigmaY;
    }
    return NativeWindow::NumericFieldId::None;
}

struct TextPropertiesView {
    bool context{}, editable{}, mixed_frame_width{}, mixed_line_spacing{}, mixed_font{};
    bool mixed_size{}, mixed_bold{}, mixed_italic{}, mixed_underline{}, mixed_alignment{};
    size_t count{};
    double frame_width_mm{}, line_spacing_ratio{1.2}, size_pt{24.0};
    bool bold{}, italic{}, underline{};
    typography::TextAlignment alignment{typography::TextAlignment::Left};
    std::string family;
};

inline std::vector<std::string>& text_font_registry() {
    static std::vector<std::string> families{
        "Be Vietnam Pro", "Segoe UI", "Arial", "Times New Roman", "JetBrains Mono"};
    return families;
}

inline std::string text_font_family_at(int index) {
    const auto& families = text_font_registry();
    return families.empty() ? std::string{"Segoe UI"}
        : families[static_cast<size_t>(std::clamp(index, 0, static_cast<int>(families.size()) - 1))];
}

inline int remember_text_font_family(std::string family) {
    if (family.empty()) return 0;
    auto& families = text_font_registry();
    const auto found = std::find(families.begin(), families.end(), family);
    if (found != families.end()) return static_cast<int>(found - families.begin());
    families.push_back(std::move(family));
    return static_cast<int>(families.size() - 1);
}

inline bool close_text_property_value(double a, double b) {
    return std::abs(a - b) <= 1e-9 * std::max({1.0, std::abs(a), std::abs(b)});
}

inline TextPropertiesView text_properties_view(NativeWindow& window) {
    TextPropertiesView view;
    const auto base = window.root().document().current_snapshot();
    const auto shown = window.workflow().snapshot_for_canvas();
    if (!base || !shown) return view;
    const bool draft = window.is_inline_editing();
    std::vector<document::ObjectId> ids;
    const auto& selected = window.root().vector_controller().selection().selected_ids();
    if (draft) ids = selected;
    if (draft && ids.empty()) {
        for (const auto& [id, object] : shown->objects().all())
            if (std::holds_alternative<document::TextObject>(object) &&
                !base->objects().contains(id)) ids.push_back(id);
    }
    if (!draft) ids = selected;
    if (ids.empty()) return view;

    std::vector<document::TextObject> texts;
    texts.reserve(ids.size());
    for (const auto id : ids) {
        const auto object = shown->objects().get(id);
        if (!object || !std::holds_alternative<document::TextObject>(*object)) return view;
        texts.push_back(std::get<document::TextObject>(*object));
        if (!draft && !tools::editable_text_parent(*base, id)) view.editable = false;
    }
    view.context = true;
    view.count = texts.size();
    view.editable = draft;
    if (!draft) {
        view.editable = std::all_of(ids.begin(), ids.end(), [&](document::ObjectId id) {
            return tools::editable_text_parent(*base, id).has_value();
        });
    }
    const auto& first = texts.front();
    view.frame_width_mm = first.frame_width_mm;
    view.line_spacing_ratio = first.line_spacing_ratio;
    view.family = first.font.family_name;
    view.size_pt = first.font.size_pt;
    view.bold = first.font.weight >= 700;
    view.italic = first.font.italic;
    view.underline = first.underline;
    view.alignment = first.alignment;
    for (size_t i = 1; i < texts.size(); ++i) {
        const auto& text = texts[i];
        view.mixed_frame_width |= !close_text_property_value(first.frame_width_mm, text.frame_width_mm);
        view.mixed_line_spacing |= !close_text_property_value(first.line_spacing_ratio, text.line_spacing_ratio);
        view.mixed_font |= first.font.family_name != text.font.family_name ||
            !close_text_property_value(first.font.size_pt, text.font.size_pt) ||
            first.font.weight != text.font.weight || first.font.italic != text.font.italic ||
            first.underline != text.underline || first.alignment != text.alignment;
        view.mixed_size |= !close_text_property_value(first.font.size_pt, text.font.size_pt);
        view.mixed_bold |= (first.font.weight >= 700) != (text.font.weight >= 700);
        view.mixed_italic |= first.font.italic != text.font.italic;
        view.mixed_underline |= first.underline != text.underline;
        view.mixed_alignment |= first.alignment != text.alignment;
    }
    return view;
}

inline bool has_text_properties_target(NativeWindow& window) {
    return text_properties_view(window).context;
}

struct TextFontChoice {
    std::string family;
    double size_pt{24.0};
    bool bold{}, italic{};
};

inline TextFontChoice current_text_font_choice(const TextPropertiesView& view,
    int family_index, double size_pt, bool bold, bool italic) {
    if (view.context)
        return {view.family, view.size_pt, view.bold, view.italic};
    return {text_font_family_at(family_index), size_pt, bold, italic};
}

#ifdef _WIN32
inline std::wstring text_font_wide(std::string_view value) {
    if (value.empty()) return {};
    const int needed = MultiByteToWideChar(CP_UTF8, 0, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring result(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        result.data(), needed);
    return result;
}

struct InstalledTextFontFamily {
    std::wstring name;
    BYTE pitch_family{};
};

inline BOOL CALLBACK collect_installed_text_font(const LOGFONTW* font,
    const TEXTMETRICW*, DWORD, LPARAM context) {
    if (!font || !context) return 1;
    const std::wstring name(font->lfFaceName);
    if (name.empty() || name.front() == L'@') return 1;
    auto& families = *reinterpret_cast<std::vector<InstalledTextFontFamily>*>(context);
    if (std::none_of(families.begin(), families.end(), [&](const auto& item) {
            return _wcsicmp(item.name.c_str(), name.c_str()) == 0;
        }))
        families.push_back({name, static_cast<BYTE>(font->lfPitchAndFamily & 0xf0)});
    return 1;
}

inline const std::vector<InstalledTextFontFamily>& installed_text_font_families() {
    static const auto families = [] {
        std::vector<InstalledTextFontFamily> result;
        HDC dc = GetDC(nullptr);
        if (!dc) return result;
        LOGFONTW query{};
        query.lfCharSet = DEFAULT_CHARSET;
        EnumFontFamiliesExW(dc, &query, collect_installed_text_font,
            reinterpret_cast<LPARAM>(&result), 0);
        ReleaseDC(nullptr, dc);
        std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
            return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
        });
        return result;
    }();
    return families;
}

inline std::wstring lower_font_family_name(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return value;
}

inline bool font_name_contains(const std::wstring& lower_name, std::wstring_view fragment) {
    return lower_name.find(fragment) != std::wstring::npos;
}

inline bool is_utm_svn_font(const InstalledTextFontFamily& family) {
    const auto name = lower_font_family_name(family.name);
    return name.starts_with(L"utm") || name.starts_with(L"svn");
}

inline bool is_script_font(const InstalledTextFontFamily& family) {
    const auto name = lower_font_family_name(family.name);
    return (family.pitch_family & 0xf0) == FF_SCRIPT ||
        font_name_contains(name, L"script") || font_name_contains(name, L"handwriting") ||
        font_name_contains(name, L"cursive") || font_name_contains(name, L"calligraphy") ||
        font_name_contains(name, L"brush");
}

inline bool is_serif_font(const InstalledTextFontFamily& family) {
    const auto name = lower_font_family_name(family.name);
    return (family.pitch_family & 0xf0) == FF_ROMAN ||
        font_name_contains(name, L"serif") || font_name_contains(name, L"times") ||
        font_name_contains(name, L"garamond") || font_name_contains(name, L"georgia") ||
        font_name_contains(name, L"baskerville");
}

inline bool is_sans_font(const InstalledTextFontFamily& family) {
    const auto name = lower_font_family_name(family.name);
    return (family.pitch_family & 0xf0) == FF_SWISS ||
        font_name_contains(name, L"sans") || font_name_contains(name, L"arial") ||
        font_name_contains(name, L"segoe") || font_name_contains(name, L"calibri") ||
        font_name_contains(name, L"roboto") || font_name_contains(name, L"inter") ||
        font_name_contains(name, L"aptos");
}

inline std::wstring escape_font_menu_label(std::wstring value) {
    std::wstring escaped;
    escaped.reserve(value.size());
    for (const wchar_t ch : value) {
        escaped.push_back(ch);
        if (ch == L'&') escaped.push_back(L'&');
    }
    return escaped;
}

inline std::optional<std::string> choose_installed_text_font(HWND owner,
    POINT screen_point, std::string_view current_family) {
    const auto& families = installed_text_font_families();
    if (families.empty()) return std::nullopt;
    HMENU root = CreatePopupMenu();
    if (!root) return std::nullopt;
    std::unordered_map<UINT, std::wstring> commands;
    const auto current = text_font_wide(current_family);
    UINT next_command = 100;
    const auto add_category = [&](const wchar_t* title, const auto& matches) {
        HMENU submenu = CreatePopupMenu();
        if (!submenu) return;
        for (const auto& family : families) {
            if (!matches(family)) continue;
            const UINT id = next_command++;
            commands.emplace(id, family.name);
            const bool checked = _wcsicmp(family.name.c_str(), current.c_str()) == 0;
            AppendMenuW(submenu, MF_STRING | (checked ? MF_CHECKED : 0), id,
                escape_font_menu_label(family.name).c_str());
        }
        if (GetMenuItemCount(submenu) == 0) {
            DestroyMenu(submenu);
            return;
        }
        AppendMenuW(root, MF_POPUP | MF_STRING,
            reinterpret_cast<UINT_PTR>(submenu), title);
    };
    add_category(L"All installed fonts", [](const auto&) { return true; });
    add_category(L"Sans serif", is_sans_font);
    add_category(L"Serif", is_serif_font);
    add_category(L"Script", is_script_font);
    add_category(L"UTM / SVN", is_utm_svn_font);
    const UINT selected = TrackPopupMenuEx(root,
        TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_RIGHTBUTTON,
        screen_point.x, screen_point.y, owner, nullptr);
    DestroyMenu(root);
    const auto found = commands.find(selected);
    return found == commands.end()
        ? std::nullopt
        : std::optional<std::string>{NativeWindow::wide_to_utf8(found->second)};
}

inline std::optional<std::string> choose_and_store_installed_text_font(HWND owner, POINT screen_point,
    const TextPropertiesView& view, int& family_index, double& size_pt, bool& bold, bool& italic,
    bool& underline, typography::TextAlignment& alignment) {
    if (view.context && view.mixed_font) return std::nullopt;
    const auto family = choose_installed_text_font(owner, screen_point, view.context ? view.family
        : text_font_family_at(family_index));
    if (!family) return std::nullopt;
    family_index = remember_text_font_family(*family);
    if (view.context) {
        size_pt = view.size_pt; bold = view.bold; italic = view.italic;
        underline = view.underline; alignment = view.alignment;
    }
    return family;
}

inline std::optional<TextFontChoice> choose_text_font(HWND owner, TextFontChoice current) {
    LOGFONTW font{};
    auto family = text_font_wide(current.family);
    if (family.empty()) family = L"Segoe UI";
    std::copy_n(family.data(), std::min<size_t>(family.size(), LF_FACESIZE - 1), font.lfFaceName);
    HDC dc = GetDC(owner);
    const int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSY) : 96;
    if (dc) ReleaseDC(owner, dc);
    font.lfHeight = -MulDiv(static_cast<int>(std::lround(current.size_pt)), dpi, 72);
    font.lfWeight = current.bold ? FW_BOLD : FW_NORMAL;
    font.lfItalic = current.italic ? TRUE : FALSE;
    CHOOSEFONTW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpLogFont = &font;
    dialog.iPointSize = static_cast<int>(std::lround(current.size_pt * 10.0));
    dialog.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_LIMITSIZE;
    dialog.nSizeMin = 1;
    dialog.nSizeMax = 2880;
    if (!ChooseFontW(&dialog)) return std::nullopt;
    const int length = static_cast<int>(wcsnlen(font.lfFaceName, LF_FACESIZE));
    if (length == 0) return std::nullopt;
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, font.lfFaceName, length,
        nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return std::nullopt;
    std::string selected(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, font.lfFaceName, length,
        selected.data(), bytes, nullptr, nullptr);
    return TextFontChoice{std::move(selected), dialog.iPointSize / 10.0,
                          font.lfWeight >= FW_BOLD, font.lfItalic != FALSE};
}

inline std::optional<TextFontChoice> choose_and_store_text_font(HWND owner,
    const TextPropertiesView& view, int& family_index, double& size_pt,
    bool& bold, bool& italic) {
    auto choice = choose_text_font(owner,
        current_text_font_choice(view, family_index, size_pt, bold, italic));
    if (!choice) return std::nullopt;
    family_index = remember_text_font_family(choice->family);
    size_pt = choice->size_pt;
    bold = choice->bold;
    italic = choice->italic;
    return choice;
}

inline void paint_text_properties_panel(HDC dc, int inspector_x, int width, int top,
    int bottom, ThemeMode theme, UILanguage language, const TextPropertiesView& view,
    NativeWindow::NumericFieldId active_field, const std::string& numeric_buffer) {
    namespace Layout = TextPropertiesLayout;
    const bool vi = language == UILanguage::Vietnamese;
    const bool dark = theme == ThemeMode::CyberDark;
    const COLORREF text = dark ? RGB(232, 239, 248) : RGB(27, 39, 54);
    const COLORREF muted = dark ? RGB(147, 164, 184) : RGB(98, 113, 131);
    const COLORREF accent = dark ? RGB(0, 242, 195) : RGB(0, 126, 99);
    const COLORREF surface = dark ? RGB(18, 29, 43) : RGB(241, 245, 249);
    const COLORREF active = dark ? RGB(24, 43, 59) : RGB(225, 240, 238);
    const COLORREF border = dark ? RGB(48, 64, 82) : RGB(194, 205, 217);
    const COLORREF disabled = dark ? RGB(72, 84, 99) : RGB(165, 176, 189);
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, inspector_x, top, inspector_x + width, bottom);
    const int left = inspector_x + 14, right = inspector_x + width - 14;
    auto label = [&](const wchar_t* caption, int offset, COLORREF color) {
        RECT rect{left, top + offset, right, top + offset + 20};
        SetTextColor(dc, color);
        DrawTextW(dc, caption, -1, &rect,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    };
    auto button = [&](int x, int offset, int button_width, int button_height,
                      const std::wstring& caption, bool enabled, bool selected = false) {
        RECT rect{x, top + offset, x + button_width, top + offset + button_height};
        HBRUSH brush = CreateSolidBrush(!enabled ? (dark ? RGB(15, 23, 34) : RGB(247, 249, 251))
            : selected ? active : surface);
        HPEN pen = CreatePen(PS_SOLID, selected ? 2 : 1,
            !enabled ? disabled : selected ? accent : border);
        const auto old_pen = SelectObject(dc, pen);
        const auto old_brush = SelectObject(dc, brush);
        RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, 5, 5);
        SelectObject(dc, old_brush); SelectObject(dc, old_pen);
        DeleteObject(brush); DeleteObject(pen);
        RECT caption_rect{rect.left + 8, rect.top, rect.right - 8, rect.bottom};
        SetTextColor(dc, !enabled ? muted : selected ? accent : text);
        DrawTextW(dc, caption.c_str(), -1, &caption_rect,
            (button_width < 100 ? DT_CENTER : DT_LEFT) | DT_VCENTER | DT_SINGLELINE |
            DT_END_ELLIPSIS | DT_NOPREFIX);
    };
    auto field = [&](int offset, TextPropertyField property, const wchar_t* name,
                     std::wstring value, bool enabled) {
        const auto id = text_numeric_field(property);
        RECT name_rect{left, top + offset, left + 96, top + offset + Layout::field_height};
        SetTextColor(dc, enabled ? text : muted);
        DrawTextW(dc, name, -1, &name_rect,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        RECT input{left + 100, top + offset, right, top + offset + Layout::field_height};
        const bool focused = active_field == id;
        HBRUSH brush = CreateSolidBrush(!enabled ? (dark ? RGB(15, 23, 34) : RGB(247, 249, 251))
                                                 : focused ? active : surface);
        HPEN pen = CreatePen(PS_SOLID, focused ? 2 : 1,
            !enabled ? disabled : focused ? accent : border);
        const auto old_pen = SelectObject(dc, pen);
        const auto old_brush = SelectObject(dc, brush);
        RoundRect(dc, input.left, input.top, input.right, input.bottom, 5, 5);
        SelectObject(dc, old_brush); SelectObject(dc, old_pen);
        DeleteObject(brush); DeleteObject(pen);
        const auto shown = focused && !numeric_buffer.empty()
            ? NativeWindow::utf8_to_wide(numeric_buffer) + L"|" : value;
        RECT value_rect{input.left + 7, input.top, input.right - 5, input.bottom};
        SetTextColor(dc, !enabled ? muted : focused ? accent : text);
        DrawTextW(dc, shown.c_str(), -1, &value_rect,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    };
    SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
    label(vi ? L"THUỘC TÍNH · VĂN BẢN" : L"PROPERTIES · TEXT", Layout::title_top, accent);
    std::wstring summary = std::to_wstring(view.count) + (vi ? L" đối tượng chữ"
        : view.count == 1 ? L" text object" : L" text objects");
    if (!view.editable) summary += vi ? L" · chỉ đọc" : L" · locked / hidden";
    RECT summary_rect{left, top + Layout::summary_top, right, top + Layout::summary_top + 20};
    SetTextColor(dc, muted);
    DrawTextW(dc, summary.c_str(), -1, &summary_rect,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    label(vi ? L"KÝ TỰ" : L"CHARACTER", Layout::character_top, accent);
    const bool can_edit_character = view.editable && !view.mixed_font;
    const std::wstring family = view.mixed_font
        ? (vi ? L"Kiểu chữ hỗn hợp" : L"Mixed font settings")
        : NativeWindow::utf8_to_wide(view.family) + L"  ▾";
    const int font_row_width = right - left;
    const int style_row_width = 3 * Layout::style_button_width + 2 * Layout::font_row_gap;
    const int family_width = std::max(72, font_row_width - Layout::font_size_width - style_row_width -
                                           2 * Layout::font_row_gap);
    button(left, Layout::font_button_top, family_width, Layout::font_button_height,
        family, can_edit_character);
    std::wostringstream size;
    if (view.mixed_size) size << (vi ? L"Hỗn hợp" : L"Mixed");
    else size << std::fixed << std::setprecision(1) << view.size_pt;
    const int size_left = left + family_width + Layout::font_row_gap;
    RECT size_input{size_left, top + Layout::size_field_top,
                    size_left + Layout::font_size_width, top + Layout::size_field_top + Layout::field_height};
    const auto size_id = text_numeric_field(TextPropertyField::FontSize);
    const bool size_focused = active_field == size_id;
    HBRUSH size_brush = CreateSolidBrush(!can_edit_character ? (dark ? RGB(15, 23, 34) : RGB(247, 249, 251))
                                               : size_focused ? active : surface);
    HPEN size_pen = CreatePen(PS_SOLID, size_focused ? 2 : 1,
                              !can_edit_character ? disabled : size_focused ? accent : border);
    const auto old_size_pen = SelectObject(dc, size_pen);
    const auto old_size_brush = SelectObject(dc, size_brush);
    RoundRect(dc, size_input.left, size_input.top, size_input.right, size_input.bottom, 5, 5);
    SelectObject(dc, old_size_brush); SelectObject(dc, old_size_pen);
    DeleteObject(size_brush); DeleteObject(size_pen);
    const auto size_text = size_focused && !numeric_buffer.empty()
        ? NativeWindow::utf8_to_wide(numeric_buffer) + L"|" : size.str() + L" pt";
    RECT size_rect{size_input.left + 5, size_input.top, size_input.right - 4, size_input.bottom};
    SetTextColor(dc, !can_edit_character ? muted : size_focused ? accent : text);
    DrawTextW(dc, size_text.c_str(), -1, &size_rect,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    const int style_left = size_input.right + Layout::font_row_gap;
    const wchar_t* style_labels[] = {L"B", L"I", L"U"};
    const bool style_selected[] = {view.bold, view.italic, view.underline};
    for (int i = 0; i < 3; ++i)
        button(style_left + i * (Layout::style_button_width + Layout::font_row_gap), Layout::style_buttons_top,
            Layout::style_button_width, Layout::style_buttons_height, style_labels[i],
            can_edit_character, style_selected[i]);
    const int triple_width = std::max(1, (right - left - 12) / 3);
    label(vi ? L"ĐOẠN VĂN VÀ KHUNG CHỮ" : L"PARAGRAPH & TEXT FRAME", Layout::paragraph_top, accent);
    std::wostringstream spacing;
    if (view.mixed_line_spacing) spacing << (vi ? L"Hỗn hợp" : L"Mixed");
    else spacing << std::fixed << std::setprecision(1) << view.line_spacing_ratio * 100.0 << L"%";
    field(Layout::spacing_field_top, TextPropertyField::LineSpacing,
          vi ? L"Giãn dòng" : L"Line spacing", spacing.str(), view.editable);
    std::wostringstream frame;
    if (view.mixed_frame_width) frame << (vi ? L"Hỗn hợp" : L"Mixed");
    else frame << std::fixed << std::setprecision(3) << view.frame_width_mm << L" mm";
    field(Layout::frame_field_top, TextPropertyField::FrameWidth,
          vi ? L"Khung chữ (mm)" : L"Text frame (mm)", frame.str(), view.editable);
    const wchar_t* alignment_labels[] = {L"Left", L"Center", L"Right"};
    if (vi) {
        alignment_labels[0] = L"Trái";
        alignment_labels[1] = L"Giữa";
        alignment_labels[2] = L"Phải";
    }
    const bool alignment_selected[] = {
        view.alignment == typography::TextAlignment::Left,
        view.alignment == typography::TextAlignment::Center,
        view.alignment == typography::TextAlignment::Right};
    for (int i = 0; i < 3; ++i)
        button(left + i * (triple_width + 6), Layout::alignment_buttons_top,
            triple_width, Layout::alignment_buttons_height, alignment_labels[i],
            can_edit_character, alignment_selected[i] && !view.mixed_font);
    button(left, Layout::image_search_top, right - left, Layout::image_search_height,
        vi ? L"Tìm font bằng ảnh · Sắp có" : L"Find font from image · Coming soon", true);
    RECT hint{left, top + Layout::hint_top, right, top + Layout::hint_top + 58};
    SetTextColor(dc, muted);
    DrawTextW(dc, vi
        ? L"0 mm = chữ tự do; khung dương tự xuống dòng. Giãn dòng 25–1000%. Font hỗn hợp cần thống nhất trước khi đổi style."
        : L"0 mm = point text; positive width wraps. Line spacing is 25–1000%. Mixed font styles must be unified before editing.",
        -1, &hint, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
    RestoreDC(dc, saved);
}
#endif

} // namespace pikdraw::desktop
