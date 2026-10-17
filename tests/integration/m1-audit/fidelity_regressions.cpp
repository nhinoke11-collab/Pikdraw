#include "pikdraw/document/document.hpp"
#include "pikdraw/document/storage/native_format.hpp"
#include "pikdraw/geometry/operations/boolean.hpp"
#include "pikdraw/geometry/operations/offset.hpp"
#include "pikdraw/interop/adapters/svg_importer.hpp"
#include "pikdraw/rendering/renderer.hpp"
#include <stdexcept>
#include <string>
#include <vector>

void require(bool condition, const char* message);
using namespace pikdraw;

void fidelity_case(const std::string& name) {
    if (name == "boolean_curve" || name == "offset_curve") {
        geometry::Path2D triangle;
        triangle.move_to(0, 0);
        triangle.line_to(10, 0);
        triangle.line_to(0, 10);
        triangle.close();
        // Either implement the operation correctly or reject it explicitly. Bounds are not geometry.
        if (name == "boolean_curve") {
            auto result = geometry::boolean_paths(triangle, triangle, geometry::BooleanOp::Intersection);
            require(!result.success || result.path.elements().size() == triangle.elements().size(),
                    "triangle intersection fabricated a bounding rectangle");
        } else {
            geometry::OffsetOptions options;
            options.distance_mm = 1;
            auto result = geometry::offset_path(triangle, options);
            require(!result.success, "unsupported triangle offset reports a fabricated rectangle as success");
        }
        return;
    }
    if (name == "svg_units") {
        auto result = interop::SvgImporter::import_from_string(
            "<svg width=\"96\" height=\"96\"><rect width=\"96\" height=\"96\"/></svg>");
        require(!result.success || std::abs(result.page.width_mm - 25.4) < 1e-9,
                "96 SVG user units were treated as 96 mm instead of 25.4 mm");
        return;
    }
    document::Document doc("fidelity-audit");
    if (name == "source_color") {
        document::PathObject path;
        path.id = 1;
        path.path = geometry::Path2D::make_rect(0, 0, 10, 10);
        path.fill = color::CMYKColor{0.15, 0.25, 0.35, 0.45, 0.8};
        path.fill.set_overprint({true, false});
        require(doc.execute_command(std::make_unique<document::AddObjectCommand>(path)).success, "add");
        const auto bytes = document::NativeFormat::serialize(*doc.current_snapshot());
        const auto loaded = document::NativeFormat::deserialize(bytes.data(), bytes.size());
        require(loaded != nullptr, "load");
        const auto restored = std::get<document::PathObject>(*loaded->get_object(1));
        require(restored.fill.color_space() == color::ColorSpace::Cmyk, "native save loses CMYK source model");
        require(restored.fill.overprint().fill, "native save loses overprint");
    } else if (name == "source_font") {
        document::TextObject text;
        text.id = 1;
        text.text = "Tiếng Việt";
        text.font = {"Arial", "Arial-BoldItalicMT", 700, true, 17.5};
        require(doc.execute_command(std::make_unique<document::AddObjectCommand>(text)).success, "add");
        const auto bytes = document::NativeFormat::serialize(*doc.current_snapshot());
        const auto loaded = document::NativeFormat::deserialize(bytes.data(), bytes.size());
        require(loaded != nullptr, "load");
        const auto restored = std::get<document::TextObject>(*loaded->get_object(1));
        require(restored.font.weight == 700 && restored.font.italic &&
                restored.font.postscript_name == text.font.postscript_name,
                "native save loses weight/italic/PostScript identity");
    } else if (name == "render_fill") {
        document::PathObject path;
        path.id = 1;
        path.path = geometry::Path2D::make_rect(0, 0, 20, 20);
        path.fill = color::RGBColor{1, 0, 0, 1};
        path.has_stroke = false;
        require(doc.execute_command(std::make_unique<document::AddObjectCommand>(path)).success, "add");
        rendering::ViewportRenderer renderer;
        renderer.camera().set_center({10, 10});
        renderer.camera().set_zoom(2);
        std::vector<uint8_t> pixels(100 * 100 * 4);
        rendering::PixelBufferTarget target{pixels.data(), 100, 100, 400};
        require(renderer.render_frame(*doc.current_snapshot(), target), "render");
        const size_t center = (50 * 100 + 50) * 4;
        require(pixels[center] == 255 && pixels[center + 1] == 0 && pixels[center + 2] == 0,
                "filled path renders as blank paper");
    } else {
        throw std::runtime_error("unknown fidelity case");
    }
}
