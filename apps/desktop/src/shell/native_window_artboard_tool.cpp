#include "pikdraw/desktop/shell/native_window.hpp"
#include "../tools/artboard_tool_controller.hpp"
#include "../tools/drawing_tool_controller.hpp"
#include "native_window_length_input.hpp"
#include "native_window_menus.hpp"
#include "pikdraw/document/commands/artboard_commands.hpp"
#include "pikdraw/vector/gestures/selection_resize.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windowsx.h>
#endif

namespace pikdraw::desktop {

#ifdef _WIN32
namespace {
using vector::precision::snap::SnapOption;
using vector::precision::snap::SnapGroup;

RECT artboard_screen_rect(const rendering::Camera2D& camera,
                          const geometry::Rect2D& bounds,
                          const RECT& area) {
    const auto upper_left = camera.world_to_screen({bounds.x, bounds.y + bounds.height});
    const auto lower_right = camera.world_to_screen({bounds.x + bounds.width, bounds.y});
    return {area.left + static_cast<LONG>(std::lround(upper_left.x)),
            area.top + static_cast<LONG>(std::lround(upper_left.y)),
            area.left + static_cast<LONG>(std::lround(lower_right.x)),
            area.top + static_cast<LONG>(std::lround(lower_right.y))};
}

std::array<geometry::Point2D, 8> artboard_handles(const geometry::Rect2D& bounds) {
    return {{{bounds.x, bounds.y + bounds.height},
             {bounds.x + bounds.width * 0.5, bounds.y + bounds.height},
             {bounds.x + bounds.width, bounds.y + bounds.height},
             {bounds.x + bounds.width, bounds.y + bounds.height * 0.5},
             {bounds.x + bounds.width, bounds.y},
             {bounds.x + bounds.width * 0.5, bounds.y},
             {bounds.x, bounds.y},
             {bounds.x, bounds.y + bounds.height * 0.5}}};
}

bool artboard_contains(const document::Artboard& board, geometry::Point2D point) {
    return point.x >= board.x_mm && point.x <= board.x_mm + board.width_mm &&
        point.y >= board.y_mm && point.y <= board.y_mm + board.height_mm;
}

void draw_panel_button(HDC dc, RECT rect, const wchar_t* label, bool primary,
                       bool dark, COLORREF accent) {
    const COLORREF background = primary ? (dark ? RGB(22, 45, 62) : RGB(236, 253, 245))
                                        : (dark ? RGB(18, 28, 42) : RGB(241, 245, 249));
    const COLORREF border = primary ? accent : (dark ? RGB(45, 58, 73) : RGB(203, 213, 225));
    const auto brush = CreateSolidBrush(background);
    const auto pen = CreatePen(PS_SOLID, 1, border);
    const auto old_brush = SelectObject(dc, brush);
    const auto old_pen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, 5, 5);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(brush);
    DeleteObject(pen);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, primary ? accent : (dark ? RGB(226, 232, 240) : RGB(30, 41, 59)));
    DrawTextW(dc, label, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE |
              DT_END_ELLIPSIS | DT_NOPREFIX);
}

struct SnapCandidateLabel { SnapOption option; const wchar_t* en; const wchar_t* vi; };
constexpr SnapCandidateLabel kSnapCandidates[] = {
    {SnapOption::Endpoint, L"Endpoint", L"Đầu mút"},
    {SnapOption::SourceNode, L"Source node", L"Node nguồn"},
    {SnapOption::LineMidpoint, L"Midpoint", L"Trung điểm"},
    {SnapOption::AnalyticCenter, L"Center", L"Tâm"},
    {SnapOption::Quadrant, L"Quadrant", L"Góc phần tư"},
    {SnapOption::Nearest, L"Nearest", L"Gần nhất"},
    {SnapOption::BezierTMidpoint, L"Curve midpoint", L"Giữa đường cong"},
    {SnapOption::HalfCurveLength, L"Half curve length", L"Nửa chiều dài cong"},
    {SnapOption::Intersection, L"Intersection", L"Giao điểm"},
    {SnapOption::Perpendicular, L"Perpendicular", L"Vuông góc"},
    {SnapOption::Tangent, L"Tangent", L"Tiếp tuyến"},
    {SnapOption::ReferenceTracking, L"Reference tracking", L"Theo tham chiếu"}
};

std::vector<SnapCandidateLabel> supported_snap_candidates() {
    std::vector<SnapCandidateLabel> result;
    for (const auto& item : kSnapCandidates)
        if (vector::precision::snap::is_supported_snap_candidate(item.option))
            result.push_back(item);
    return result;
}

} // namespace

bool NativeWindow::handle_artboard_pointer_down(int x, int y, unsigned) {
    if (!is_canvas_point(x, y)) return false;
    const auto snapshot = root().document().current_snapshot();
    if (!snapshot) return false;
    if (!artboard_tool_) {
        try { artboard_tool_ = std::make_unique<tools::ArtboardToolController>(); }
        catch (...) { status_msg_ = "Could not start Artboard Tool"; return true; }
    }
    const auto& boards = snapshot->active_page().artboards;
    const auto world = canvas_to_world(x, y);
    artboard_pointer_start_world_ = world;
    artboard_resize_handle_ = -1;
    const auto& camera = root().renderer().camera();
    const auto area = canvas_rect(width_, height_);
    const document::Artboard* hit = nullptr;
    for (auto item = boards.rbegin(); item != boards.rend(); ++item) {
        if (artboard_contains(*item, world)) { hit = &*item; break; }
    }
    bool began = false;
    if (hit) {
        (void)set_active_artboard(hit->id);
        const geometry::Rect2D bounds{hit->x_mm, hit->y_mm, hit->width_mm, hit->height_mm};
        int handle = -1;
        const auto handles = artboard_handles(bounds);
        for (int index = 0; index < static_cast<int>(handles.size()); ++index) {
            const auto point = camera.world_to_screen(handles[static_cast<size_t>(index)]);
            const double dx = x - (area.left + point.x);
            const double dy = y - (area.top + point.y);
            if (std::hypot(dx, dy) <= 8.0) { handle = index; break; }
        }
        began = handle >= 0
            ? artboard_tool_->begin_resize(*hit, world, handle)
            : artboard_tool_->begin_move(*hit, world);
        artboard_resize_handle_ = handle;
        status_msg_ = language_ == UILanguage::Vietnamese
            ? (handle >= 0 ? "Đang đổi kích thước Artboard · thả chuột để lưu · Esc hủy"
                           : "Đang di chuyển Artboard · thả chuột để lưu · Esc hủy")
            : (handle >= 0 ? "Resizing Artboard · release to apply · Esc cancels"
                           : "Moving Artboard · release to apply · Esc cancels");
    } else {
        const std::string name = "Artboard " + std::to_string(boards.size() + 1);
        began = artboard_tool_->begin_create(snapshot->active_page_id(), world, name);
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Kéo để tạo Artboard mới · thả chuột để lưu · Esc hủy"
            : "Drag to create an Artboard · release to apply · Esc cancels";
    }
    if (!began) {
        if (GetCapture() == hwnd_) ReleaseCapture();
        return true;
    }
    artboard_pointer_start_x_ = x;
    artboard_pointer_start_y_ = y;
    artboard_pointer_revision_ = snapshot->revision();
    artboard_tool_->update(world);
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

void NativeWindow::update_artboard_snap_preview(const document::DocumentSnapshot& snapshot,
                                               geometry::Point2D pointer_world,
                                               unsigned) {
    using namespace vector::precision::snap;
    if (!artboard_tool_ || !artboard_tool_->active() || !drawing_tool_) return;
    const auto config = drawing_tool_->snap_config_snapshot();
    const auto kind = artboard_tool_->kind();
    const auto page_id = snapshot.active_page_id();
    const bool target_policy_group = config.groups[static_cast<size_t>(SnapGroup::TargetPolicy)];
    const bool current_layer_only = target_policy_group &&
        config.options[static_cast<size_t>(SnapOption::CurrentLayerOnly)];
    const bool locked_as_reference = target_policy_group &&
        config.options[static_cast<size_t>(SnapOption::LockedAsReference)];
    const bool hidden_as_reference = target_policy_group &&
        config.options[static_cast<size_t>(SnapOption::HiddenLayers)];
    const int current_layer = active_layer_index();
    const auto isolation_owner = current_isolated_group_id().value_or(document::kInvalidObjectId);
    if (smart_guide_reference_revision_ != snapshot.revision() ||
        smart_guide_reference_page_ != page_id ||
        smart_guide_reference_artboard_mode_ != artboard_overview_enabled() ||
        !smart_guide_reference_selection_roots_.empty() ||
        smart_guide_reference_isolation_ != isolation_owner ||
        smart_guide_reference_active_layer_ != current_layer ||
        smart_guide_reference_locked_ != locked_as_reference ||
        smart_guide_reference_current_layer_only_ != current_layer_only ||
        smart_guide_reference_hidden_ != hidden_as_reference) {
        smart_guide_references_.clear();
        for (const auto& [id, object] : snapshot.objects().all()) {
            (void)object;
            const auto* entry = snapshot.hierarchy().find(id);
            if (!entry || entry->parent != document::kInvalidObjectId || !entry->bounds ||
                (!entry->visible && !hidden_as_reference) || (entry->locked && !locked_as_reference) ||
                (current_layer_only && entry->layer != static_cast<size_t>(current_layer)) ||
                (isolation_owner != document::kInvalidObjectId &&
                 !snapshot.hierarchy().contains(isolation_owner, id)))
                continue;
            smart_guide_references_.push_back({id, *entry->bounds});
        }
        smart_guide_reference_revision_ = snapshot.revision();
        smart_guide_reference_page_ = page_id;
        smart_guide_reference_artboard_mode_ = artboard_overview_enabled();
        smart_guide_reference_selection_roots_.clear();
        smart_guide_reference_isolation_ = isolation_owner;
        smart_guide_reference_active_layer_ = current_layer;
        smart_guide_reference_locked_ = locked_as_reference;
        smart_guide_reference_current_layer_only_ = current_layer_only;
        smart_guide_reference_hidden_ = hidden_as_reference;
    }

    LayoutSnapRequest request;
    const auto page_snap_enabled = config.groups[static_cast<size_t>(SnapGroup::PageGridGuidesConstruction)] &&
        (config.options[static_cast<size_t>(SnapOption::PageEdge)] ||
         config.options[static_cast<size_t>(SnapOption::PageCenter)]);
    if (kind == tools::ArtboardGestureKind::Create) {
        constexpr double extent = 1e-6;
        request.moving_bounds = {pointer_world.x - extent, pointer_world.y - extent,
                                 extent * 2.0, extent * 2.0};
        request.raw_delta = {};
    } else {
        const auto board = std::find_if(snapshot.active_page().artboards.begin(),
            snapshot.active_page().artboards.end(), [&](const auto& item) {
                return item.id == artboard_tool_->artboard_id();
            });
        if (board == snapshot.active_page().artboards.end()) return;
        request.moving_bounds = {board->x_mm, board->y_mm, board->width_mm, board->height_mm};
        request.raw_delta = {pointer_world.x - artboard_pointer_start_world_.x,
                             pointer_world.y - artboard_pointer_start_world_.y};
        if (kind == tools::ArtboardGestureKind::Resize && artboard_resize_handle_ >= 0) {
            constexpr int horizontal[] = {-1, 0, 1, 1, 1, 0, -1, -1};
            constexpr int vertical[] = {1, 1, 1, 0, -1, -1, -1, 0};
            const int hx = horizontal[artboard_resize_handle_];
            const int hy = vertical[artboard_resize_handle_];
            request.moving_feature_mask_x = hx == 0 ? 0 : static_cast<uint8_t>(1u << (hx < 0 ? 0 : 2));
            request.moving_feature_mask_y = hy == 0 ? 0 : static_cast<uint8_t>(1u << (hy < 0 ? 0 : 2));
            request.spacing_enabled = false;
        }
    }
    request.references = smart_guide_references_;
    if (page_snap_enabled) {
        if (artboard_overview_enabled()) {
            for (const auto& board : snapshot.active_page().artboards) {
                if (board.id == artboard_tool_->artboard_id() ||
                    !board.is_valid_for(snapshot.active_page_id())) continue;
                request.references.push_back({board.id,
                    {board.x_mm, board.y_mm, board.width_mm, board.height_mm},
                    LayoutSnapReferenceKind::Artboard});
            }
        } else {
            const auto& page = snapshot.active_page().layout;
            request.references.push_back({page_id,
                {0.0, 0.0, page.width_mm, page.height_mm}, LayoutSnapReferenceKind::Page});
        }
    }
    const bool guides_enabled = show_guidelines_ &&
        config.groups[static_cast<size_t>(SnapGroup::PageGridGuidesConstruction)] &&
        config.options[static_cast<size_t>(SnapOption::Guide)];
    if (guides_enabled) {
        for (size_t index = 0; index < guidelines_.size(); ++index) {
            const auto& guide = guidelines_[index];
            request.guidelines.push_back({guide.id,
                guide.orientation == GuidelineOrientation::Horizontal,
                guide.position_mm, -1'000'000.0, 1'000'000.0});
        }
    }
    request.previous_acquisition = smart_guide_acquisition_;
    request.acquire_radius_dip = config.acquire_radius_dip;
    request.release_radius_dip = config.release_radius_dip;
    request.objects_enabled = config.smart_guides_enabled &&
        config.groups[static_cast<size_t>(SnapGroup::ObjectPoints)];
    request.page_enabled = page_snap_enabled;
    request.guidelines_enabled = guides_enabled;
    request.spacing_enabled = kind == tools::ArtboardGestureKind::Move &&
        config.smart_spacing_enabled &&
        config.groups[static_cast<size_t>(SnapGroup::AutomaticRelations)];
    const double dpi_scale = hwnd_ ? std::max(1.0, static_cast<double>(GetDpiForWindow(hwnd_)) / 96.0) : 1.0;
    const double dip_per_mm = std::max(0.01, root().renderer().camera().zoom()) / dpi_scale;
    request.dip_per_mm_x = dip_per_mm;
    request.dip_per_mm_y = dip_per_mm;

    if (config.master_enabled) smart_guide_solution_ = solve_layout_snap(request);
    else {
        smart_guide_solution_ = {};
        smart_guide_solution_.accepted_delta = kind == tools::ArtboardGestureKind::Create
            ? geometry::Point2D{} : request.raw_delta;
    }
    smart_guide_acquisition_ = smart_guide_solution_.acquisition;
    if (kind == tools::ArtboardGestureKind::Create) {
        pointer_world.x += smart_guide_solution_.accepted_delta.x;
        pointer_world.y += smart_guide_solution_.accepted_delta.y;
    } else {
        pointer_world = {artboard_pointer_start_world_.x + smart_guide_solution_.accepted_delta.x,
                         artboard_pointer_start_world_.y + smart_guide_solution_.accepted_delta.y};
    }
    artboard_tool_->update(pointer_world);
    drag_preview_bounds_ = artboard_tool_->preview_bounds();
    smart_guide_moving_bounds_ = drag_preview_bounds_;
    if (smart_guide_solution_.snapped && !smart_guide_solution_.guides.empty()) {
        const auto& guide = smart_guide_solution_.guides.front();
        status_msg_ = guide.key.relation == LayoutSnapRelation::EqualSpacing
            ? (language_ == UILanguage::Vietnamese ? "Artboard · khoảng cách bằng nhau" : "Artboard · equal spacing")
            : (language_ == UILanguage::Vietnamese ? "Artboard · Smart Guide đã bắt điểm" : "Artboard · Smart Guide snapped");
    }
}

bool NativeWindow::handle_artboard_pointer_move(int x, int y, unsigned modifiers) {
    if (!artboard_tool_ || !artboard_tool_->active()) return false;
    if (root().document().revision() != artboard_pointer_revision_ ||
        root().document().active_page_id() != artboard_tool_->page_id()) {
        artboard_tool_->cancel();
        drag_preview_bounds_.reset();
        smart_guide_solution_ = {};
        smart_guide_acquisition_ = {};
        smart_guide_moving_bounds_.reset();
        if (GetCapture() == hwnd_) ReleaseCapture();
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Tài liệu hoặc trang đã đổi · thao tác Artboard bị hủy"
            : "Document or page changed · Artboard gesture cancelled";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    update_artboard_snap_preview(*root().document().current_snapshot(), canvas_to_world(x, y), modifiers);
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

bool NativeWindow::handle_artboard_pointer_up(int x, int y, unsigned modifiers) {
    if (!artboard_tool_ || !artboard_tool_->active()) return false;
    (void)handle_artboard_pointer_move(x, y, modifiers);
    if (!artboard_tool_->active()) return true;
    const auto kind = artboard_tool_->kind();
    if (kind == tools::ArtboardGestureKind::Create &&
        std::hypot(x - artboard_pointer_start_x_, y - artboard_pointer_start_y_) < 3.0) {
        artboard_tool_->cancel();
        drag_preview_bounds_.reset();
        smart_guide_solution_ = {};
        smart_guide_acquisition_ = {};
        smart_guide_moving_bounds_.reset();
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Kéo trên vùng trống để tạo Artboard · chưa có thay đổi"
            : "Drag on empty canvas to create an Artboard · no change made";
        if (GetCapture() == hwnd_) ReleaseCapture();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    const auto edit = artboard_tool_->finish(1.0);
    drag_preview_bounds_.reset();
    smart_guide_solution_ = {};
    smart_guide_acquisition_ = {};
    smart_guide_moving_bounds_.reset();
    if (GetCapture() == hwnd_) ReleaseCapture();
    if (!edit) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Artboard tối thiểu 1 × 1 mm · thao tác chưa áp dụng"
            : "Artboard must be at least 1 × 1 mm · no change applied";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    document::CommandResult result;
    if (edit->kind == tools::ArtboardGestureKind::Create) {
        auto command = std::make_unique<document::AddArtboardCommand>(
            edit->page_id, edit->name, edit->bounds);
        const auto id = command->artboard_id();
        result = root().document().execute_command(std::move(command));
        if (result.success) (void)set_active_artboard(id);
    } else {
        result = root().document().execute_command(
            std::make_unique<document::SetArtboardBoundsCommand>(
                edit->page_id, edit->artboard_id, edit->bounds));
    }
    status_msg_ = result.success
        ? (language_ == UILanguage::Vietnamese
            ? (edit->kind == tools::ArtboardGestureKind::Create
                ? "Đã tạo Artboard · artwork trang vẫn dùng chung · Ctrl+Z hoàn tác"
                : "Đã cập nhật Artboard · Ctrl+Z hoàn tác")
            : (edit->kind == tools::ArtboardGestureKind::Create
                ? "Artboard created · page artwork remains shared · Ctrl+Z undoes"
                : "Artboard updated · Ctrl+Z undoes"))
        : result.error_message;
    if (artboard_overview_enabled() && result.success) fit_artboards_to_canvas();
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

void NativeWindow::paint_artboard_canvas_overlay(HDC dc, const RECT& area) {
    if (active_tool_ != 14) return;
    const auto snapshot = root().document().current_snapshot();
    if (!snapshot) return;
    const auto& camera = root().renderer().camera();
    HPEN board_pen = CreatePen(PS_DASH, 1, theme_ == ThemeMode::CyberDark
        ? RGB(0, 242, 195) : RGB(0, 125, 105));
    const auto old_pen = SelectObject(dc, board_pen);
    const auto old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    for (const auto& board : snapshot->active_page().artboards) {
        const RECT rect = artboard_screen_rect(camera,
            {board.x_mm, board.y_mm, board.width_mm, board.height_mm}, area);
        Rectangle(dc, rect.left, rect.top, rect.right + 1, rect.bottom + 1);
        if (board.id != active_tab().active_artboard_id) continue;
        const geometry::Rect2D bounds{board.x_mm, board.y_mm, board.width_mm, board.height_mm};
        for (const auto point : artboard_handles(bounds)) {
            const auto screen = camera.world_to_screen(point);
            RECT handle{area.left + static_cast<LONG>(std::lround(screen.x)) - 4,
                        area.top + static_cast<LONG>(std::lround(screen.y)) - 4,
                        area.left + static_cast<LONG>(std::lround(screen.x)) + 4,
                        area.top + static_cast<LONG>(std::lround(screen.y)) + 4};
            const auto fill = CreateSolidBrush(theme_ == ThemeMode::CyberDark
                ? RGB(12, 19, 29) : RGB(255, 255, 255));
            FillRect(dc, &handle, fill);
            DeleteObject(fill);
            Rectangle(dc, handle.left, handle.top, handle.right, handle.bottom);
        }
    }
    if (artboard_tool_ && artboard_tool_->active()) {
        const auto preview = artboard_screen_rect(camera, artboard_tool_->preview_bounds(), area);
        Rectangle(dc, preview.left, preview.top, preview.right + 1, preview.bottom + 1);
    }
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(board_pen);
}

void NativeWindow::paint_artboard_properties_panel(HDC dc, int window_width,
                                                    int inspector_x, int content_y) {
    const auto snapshot = root().document().current_snapshot();
    const document::Artboard* board = nullptr;
    if (snapshot) {
        const auto& boards = snapshot->active_page().artboards;
        const auto found = std::find_if(boards.begin(), boards.end(), [&](const auto& item) {
            return item.id == active_tab().active_artboard_id;
        });
        if (found != boards.end()) board = &*found;
    }
    const bool dark = theme_ == ThemeMode::CyberDark;
    const COLORREF text = dark ? RGB(241, 245, 249) : RGB(15, 23, 42);
    const COLORREF muted = dark ? RGB(148, 163, 184) : RGB(100, 116, 139);
    const COLORREF accent = dark ? RGB(0, 242, 195) : RGB(0, 135, 106);
    HFONT font = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT bold = CreateFontW(12, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    const auto old_font = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, accent);
    RECT title{inspector_x + 14, content_y + 2, window_width - 14, content_y + 24};
    DrawTextW(dc, language_ == UILanguage::Vietnamese ? L"THUỘC TÍNH · ARTBOARD" : L"ARTBOARD PROPERTIES",
              -1, &title, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    const auto draw_field = [&](int y, const wchar_t* label, NumericFieldId field, double value) {
        RECT label_rect{inspector_x + 14, y, inspector_x + 74, y + 26};
        SetTextColor(dc, muted);
        DrawTextW(dc, label, -1, &label_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        RECT input{inspector_x + 80, y, window_width - 14, y + 26};
        const bool focused = active_num_field_ == field;
        const auto brush = CreateSolidBrush(focused ? (dark ? RGB(22, 45, 62) : RGB(236, 253, 245))
                                                    : (dark ? RGB(18, 28, 42) : RGB(241, 245, 249)));
        const auto pen = CreatePen(PS_SOLID, focused ? 2 : 1, focused ? accent
            : (dark ? RGB(45, 58, 73) : RGB(203, 213, 225)));
        const auto old_brush = SelectObject(dc, brush);
        const auto old_pen = SelectObject(dc, pen);
        RoundRect(dc, input.left, input.top, input.right, input.bottom, 5, 5);
        SelectObject(dc, old_brush); SelectObject(dc, old_pen);
        DeleteObject(brush); DeleteObject(pen);
        RECT value_rect{input.left + 7, input.top, input.right - 7, input.bottom};
        const std::wstring shown = focused ? utf8_to_wide(num_input_buffer_) + L"|"
            : board ? utf8_to_wide(format_mm_display_utf8(value, display_length_unit_, 5)) : L"—";
        SetTextColor(dc, focused ? accent : board ? text : muted);
        SelectObject(dc, font);
        DrawTextW(dc, shown.c_str(), -1, &value_rect,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    };
    const int left = inspector_x + 14;
    const int right = window_width - 14;
    const int y = content_y + 30;
    RECT name_label{left, y, right, y + 18};
    SetTextColor(dc, muted);
    DrawTextW(dc, language_ == UILanguage::Vietnamese ? L"Tên Artboard" : L"Artboard name",
              -1, &name_label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    RECT name_input{left, y + 20, right, y + 48};
    const bool name_focused = artboard_name_editing_;
    const auto name_brush = CreateSolidBrush(name_focused ? (dark ? RGB(22, 45, 62) : RGB(236, 253, 245))
                                                          : (dark ? RGB(18, 28, 42) : RGB(241, 245, 249)));
    const auto name_pen = CreatePen(PS_SOLID, name_focused ? 2 : 1, name_focused ? accent
        : (dark ? RGB(45, 58, 73) : RGB(203, 213, 225)));
    const auto old_name_brush = SelectObject(dc, name_brush);
    const auto old_name_pen = SelectObject(dc, name_pen);
    RoundRect(dc, name_input.left, name_input.top, name_input.right, name_input.bottom, 5, 5);
    SelectObject(dc, old_name_brush); SelectObject(dc, old_name_pen);
    DeleteObject(name_brush); DeleteObject(name_pen);
    const std::wstring name = name_focused ? artboard_name_buffer_ + L"|"
        : board ? utf8_to_wide(board->name) : (language_ == UILanguage::Vietnamese ? L"Chưa có Artboard" : L"No artboard");
    RECT name_text{name_input.left + 8, name_input.top, name_input.right - 8, name_input.bottom};
    SetTextColor(dc, name_focused ? accent : board ? text : muted);
    DrawTextW(dc, name.c_str(), -1, &name_text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    draw_field(y + 56, L"X", NumericFieldId::ArtboardX, board ? board->x_mm : 0.0);
    draw_field(y + 88, L"Y", NumericFieldId::ArtboardY, board ? board->y_mm : 0.0);
    draw_field(y + 120, L"W", NumericFieldId::ArtboardWidth, board ? board->width_mm : 0.0);
    draw_field(y + 152, L"H", NumericFieldId::ArtboardHeight, board ? board->height_mm : 0.0);
    const int gap = 6;
    const int half = (right - left - gap) / 2;
    SelectObject(dc, bold);
    draw_panel_button(dc, {left, y + 188, left + half, y + 218},
        language_ == UILanguage::Vietnamese ? L"+ Thêm" : L"+ Add", true, dark, accent);
    draw_panel_button(dc, {left + half + gap, y + 188, right, y + 218},
        language_ == UILanguage::Vietnamese ? L"Nhân đôi" : L"Duplicate", false, dark, accent);
    const int third = (right - left - 2 * gap) / 3;
    draw_panel_button(dc, {left, y + 224, left + third, y + 254},
        language_ == UILanguage::Vietnamese ? L"Xóa" : L"Delete", false, dark, accent);
    draw_panel_button(dc, {left + third + gap, y + 224, left + 2 * third + gap, y + 254}, L"↑", false, dark, accent);
    draw_panel_button(dc, {left + 2 * (third + gap), y + 224, right, y + 254}, L"↓", false, dark, accent);
    draw_panel_button(dc, {left, y + 260, right, y + 290},
        language_ == UILanguage::Vietnamese ? L"Xuất Artboard đang chọn…" : L"Export Active Artboard…",
        false, dark, accent);
    SetTextColor(dc, muted);
    RECT note{left, y + 298, right, y + 354};
    DrawTextW(dc, language_ == UILanguage::Vietnamese
        ? L"Artboard xác định khung xuất. Artwork vẫn dùng chung trên trang; các thao tác hình học có một bước Undo."
        : L"Artboards define export bounds. Page artwork remains shared; each geometry edit is one Undo step.",
        -1, &note, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, old_font);
    DeleteObject(font); DeleteObject(bold);
}

bool NativeWindow::handle_artboard_property_click(int x, int y) {
    int window_width = width_;
    if (hwnd_) { RECT rc{}; if (GetClientRect(hwnd_, &rc)) window_width = logical_pixel(rc.right - rc.left); }
    const auto snapshot = root().document().current_snapshot();
    if (!snapshot) return false;
    const auto& boards = snapshot->active_page().artboards;
    const auto board = std::find_if(boards.begin(), boards.end(), [&](const auto& item) {
        return item.id == active_tab().active_artboard_id;
    });
    if (y >= 66 && y <= 102) {
        if (x >= 86 && x <= 190) start_numeric_input(NumericFieldId::ArtboardX);
        else if (x >= 196 && x <= 300) start_numeric_input(NumericFieldId::ArtboardY);
        else if (x >= 306 && x <= 410) start_numeric_input(NumericFieldId::ArtboardWidth);
        else if (x >= 416 && x <= 520) start_numeric_input(NumericFieldId::ArtboardHeight);
        else if (x >= 530 && x <= 650) { active_dock_tab_ = 0; show_snap_properties_ = false; }
        else return true;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    if (x < window_width - 280 || y < 138) return false;
    const int left = window_width - 266;
    const int right = window_width - 14;
    const int y0 = 138 + 30;
    if (y >= y0 + 20 && y < y0 + 48 && x >= left && x <= right && board != boards.end()) {
        if (!artboard_name_editing_) {
            artboard_name_editing_ = true;
            artboard_name_target_ = board->id;
            artboard_name_buffer_ = utf8_to_wide(board->name);
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Sửa tên Artboard · Enter lưu · Esc hủy"
                : "Edit Artboard name · Enter applies · Esc cancels";
        }
    } else if (board != boards.end() && y >= y0 + 56 && y < y0 + 82) start_numeric_input(NumericFieldId::ArtboardX);
    else if (board != boards.end() && y >= y0 + 88 && y < y0 + 114) start_numeric_input(NumericFieldId::ArtboardY);
    else if (board != boards.end() && y >= y0 + 120 && y < y0 + 146) start_numeric_input(NumericFieldId::ArtboardWidth);
    else if (board != boards.end() && y >= y0 + 152 && y < y0 + 178) start_numeric_input(NumericFieldId::ArtboardHeight);
    else if (y >= y0 + 188 && y < y0 + 218) {
        execute_menu_action(static_cast<int>(x < left + (right-left-6)/2 ? MenuActionId::LayoutArtboardAdd : MenuActionId::LayoutArtboardDuplicate));
    } else if (y >= y0 + 224 && y < y0 + 254) {
        const int third = (right - left - 12) / 3;
        const auto index = board == boards.end() ? size_t{0} : static_cast<size_t>(std::distance(boards.begin(), board));
        if (x < left + third) execute_menu_action(static_cast<int>(MenuActionId::LayoutArtboardDelete));
        else if (board != boards.end() && x < left + 2 * third + 6 && index > 0) {
            const auto result = root().document().execute_command(
                std::make_unique<document::ReorderArtboardCommand>(snapshot->active_page_id(), board->id, index - 1));
            status_msg_ = result.success ? (language_ == UILanguage::Vietnamese ? "Đã đưa Artboard lên trước · Ctrl+Z hoàn tác" : "Artboard moved earlier · Ctrl+Z undoes") : result.error_message;
        } else if (board != boards.end() && index + 1 < boards.size()) {
            const auto result = root().document().execute_command(
                std::make_unique<document::ReorderArtboardCommand>(snapshot->active_page_id(), board->id, index + 1));
            status_msg_ = result.success ? (language_ == UILanguage::Vietnamese ? "Đã đưa Artboard xuống sau · Ctrl+Z hoàn tác" : "Artboard moved later · Ctrl+Z undoes") : result.error_message;
        }
    } else if (y >= y0 + 260 && y < y0 + 290) {
        execute_menu_action(static_cast<int>(MenuActionId::LayoutArtboardExport));
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

bool NativeWindow::handle_artboard_tool_key(WPARAM key) {
    if (!artboard_name_editing_) {
        if (key == VK_ESCAPE && artboard_tool_ && artboard_tool_->active()) {
            artboard_tool_->cancel();
            if (GetCapture() == hwnd_) ReleaseCapture();
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Đã hủy thao tác Artboard" : "Artboard gesture cancelled";
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        return false;
    }
    if (key == VK_ESCAPE) {
        artboard_name_editing_ = false;
        artboard_name_buffer_.clear();
        artboard_name_target_ = document::kInvalidArtboardId;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    if (key == VK_RETURN) {
        const auto snapshot = root().document().current_snapshot();
        const auto utf8_name = wide_to_utf8(artboard_name_buffer_);
        if (!snapshot || utf8_name.empty() || utf8_name.size() > 1024) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Tên Artboard không hợp lệ · cần 1–1024 byte UTF-8"
                : "Artboard name must contain 1–1024 UTF-8 bytes";
            return true;
        }
        const auto result = root().document().execute_command(
            std::make_unique<document::RenameArtboardCommand>(snapshot->active_page_id(), artboard_name_target_, utf8_name));
        if (result.success) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Đã đổi tên Artboard · Ctrl+Z hoàn tác" : "Artboard renamed · Ctrl+Z undoes";
            artboard_name_editing_ = false;
            artboard_name_buffer_.clear();
            artboard_name_target_ = document::kInvalidArtboardId;
        } else status_msg_ = result.error_message;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    return false;
}

bool NativeWindow::handle_artboard_tool_char(wchar_t ch) {
    if (!artboard_name_editing_) return false;
    if (ch == L'\b') {
        if (!artboard_name_buffer_.empty()) {
            if (artboard_name_buffer_.size() >= 2 &&
                artboard_name_buffer_.back() >= 0xDC00 && artboard_name_buffer_.back() <= 0xDFFF &&
                artboard_name_buffer_[artboard_name_buffer_.size() - 2] >= 0xD800 &&
                artboard_name_buffer_[artboard_name_buffer_.size() - 2] <= 0xDBFF)
                artboard_name_buffer_.resize(artboard_name_buffer_.size() - 2);
            else artboard_name_buffer_.pop_back();
        }
    } else if (ch >= L' ' && ch != 0x7F && artboard_name_buffer_.size() < 256) {
        artboard_name_buffer_.push_back(ch);
    } else return true;
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

bool NativeWindow::commit_artboard_numeric_field(NumericFieldId field) {
    std::string normalized = num_input_buffer_;
    std::replace(normalized.begin(), normalized.end(), ',', '.');
    double value = 0.0;
    if (field == NumericFieldId::SnapAcquireRadius || field == NumericFieldId::SnapReleaseRadius) {
        try {
            size_t parsed = 0;
            value = std::stod(normalized, &parsed);
            if (parsed != normalized.size() || !std::isfinite(value)) throw std::out_of_range("snap radius");
        } catch (...) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Bán kính Snap phải từ 1 đến 64 DIP"
                : "Snap radius must be between 1 and 64 DIP";
            InvalidateRect(hwnd_, nullptr, FALSE);
            return false;
        }
        const auto config = drawing_tool_ ? drawing_tool_->snap_config_snapshot()
            : vector::precision::snap::SnapConfigSnapshot{};
        const double acquire = field == NumericFieldId::SnapAcquireRadius ? value : config.acquire_radius_dip;
        const double release = field == NumericFieldId::SnapReleaseRadius ? value : config.release_radius_dip;
        std::string error;
        if (!drawing_tool_ || !drawing_tool_->set_snap_radii(acquire, release, &error)) {
            status_msg_ = error.empty() ? (language_ == UILanguage::Vietnamese
                ? "Không thể áp dụng bán kính Snap" : "Could not apply Snap radius") : error;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return false;
        }
        active_num_field_ = NumericFieldId::None;
        num_input_buffer_.clear();
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã lưu bán kính bắt/nhả Snap · áp dụng ngay · cài đặt không thuộc Undo"
            : "Snap acquire/release radius saved and active · preferences are outside Undo";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    try {
        value = parse_length_input_mm(normalized, display_length_unit_);
        if (!std::isfinite(value)) throw std::out_of_range("artboard length");
    } catch (...) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Kích thước Artboard phải là số hữu hạn theo đơn vị trang; hậu tố rõ được phép"
            : "Enter a finite Artboard length in page units; explicit unit suffixes are allowed";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return false;
    }
    const auto snapshot = root().document().current_snapshot();
    if (!snapshot || active_tab().active_artboard_id == document::kInvalidArtboardId) return false;
    const auto& boards = snapshot->active_page().artboards;
    const auto board = std::find_if(boards.begin(), boards.end(), [&](const auto& item) {
        return item.id == active_tab().active_artboard_id;
    });
    if (board == boards.end()) return false;
    geometry::Rect2D bounds{board->x_mm, board->y_mm, board->width_mm, board->height_mm};
    if (field == NumericFieldId::ArtboardX) bounds.x = value;
    else if (field == NumericFieldId::ArtboardY) bounds.y = value;
    else if (field == NumericFieldId::ArtboardWidth) bounds.width = value;
    else if (field == NumericFieldId::ArtboardHeight) bounds.height = value;
    const document::Artboard candidate{board->id, board->owner_page_id, board->name,
        bounds.x, bounds.y, bounds.width, bounds.height, false};
    const bool within_canvas = bounds.x >= -geometry::kMaxCanvasDimensionMm &&
        bounds.y >= -geometry::kMaxCanvasDimensionMm &&
        bounds.width <= geometry::kMaxCanvasDimensionMm &&
        bounds.height <= geometry::kMaxCanvasDimensionMm &&
        bounds.right() <= geometry::kMaxCanvasDimensionMm &&
        bounds.bottom() <= geometry::kMaxCanvasDimensionMm;
    if (!candidate.is_valid_for(snapshot->active_page_id()) || bounds.width < 1.0 ||
        bounds.height < 1.0 || !within_canvas) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Artboard phải hữu hạn, rộng/cao tối thiểu 1 mm và trong giới hạn canvas"
            : "Artboard must be finite, at least 1 mm in each dimension, and within canvas limits";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return false;
    }
    const auto result = root().document().execute_command(
        std::make_unique<document::SetArtboardBoundsCommand>(
            snapshot->active_page_id(), board->id, bounds));
    if (!result.success) {
        status_msg_ = result.error_message;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return false;
    }
    active_num_field_ = NumericFieldId::None;
    num_input_buffer_.clear();
    status_msg_ = language_ == UILanguage::Vietnamese
        ? "Đã cập nhật Artboard · Ctrl+Z hoàn tác"
        : "Artboard updated · Ctrl+Z undoes";
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

bool NativeWindow::handle_snap_toolbar_click(int x, int y) {
    const int window_width = width_;
    const bool on_button = x >= window_width - 104 && x <= window_width - 8 && y >= 4 && y <= 35;
    if (on_button) {
        if (x < window_width - 36 && drawing_tool_) {
            const auto config = drawing_tool_->snap_config_snapshot();
            if (drawing_tool_->set_snap_master_enabled(!config.master_enabled))
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Đã bật/tắt bắt điểm tổng · cài đặt dùng chung cho công cụ"
                    : "Master snapping toggled · shared by drawing tools";
        } else {
            show_snap_to_popup_ = !show_snap_to_popup_;
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    if (!show_snap_to_popup_) return false;
    const int left = std::max(8, window_width - 264);
    const int right = std::min(window_width - 8, left + 256);
    const int top = 38;
    if (x < left || x > right || y < top || y >= top + 8 * 30) return false;
    const int row = (y - top) / 30;
    if (row == 7) {
        show_snap_to_popup_ = false;
        show_snap_properties_ = true;
        active_dock_tab_ = 0;
        show_style_inspector_ = false;
        show_align_panel_ = false;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    if (!drawing_tool_) return true;
    const auto config = drawing_tool_->snap_config_snapshot();
    bool changed = false;
    switch (row) {
    case 0: changed = drawing_tool_->set_snap_master_enabled(!config.master_enabled); break;
    case 1: changed = drawing_tool_->set_snap_group_enabled(SnapGroup::ObjectPoints,
        !config.groups[static_cast<size_t>(SnapGroup::ObjectPoints)]); break;
    case 2: changed = drawing_tool_->set_snap_option_enabled(SnapOption::Grid,
        !config.options[static_cast<size_t>(SnapOption::Grid)]); break;
    case 3: changed = drawing_tool_->set_snap_option_enabled(SnapOption::PageEdge,
        !config.options[static_cast<size_t>(SnapOption::PageEdge)]); break;
    case 4: changed = drawing_tool_->set_snap_option_enabled(SnapOption::Guide,
        !config.options[static_cast<size_t>(SnapOption::Guide)]); break;
    case 5: changed = drawing_tool_->set_smart_guides_enabled(!config.smart_guides_enabled); break;
    case 6: changed = drawing_tool_->set_smart_spacing_enabled(!config.smart_spacing_enabled); break;
    default: break;
    }
    if (changed) status_msg_ = language_ == UILanguage::Vietnamese
        ? "Snap cập nhật và lưu ngay · mở View > Snap Properties để tinh chỉnh chi tiết"
        : "Snap settings saved and active · open View > Snap Properties for advanced controls";
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

void NativeWindow::paint_snap_to_popup(HDC dc, int window_width, int) {
    const int left = std::max(8, window_width - 264);
    const int top = 38;
    const int width = std::min(256, window_width - left - 8);
    const bool dark = theme_ == ThemeMode::CyberDark;
    const COLORREF accent = dark ? RGB(0, 242, 195) : RGB(0, 135, 106);
    const auto config = drawing_tool_ ? drawing_tool_->snap_config_snapshot()
        : vector::precision::snap::SnapConfigSnapshot{};
    const auto background = CreateSolidBrush(dark ? RGB(12, 19, 29) : RGB(255, 255, 255));
    const auto border = CreatePen(PS_SOLID, 1, dark ? RGB(45, 58, 73) : RGB(203, 213, 225));
    const auto old_background = SelectObject(dc, background);
    const auto old_border = SelectObject(dc, border);
    RoundRect(dc, left, top, left + width, top + 8 * 30, 8, 8);
    SelectObject(dc, old_background); SelectObject(dc, old_border);
    DeleteObject(background); DeleteObject(border);
    constexpr SnapOption options[] = {SnapOption::Grid, SnapOption::PageEdge, SnapOption::Guide};
    const wchar_t* labels_vi[] = {L"Bắt điểm tổng", L"Đối tượng", L"Lưới", L"Trang / Artboard", L"Đường gióng", L"Smart Guides", L"Khoảng cách bằng nhau", L"Thuộc tính Snap…"};
    const wchar_t* labels_en[] = {L"Master snapping", L"Object points", L"Grid", L"Page / Artboard", L"Guidelines", L"Smart Guides", L"Equal spacing", L"Snap Properties…"};
    HFONT font = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    const auto old_font = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    for (int row = 0; row < 8; ++row) {
        RECT item{left + 9, top + row * 30, left + width - 8, top + (row + 1) * 30};
        SetTextColor(dc, row == 7 ? accent : dark ? RGB(226, 232, 240) : RGB(30, 41, 59));
        if (row == 7) {
            DrawTextW(dc, language_ == UILanguage::Vietnamese ? labels_vi[row] : labels_en[row], -1,
                      &item, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            continue;
        }
        RECT check{left + 12, item.top + 8, left + 25, item.top + 21};
        const auto check_pen = CreatePen(PS_SOLID, 1, dark ? RGB(100, 116, 139) : RGB(148, 163, 184));
        const bool checked = row == 0 ? config.master_enabled
            : row == 1 ? config.groups[static_cast<size_t>(SnapGroup::ObjectPoints)]
            : row == 2 ? config.options[static_cast<size_t>(options[0])]
            : row == 3 ? config.options[static_cast<size_t>(options[1])]
            : row == 4 ? config.options[static_cast<size_t>(options[2])]
            : row == 5 ? config.smart_guides_enabled : config.smart_spacing_enabled;
        const auto check_brush = CreateSolidBrush(checked ? accent
            : (dark ? RGB(18, 28, 42) : RGB(255, 255, 255)));
        const auto old_cp = SelectObject(dc, check_pen); const auto old_cb = SelectObject(dc, check_brush);
        Rectangle(dc, check.left, check.top, check.right, check.bottom);
        SelectObject(dc, old_cb); SelectObject(dc, old_cp); DeleteObject(check_pen); DeleteObject(check_brush);
        if (checked) {
            HPEN tick = CreatePen(PS_SOLID, 2, dark ? RGB(12, 19, 29) : RGB(255, 255, 255));
            const auto old_tick = SelectObject(dc, tick);
            MoveToEx(dc, check.left + 3, check.top + 6, nullptr); LineTo(dc, check.left + 5, check.top + 9); LineTo(dc, check.left + 10, check.top + 3);
            SelectObject(dc, old_tick); DeleteObject(tick);
        }
        RECT label{left + 34, item.top, left + width - 8, item.bottom};
        DrawTextW(dc, language_ == UILanguage::Vietnamese ? labels_vi[row] : labels_en[row], -1,
                  &label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    SelectObject(dc, old_font); DeleteObject(font);
}

void NativeWindow::paint_snap_properties_panel(HDC dc, int window_width,
                                                int inspector_x, int content_y) {
    const bool dark = theme_ == ThemeMode::CyberDark;
    const bool vi = language_ == UILanguage::Vietnamese;
    const COLORREF text = dark ? RGB(241, 245, 249) : RGB(15, 23, 42);
    const COLORREF muted = dark ? RGB(148, 163, 184) : RGB(100, 116, 139);
    const COLORREF accent = dark ? RGB(0, 242, 195) : RGB(0, 135, 106);
    const auto config = drawing_tool_ ? drawing_tool_->snap_config_snapshot()
        : vector::precision::snap::SnapConfigSnapshot{};
    HFONT font = CreateFontW(12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT bold = CreateFontW(12, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    const auto old_font = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, accent);
    RECT title{inspector_x + 14, content_y + 2, window_width - 14, content_y + 24};
    DrawTextW(dc, vi ? L"THUỘC TÍNH · SNAP" : L"SNAP & SMART GUIDES", -1, &title,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    const int left = inspector_x + 14, right = window_width - 14;
    const auto checkbox = [&](int x, int y, int width, const wchar_t* label, bool checked) {
        RECT box{x, y + 5, x + 12, y + 17};
        const auto brush = CreateSolidBrush(checked ? accent : (dark ? RGB(18, 28, 42) : RGB(255, 255, 255)));
        const auto pen = CreatePen(PS_SOLID, 1, checked ? accent : (dark ? RGB(100, 116, 139) : RGB(148, 163, 184)));
        const auto old_brush = SelectObject(dc, brush); const auto old_pen = SelectObject(dc, pen);
        Rectangle(dc, box.left, box.top, box.right, box.bottom);
        SelectObject(dc, old_brush); SelectObject(dc, old_pen); DeleteObject(brush); DeleteObject(pen);
        RECT label_rect{x + 18, y, x + width, y + 22};
        SetTextColor(dc, text);
        DrawTextW(dc, label, -1, &label_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    };
    const int y = content_y + 30;
    checkbox(left, y, right - left, vi ? L"Bật bắt điểm tổng" : L"Master snapping", config.master_enabled);
    checkbox(left, y + 25, 120, vi ? L"Điểm đối tượng" : L"Object points",
        config.groups[static_cast<size_t>(SnapGroup::ObjectPoints)]);
    checkbox(left + 128, y + 25, right - left - 128, vi ? L"Quan hệ tự động" : L"Auto relations",
        config.groups[static_cast<size_t>(SnapGroup::AutomaticRelations)]);
    checkbox(left, y + 50, 120, vi ? L"Trang / lưới" : L"Page / grid",
        config.groups[static_cast<size_t>(SnapGroup::PageGridGuidesConstruction)]);
    checkbox(left + 128, y + 50, right - left - 128, vi ? L"Phạm vi đích" : L"Target policy",
        config.groups[static_cast<size_t>(SnapGroup::TargetPolicy)]);
    checkbox(left, y + 75, 120, vi ? L"Smart Guides" : L"Smart Guides", config.smart_guides_enabled);
    checkbox(left + 128, y + 75, right - left - 128, vi ? L"Khoảng cách bằng nhau" : L"Equal spacing",
        config.smart_spacing_enabled);
    SetTextColor(dc, accent);
    RECT section{left, y + 104, right, y + 124};
    SelectObject(dc, bold);
    DrawTextW(dc, vi ? L"ĐIỂM BẮT THEO ĐỐI TƯỢNG" : L"OBJECT SNAP CANDIDATES", -1,
              &section, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, font);
    const auto candidates = supported_snap_candidates();
    const int option_top = y + 128;
    const int column_width = (right - left - 8) / 2;
    for (size_t index = 0; index < candidates.size(); ++index) {
        const int column = static_cast<int>(index % 2);
        const int row = static_cast<int>(index / 2);
        const auto& item = candidates[index];
        checkbox(left + column * (column_width + 8), option_top + row * 22,
                 column_width, vi ? item.vi : item.en,
                 config.options[static_cast<size_t>(item.option)]);
    }
    const int option_rows = static_cast<int>((candidates.size() + 1) / 2);
    const int radius_top = option_top + option_rows * 22 + 28;
    SetTextColor(dc, accent);
    RECT radius_title{left, radius_top - 20, right, radius_top};
    SelectObject(dc, bold);
    DrawTextW(dc, vi ? L"KHOẢNG BẮT / NHẢ (DIP)" : L"ACQUIRE / RELEASE DISTANCE (DIP)", -1,
              &radius_title, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    const auto draw_radius = [&](int row_y, const wchar_t* label, NumericFieldId field, double value) {
        RECT label_rect{left, row_y, left + 106, row_y + 26};
        SetTextColor(dc, muted); SelectObject(dc, font);
        DrawTextW(dc, label, -1, &label_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        RECT input{left + 108, row_y, right, row_y + 26};
        const bool focused = active_num_field_ == field;
        const auto brush = CreateSolidBrush(focused ? (dark ? RGB(22, 45, 62) : RGB(236, 253, 245))
                                                    : (dark ? RGB(18, 28, 42) : RGB(241, 245, 249)));
        const auto pen = CreatePen(PS_SOLID, focused ? 2 : 1, focused ? accent
            : (dark ? RGB(45, 58, 73) : RGB(203, 213, 225)));
        const auto old_brush = SelectObject(dc, brush); const auto old_pen = SelectObject(dc, pen);
        RoundRect(dc, input.left, input.top, input.right, input.bottom, 5, 5);
        SelectObject(dc, old_brush); SelectObject(dc, old_pen); DeleteObject(brush); DeleteObject(pen);
        const auto value_text = focused ? utf8_to_wide(num_input_buffer_) + L"|"
            : std::to_wstring(value) + L" DIP";
        RECT value_rect{input.left + 7, input.top, input.right - 7, input.bottom};
        SetTextColor(dc, focused ? accent : text);
        DrawTextW(dc, value_text.c_str(), -1, &value_rect,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    };
    draw_radius(radius_top + 4, vi ? L"Bắt tại" : L"Acquire", NumericFieldId::SnapAcquireRadius, config.acquire_radius_dip);
    draw_radius(radius_top + 34, vi ? L"Nhả tại" : L"Release", NumericFieldId::SnapReleaseRadius, config.release_radius_dip);
    const int policy_y = radius_top + 68;
    checkbox(left, policy_y, right - left, vi ? L"Chỉ bắt đối tượng trong lớp hiện hành" : L"Limit candidates to current layer",
        config.options[static_cast<size_t>(SnapOption::CurrentLayerOnly)]);
    checkbox(left, policy_y + 22, right - left,
        vi ? L"Cho phép đối tượng bị khóa làm mốc" : L"Use locked objects as references",
        config.options[static_cast<size_t>(SnapOption::LockedAsReference)]);
    checkbox(left, policy_y + 44, right - left,
        vi ? L"Bắt theo đường đang vẽ" : L"Snap to the path being drawn",
        config.options[static_cast<size_t>(SnapOption::SelfPath)]);
    checkbox(left, policy_y + 66, right - left,
        vi ? L"Cho phép bắt đối tượng trên lớp ẩn" : L"Include objects on hidden layers",
        config.options[static_cast<size_t>(SnapOption::HiddenLayers)]);
    RECT hint{left, policy_y + 90, right, policy_y + 132};
    SetTextColor(dc, muted);
    DrawTextW(dc, vi ? L"Các loại điểm chưa được thuật toán hỗ trợ không hiển thị như tùy chọn bật được. Bán kính tính theo DIP màn hình."
                      : L"Snap candidates not supported by the current engine are not shown as active controls. Radius uses screen DIP.",
              -1, &hint, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, old_font);
    DeleteObject(font); DeleteObject(bold);
}

bool NativeWindow::handle_snap_properties_click(int x, int y) {
    if (!drawing_tool_) return true;
    const int window_width = width_;
    const int left = window_width - 266, right = window_width - 14;
    const int y0 = 138 + 30;
    const auto config = drawing_tool_->snap_config_snapshot();
    const auto toggle_group = [&](SnapGroup group) {
        (void)drawing_tool_->set_snap_group_enabled(group,
            !config.groups[static_cast<size_t>(group)]);
    };
    if (y >= y0 && y < y0 + 22) (void)drawing_tool_->set_snap_master_enabled(!config.master_enabled);
    else if (y >= y0 + 25 && y < y0 + 47) {
        if (x < left + 124) toggle_group(SnapGroup::ObjectPoints);
        else toggle_group(SnapGroup::AutomaticRelations);
    } else if (y >= y0 + 50 && y < y0 + 72) {
        if (x < left + 124) toggle_group(SnapGroup::PageGridGuidesConstruction);
        else toggle_group(SnapGroup::TargetPolicy);
    } else if (y >= y0 + 75 && y < y0 + 97) {
        if (x < left + 124) (void)drawing_tool_->set_smart_guides_enabled(!config.smart_guides_enabled);
        else (void)drawing_tool_->set_smart_spacing_enabled(!config.smart_spacing_enabled);
    } else {
        const auto candidates = supported_snap_candidates();
        const int option_top = y0 + 128;
        const int option_rows = static_cast<int>((candidates.size() + 1) / 2);
        const int column_width = (right - left - 8) / 2;
        if (y >= option_top && y < option_top + option_rows * 22) {
            const int row = (y - option_top) / 22;
            const int column = x < left + column_width ? 0 : 1;
            const size_t index = static_cast<size_t>(row * 2 + column);
            if (index < candidates.size()) {
                const auto option = candidates[index].option;
                (void)drawing_tool_->set_snap_option_enabled(option,
                    !config.options[static_cast<size_t>(option)]);
            }
        } else {
            const int radius_top = option_top + option_rows * 22 + 28;
            if (y >= radius_top + 4 && y < radius_top + 30)
                start_numeric_input(NumericFieldId::SnapAcquireRadius);
            else if (y >= radius_top + 34 && y < radius_top + 60)
                start_numeric_input(NumericFieldId::SnapReleaseRadius);
            else if (y >= radius_top + 68 && y < radius_top + 90)
                (void)drawing_tool_->set_snap_option_enabled(SnapOption::CurrentLayerOnly,
                    !config.options[static_cast<size_t>(SnapOption::CurrentLayerOnly)]);
            else if (y >= radius_top + 90 && y < radius_top + 112)
                (void)drawing_tool_->set_snap_option_enabled(SnapOption::LockedAsReference,
                    !config.options[static_cast<size_t>(SnapOption::LockedAsReference)]);
            else if (y >= radius_top + 112 && y < radius_top + 134)
                (void)drawing_tool_->set_snap_option_enabled(SnapOption::SelfPath,
                    !config.options[static_cast<size_t>(SnapOption::SelfPath)]);
            else if (y >= radius_top + 134 && y < radius_top + 156)
                (void)drawing_tool_->set_snap_option_enabled(SnapOption::HiddenLayers,
                    !config.options[static_cast<size_t>(SnapOption::HiddenLayers)]);
        }
    }
    status_msg_ = language_ == UILanguage::Vietnamese
        ? "Snap cập nhật và lưu ngay · cài đặt dùng chung cho công cụ vẽ/di chuyển · không thuộc Undo"
        : "Snap settings saved and shared by drawing and move tools · preferences are outside Undo";
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

#endif
} // namespace pikdraw::desktop
