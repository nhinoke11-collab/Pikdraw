#pragma once

#include <string>
#include <string_view>

namespace pikdraw::desktop {

enum class ThemeMode {
    CyberDark,
    Light
};

struct ThemeTokens {
    std::string bg_app;
    std::string bg_header;
    std::string bg_ribbon;
    std::string bg_dock;
    std::string border_tech;
    std::string accent_cyber;
    std::string accent_cyber_hover;
    std::string accent_cyber_dim;
    std::string text_primary;
    std::string text_muted;
    std::string font_ui;
    std::string font_numeric;

    [[nodiscard]] static ThemeTokens cyber_dark() noexcept;
    [[nodiscard]] static ThemeTokens light() noexcept;
    [[nodiscard]] std::string generate_qss() const;
};

class ThemeManager {
public:
    ThemeManager() = default;

    [[nodiscard]] ThemeMode current_mode() const noexcept { return mode_; }
    [[nodiscard]] const ThemeTokens& current_tokens() const noexcept { return tokens_; }

    void set_theme_mode(ThemeMode mode) noexcept;
    void toggle_theme() noexcept;

private:
    ThemeMode mode_{ThemeMode::CyberDark};
    ThemeTokens tokens_{ThemeTokens::cyber_dark()};
};

} // namespace pikdraw::desktop
