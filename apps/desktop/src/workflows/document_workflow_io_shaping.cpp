#include "pikdraw/desktop/workflows/document_workflow.hpp"
#include "document_workflow_internal.hpp"
#include "pikdraw/vector/tools/shape_tools.hpp"
#include "pikdraw/vector/gestures/text_tool.hpp"
#include "pikdraw/typography/layout/glyph_outline.hpp"
#include "pikdraw/typography/layout/text_layout.hpp"
#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/commands/appearance_commands.hpp"
#include "pikdraw/document/commands/transaction_writer.hpp"
#include "pikdraw/color/values/color_value.hpp"
#include "pikdraw/interop/adapters/svg_importer.hpp"
#include "pikdraw/document/storage/native_format.hpp"
#include "pikdraw/document/model/clone_utils.hpp"
#include "../clipboard/native_clipboard.hpp"
#include "../utf8_path.hpp"

#include <chrono>
#include <algorithm>
#include <cstddef>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <tuple>
#include <unordered_set>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace pikdraw::desktop {
using namespace detail;

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
    if (!root_.document().reset_from_snapshot(loaded_snapshot))
        return false;
    root_.vector_controller().on_escape();
    root_.vector_controller().selection().clear();
    sync_status_bar();
    return true;
}

bool DocumentWorkflow::save_svg(const std::string& file_path) {
    if (file_path.empty())
        return false;
    const auto separator = file_path.find_last_of("/\\");
    const auto dot = file_path.find_last_of('.');
    if (dot == std::string::npos || (separator != std::string::npos && dot < separator))
        return false;
    std::string extension = file_path.substr(dot);
    for (auto& character : extension)
        if (character >= 'A' && character <= 'Z')
            character = static_cast<char>(character - 'A' + 'a');
    if (extension != ".svg")
        return false;

    std::filesystem::path target;
    std::filesystem::path temporary;
    try {
        target = detail::path_from_utf8(file_path);
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        temporary = target;
        temporary += ".tmp-" + std::to_string(nonce);

        const auto export_result = interop::SvgExporter::export_with_report(
            *root_.document().current_snapshot());
        if (!export_result.success || export_result.svg.empty())
            return false;
        const auto& contents = export_result.svg;
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output)
            return false;
        output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        output.flush();
        if (!output)
            throw std::runtime_error("SVG write failed");
        output.close();
        if (!output)
            throw std::runtime_error("SVG close failed");

#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), target.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("SVG replace failed");
#else
        std::filesystem::rename(temporary, target);
#endif
        return true;
    } catch (...) {
        if (!temporary.empty()) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
        }
        return false;
    }
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

bool DocumentWorkflow::can_boolean_operation_selected(geometry::BooleanOp op) const {
    const auto& selected = root_.vector_controller().selection().selected_ids();
    if (selected.size() != 2) return false;
    const auto snapshot = root_.document().current_snapshot();
    return compute_selected_boolean(*snapshot, selected, op).has_value();
}

bool DocumentWorkflow::boolean_operation_selected(
    geometry::BooleanOp op,
    bool keep_source,
    bool keep_target
) {
    const auto& sel = root_.vector_controller().selection().selected_ids();
    if (!can_boolean_operation_selected(op)) return false;

    const auto snapshot = root_.document().current_snapshot();
    const auto target_id = sel[0];
    const auto source_id = sel[1];

    const auto target_var = snapshot->objects().get(target_id);
    if (!target_var) return false;
    const auto* target = std::get_if<document::PathObject>(&*target_var);
    if (!target) return false;

    auto result = compute_selected_boolean(*snapshot, sel, op);
    if (!result) return false;

    std::vector<std::unique_ptr<document::DocumentCommand>> cmds;
    if (!keep_target && op != geometry::BooleanOp::Boundary) {
        cmds.push_back(std::make_unique<document::RemoveObjectCommand>(target_id));
    }
    if (!keep_source && op != geometry::BooleanOp::Boundary) {
        cmds.push_back(std::make_unique<document::RemoveObjectCommand>(source_id));
    }

    document::PathObject new_obj;
    new_obj.id = document::generate_object_id();
    switch (op) {
        case geometry::BooleanOp::Union: new_obj.name = "Hàn (Weld)"; break;
        case geometry::BooleanOp::Difference: new_obj.name = "Cắt (Trim)"; break;
        case geometry::BooleanOp::Intersection: new_obj.name = "Giao (Intersect)"; break;
        case geometry::BooleanOp::Xor: new_obj.name = "Rỗng (Exclude)"; break;
        case geometry::BooleanOp::BackMinusFront: new_obj.name = "Trừ dưới (Back Minus Front)"; break;
        case geometry::BooleanOp::Boundary: new_obj.name = "Đường bao (Boundary)"; break;
        default: new_obj.name = "Tạo hình (Shaping)"; break;
    }
    new_obj.path = std::move(result->path);
    new_obj.transform = geometry::Transform2D::identity();
    new_obj.fill = target->fill;
    new_obj.stroke = target->stroke;
    new_obj.stroke_width_mm = target->stroke_width_mm;
    new_obj.has_fill = target->has_fill;
    new_obj.has_stroke = target->has_stroke;

    const auto new_id = new_obj.id;
    cmds.push_back(std::make_unique<document::AddObjectCommand>(std::move(new_obj)));

    auto composite = std::make_unique<document::CompositeCommand>("Boolean Operation");
    for (auto& cmd : cmds) {
        composite->add_command(std::move(cmd));
    }
    const auto command_result = root_.document().execute_command(std::move(composite));
    if (!command_result.success) return false;

    root_.vector_controller().selection().clear();
    root_.vector_controller().selection().select(new_id);
    sync_status_bar();
    return true;
}

document::ObjectId DocumentWorkflow::add_bitmap_image(
    double x_mm, double y_mm, double width_mm, double height_mm,
    const std::string& name
) {
    auto id = document::generate_object_id();
    auto img = document::ImageObject::make_procedural_photo(id, x_mm, y_mm, width_mm, height_mm, name);
    auto cmd = std::make_unique<document::AddObjectCommand>(std::move(img));
    root_.document().execute_command(std::move(cmd));
    sync_status_bar();
    return id;
}

bool DocumentWorkflow::split_objects_by_line(
    const geometry::Point2D& p1,
    const geometry::Point2D& p2,
    double separate_offset_mm
) {
    const auto snapshot = root_.document().current_snapshot();
    const auto& sel = root_.vector_controller().selection().selected_ids();

    std::vector<document::ObjectId> candidate_ids;
    if (!sel.empty()) {
        candidate_ids = sel;
    } else {
        for (const auto& [id, obj] : snapshot->objects().all()) {
            if (std::holds_alternative<document::PathObject>(obj) ||
                std::holds_alternative<document::TextObject>(obj) ||
                std::holds_alternative<document::ImageObject>(obj)) {
                candidate_ids.push_back(id);
            }
        }
    }

    std::vector<std::unique_ptr<document::DocumentCommand>> cmds;
    std::vector<document::ObjectId> new_ids;

    for (const auto id : candidate_ids) {
        const auto obj_var = snapshot->objects().get(id);
        if (!obj_var) continue;
        if (const auto* po = std::get_if<document::PathObject>(&*obj_var)) {
            const auto bounds = po->bounds();
            auto [split_ok, pieces] = geometry::split_rect_by_line(bounds, p1, p2);
            if (split_ok && pieces.size() >= 2) {
                auto b0 = pieces[0].bounds();
                auto b1 = pieces[1].bounds();
                if (b0.x > b1.x) {
                    std::swap(pieces[0], pieces[1]);
                }

                cmds.push_back(std::make_unique<document::RemoveObjectCommand>(id));
                int idx = 1;
                for (auto& piece_path : pieces) {
                    document::PathObject piece_obj;
                    piece_obj.id = document::generate_object_id();
                    piece_obj.name = po->name + " (Mảnh " + std::to_string(idx) + ")";
                    piece_obj.path = std::move(piece_path);
                    if (idx == 1) {
                        piece_obj.fill = po->fill;
                        piece_obj.stroke = po->stroke;
                        if (separate_offset_mm > 0.0) {
                            piece_obj.transform = geometry::Transform2D::translation(-separate_offset_mm * 0.5, 0.0);
                        }
                    } else {
                        // Contrasting color for piece 2 to prove distinct objects
                        piece_obj.fill = color::ColorValue{color::RGBColor{0.96, 0.62, 0.04, 1.0}};
                        piece_obj.stroke = color::ColorValue{color::RGBColor{0.85, 0.47, 0.02, 1.0}};
                        if (separate_offset_mm > 0.0) {
                            piece_obj.transform = geometry::Transform2D::translation(separate_offset_mm * 0.5, 0.0);
                        }
                    }
                    piece_obj.stroke_width_mm = po->stroke_width_mm;
                    piece_obj.has_fill = po->has_fill;
                    piece_obj.has_stroke = po->has_stroke;
                    new_ids.push_back(piece_obj.id);
                    cmds.push_back(std::make_unique<document::AddObjectCommand>(std::move(piece_obj)));
                    idx++;
                }
            }
        } else if (const auto* im = std::get_if<document::ImageObject>(&*obj_var)) {
            const auto bounds = im->bounds();
            auto [split_ok, pieces] = geometry::split_rect_by_line(bounds, p1, p2);
            if (split_ok && pieces.size() >= 2) {
                auto r1 = pieces[0].bounds();
                auto r2 = pieces[1].bounds();
                if (r1.x > r2.x) {
                    std::swap(r1, r2);
                }

                cmds.push_back(std::make_unique<document::RemoveObjectCommand>(id));

                int w_px = im->width_px;
                int h_px = im->height_px;
                int w_px1 = std::clamp<int>(static_cast<int>(std::round(w_px * (r1.width / bounds.width))), 2, w_px - 2);
                int w_px2 = w_px - w_px1;

                document::ImageObject im1;
                im1.id = document::generate_object_id();
                im1.name = im->name + " (Mảnh 1)";
                im1.bounds_rect = r1;
                im1.width_px = w_px1;
                im1.height_px = h_px;
                im1.pixels_rgba.resize(static_cast<size_t>(w_px1) * h_px);
                const auto source_pixels = im->rgba_pixels();
                for (int y = 0; y < h_px; ++y) {
                    for (int x = 0; x < w_px1; ++x) {
                        im1.pixels_rgba[static_cast<size_t>(y) * w_px1 + x] =
                            source_pixels[static_cast<size_t>(y) * w_px + x];
                    }
                }

                document::ImageObject im2;
                im2.id = document::generate_object_id();
                im2.name = im->name + " (Mảnh 2)";
                im2.bounds_rect = r2;
                im2.width_px = w_px2;
                im2.height_px = h_px;
                im2.pixels_rgba.resize(static_cast<size_t>(w_px2) * h_px);
                for (int y = 0; y < h_px; ++y) {
                    for (int x = 0; x < w_px2; ++x) {
                        im2.pixels_rgba[static_cast<size_t>(y) * w_px2 + x] =
                            source_pixels[static_cast<size_t>(y) * w_px + (w_px1 + x)];
                    }
                }

                if (separate_offset_mm > 0.0) {
                    im1.transform = geometry::Transform2D::translation(-separate_offset_mm * 0.5, 0.0);
                    im2.transform = geometry::Transform2D::translation(separate_offset_mm * 0.5, 0.0);
                }

                new_ids.push_back(im1.id);
                new_ids.push_back(im2.id);
                cmds.push_back(std::make_unique<document::AddObjectCommand>(std::move(im1)));
                cmds.push_back(std::make_unique<document::AddObjectCommand>(std::move(im2)));
            }
        } else if (const auto* to = std::get_if<document::TextObject>(&*obj_var)) {
            const auto bounds = to->bounds();
            auto [split_ok, pieces] = geometry::split_rect_by_line(bounds, p1, p2);
            if (split_ok && pieces.size() >= 2) {
                auto b0 = pieces[0].bounds();
                auto b1 = pieces[1].bounds();
                if (b0.x > b1.x) {
                    std::swap(pieces[0], pieces[1]);
                }

                cmds.push_back(std::make_unique<document::RemoveObjectCommand>(id));
                int idx = 1;
                for (auto& piece_path : pieces) {
                    document::PathObject piece_obj;
                    piece_obj.id = document::generate_object_id();
                    piece_obj.name = to->name + " (Mảnh " + std::to_string(idx) + ")";
                    piece_obj.path = std::move(piece_path);
                    piece_obj.fill = to->fill;
                    piece_obj.stroke = color::ColorValue{color::RGBColor{0.0, 0.0, 0.0, 1.0}};
                    piece_obj.stroke_width_mm = 0.5;
                    piece_obj.has_fill = true;
                    piece_obj.has_stroke = true;
                    if (idx == 1) {
                        if (separate_offset_mm > 0.0) {
                            piece_obj.transform = geometry::Transform2D::translation(-separate_offset_mm * 0.5, 0.0);
                        }
                    } else {
                        piece_obj.fill = color::ColorValue{color::RGBColor{0.96, 0.62, 0.04, 1.0}};
                        if (separate_offset_mm > 0.0) {
                            piece_obj.transform = geometry::Transform2D::translation(separate_offset_mm * 0.5, 0.0);
                        }
                    }
                    const auto nid = piece_obj.id;
                    cmds.push_back(std::make_unique<document::AddObjectCommand>(std::move(piece_obj)));
                    new_ids.push_back(nid);
                    idx++;
                }
            }
        }
    }

    if (cmds.empty()) return false;

    auto composite = std::make_unique<document::CompositeCommand>("Split Objects");
    for (auto& cmd : cmds) {
        composite->add_command(std::move(cmd));
    }
    root_.document().execute_command(std::move(composite));

    root_.vector_controller().selection().clear();
    for (const auto nid : new_ids) {
        root_.vector_controller().selection().select(nid, vector::SelectionMode::Add);
    }
    sync_status_bar();
    return true;
}

bool DocumentWorkflow::trim_bitmap_with_vector(
    document::ObjectId bitmap_id,
    document::ObjectId vector_cutter_id,
    double separate_offset_mm
) {
    const auto snapshot = root_.document().current_snapshot();
    const auto im_var = snapshot->objects().get(bitmap_id);
    const auto vec_var = snapshot->objects().get(vector_cutter_id);
    if (!im_var || !vec_var) return false;

    const auto* im = std::get_if<document::ImageObject>(&*im_var);
    if (!im) return false;

    auto vb = get_object_bounds(vector_cutter_id);
    if (!vb) return false;

    auto ib = im->bounds();
    if (!ib.intersects(*vb)) return false;

    auto overlap = geometry::Rect2D{
        (std::max)(ib.x, vb->x),
        (std::max)(ib.y, vb->y),
        (std::min)(ib.x + ib.width, vb->x + vb->width) - (std::max)(ib.x, vb->x),
        (std::min)(ib.y + ib.height, vb->y + vb->height) - (std::max)(ib.y, vb->y)
    };
    if (overlap.width <= 1.0 || overlap.height <= 1.0) return false;

    int src_x0 = std::clamp<int>(static_cast<int>((overlap.x - ib.x) / ib.width * im->width_px), 0, im->width_px - 1);
    int src_y0 = std::clamp<int>(static_cast<int>((overlap.y - ib.y) / ib.height * im->height_px), 0, im->height_px - 1);
    int sub_w = std::clamp<int>(static_cast<int>(overlap.width / ib.width * im->width_px), 2, im->width_px - src_x0);
    int sub_h = std::clamp<int>(static_cast<int>(overlap.height / ib.height * im->height_px), 2, im->height_px - src_y0);

    document::ImageObject patch;
    patch.id = document::generate_object_id();
    patch.name = "Bitmap Patch (Trimmed)";
    patch.bounds_rect = overlap;
    patch.width_px = sub_w;
    patch.height_px = sub_h;
    patch.pixels_rgba.resize(static_cast<size_t>(sub_w) * sub_h);
    const auto source_pixels = im->rgba_pixels();

    for (int y = 0; y < sub_h; ++y) {
        for (int x = 0; x < sub_w; ++x) {
            patch.pixels_rgba[static_cast<size_t>(y) * sub_w + x] =
                source_pixels[static_cast<size_t>(src_y0 + y) * im->width_px + (src_x0 + x)];
        }
    }

    if (separate_offset_mm > 0.0) {
        patch.transform = geometry::Transform2D::translation(separate_offset_mm, separate_offset_mm * 0.4);
    }

    auto cmd = std::make_unique<document::AddObjectCommand>(std::move(patch));
    root_.document().execute_command(std::move(cmd));
    sync_status_bar();
    return true;
}

bool DocumentWorkflow::auto_weld_overlapping() {
    const auto snapshot = root_.document().current_snapshot();
    std::vector<document::ObjectId> all_ids;
    for (const auto& [id, obj] : snapshot->objects().all()) {
        if (std::holds_alternative<document::PathObject>(obj)) {
            all_ids.push_back(id);
        }
    }
    if (all_ids.size() < 2) return false;

    for (size_t i = 0; i < all_ids.size(); ++i) {
        auto bi = get_object_bounds(all_ids[i]);
        if (!bi) continue;
        for (size_t j = i + 1; j < all_ids.size(); ++j) {
            auto bj = get_object_bounds(all_ids[j]);
            if (!bj) continue;
            if (bi->intersects(*bj)) {
                root_.vector_controller().selection().clear();
                root_.vector_controller().selection().select(all_ids[i], vector::SelectionMode::Add);
                root_.vector_controller().selection().select(all_ids[j], vector::SelectionMode::Add);
                return boolean_operation_selected(geometry::BooleanOp::Union);
            }
        }
    }
    return false;
}

document::SnapshotPtr DocumentWorkflow::snapshot_for_canvas() {
    if (text_preview_ && text_preview_base_ == root_.document().current_snapshot())
      return text_preview_;
    clear_text_preview();
    (void)cancel_stale_effect_draft();
    if (effect_draft_ && effect_draft_->preview_enabled && effect_draft_->preview_snapshot)
        return effect_draft_->preview_snapshot;
    return root_.document().current_snapshot();
}
void DocumentWorkflow::render_snapshot(
    int32_t width_px, int32_t height_px, std::vector<uint8_t>& out_buffer
) {
    if (width_px <= 0 || height_px <= 0 ||
        static_cast<uint64_t>(width_px) * static_cast<uint64_t>(height_px) > 8388608) {
        out_buffer.clear();
        return;
    }
    const size_t expected = static_cast<size_t>(width_px) * height_px * 4;
    if (out_buffer.size() != expected) {
        out_buffer.resize(expected, 0);
    }
    rendering::PixelBufferTarget target{out_buffer.data(), width_px, height_px, width_px * 4};
    const auto snapshot = snapshot_for_canvas();
    if (!snapshot) { out_buffer.clear(); return; }
    root_.renderer().render_frame(*snapshot, target);
}

std::vector<uint8_t> DocumentWorkflow::render_snapshot(
    int32_t width_px, int32_t height_px
) {
    std::vector<uint8_t> buffer;
    render_snapshot(width_px, height_px, buffer);
    return buffer;
}

void DocumentWorkflow::sync_status_bar() {
    auto& status = root_.window().status_bar();
    const auto snap = root_.document().current_snapshot();
    status.set_document_state(snap->revision(), root_.document().is_dirty());
    status.set_selection_count(root_.vector_controller().selection().count());
}

} // namespace pikdraw::desktop
