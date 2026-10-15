#include "pikdraw/desktop/themes/theme_tokens.hpp"
#include <sstream>

namespace pikdraw::desktop {

ThemeTokens ThemeTokens::cyber_dark() noexcept {
    ThemeTokens t;
    t.bg_app = "#070b10";
    t.bg_header = "#0c131d";
    t.bg_ribbon = "#0e1622";
    t.bg_dock = "#0c131d";
    t.border_tech = "#1d2636";
    t.accent_cyber = "#00f2c3";
    t.accent_cyber_hover = "#00d2aa";
    t.accent_cyber_dim = "rgba(0, 242, 195, 0.12)";
    t.text_primary = "#e2e8f0";
    t.text_muted = "rgba(255, 255, 255, 0.6)";
    t.font_ui = "Plus Jakarta Sans, Segoe UI, sans-serif";
    t.font_numeric = "JetBrains Mono, monospace";
    return t;
}

ThemeTokens ThemeTokens::light() noexcept {
    ThemeTokens t;
    t.bg_app = "#f8fafc";
    t.bg_header = "#ffffff";
    t.bg_ribbon = "#f1f5f9";
    t.bg_dock = "#e8ecf2";
    t.border_tech = "#cbd5e1";
    t.accent_cyber = "#00876a";
    t.accent_cyber_hover = "#006d55";
    t.accent_cyber_dim = "rgba(0, 135, 106, 0.12)";
    t.text_primary = "#0f172a";
    t.text_muted = "#64748b";
    t.font_ui = "Plus Jakarta Sans, Segoe UI, sans-serif";
    t.font_numeric = "JetBrains Mono, monospace";
    return t;
}

std::string ThemeTokens::generate_qss() const {
    std::ostringstream ss;
    ss << "/* PikDraw Cyber Dark 2026 Theme */\n"
       << "QMainWindow { background-color: " << bg_app << "; color: " << text_primary << "; }\n"
       << "#TopbarWidget { background-color: " << bg_header << "; border-bottom: 1px solid " << border_tech << "; min-height: 38px; max-height: 38px; }\n"
       << "#ContextRibbonWidget { background-color: " << bg_ribbon << "; border-bottom: 1px solid " << border_tech << "; min-height: 34px; max-height: 34px; }\n"
       << "#ToolStripWidget { background-color: " << bg_header << "; border-right: 1px solid " << border_tech << "; min-width: 48px; max-width: 56px; }\n"
       << "#InspectorDockWidget { background-color: " << bg_dock << "; border-left: 1px solid " << border_tech << "; min-width: 280px; max-width: 320px; }\n"
       << "#StatusBarWidget { background-color: " << bg_header << "; border-top: 1px solid " << border_tech << "; min-height: 26px; max-height: 26px; }\n"
       << "QPushButton:hover { background-color: " << accent_cyber_dim << "; color: " << accent_cyber << "; }\n";
    return ss.str();
}

void ThemeManager::set_theme_mode(ThemeMode mode) noexcept {
    mode_ = mode;
    tokens_ = (mode_ == ThemeMode::CyberDark) ? ThemeTokens::cyber_dark() : ThemeTokens::light();
}

void ThemeManager::toggle_theme() noexcept {
    set_theme_mode(mode_ == ThemeMode::CyberDark ? ThemeMode::Light : ThemeMode::CyberDark);
}

} // namespace pikdraw::desktop
