#include "pikdraw/desktop/workflows/document_workflow.hpp"
#include "pikdraw/vector/tools/shape_tools.hpp"
#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/color/values/color_value.hpp"

namespace pikdraw::desktop {

namespace {

color::RGBColor parse_hex_color(const std::string& hex) {
    if (hex.length() >= 7 && hex[0] == '#') {
        int r = std::stoi(hex.substr(1, 2), nullptr, 16);
        int g = std::stoi(hex.substr(3, 2), nullptr, 16);
        int b = std::stoi(hex.substr(5, 2), nullptr, 16);
        return color::RGBColor{r / 255.0, g / 255.0, b / 255.0, 1.0};
    }
    return color::RGBColor{0.0, 0.949, 0.765, 1.0}; // Cyber Teal #00f2c3
}

} // namespace

DocumentWorkflow::DocumentWorkflow(CompositionRoot& root)
    : root_(root) {}

document::ObjectId DocumentWorkflow::add_rectangle(
    double x_mm, double y_mm, double width_mm, double height_mm,
    const std::string& fill_hex,
    const std::string& stroke_hex,
    double stroke_width_mm
) {
    auto rect_obj = vector::ShapeFactory::create_rectangle(
        geometry::Rect2D{x_mm, y_mm, width_mm, height_mm},
        parse_hex_color(fill_hex),
        parse_hex_color(stroke_hex),
        stroke_width_mm
    );

    const auto id = rect_obj.id;
    root_.document().execute_command(std::make_unique<document::AddObjectCommand>(std::move(rect_obj)));
    sync_status_bar();
    return id;
}

document::ObjectId DocumentWorkflow::add_ellipse(
    double cx_mm, double cy_mm, double rx_mm, double ry_mm,
    const std::string& fill_hex,
    const std::string& stroke_hex,
    double stroke_width_mm
) {
    auto ellipse_obj = vector::ShapeFactory::create_ellipse(
        geometry::Point2D{cx_mm, cy_mm},
        rx_mm, ry_mm,
        parse_hex_color(fill_hex),
        parse_hex_color(stroke_hex),
        stroke_width_mm
    );

    const auto id = ellipse_obj.id;
    root_.document().execute_command(std::make_unique<document::AddObjectCommand>(std::move(ellipse_obj)));
    sync_status_bar();
    return id;
}

document::ObjectId DocumentWorkflow::add_vietnamese_text(
    double x_mm, double y_mm,
    const std::string& utf8_text,
    const std::string& font_family,
    double font_size_pt,
    const std::string& fill_hex
) {
    document::TextObject text_obj;
    text_obj.id = document::generate_object_id();
    text_obj.name = "Văn bản: " + utf8_text.substr(0, 20);
    text_obj.text = utf8_text;
    text_obj.font = typography::FontIdentity{font_family, font_family + "-Regular", 400, false, font_size_pt};
    text_obj.fill = parse_hex_color(fill_hex);
    text_obj.position = geometry::Point2D{x_mm, y_mm};

    const auto id = text_obj.id;
    root_.document().execute_command(std::make_unique<document::AddObjectCommand>(std::move(text_obj)));
    sync_status_bar();
    return id;
}

void DocumentWorkflow::select_object(document::ObjectId id, bool toggle) {
    root_.vector_controller().selection().select(
        id,
        toggle ? vector::SelectionMode::Toggle : vector::SelectionMode::Replace
    );
    sync_status_bar();
}

bool DocumentWorkflow::transform_object(document::ObjectId id, double dx_mm, double dy_mm) {
    auto cmd = std::make_unique<document::TransformObjectCommand>(
        id, geometry::Transform2D::translation(dx_mm, dy_mm)
    );
    auto res = root_.document().execute_command(std::move(cmd));
    sync_status_bar();
    return res.success;
}

bool DocumentWorkflow::undo() {
    bool res = root_.document().undo();
    sync_status_bar();
    return res;
}

bool DocumentWorkflow::redo() {
    bool res = root_.document().redo();
    sync_status_bar();
    return res;
}

bool DocumentWorkflow::save_native(const std::string& file_path) {
    const auto snapshot = root_.document().current_snapshot();
    auto result = document::SaveRecoveryService::atomic_save(file_path, *snapshot);
    if (result.success) {
        root_.document().mark_saved(snapshot->epoch(), snapshot->revision());
        sync_status_bar();
        return true;
    }
    return false;
}

bool DocumentWorkflow::open_native(const std::string& file_path) {
    std::string err;
    auto loaded_snapshot = document::SaveRecoveryService::load(file_path, &err);
    if (!loaded_snapshot) {
        return false;
    }

    // Replace current document content with loaded snapshot
    root_.document().reset_from_snapshot(loaded_snapshot);
    sync_status_bar();
    return true;
}

bool DocumentWorkflow::import_svg(const std::string& svg_xml) {
    auto res = interop::SvgImporter::import_from_string(svg_xml);
    if (!res.success || res.objects.empty()) {
        return false;
    }

    for (auto& obj : res.objects) {
        root_.document().execute_command(std::make_unique<document::AddObjectCommand>(std::move(obj)));
    }
    sync_status_bar();
    return true;
}

std::string DocumentWorkflow::export_svg() {
    return svg_exporter_.export_to_string(*root_.document().current_snapshot());
}

std::vector<uint8_t> DocumentWorkflow::render_snapshot(
    int32_t width_px, int32_t height_px
) {
    std::vector<uint8_t> buffer(static_cast<size_t>(width_px) * height_px * 4, 0);
    rendering::PixelBufferTarget target{buffer.data(), width_px, height_px, width_px * 4};
    root_.renderer().render_frame(*root_.document().current_snapshot(), target);
    return buffer;
}

void DocumentWorkflow::sync_status_bar() {
    auto& status = root_.window().status_bar();
    const auto snap = root_.document().current_snapshot();
    status.set_document_state(snap->revision(), root_.document().is_dirty());
    status.set_selection_count(root_.vector_controller().selection().count());
}

} // namespace pikdraw::desktop
