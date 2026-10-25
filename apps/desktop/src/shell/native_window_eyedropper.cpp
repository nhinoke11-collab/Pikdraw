#include "pikdraw/desktop/shell/native_window.hpp"
#include "native_window_eyedropper.hpp"
#include "native_window_paint_icons.hpp"
#include "pikdraw/color/preview/color_transform.hpp"
#include "pikdraw/color/preview/icc_preview.hpp"
#include "pikdraw/document/commands/gradient_commands.hpp"
#include "pikdraw/raster/edit_preview_service.hpp"
#include "pikdraw/raster/evaluator.hpp"
#include "pikdraw/rendering/software_rasterizer.hpp"
#include "pikdraw/vector/selection/selection_model.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <sstream>
#include <unordered_set>

namespace pikdraw::desktop {

namespace {

void populate_sample_from_rgba(CanvasColorSample& sample, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    sample.r = r;
    sample.g = g;
    sample.b = b;
    sample.a = a;

    const double rn = static_cast<double>(r) / 255.0;
    const double gn = static_cast<double>(g) / 255.0;
    const double bn = static_cast<double>(b) / 255.0;
    const double an = static_cast<double>(a) / 255.0;
    sample.color_value = color::RGBColor{rn, gn, bn, an};
}

void populate_sample_from_color_value(CanvasColorSample& sample,
                                      const color::ColorValue& cv,
                                      const color::IccPreview* preview) {
    const auto pr = preview ? preview->convert(cv) : color::to_preview_rgba(cv);
    const auto rgba = pr.to_device();
    populate_sample_from_rgba(sample, rgba.r, rgba.g, rgba.b, rgba.a);
    sample.color_value = cv;
}

color::ColorValue evaluate_gradient_at_world(
    const document::GradientPaint& gradient,
    const geometry::Transform2D& object_world_transform,
    const geometry::Point2D& world_pt,
    const color::IccPreview* preview) {
    if (gradient.stops.empty()) {
        return color::RGBColor{1.0, 1.0, 1.0, 1.0};
    }
    if (gradient.stops.size() == 1) {
        return gradient.stops.front().color;
    }

    geometry::Point2D obj_pt = world_pt;
    if (const auto inv_obj = object_world_transform.inverted()) {
        obj_pt = inv_obj->map_point(world_pt);
    }
    geometry::Point2D grad_pt = obj_pt;
    if (const auto inv_grad = gradient.gradient_to_object.inverted()) {
        grad_pt = inv_grad->map_point(obj_pt);
    }

    double t = (gradient.kind == document::GradientKind::Radial)
        ? std::hypot(grad_pt.x, grad_pt.y)
        : grad_pt.x;
    if (!std::isfinite(t)) t = 0.0;

    switch (gradient.spread) {
    case document::GradientSpread::Pad:
        t = std::clamp(t, 0.0, 1.0);
        break;
    case document::GradientSpread::Repeat:
        t = t - std::floor(t);
        break;
    case document::GradientSpread::Reflect: {
        double cycle = std::fmod(std::abs(t), 2.0);
        t = (cycle > 1.0) ? (2.0 - cycle) : cycle;
        break;
    }
    }

    if (t <= gradient.stops.front().offset) {
        return gradient.stops.front().color;
    }
    if (t >= gradient.stops.back().offset) {
        return gradient.stops.back().color;
    }

    for (size_t i = 0; i + 1 < gradient.stops.size(); ++i) {
        const auto& s0 = gradient.stops[i];
        const auto& s1 = gradient.stops[i + 1];
        if (t >= s0.offset && t <= s1.offset) {
            const double span = s1.offset - s0.offset;
            double local_t = (span > 1e-9) ? (t - s0.offset) / span : 0.0;
            const double mid = std::clamp(s0.midpoint_to_next, 0.01, 0.99);
            if (std::abs(mid - 0.5) > 1e-4) {
                local_t = std::pow(std::clamp(local_t, 0.0, 1.0), std::log(0.5) / std::log(mid));
            }
            const auto p0 = preview ? preview->convert(s0.color) : color::to_preview_rgba(s0.color);
            const auto p1 = preview ? preview->convert(s1.color) : color::to_preview_rgba(s1.color);
            const auto c0 = p0.to_device();
            const auto c1 = p1.to_device();
            const double r = (c0.r * (1.0 - local_t) + c1.r * local_t) / 255.0;
            const double g = (c0.g * (1.0 - local_t) + c1.g * local_t) / 255.0;
            const double b = (c0.b * (1.0 - local_t) + c1.b * local_t) / 255.0;
            const double a = (c0.a * s0.opacity * (1.0 - local_t) + c1.a * s1.opacity * local_t) / 255.0;
            return color::RGBColor{
                std::clamp(r, 0.0, 1.0),
                std::clamp(g, 0.0, 1.0),
                std::clamp(b, 0.0, 1.0),
                std::clamp(a, 0.0, 1.0)};
        }
    }
    return gradient.stops.back().color;
}

void collect_object_and_descendants(
    const document::DocumentSnapshot& snapshot,
    document::ObjectId id,
    std::unordered_set<document::ObjectId>& out_ids) {
    if (id == document::kInvalidObjectId || !out_ids.insert(id).second) {
        return;
    }
    const auto* obj = snapshot.objects().lookup_ref(id);
    if (!obj) return;
    if (const auto* group = std::get_if<document::GroupObject>(obj)) {
        for (const auto child_id : group->children_ids) {
            collect_object_and_descendants(snapshot, child_id, out_ids);
        }
        if (group->clip && group->clip->mask_object_id != document::kInvalidObjectId) {
            collect_object_and_descendants(snapshot, group->clip->mask_object_id, out_ids);
        }
    }
}

bool collect_unlocked_conversion_tree(
    const document::DocumentSnapshot& snapshot,
    document::ObjectId id,
    std::unordered_set<document::ObjectId>& ids,
    std::unordered_set<document::ObjectId>& recursion_stack) {
    if (id == document::kInvalidObjectId || recursion_stack.contains(id))
        return false;
    if (ids.contains(id)) return true;
    const auto* object = snapshot.objects().lookup_ref(id);
    if (!object || std::visit([](const auto& value) { return value.locked; }, *object))
        return false;
    ids.insert(id);
    const auto* group = std::get_if<document::GroupObject>(object);
    if (!group) return true;
    recursion_stack.insert(id);
    for (const auto child : group->children_ids) {
        if (!collect_unlocked_conversion_tree(snapshot, child, ids, recursion_stack))
            return false;
    }
    if (group->clip && group->clip->mask_object_id != document::kInvalidObjectId &&
        !collect_unlocked_conversion_tree(
            snapshot, group->clip->mask_object_id, ids, recursion_stack))
        return false;
    recursion_stack.erase(id);
    return true;
}

class ConvertSelectionToBitmapCommand final : public document::DocumentCommand {
public:
    ConvertSelectionToBitmapCommand(
        std::vector<document::ObjectId> removed_top_level_ids,
        document::ImageObject new_bitmap,
        size_t target_layer_index)
        : removed_ids_(std::move(removed_top_level_ids)),
          new_bitmap_(std::move(new_bitmap)),
          target_layer_index_(target_layer_index) {}

    [[nodiscard]] std::string_view name() const noexcept override {
        return "Convert Selection to Bitmap";
    }

    document::CommandResult execute(document::Document& doc) override {
        if (new_bitmap_.id == document::kInvalidObjectId || removed_ids_.empty()) {
            return document::CommandResult::fail("Invalid bitmap conversion parameters");
        }
        previous_page_ = doc.page();
        previous_objects_ = doc.objects();

        std::unordered_set<document::ObjectId> remove_all;
        if (const auto snap = doc.current_snapshot()) {
            for (const auto id : removed_ids_) {
                collect_object_and_descendants(*snap, id, remove_all);
            }
        } else {
            remove_all.insert(removed_ids_.begin(), removed_ids_.end());
        }

        auto next_page = doc.page();
        if (next_page.layers.empty()) {
            return document::CommandResult::fail("Document has no layers");
        }
        const size_t layer_idx = std::min(target_layer_index_, next_page.layers.size() - 1);
        bool inserted = false;
        for (auto& layer : next_page.layers) {
            std::vector<document::ObjectId> updated_ids;
            updated_ids.reserve(layer.object_ids.size() + 1);
            for (const auto obj_id : layer.object_ids) {
                if (remove_all.find(obj_id) != remove_all.end()) {
                    if (!inserted) {
                        updated_ids.push_back(new_bitmap_.id);
                        inserted = true;
                    }
                    continue;
                }
                updated_ids.push_back(obj_id);
            }
            layer.object_ids = std::move(updated_ids);
        }
        if (!inserted) {
            next_page.layers[layer_idx].object_ids.push_back(new_bitmap_.id);
        }

        auto next_objects = doc.objects();
        for (const auto rem_id : remove_all) {
            next_objects = next_objects.remove(rem_id);
        }
        next_objects = next_objects.set(new_bitmap_.id, document::ObjectVariant{new_bitmap_});
        doc.apply_mutation(std::move(next_page), std::move(next_objects));
        executed_ = true;
        return document::CommandResult::ok();
    }

    document::CommandResult undo(document::Document& doc) override {
        if (!executed_ || !previous_page_ || !previous_objects_) {
            return document::CommandResult::fail("No bitmap conversion state to undo");
        }
        doc.apply_mutation(*previous_page_, *previous_objects_);
        executed_ = false;
        return document::CommandResult::ok();
    }

private:
    std::vector<document::ObjectId> removed_ids_;
    document::ImageObject new_bitmap_;
    size_t target_layer_index_{0};
    std::optional<document::Page> previous_page_;
    std::optional<document::PersistentObjectMap> previous_objects_;
    bool executed_{false};
};

class EyedropperApplyObjectStyleCommand final : public document::DocumentCommand {
public:
    EyedropperApplyObjectStyleCommand(
        std::vector<document::ObjectId> target_ids,
        CanvasColorSample source)
        : target_ids_(std::move(target_ids)), source_(std::move(source)) {}

    [[nodiscard]] std::string_view name() const noexcept override {
        return "Apply Eyedropper Object Style";
    }

    document::CommandResult execute(document::Document& doc) override {
        if (target_ids_.empty() || !source_.has_object_style ||
            !source_.object_style.fill.is_valid() ||
            !source_.object_style.stroke.is_valid() ||
            !std::isfinite(source_.object_opacity) ||
            source_.object_opacity < 0.0 || source_.object_opacity > 1.0)
            return document::CommandResult::fail("Invalid eyedropper style source");
        const auto snapshot = doc.current_snapshot();
        if (!snapshot) return document::CommandResult::fail("No document snapshot");

        auto next_objects = doc.objects();
        std::unordered_set<document::ObjectId> seen;
        for (const auto id : target_ids_) {
            if (!seen.insert(id).second) continue;
            if (!snapshot->hierarchy().editable(id))
                return document::CommandResult::fail("Style target is missing, hidden or locked");
            const auto* source_object = snapshot->objects().lookup_ref(id);
            if (!source_object)
                return document::CommandResult::fail("Style target is missing");
            auto updated = *source_object;
            if (auto* path = std::get_if<document::PathObject>(&updated)) {
                path->fill = source_.object_style.fill;
                path->stroke = source_.object_style.stroke;
                path->stroke_width_mm = source_.object_style.width_mm;
                path->has_fill = source_.object_style.has_fill;
                path->has_stroke = source_.object_style.has_stroke;
                path->appearance.object_opacity = source_.object_opacity;
                path->appearance.blend_mode = source_.object_blend_mode;
                if (source_.has_gradient && source_.gradient_paint && path->has_fill) {
                    auto paint = *source_.gradient_paint;
                    std::vector<uint64_t> stop_ids;
                    stop_ids.reserve(paint.stops.size());
                    for (size_t index = 0; index < paint.stops.size(); ++index)
                        stop_ids.push_back(document::allocate_gradient_stop_id());
                    paint = paint.clone_with_new_ids(
                        document::allocate_gradient_paint_id(), stop_ids);
                    path->fill_gradient = std::make_shared<const document::GradientPaint>(
                        std::move(paint));
                } else {
                    path->fill_gradient.reset();
                }
            } else if (auto* text = std::get_if<document::TextObject>(&updated)) {
                text->fill = source_.object_style.fill;
                text->appearance.object_opacity = source_.object_opacity;
                text->appearance.blend_mode = source_.object_blend_mode;
            } else {
                return document::CommandResult::fail(
                    "Eyedropper style supports paths and text targets");
            }
            next_objects = next_objects.set(id, std::move(updated));
        }

        before_objects_ = doc.objects();
        doc.apply_mutation(doc.page(), std::move(next_objects));
        executed_ = true;
        return document::CommandResult::ok();
    }

    document::CommandResult undo(document::Document& doc) override {
        if (!executed_ || !before_objects_)
            return document::CommandResult::fail("No eyedropper style state to undo");
        doc.apply_mutation(doc.page(), *before_objects_);
        executed_ = false;
        return document::CommandResult::ok();
    }

private:
    std::vector<document::ObjectId> target_ids_;
    CanvasColorSample source_;
    std::optional<document::PersistentObjectMap> before_objects_;
    bool executed_{false};
};

} // namespace

CanvasColorSample NativeWindow::sample_canvas_color_at(int client_x, int client_y) const {
    CanvasColorSample sample{};
    if (show_home_screen_) return sample;
    if (!is_canvas_point(client_x, client_y)) return sample;

    const auto snapshot = root().document().current_snapshot();
    if (!snapshot) return sample;

    const auto world_pt = const_cast<NativeWindow*>(this)->canvas_to_world(client_x, client_y);
    const double zoom = std::max(0.01, root().renderer().camera().zoom());
    const double tolerance = 4.0 / zoom;
    const auto* preview = effective_color_preview();
    const auto finish_sample = [&]() {
        (void)apply_displayed_canvas_pixel(client_x, client_y, sample);
        return sample;
    };

    const auto& page = snapshot->page();
    // Use painter order across every visible object type. Locked objects still
    // contribute visible pixels and therefore remain valid sample sources.
    if (const auto hit = vector::SelectionModel::hit_test_visible_detail(
            world_pt, *snapshot, tolerance)) {
        const document::ObjectId hit_id = hit->id;
        const auto* obj_var = snapshot->objects().lookup_ref(hit_id);
        if (obj_var) {
            if (const auto* image = std::get_if<document::ImageObject>(obj_var)) {
                geometry::Transform2D world_xf = image->transform;
                if (const auto* entry = snapshot->hierarchy().find(hit_id))
                    world_xf = entry->world;
                const document::ImageObject* pixel_src = image;
#ifdef _WIN32
                std::shared_ptr<const document::ImageObject> preview_img;
                const bool has_enabled_edits = std::any_of(
                    image->edit_graph.nodes.begin(), image->edit_graph.nodes.end(),
                    [](const auto& node) { return node.enabled; });
                if (raster_preview_service_) {
                    preview_img = raster_preview_service_->find(*snapshot, *image);
                    if (preview_img && preview_img->width_px > 0 && preview_img->height_px > 0) {
                        pixel_src = preview_img.get();
                    }
                }
                if (has_enabled_edits && !preview_img)
                    return CanvasColorSample{};
#endif
                const auto address = eyedropper_detail::bitmap_pixel_address(
                    *pixel_src, world_xf, world_pt);
                if (!address) return CanvasColorSample{};
                const auto pixels = pixel_src->rgba_pixels();
                const size_t idx = static_cast<size_t>(address->y) *
                                       static_cast<size_t>(pixel_src->width_px) +
                                   static_cast<size_t>(address->x);
                if (idx >= pixels.size()) return CanvasColorSample{};
                const uint32_t packed = pixels[idx];
                const uint8_t r = static_cast<uint8_t>((packed >> 24) & 0xFFu);
                const uint8_t g = static_cast<uint8_t>((packed >> 16) & 0xFFu);
                const uint8_t b = static_cast<uint8_t>((packed >> 8) & 0xFFu);
                const uint8_t a = static_cast<uint8_t>(packed & 0xFFu);
                sample.valid = true;
                sample.on_object = true;
                sample.object_id = hit_id;
                sample.is_bitmap_pixel = true;
                sample.object_label = image->name.empty() ? "Bitmap" : image->name;
                sample.pixel_x = address->x;
                sample.pixel_y = address->y;
                populate_sample_from_rgba(sample, r, g, b, a);
                return finish_sample();
            }

            if (const auto* path_obj = std::get_if<document::PathObject>(obj_var)) {
                sample.valid = true;
                sample.on_object = true;
                sample.object_id = hit_id;
                sample.object_label = path_obj->name.empty() ? "Path" : path_obj->name;
                sample.has_object_style = true;
                sample.object_style.fill = path_obj->fill;
                sample.object_style.stroke = path_obj->stroke;
                sample.object_style.width_mm = path_obj->stroke_width_mm;
                sample.object_style.has_fill = path_obj->has_fill;
                sample.object_style.has_stroke = path_obj->has_stroke;
                sample.object_opacity = path_obj->appearance.object_opacity;
                sample.object_blend_mode = path_obj->appearance.blend_mode;

                geometry::Transform2D world_xf = path_obj->transform;
                if (const auto* entry = snapshot->hierarchy().find(hit_id)) {
                    world_xf = entry->world;
                }
                if (path_obj->has_fill && path_obj->fill_gradient) {
                    sample.has_gradient = true;
                    sample.gradient_paint = *path_obj->fill_gradient;
                    const auto eval_col = evaluate_gradient_at_world(
                        *path_obj->fill_gradient, world_xf, world_pt, preview);
                    populate_sample_from_color_value(sample, eval_col, preview);
                } else if (path_obj->has_fill) {
                    populate_sample_from_color_value(sample, path_obj->fill, preview);
                } else if (path_obj->has_stroke) {
                    populate_sample_from_color_value(sample, path_obj->stroke, preview);
                } else {
                    populate_sample_from_rgba(sample, 255, 255, 255, 0);
                }
                return finish_sample();
            }
            if (const auto* text_obj = std::get_if<document::TextObject>(obj_var)) {
                sample.valid = true;
                sample.on_object = true;
                sample.object_id = hit_id;
                sample.object_label = text_obj->name.empty() ? "Text" : text_obj->name;
                sample.has_object_style = true;
                sample.object_style.fill = text_obj->fill;
                sample.object_style.has_fill = true;
                sample.object_style.has_stroke = false;
                sample.object_opacity = text_obj->appearance.object_opacity;
                sample.object_blend_mode = text_obj->appearance.blend_mode;
                populate_sample_from_color_value(sample, text_obj->fill, preview);
                return finish_sample();
            }
        }
    }

    // Background / Page sample
    sample.valid = true;
    sample.on_object = false;
    const geometry::Rect2D page_rect{0.0, 0.0, page.width_mm, page.height_mm};
    if (page_rect.contains(world_pt)) {
        sample.object_label = (language_ == UILanguage::Vietnamese) ? "Trang giấy" : "Page Paper";
        populate_sample_from_rgba(sample, 255, 255, 255, 255);
    } else {
        sample.object_label = (language_ == UILanguage::Vietnamese) ? "Nền Canvas" : "Pasteboard";
        if (theme_ == ThemeMode::CyberDark) {
            populate_sample_from_rgba(sample, 18, 24, 36, 255);
        } else {
            populate_sample_from_rgba(sample, 226, 232, 240, 255);
        }
    }
    return finish_sample();
}

bool NativeWindow::apply_displayed_canvas_pixel(
    int client_x, int client_y, CanvasColorSample& sample) const {
#ifdef _WIN32
    if (!sample.valid || sample.color_value.color_space() != color::ColorSpace::Rgb ||
        !cached_canvas_dc_ || !canvas_cache_valid_ ||
        direct_present_frame_ || direct_present_active_)
        return false;

    const RECT area = canvas_rect(width_, height_);
    if (client_x < area.left || client_x >= area.right ||
        client_y < area.top || client_y >= area.bottom ||
        cached_canvas_w_ != area.right - area.left ||
        cached_canvas_h_ != area.bottom - area.top)
        return false;

    const auto snapshot = root().document().current_snapshot();
    const auto& camera = root().renderer().camera();
    const rendering::RenderViewContext clip_view{
        current_isolated_group_id().value_or(0), isolation_generation_,
        isolation_reveal_, isolation_frame_};
    if (!snapshot || cached_canvas_snapshot_ != snapshot ||
        cached_revision_ != snapshot->revision() ||
        cached_canvas_profile_ != effective_color_preview_owner() ||
        cached_zoom_ != camera.zoom() ||
        cached_camera_x_ != camera.center().x ||
        cached_camera_y_ != camera.center().y || cached_theme_ != theme_ ||
        cached_clip_view_ != clip_view ||
        cached_artboard_overview_ != artboard_overview_enabled() ||
        cached_active_artboard_id_ != active_tab().active_artboard_id)
        return false;

    // GetPixel uses the memory DC's DIP mapping, matching the pointer/canvas
    // mapping used for hit testing and drawing. This reads the composited frame,
    // including overlaps, opacity, blend modes and the active ICC preview.
    const COLORREF displayed = GetPixel(
        cached_canvas_dc_, client_x - area.left, client_y - area.top);
    if (displayed == CLR_INVALID) return false;

    const auto r = static_cast<uint8_t>(GetRValue(displayed));
    const auto g = static_cast<uint8_t>(GetGValue(displayed));
    const auto b = static_cast<uint8_t>(GetBValue(displayed));
    populate_sample_from_rgba(sample, r, g, b, 255);
    sample.color_value = color::RGBColor{
        static_cast<double>(r) / 255.0,
        static_cast<double>(g) / 255.0,
        static_cast<double>(b) / 255.0,
        1.0};
    return true;
#else
    (void)client_x;
    (void)client_y;
    (void)sample;
    return false;
#endif
}

bool NativeWindow::apply_eyedropper_sample_to_objects(
    const CanvasColorSample& sample,
    const std::vector<document::ObjectId>& target_ids,
    bool apply_to_stroke,
    bool force_solid_color) {
    if (!sample.valid) return false;
    const bool vi = (language_ == UILanguage::Vietnamese);

    // Mode 1: Copy Gradient Paint (or when sample has a gradient and mode == 1)
    if (eyedropper_mode_ == 1 && sample.has_gradient && sample.gradient_paint && !apply_to_stroke) {
        const auto snapshot = root().document().current_snapshot();
        if (!snapshot) return false;
        std::vector<document::FillPaintReplacement> replacements;
        for (const auto id : target_ids) {
            const auto* obj_var = snapshot->objects().lookup_ref(id);
            if (!obj_var || !std::holds_alternative<document::PathObject>(*obj_var)) continue;
            document::GradientPaint paint_copy = *sample.gradient_paint;
            const uint64_t new_paint_id = document::allocate_gradient_paint_id();
            std::vector<uint64_t> new_stop_ids;
            new_stop_ids.reserve(paint_copy.stops.size());
            for (size_t s = 0; s < paint_copy.stops.size(); ++s) {
                new_stop_ids.push_back(document::allocate_gradient_stop_id());
            }
            paint_copy = paint_copy.clone_with_new_ids(new_paint_id, new_stop_ids);
            document::FillPaintReplacement rep;
            rep.object_id = id;
            rep.has_fill = true;
            rep.fill = paint_copy.stops.empty() ? sample.color_value : paint_copy.stops.front().color;
            rep.fill_gradient = std::make_shared<const document::GradientPaint>(std::move(paint_copy));
            replacements.push_back(std::move(rep));
        }
        if (!replacements.empty()) {
            const auto res = root().document().execute_command(
                std::make_unique<document::SetFillPaintCommand>(std::move(replacements)));
            if (res.success) {
                status_msg_ = vi
                    ? "Đã sao chép & áp dụng dải màu Gradient · Ctrl+Z để hoàn tác"
                    : "Copied & applied Gradient paint · Ctrl+Z to undo";
#ifdef _WIN32
                if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
                return true;
            }
        }
    }

    // Mode 2: Copy full Object Attributes (Fill + Stroke + Width + Gradient)
    if (eyedropper_mode_ == 2 && sample.has_object_style &&
        !apply_to_stroke && !force_solid_color) {
        bool ok = false;
        if (!target_ids.empty()) {
            ok = root().document().execute_command(
                std::make_unique<EyedropperApplyObjectStyleCommand>(
                    target_ids, sample)).success;
        } else {
            vector::StylePatch patch;
            patch.fill = sample.object_style.fill;
            patch.stroke = sample.object_style.stroke;
            patch.width_mm = sample.object_style.width_mm;
            patch.has_fill = sample.object_style.has_fill;
            patch.has_stroke = sample.object_style.has_stroke;
            ok = apply_style_patch(patch);
        }
        if (ok) {
            status_msg_ = vi
                ? "Đã sao chép toàn bộ thuộc tính màu & nét vẽ · Ctrl+Z hoàn tác"
                : "Copied full object fill & stroke attributes · Ctrl+Z to undo";
#ifdef _WIN32
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
        }
        return ok;
    }

    // Mode 0 (or fallback): Apply sampled solid color to Fill or Stroke
    vector::StylePatch patch;
    if (apply_to_stroke) {
        patch.stroke = sample.color_value;
        patch.has_stroke = true;
    } else {
        patch.fill = sample.color_value;
        patch.has_fill = true;
    }
    bool ok = false;
    if (!target_ids.empty()) {
        ok = vector::apply_object_styles(root().document(), target_ids, patch).success;
    } else {
        ok = apply_style_patch(patch);
    }
    if (ok) {
        status_msg_ = vi
            ? ("Đã áp dụng " + eyedropper_detail::format_sample_color(sample) +
               (apply_to_stroke ? " cho viền" : " cho nền") + " · Ctrl+Z hoàn tác")
            : ("Applied " + eyedropper_detail::format_sample_color(sample) +
               (apply_to_stroke ? " to stroke" : " to fill") + " · Ctrl+Z to undo");
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
    }
    return ok;
}

bool NativeWindow::handle_eyedropper_mouse_down(int x, int y, unsigned modifiers) {
    if (active_tool_ != 15) return false;
    const auto sample = sample_canvas_color_at(x, y);
    if (!sample.valid) return false;

    const bool alt_down = (modifiers & 0x2000u) != 0
#ifdef _WIN32
        || (GetKeyState(VK_MENU) < 0)
#endif
        ;
    const bool shift_down = (modifiers & 0x0004u) != 0
#ifdef _WIN32
        || (GetKeyState(VK_SHIFT) < 0)
#endif
        ;
    const bool vi = (language_ == UILanguage::Vietnamese);

    // Alt+Click (or when a sample is already loaded and clicking an object with Alt): Pour / Paint Bucket onto hovered object
    if (alt_down && eyedropper_has_picked_sample_ && sample.on_object &&
        sample.object_id != document::kInvalidObjectId) {
        return apply_eyedropper_sample_to_objects(
            eyedropper_picked_sample_, {sample.object_id}, shift_down);
    }

    // Pick sample from canvas
    eyedropper_picked_sample_ = sample;
    eyedropper_has_picked_sample_ = true;
    hover_color_sample_ = sample;

    // Copy HEX to clipboard or apply to current selection immediately
    const auto& selected_ids = root().vector_controller().selection().selected_ids();
    if (!selected_ids.empty()) {
        // Don't overwrite the bitmap itself if the user is sampling pixels from the currently selected bitmap
        std::vector<document::ObjectId> PaintTargets;
        const auto snapshot = root().document().current_snapshot();
        for (const auto sel_id : selected_ids) {
            if (snapshot) {
                if (const auto* obj = snapshot->objects().lookup_ref(sel_id);
                    obj && std::holds_alternative<document::ImageObject>(*obj)) {
                    continue;
                }
            }
            PaintTargets.push_back(sel_id);
        }
        if (!PaintTargets.empty()) {
            (void)apply_eyedropper_sample_to_objects(sample, PaintTargets, shift_down);
            return true;
        }
    }

    // Also update default style fill/stroke so next drawn shape uses the sampled color
    if (shift_down) {
        default_style_.stroke = sample.color_value;
        default_style_.has_stroke = true;
    } else {
        default_style_.fill = sample.color_value;
        default_style_.has_fill = true;
    }

    std::ostringstream oss;
    oss << (vi ? "Đã hút màu " : "Sampled ")
        << eyedropper_detail::format_sample_color(sample);
    if (sample.is_bitmap_pixel) {
        oss << " @ (" << sample.pixel_x << ", " << sample.pixel_y << " px)";
    } else if (sample.has_gradient) {
        oss << (vi ? " [Có dải Gradient]" : " [Gradient Paint]");
    }
    status_msg_ = oss.str();
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
    return true;
}

bool NativeWindow::handle_eyedropper_mouse_move(int x, int y, unsigned /*modifiers*/) {
    const auto sample = sample_canvas_color_at(x, y);
    const bool changed = (sample.valid != hover_color_sample_.valid) ||
                         (sample.r != hover_color_sample_.r) ||
                         (sample.g != hover_color_sample_.g) ||
                         (sample.b != hover_color_sample_.b) ||
                         (sample.pixel_x != hover_color_sample_.pixel_x) ||
                         (sample.pixel_y != hover_color_sample_.pixel_y) ||
                         (sample.object_id != hover_color_sample_.object_id);
    hover_color_sample_ = sample;
    // The cursor overlay must follow the pointer over flat-color regions too.
    return changed || active_tool_ == 15;
}

bool NativeWindow::handle_eyedropper_property_bar_click(int x, int y) {
    const int topbar_h = 38, tabbar_h = 28, ribbon_h = 36;
    const int bar_top = topbar_h + tabbar_h;
    if (y < bar_top || y >= bar_top + ribbon_h) return false;
    if (active_tool_ != 15) return false;

    const bool vi = (language_ == UILanguage::Vietnamese);
    // Mode buttons: [58..162] Solid, [168..282] Gradient, [288..408] Attributes
    if (x >= 58 && x <= 162) {
        eyedropper_mode_ = 0;
        status_msg_ = vi ? "Chế độ Hút màu đơn (Solid Color)" : "Eyedropper Mode: Solid Color";
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
        return true;
    }
    if (x >= 168 && x <= 282) {
        eyedropper_mode_ = 1;
        status_msg_ = vi ? "Chế độ Sao chép dải màu Gradient" : "Eyedropper Mode: Copy Gradient Paint";
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
        return true;
    }
    if (x >= 288 && x <= 408) {
        eyedropper_mode_ = 2;
        status_msg_ = vi ? "Chế độ Sao chép thuộc tính đối tượng (Nền + Viền)" : "Eyedropper Mode: Copy Object Style";
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
        return true;
    }
    // Apply to Selection Fill [420..520] / Apply to Selection Stroke [526..626]
    if (x >= 420 && x <= 520 && eyedropper_has_picked_sample_) {
        const auto& ids = root().vector_controller().selection().selected_ids();
        (void)apply_eyedropper_sample_to_objects(
            eyedropper_picked_sample_, ids, false, true);
        return true;
    }
    if (x >= 526 && x <= 626 && eyedropper_has_picked_sample_) {
        const auto& ids = root().vector_controller().selection().selected_ids();
        (void)apply_eyedropper_sample_to_objects(
            eyedropper_picked_sample_, ids, true, true);
        return true;
    }
    return false;
}

#ifdef _WIN32
void NativeWindow::paint_eyedropper_property_bar(
    HDC mem_dc, int w, int topbar_h, int tabbar_h, int content_top) {
    (void)w;
    ensure_ui_fonts();
    const bool is_dark = (theme_ == ThemeMode::CyberDark);
    const bool is_vi = (language_ == UILanguage::Vietnamese);
    const COLORREF col_border = is_dark ? RGB(45, 58, 79) : RGB(203, 213, 225);
    const COLORREF col_teal = is_dark ? RGB(0, 242, 195) : RGB(0, 135, 106);
    const COLORREF col_text = is_dark ? RGB(241, 245, 249) : RGB(15, 23, 42);
    const COLORREF col_muted = is_dark ? RGB(148, 163, 184) : RGB(100, 116, 139);
    const COLORREF col_btn = is_dark ? RGB(22, 33, 48) : RGB(241, 245, 249);
    const COLORREF col_btn_active = is_dark ? RGB(18, 52, 62) : RGB(209, 250, 229);

    const int bar_y = topbar_h + tabbar_h;
    const int btn_top = bar_y + 5;
    const int btn_bottom = content_top - 5;

    auto draw_pill = [&](int l, int r, bool active, const wchar_t* label) {
        HBRUSH br = CreateSolidBrush(active ? col_btn_active : col_btn);
        HPEN pen = CreatePen(PS_SOLID, 1, active ? col_teal : col_border);
        HGDIOBJ old_b = SelectObject(mem_dc, br);
        HGDIOBJ old_p = SelectObject(mem_dc, pen);
        RoundRect(mem_dc, l, btn_top, r, btn_bottom, 5, 5);
        SelectObject(mem_dc, old_b);
        SelectObject(mem_dc, old_p);
        DeleteObject(br);
        DeleteObject(pen);

        SelectObject(mem_dc, active ? font_be_vietnam_bold_ : font_secondary_);
        SetTextColor(mem_dc, active ? col_teal : col_text);
        RECT rc = {l + 4, btn_top, r - 4, btn_bottom};
        DrawTextW(mem_dc, label, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    };

    draw_pill(58, 162, eyedropper_mode_ == 0, is_vi ? L"Hút màu (I)" : L"Solid Color (I)");
    draw_pill(168, 282, eyedropper_mode_ == 1, is_vi ? L"Sao chép Gradient" : L"Copy Gradient");
    draw_pill(288, 408, eyedropper_mode_ == 2, is_vi ? L"Sao chép Style" : L"Copy Style");

    const CanvasColorSample& active_sample =
        eyedropper_has_picked_sample_ ? eyedropper_picked_sample_ : hover_color_sample_;

    draw_pill(420, 520, false, is_vi ? L"Đổ màu Nền" : L"Apply Fill");
    draw_pill(526, 626, false, is_vi ? L"Đổ màu Viền" : L"Apply Stroke");

    // Live color swatch + one exact source color model on the Property Bar.
    int sw_l = 638;
    HBRUSH sw_br = CreateSolidBrush(RGB(active_sample.r, active_sample.g, active_sample.b));
    HPEN sw_pen = CreatePen(PS_SOLID, 1, col_border);
    HGDIOBJ ob = SelectObject(mem_dc, sw_br);
    HGDIOBJ op = SelectObject(mem_dc, sw_pen);
    RoundRect(mem_dc, sw_l, btn_top + 2, sw_l + 24, btn_bottom - 2, 4, 4);
    SelectObject(mem_dc, ob);
    SelectObject(mem_dc, op);
    DeleteObject(sw_br);
    DeleteObject(sw_pen);

    std::ostringstream info;
    if (active_sample.valid) {
        info << eyedropper_detail::format_sample_color(active_sample);
        if (active_sample.is_bitmap_pixel) {
            info << "   [px: " << active_sample.pixel_x << ", " << active_sample.pixel_y << "]";
        } else if (active_sample.has_gradient) {
            info << (is_vi ? "   [Gradient]" : "   [Gradient]");
        }
    } else {
        info << (is_vi
            ? "Di chuột lên đối tượng hoặc ảnh để kiểm tra chỉ số màu · Click để hút màu · Shift+Click áp vào viền"
            : "Hover any object or bitmap to inspect color values · Click to sample · Shift+Click for stroke");
    }

    SelectObject(mem_dc, font_mono_ ? font_mono_ : font_secondary_);
    SetTextColor(mem_dc, active_sample.valid ? col_text : col_muted);
    RECT info_rc = {sw_l + 32, btn_top, w - 16, btn_bottom};
    const std::wstring wide_info = utf8_to_wide(info.str());
    DrawTextW(mem_dc, wide_info.c_str(), -1, &info_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void NativeWindow::paint_eyedropper_cursor_overlay(HDC dc, int width, int height) {
    if (active_tool_ != 15 || !hover_color_sample_.valid) return;

    ensure_ui_fonts();
    const int cx = last_canvas_pointer_x_;
    const int cy = last_canvas_pointer_y_;
    const bool is_dark = (theme_ == ThemeMode::CyberDark);

    // A small reticle marks one sample point; it does not represent an average area.
    const int ring_r = 6;
    HPEN outer_pen = CreatePen(PS_SOLID, 2, RGB(15, 23, 42));
    HPEN color_pen = CreatePen(PS_SOLID, 1, RGB(hover_color_sample_.r, hover_color_sample_.g, hover_color_sample_.b));
    HGDIOBJ old_br = SelectObject(dc, GetStockObject(NULL_BRUSH));
    HGDIOBJ old_pen = SelectObject(dc, outer_pen);
    Ellipse(dc, cx - ring_r - 2, cy - ring_r - 2, cx + ring_r + 2, cy + ring_r + 2);
    SelectObject(dc, color_pen);
    Ellipse(dc, cx - ring_r, cy - ring_r, cx + ring_r, cy + ring_r);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_br);
    DeleteObject(outer_pen);
    DeleteObject(color_pen);

    const POINT target_arms[][2] = {
        {{cx - 3, cy}, {cx - 1, cy}}, {{cx + 1, cy}, {cx + 3, cy}},
        {{cx, cy - 3}, {cx, cy - 1}}, {{cx, cy + 1}, {cx, cy + 3}}
    };
    HPEN target_outline = CreatePen(PS_SOLID, 3, RGB(15, 23, 42));
    HPEN target_ink = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
    old_pen = SelectObject(dc, target_outline);
    for (const auto& arm : target_arms) {
        MoveToEx(dc, arm[0].x, arm[0].y, nullptr);
        LineTo(dc, arm[1].x, arm[1].y);
    }
    SelectObject(dc, target_ink);
    for (const auto& arm : target_arms) {
        MoveToEx(dc, arm[0].x, arm[0].y, nullptr);
        LineTo(dc, arm[1].x, arm[1].y);
    }
    SelectObject(dc, old_pen);
    DeleteObject(target_outline);
    DeleteObject(target_ink);

    // Floating readout badge beside cursor
    const int badge_w = 196;
    const int badge_h = hover_color_sample_.is_bitmap_pixel ? 40 : 28;
    const RECT area = canvas_rect(width, height);
    const int min_badge_x = static_cast<int>(area.left) + 4;
    const int max_badge_x = std::max(
        min_badge_x, static_cast<int>(area.right) - badge_w - 4);
    int badge_x = cx + 13;
    if (badge_x + badge_w > area.right - 4) badge_x = cx - badge_w - 13;
    badge_x = std::clamp(badge_x, min_badge_x, max_badge_x);
    const int min_badge_y = static_cast<int>(area.top) + 4;
    const int max_badge_y = std::max(
        min_badge_y, static_cast<int>(area.bottom) - badge_h - 4);
    int badge_y = cy + 11;
    if (badge_y + badge_h > area.bottom - 4) badge_y = cy - badge_h - 11;
    badge_y = std::clamp(badge_y, min_badge_y, max_badge_y);

    HBRUSH bg_br = CreateSolidBrush(is_dark ? RGB(15, 23, 42) : RGB(255, 255, 255));
    HPEN bg_pen = CreatePen(PS_SOLID, 1, is_dark ? RGB(51, 65, 85) : RGB(148, 163, 184));
    old_br = SelectObject(dc, bg_br);
    old_pen = SelectObject(dc, bg_pen);
    RoundRect(dc, badge_x, badge_y, badge_x + badge_w, badge_y + badge_h, 6, 6);
    SelectObject(dc, old_br);
    SelectObject(dc, old_pen);
    DeleteObject(bg_br);
    DeleteObject(bg_pen);

    // Color swatch inside badge
    HBRUSH sw_br = CreateSolidBrush(RGB(hover_color_sample_.r, hover_color_sample_.g, hover_color_sample_.b));
    RECT sw_rc = {badge_x + 6, badge_y + 6, badge_x + 28, badge_y + badge_h - 6};
    FillRect(dc, &sw_rc, sw_br);
    DeleteObject(sw_br);

    SetBkMode(dc, TRANSPARENT);
    SelectObject(dc, font_be_vietnam_bold_);
    SetTextColor(dc, is_dark ? RGB(241, 245, 249) : RGB(15, 23, 42));
    const std::wstring readout = utf8_to_wide(
        eyedropper_detail::format_sample_color(hover_color_sample_));
    RECT r1 = {badge_x + 34, badge_y + 4, badge_x + badge_w - 4, badge_y + badge_h - 4};
    DrawTextW(dc, readout.c_str(), -1, &r1,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    if (hover_color_sample_.is_bitmap_pixel) {
        std::ostringstream line3;
        line3 << "Bitmap px (" << hover_color_sample_.pixel_x << ", " << hover_color_sample_.pixel_y << ")";
        RECT r3 = {badge_x + 34, badge_y + 22, badge_x + badge_w - 4, badge_y + 37};
        const std::wstring w3 = utf8_to_wide(line3.str());
        DrawTextW(dc, w3.c_str(), -1, &r3, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
}
#endif

bool NativeWindow::can_convert_selected_to_bitmap() const {
    if (root().document().is_content_locked()) return false;
    const auto& selected = root().vector_controller().selection().selected_ids();
    if (selected.empty()) return false;
    const auto snapshot = root().document().current_snapshot();
    if (!snapshot) return false;
    std::unordered_set<document::ObjectId> page_roots;
    for (const auto& layer : snapshot->page().layers) {
        page_roots.insert(layer.object_ids.begin(), layer.object_ids.end());
    }
    std::unordered_set<document::ObjectId> converted_tree_ids;
    std::unordered_set<document::ObjectId> recursion_stack;
    for (const auto id : selected) {
        // Replacing a nested child needs its parent group, transform and clip
        // topology to be rewritten. Keep this operation at page-layer roots.
        if (!page_roots.contains(id) ||
            !snapshot->hierarchy().editable(id) ||
            !collect_unlocked_conversion_tree(
                *snapshot, id, converted_tree_ids, recursion_stack))
            return false;
    }
    for (const auto id : converted_tree_ids) {
        const auto* obj = snapshot->objects().lookup_ref(id);
        const auto* text = obj ? std::get_if<document::TextObject>(obj) : nullptr;
        if (text && ((text->next_frame_id != document::kInvalidObjectId &&
                      !converted_tree_ids.contains(text->next_frame_id)) ||
                     (text->prev_frame_id != document::kInvalidObjectId &&
                      !converted_tree_ids.contains(text->prev_frame_id))))
            return false;
    }
    return true;
}

bool NativeWindow::convert_selected_to_bitmap(uint32_t dpi) {
    if (!can_convert_selected_to_bitmap()) {
        status_msg_ = (language_ == UILanguage::Vietnamese)
            ? "Chọn đối tượng cấp layer không khóa; giữ trọn nhóm và chuỗi Text liên kết khi chuyển Bitmap"
            : "Select unlocked layer-level objects; keep groups and linked Text chains intact when converting";
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
        return false;
    }

    const auto snapshot = root().document().current_snapshot();
    if (!snapshot) return false;
    const auto& selected = root().vector_controller().selection().selected_ids();
    if (selected.empty()) return false;

    std::unordered_set<document::ObjectId> top_selected(selected.begin(), selected.end());
    const auto sel_bounds = root().vector_controller().selection().bounds(*snapshot);
    if (!sel_bounds || sel_bounds->width <= 1e-3 || sel_bounds->height <= 1e-3) {
        return false;
    }
    geometry::Rect2D united_bounds = *sel_bounds;

    // Pad slightly so stroke edges are not clipped
    const double pad_mm = 0.5;
    united_bounds.x -= pad_mm;
    united_bounds.y -= pad_mm;
    united_bounds.width += pad_mm * 2.0;
    united_bounds.height += pad_mm * 2.0;

    const uint32_t effective_dpi = std::clamp(dpi, 72u, 600u);
    double scale = static_cast<double>(effective_dpi) / 25.4;
    const double max_dim = std::max(united_bounds.width * scale, united_bounds.height * scale);
    if (max_dim > 2048.0) {
        scale = 2048.0 / std::max(united_bounds.width, united_bounds.height);
    }
    const int32_t width_px = std::clamp(static_cast<int32_t>(std::ceil(united_bounds.width * scale)), 4, 2048);
    const int32_t height_px = std::clamp(static_cast<int32_t>(std::ceil(united_bounds.height * scale)), 4, 2048);

    // Build isolated snapshot containing only the selected objects
    document::Page isolated_page = snapshot->page();
    for (auto& layer : isolated_page.layers) {
        std::vector<document::ObjectId> filtered;
        for (const auto obj_id : layer.object_ids) {
            if (top_selected.find(obj_id) != top_selected.end()) {
                filtered.push_back(obj_id);
            }
        }
        layer.object_ids = std::move(filtered);
    }

    document::DocumentSnapshot isolated_snapshot(
        snapshot->epoch(), snapshot->revision(),
        std::move(isolated_page), snapshot->objects(),
        snapshot->document_id(), snapshot->content_mode());

    std::vector<uint8_t> raw_rgba(static_cast<size_t>(width_px) * static_cast<size_t>(height_px) * 4u, 0u);
    rendering::SoftwareRasterizer rasterizer;
    (void)rasterizer.set_color_preview(effective_color_preview_owner());
    (void)rasterizer.set_canvas_background({0, 0, 0, 0});
    rasterizer.set_render_page_background(false);
    bool unresolved_raster_preview = false;
    rasterizer.set_raster_image_resolver(
        [this, &unresolved_raster_preview](
            const document::DocumentSnapshot& source_snapshot,
            const document::ImageObject& image) {
            auto preview = raster_preview_service_
                ? raster_preview_service_->find(source_snapshot, image)
                : std::shared_ptr<const document::ImageObject>{};
            const bool has_enabled_edits = std::any_of(
                image.edit_graph.nodes.begin(), image.edit_graph.nodes.end(),
                [](const auto& node) { return node.enabled; });
            if (has_enabled_edits && !preview)
                unresolved_raster_preview = true;
            return preview;
        });
    rendering::PixelBufferTarget target{
        raw_rgba.data(), width_px, height_px, width_px * 4};
    rendering::Camera2D camera(
        {united_bounds.x + united_bounds.width * 0.5, united_bounds.y + united_bounds.height * 0.5},
        scale, width_px, height_px);
    if (!rasterizer.render(isolated_snapshot, camera, target)) {
        return false;
    }
    if (unresolved_raster_preview) {
        status_msg_ = (language_ == UILanguage::Vietnamese)
            ? "Ảnh đang cập nhật bản xem trước chỉnh sửa; hãy thử chuyển Bitmap lại sau"
            : "Raster edits are still preparing a preview; retry Bitmap conversion after it updates";
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
        return false;
    }

    const size_t total_pixels = static_cast<size_t>(width_px) * static_cast<size_t>(height_px);
    auto packed_rgba = std::make_shared<std::vector<uint32_t>>(total_pixels);
    auto rgba16le = std::make_shared<std::vector<uint8_t>>(total_pixels * 8u);
    for (size_t i = 0; i < total_pixels; ++i) {
        const uint8_t r = raw_rgba[i * 4 + 0];
        const uint8_t g = raw_rgba[i * 4 + 1];
        const uint8_t b = raw_rgba[i * 4 + 2];
        const uint8_t a = raw_rgba[i * 4 + 3];
        (*packed_rgba)[i] = (static_cast<uint32_t>(r) << 24) |
                            (static_cast<uint32_t>(g) << 16) |
                            (static_cast<uint32_t>(b) << 8) |
                            static_cast<uint32_t>(a);
        // The CPU renderer currently emits 8-bit RGBA. Expand each channel to
        // the 16-bit working plane without claiming extra source precision.
        const size_t o16 = i * 8u;
        (*rgba16le)[o16 + 0] = r;
        (*rgba16le)[o16 + 1] = r;
        (*rgba16le)[o16 + 2] = g;
        (*rgba16le)[o16 + 3] = g;
        (*rgba16le)[o16 + 4] = b;
        (*rgba16le)[o16 + 5] = b;
        (*rgba16le)[o16 + 6] = a;
        (*rgba16le)[o16 + 7] = a;
    }

    document::ImageObject bitmap;
    bitmap.id = document::generate_object_id();
    bitmap.name = (language_ == UILanguage::Vietnamese) ? "Ảnh Bitmap đã chuyển đổi" : "Converted Bitmap";
    bitmap.bounds_rect = united_bounds;
    bitmap.width_px = width_px;
    bitmap.height_px = height_px;
    bitmap.shared_pixels_rgba = std::move(packed_rgba);
    bitmap.shared_pixels_rgba16le = std::move(rgba16le);
    bitmap.source.media_type = "image/png";
    const double output_dpi_x = static_cast<double>(width_px) * 25.4 / united_bounds.width;
    const double output_dpi_y = static_cast<double>(height_px) * 25.4 / united_bounds.height;
    bitmap.source.dpi_x = output_dpi_x;
    bitmap.source.dpi_y = output_dpi_y;
    bitmap.source.dpi_from_source = false;
    bitmap.source.source_bits_per_pixel = 32;
    bitmap.source.preview_pixel_color_basis = document::RasterPixelColorBasis::AssumedSRgb;

    const document::ObjectId new_id = bitmap.id;
    auto cmd = std::make_unique<ConvertSelectionToBitmapCommand>(
        selected, std::move(bitmap), workflow().active_layer_index());
    const auto res = root().document().execute_command(std::move(cmd));
    if (!res.success) return false;

    if (const auto updated_snap = root().document().current_snapshot()) {
        root().vector_controller().set_selection({new_id}, *updated_snap);
    }
    focus_raster_adjustment_in_dock(1);
    const auto output_dpi = static_cast<uint32_t>(std::lround(
        std::min(output_dpi_x, output_dpi_y)));
    const std::string resolution_note = (output_dpi + 1u < effective_dpi)
        ? (language_ == UILanguage::Vietnamese
            ? " · " + std::to_string(output_dpi) + " DPI do giới hạn 2048 px"
            : " · " + std::to_string(output_dpi) + " DPI at the 2048 px limit")
        : (language_ == UILanguage::Vietnamese
            ? " · " + std::to_string(output_dpi) + " DPI"
            : " · " + std::to_string(output_dpi) + " DPI");
    status_msg_ = (language_ == UILanguage::Vietnamese)
        ? ("Đã chuyển đối tượng thành ảnh Bitmap (" + std::to_string(width_px) + "×" + std::to_string(height_px) + " px)" + resolution_note + " · Ctrl+Z hoàn tác")
        : ("Converted selection to Bitmap (" + std::to_string(width_px) + "×" + std::to_string(height_px) + " px)" + resolution_note + " · Ctrl+Z to undo");
#ifdef _WIN32
    canvas_cache_valid_ = false;
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
    return true;
}

} // namespace pikdraw::desktop
