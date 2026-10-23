#include "native_window_gradient_ramp.hpp"
#include <algorithm>
#include <cmath>

namespace pikdraw::desktop {

namespace {

COLORREF resolve_color(const color::ColorValue& cv, const color::IccPreview* preview) {
    const auto p = preview ? preview->convert(cv) : color::to_preview_rgba(cv);
    const auto dev = p.to_device();
    return RGB(dev.r, dev.g, dev.b);
}

color::RGBColor to_rgb_unit(const color::ColorValue& cv) {
    return std::visit([](const auto& c) -> color::RGBColor {
        using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, color::RGBColor>) {
            return c;
        } else {
            return color::RGBColor{0.0, 0.0, 0.0, 1.0};
        }
    }, cv.payload());
}

} // namespace

void paint_gradient_ramp(
    HDC dc,
    const RECT& bounds,
    const document::GradientPaint& paint,
    uint64_t selected_stop_id,
    ThemeMode theme,
    const color::IccPreview* preview) {
    if (bounds.right <= bounds.left || bounds.bottom <= bounds.top) return;

    const bool dark = theme == ThemeMode::CyberDark;
    const COLORREF border_col = dark ? RGB(48, 64, 82) : RGB(194, 205, 217);
    const COLORREF accent_col = dark ? RGB(0, 242, 195) : RGB(0, 126, 99);

    const int track_left = bounds.left + 8;
    const int track_right = bounds.right - 8;
    const int track_top = bounds.top + 14;
    const int track_bottom = track_top + 18;
    const int track_width = std::max(1, track_right - track_left);
    const int track_height = track_bottom - track_top;

    // Draw track gradient using 1D interpolated scanline
    const auto& stops = paint.stops;
    if (!stops.empty()) {
        std::vector<uint32_t> scanline(track_width);
        for (int x = 0; x < track_width; ++x) {
            const double t = static_cast<double>(x) / std::max(1, track_width - 1);
            color::RGBColor sample{0.0, 0.0, 0.0, 1.0};

            if (t <= stops.front().offset) {
                sample = to_rgb_unit(stops.front().color);
            } else if (t >= stops.back().offset) {
                sample = to_rgb_unit(stops.back().color);
            } else {
                for (size_t i = 0; i + 1 < stops.size(); ++i) {
                    if (t >= stops[i].offset && t <= stops[i + 1].offset) {
                        const double s0 = stops[i].offset;
                        const double s1 = stops[i + 1].offset;
                        const double seg_len = s1 - s0;
                        double local_t = seg_len > 1e-6 ? (t - s0) / seg_len : 0.0;
                        const double mid = std::clamp(stops[i].midpoint_to_next, 0.05, 0.95);
                        if (local_t < mid) {
                            local_t = 0.5 * (local_t / mid);
                        } else {
                            local_t = 0.5 + 0.5 * ((local_t - mid) / (1.0 - mid));
                        }
                        const auto c0 = to_rgb_unit(stops[i].color);
                        const auto c1 = to_rgb_unit(stops[i + 1].color);
                        sample.r = c0.r + (c1.r - c0.r) * local_t;
                        sample.g = c0.g + (c1.g - c0.g) * local_t;
                        sample.b = c0.b + (c1.b - c0.b) * local_t;
                        sample.a = c0.a + (c1.a - c0.a) * local_t;
                        break;
                    }
                }
            }

            const uint8_t r = static_cast<uint8_t>(std::clamp(sample.r * 255.0, 0.0, 255.0));
            const uint8_t g = static_cast<uint8_t>(std::clamp(sample.g * 255.0, 0.0, 255.0));
            const uint8_t b = static_cast<uint8_t>(std::clamp(sample.b * 255.0, 0.0, 255.0));
            scanline[x] = (static_cast<uint32_t>(r) << 16) |
                          (static_cast<uint32_t>(g) << 8) |
                          static_cast<uint32_t>(b);
        }

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = track_width;
        bmi.bmiHeader.biHeight = -1; // top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        StretchDIBits(
            dc,
            track_left, track_top, track_width, track_height,
            0, 0, track_width, 1,
            scanline.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
    }

    // Track frame border
    RECT track_rect{track_left, track_top, track_right, track_bottom};
    HBRUSH frame_br = CreateSolidBrush(border_col);
    FrameRect(dc, &track_rect, frame_br);
    DeleteObject(frame_br);

    // Draw midpoint diamonds above the track
    if (stops.size() > 1) {
        for (size_t i = 0; i + 1 < stops.size(); ++i) {
            const double s0 = stops[i].offset;
            const double s1 = stops[i + 1].offset;
            const double mid_t = s0 + (s1 - s0) * stops[i].midpoint_to_next;
            const int mx = track_left + static_cast<int>(std::round(mid_t * (track_width - 1)));
            const int my = bounds.top + 7;

            POINT pts[4] = {
                {mx, my - 4},
                {mx + 4, my},
                {mx, my + 4},
                {mx - 4, my}
            };
            HBRUSH dbr = CreateSolidBrush(dark ? RGB(220, 225, 235) : RGB(245, 245, 250));
            HPEN dpen = CreatePen(PS_SOLID, 1, dark ? RGB(20, 25, 35) : RGB(80, 90, 105));
            auto old_br = SelectObject(dc, dbr);
            auto old_pen = SelectObject(dc, dpen);
            Polygon(dc, pts, 4);
            SelectObject(dc, old_br);
            SelectObject(dc, old_pen);
            DeleteObject(dpen);
            DeleteObject(dbr);
        }
    }

    // Draw stop pins below the track
    for (size_t i = 0; i < stops.size(); ++i) {
        const auto& stop = stops[i];
        const int px = track_left + static_cast<int>(std::round(stop.offset * (track_width - 1)));
        const bool is_selected = stop.id == selected_stop_id;
        const COLORREF stop_color = resolve_color(stop.color, preview);

        POINT pin_pts[5] = {
            {px, track_bottom},
            {px + 6, track_bottom + 5},
            {px + 6, bounds.bottom - 2},
            {px - 6, bounds.bottom - 2},
            {px - 6, track_bottom + 5}
        };

        // Pin fill with real stop color
        HBRUSH pbr = CreateSolidBrush(stop_color);
        HPEN ppen = CreatePen(PS_SOLID, is_selected ? 2 : 1, is_selected ? accent_col : border_col);
        auto old_br = SelectObject(dc, pbr);
        auto old_pen = SelectObject(dc, ppen);
        Polygon(dc, pin_pts, 5);
        SelectObject(dc, old_br);
        SelectObject(dc, old_pen);
        DeleteObject(ppen);
        DeleteObject(pbr);

        // Highlight ring around active pin tip
        if (is_selected) {
            HPEN halo_pen = CreatePen(PS_SOLID, 1, dark ? RGB(255, 255, 255) : RGB(10, 15, 25));
            auto old_h = SelectObject(dc, halo_pen);
            MoveToEx(dc, px - 3, track_bottom + 8, nullptr);
            LineTo(dc, px + 4, track_bottom + 8);
            SelectObject(dc, old_h);
            DeleteObject(halo_pen);
        }
    }
}

RampHitResult hit_test_gradient_ramp(
    POINT pt,
    const RECT& bounds,
    const document::GradientPaint& paint) {
    RampHitResult result{};
    if (bounds.right <= bounds.left || bounds.bottom <= bounds.top) return result;

    const int track_left = bounds.left + 8;
    const int track_right = bounds.right - 8;
    const int track_top = bounds.top + 14;
    const int track_bottom = track_top + 18;
    const int track_width = std::max(1, track_right - track_left);
    const auto& stops = paint.stops;

    // Check Stop Pins (reverse order so selected/later pins get hit first)
    for (int i = static_cast<int>(stops.size()) - 1; i >= 0; --i) {
        const int px = track_left + static_cast<int>(std::round(stops[i].offset * (track_width - 1)));
        if (pt.x >= px - 8 && pt.x <= px + 8 && pt.y >= track_bottom && pt.y <= bounds.bottom + 4) {
            result.kind = RampHitKind::StopPin;
            result.index = static_cast<size_t>(i);
            result.offset = stops[i].offset;
            return result;
        }
    }

    // Check Midpoint Diamonds
    if (stops.size() > 1) {
        for (size_t i = 0; i + 1 < stops.size(); ++i) {
            const double s0 = stops[i].offset;
            const double s1 = stops[i + 1].offset;
            const double mid_t = s0 + (s1 - s0) * stops[i].midpoint_to_next;
            const int mx = track_left + static_cast<int>(std::round(mid_t * (track_width - 1)));
            const int my = bounds.top + 7;
            if (std::abs(pt.x - mx) <= 6 && std::abs(pt.y - my) <= 6) {
                result.kind = RampHitKind::MidpointDiamond;
                result.index = i;
                result.offset = mid_t;
                return result;
            }
        }
    }

    // Check Track
    if (pt.x >= track_left && pt.x <= track_right && pt.y >= track_top && pt.y <= track_bottom) {
        result.kind = RampHitKind::RampTrack;
        result.offset = std::clamp(static_cast<double>(pt.x - track_left) / track_width, 0.0, 1.0);
        return result;
    }

    return result;
}

const std::vector<GradientPreset>& get_gradient_presets() {
    static const std::vector<GradientPreset> presets = {
        {
            L"Vàng Kim", L"Gold",
            document::GradientKind::Linear,
            {
                {0.0, color::RGBColor{1.0, 0.886, 0.349, 1.0}},
                {0.5, color::RGBColor{1.0, 0.765, 0.235, 1.0}},
                {1.0, color::RGBColor{1.0, 0.655, 0.318, 1.0}}
            }
        },
        {
            L"Kim Loại", L"Chrome",
            document::GradientKind::Linear,
            {
                {0.0, color::RGBColor{0.925, 0.914, 0.902, 1.0}},
                {0.35, color::RGBColor{1.0, 1.0, 1.0, 1.0}},
                {0.65, color::RGBColor{0.694, 0.702, 0.714, 1.0}},
                {1.0, color::RGBColor{0.941, 0.941, 0.941, 1.0}}
            }
        },
        {
            L"Hoàng Hôn", L"Sunset",
            document::GradientKind::Linear,
            {
                {0.0, color::RGBColor{0.043, 0.282, 0.420, 1.0}},
                {0.55, color::RGBColor{0.961, 0.384, 0.090, 1.0}},
                {1.0, color::RGBColor{1.0, 0.800, 0.200, 1.0}}
            }
        },
        {
            L"Cầu Vồng", L"Rainbow",
            document::GradientKind::Linear,
            {
                {0.0, color::RGBColor{1.0, 0.0, 0.0, 1.0}},
                {0.17, color::RGBColor{1.0, 0.647, 0.0, 1.0}},
                {0.33, color::RGBColor{1.0, 1.0, 0.0, 1.0}},
                {0.50, color::RGBColor{0.0, 0.85, 0.2, 1.0}},
                {0.67, color::RGBColor{0.0, 0.8, 1.0, 1.0}},
                {0.83, color::RGBColor{0.1, 0.2, 1.0, 1.0}},
                {1.0, color::RGBColor{0.6, 0.1, 0.9, 1.0}}
            }
        },
        {
            L"Đen Trắng", L"B&W",
            document::GradientKind::Linear,
            {
                {0.0, color::RGBColor{0.0, 0.0, 0.0, 1.0}},
                {1.0, color::RGBColor{1.0, 1.0, 1.0, 1.0}}
            }
        },
        {
            L"Biển Sâu", L"Ocean",
            document::GradientKind::Linear,
            {
                {0.0, color::RGBColor{0.180, 0.192, 0.573, 1.0}},
                {1.0, color::RGBColor{0.106, 1.0, 1.0, 1.0}}
            }
        }
    };
    return presets;
}

} // namespace pikdraw::desktop
