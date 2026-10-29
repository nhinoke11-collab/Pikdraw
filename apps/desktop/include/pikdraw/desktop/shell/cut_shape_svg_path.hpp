#pragma once

#include <charconv>
#include <cmath>
#include <cstddef>
#include <string_view>
#include <system_error>

namespace pikdraw::desktop {

enum class CutShapeSvgPathVerb { MoveTo, LineTo, Close };

template <typename Emit>
[[nodiscard]] bool visit_cut_shape_svg_path(std::string_view path, Emit&& emit) {
    std::size_t offset = 0;
    bool has_current_point = false;
    std::size_t line_count = 0;
    const auto skip_separators = [&] {
        while (offset < path.size() &&
               (path[offset] == ' ' || path[offset] == '\t' ||
                path[offset] == '\r' || path[offset] == '\n' || path[offset] == ',')) {
            ++offset;
        }
    };
    const auto read_coordinate = [&](float& value) {
        skip_separators();
        if (offset >= path.size()) return false;
        const auto parsed = std::from_chars(path.data() + offset,
                                            path.data() + path.size(), value);
        if (parsed.ec != std::errc{} || parsed.ptr == path.data() + offset ||
            !std::isfinite(value) || value < 0.0f || value > 24.0f) {
            return false;
        }
        offset = static_cast<std::size_t>(parsed.ptr - path.data());
        return true;
    };

    while (true) {
        skip_separators();
        if (offset == path.size()) return line_count > 0;
        const char command = path[offset++];
        if (command == 'Z') {
            if (!has_current_point) return false;
            emit(CutShapeSvgPathVerb::Close, 0.0f, 0.0f);
            has_current_point = false;
            continue;
        }
        if (command != 'M' && command != 'L') return false;
        float x = 0.0f;
        float y = 0.0f;
        if (!read_coordinate(x) || !read_coordinate(y)) return false;
        if (command == 'M') {
            emit(CutShapeSvgPathVerb::MoveTo, x, y);
            has_current_point = true;
        } else {
            if (!has_current_point) return false;
            emit(CutShapeSvgPathVerb::LineTo, x, y);
            ++line_count;
        }
    }
}

} // namespace pikdraw::desktop
