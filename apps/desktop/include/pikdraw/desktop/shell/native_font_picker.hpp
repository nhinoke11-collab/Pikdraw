#pragma once
#include "pikdraw/document/model/object.hpp"
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace pikdraw::desktop {

enum class FontCategoryFilter {
    All = 0,
    Recent,
    Favorites,
    SansSerif,
    Serif,
    Script,
    Display,
    Vietnamese
};

struct FontPickerItem {
    std::string family_name;
    FontCategoryFilter category{FontCategoryFilter::SansSerif};
    bool has_vietnamese{true};
    bool is_favorite{false};
    bool is_recent{false};
};

class NativeFontPicker {
public:
    NativeFontPicker();
    ~NativeFontPicker();

    void open(int anchor_x, int anchor_y, int max_w, int max_h,
              std::string current_font, std::string sample_text = "");
    void close();
    [[nodiscard]] bool is_open() const noexcept { return is_open_; }
    [[nodiscard]] const RECT& bounds() const noexcept { return bounds_; }

    void set_on_preview(std::function<void(const std::string&)> cb) { on_preview_ = std::move(cb); }
    void set_on_select(std::function<void(const std::string&)> cb) { on_select_ = std::move(cb); }
    void set_on_cancel(std::function<void()> cb) { on_cancel_ = std::move(cb); }

    [[nodiscard]] const std::string& current_font() const noexcept { return current_font_; }
    [[nodiscard]] const std::string& baseline_font() const noexcept { return baseline_font_; }
    [[nodiscard]] double sample_size_pt() const noexcept { return sample_size_pt_; }
    [[nodiscard]] FontCategoryFilter active_filter() const noexcept { return active_filter_; }
    void set_active_filter(FontCategoryFilter filter);
    [[nodiscard]] const std::string& search_query() const noexcept { return search_query_; }
    void set_search_query(std::string query);
    [[nodiscard]] size_t filtered_count() const noexcept { return filtered_indices_.size(); }
    [[nodiscard]] int selected_index() const noexcept { return selected_filtered_idx_; }
    void select_index(int idx);

#ifdef _WIN32
    void paint(HDC dc);
    bool handle_mouse_down(int x, int y);
    bool handle_mouse_move(int x, int y);
    bool handle_mouse_up(int x, int y);
    bool handle_mouse_wheel(int delta, int x, int y);
    bool handle_key_down(uint32_t key, bool ctrl, bool shift);
    bool handle_char_input(wchar_t ch);
#endif

private:
    void reload_fonts();
    void apply_filter();
    void ensure_visible(int index);
    [[nodiscard]] static FontCategoryFilter classify_font(const std::string& name);
    [[nodiscard]] static bool check_vietnamese_support(const std::string& name);

    bool is_open_{false};
    RECT bounds_{0, 0, 440, 520};
    std::string baseline_font_{"Be Vietnam Pro"};
    std::string current_font_{"Be Vietnam Pro"};
    std::string sample_text_{"PikDraw Tiếng Việt Ánh Sáng 123"};
    std::string search_query_;
    FontCategoryFilter active_filter_{FontCategoryFilter::All};
    double sample_size_pt_{18.0};

    std::vector<FontPickerItem> all_fonts_;
    std::vector<int> filtered_indices_;
    std::unordered_set<std::string> favorites_;
    std::vector<std::string> recent_fonts_;

    int selected_filtered_idx_{-1};
    int hover_filtered_idx_{-1};
    int scroll_offset_{0};
    int max_scroll_{0};
    bool is_dragging_slider_{false};
    bool is_dragging_scrollbar_{false};
    int drag_start_y_{0};
    int drag_start_scroll_{0};

    std::function<void(const std::string&)> on_preview_;
    std::function<void(const std::string&)> on_select_;
    std::function<void()> on_cancel_;
};

} // namespace pikdraw::desktop
