#include "pikdraw/desktop/shell/native_window.hpp"

#include "pikdraw/desktop/actions/action_dispatcher.hpp"
#include "pikdraw/document/commands/raster_edit_commands.hpp"
#include "pikdraw/raster/edit_geometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace pikdraw::desktop {
namespace {

#ifdef _WIN32
constexpr int kExposureId = 501;
constexpr int kLeftId = 502;
constexpr int kTopId = 503;
constexpr int kWidthId = 504;
constexpr int kHeightId = 505;
constexpr int kFeatherId = 506;
constexpr int kHelpId = 507;
constexpr int kDensityId = 508;
constexpr int kInvertId = 509;
constexpr int kShapeId = 510;
constexpr int kAngleId = 511;

struct LocalExposureState {
    bool vietnamese{false};
    bool accepted{false};
    bool editing_existing_node{false};
    double exposure_ev{1.0};
    double left{0.0};
    double top{0.0};
    double width{0.5};
    double height{0.5};
    double feather_px{8.0};
    double gradient_angle_degrees{0.0};
    double density_percent{100.0};
    bool inverted{false};
    document::RasterAdjustmentMaskKind kind{
        document::RasterAdjustmentMaskKind::Rectangle};
};

HWND add_control(HWND dialog, const wchar_t* type, const std::wstring& text,
                 DWORD style, int x, int y, int width, int height, int id,
                 HFONT font) {
    const HWND control = CreateWindowExW(0, type, text.c_str(),
        WS_CHILD | WS_VISIBLE | style, x, y, width, height, dialog,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
    if (control && font)
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return control;
}

void set_number(HWND dialog, int id, double value) {
    const auto text = std::to_wstring(value);
    SetDlgItemTextW(dialog, id, text.c_str());
}

bool read_number(HWND dialog, int id, double& value) {
    wchar_t text[128]{};
    const int length = GetDlgItemTextW(dialog, id, text,
                                       static_cast<int>(std::size(text)));
    if (length <= 0) return false;
    wchar_t* end = nullptr;
    value = std::wcstod(text, &end);
    while (end && *end && std::iswspace(*end)) ++end;
    return end != text && end && *end == L'\0' && std::isfinite(value);
}

INT_PTR CALLBACK local_exposure_dialog_proc(HWND dialog, UINT message,
                                             WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<LocalExposureState*>(
        GetWindowLongPtrW(dialog, DWLP_USER));
    if (message == WM_INITDIALOG) {
        state = reinterpret_cast<LocalExposureState*>(lparam);
        SetWindowLongPtrW(dialog, DWLP_USER, lparam);
        SetWindowTextW(dialog, state->editing_existing_node
            ? (state->vietnamese ? L"Mặt nạ cho chỉnh sửa gần nhất"
                                 : L"Mask Last Adjustment")
            : (state->vietnamese ? L"Phơi sáng cục bộ có mặt nạ"
                                 : L"Local Exposure Mask"));
        const HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        struct Field { const wchar_t* vi; const wchar_t* en; int id; double value; };
        const Field fields[] = {
            {state->editing_existing_node
                 ? L"Đích: node chỉnh sửa cuối"
                 : L"Phơi sáng (−10…10 EV)",
             state->editing_existing_node
                 ? L"Target: last adjustment node"
                 : L"Exposure (−10…10 EV)",
                kExposureId, state->exposure_ev},
            {L"Trái (0…1)", L"Left (0…1)", kLeftId, state->left},
            {L"Trên (0…1)", L"Top (0…1)", kTopId, state->top},
            {L"Rộng (0…1)", L"Width (0…1)", kWidthId, state->width},
            {L"Cao (0…1)", L"Height (0…1)", kHeightId, state->height},
            {L"Feather (pixel)", L"Feather (pixels)", kFeatherId, state->feather_px},
            {L"Mật độ (0–100%)", L"Density (0–100%)", kDensityId,
                state->density_percent}};
        int y = 12;
        for (const auto& field : fields) {
            add_control(dialog, L"STATIC",
                state->vietnamese ? field.vi : field.en,
                SS_LEFT | WS_GROUP, 16, y + 4, 178, 22, field.id + 100, font);
            add_control(dialog, L"EDIT", L"",
                ES_LEFT | ES_AUTOHSCROLL | WS_TABSTOP,
                202, y, 192, 25, field.id, font);
            set_number(dialog, field.id, field.value);
            y += 35;
        }
        if (state->editing_existing_node)
            ShowWindow(GetDlgItem(dialog, kExposureId), SW_HIDE);
        add_control(dialog, L"STATIC", state->vietnamese
            ? L"Kiểu mặt nạ" : L"Mask shape",
            SS_LEFT | WS_GROUP, 16, 257, 178, 22, kShapeId + 100, font);
        const HWND shape = add_control(dialog, L"COMBOBOX", L"",
            CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
            202, 253, 192, 92, kShapeId, font);
        const wchar_t* shape_labels_vi[] = {
            L"Chữ nhật feather", L"Gradient tuyến tính (chọn góc)",
            L"Gradient radial tâm → biên"};
        const wchar_t* shape_labels_en[] = {
            L"Feathered rectangle", L"Linear gradient (set angle)",
            L"Radial gradient center → edge"};
        const document::RasterAdjustmentMaskKind shape_kinds[] = {
            document::RasterAdjustmentMaskKind::Rectangle,
            document::RasterAdjustmentMaskKind::LinearGradient,
            document::RasterAdjustmentMaskKind::RadialGradient};
        LRESULT selected_shape = 0;
        for (size_t i = 0; i < std::size(shape_kinds); ++i) {
            SendMessageW(shape, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(
                state->vietnamese ? shape_labels_vi[i] : shape_labels_en[i]));
            if (shape_kinds[i] == state->kind)
                selected_shape = static_cast<LRESULT>(i);
        }
        SendMessageW(shape, CB_SETCURSEL, selected_shape, 0);
        const HWND angle_label = add_control(dialog, L"STATIC",
            state->vietnamese ? L"Góc gradient (−180…180°)"
                              : L"Gradient angle (−180…180°)",
            SS_LEFT | WS_GROUP, 16, 307, 178, 22, kAngleId + 100, font);
        const HWND angle = add_control(dialog, L"EDIT", L"",
            ES_LEFT | ES_AUTOHSCROLL | WS_TABSTOP,
            202, 303, 192, 25, kAngleId, font);
        set_number(dialog, kAngleId, state->gradient_angle_degrees);
        const bool linear_selected = selected_shape == 1;
        ShowWindow(angle_label, linear_selected ? SW_SHOW : SW_HIDE);
        ShowWindow(angle, linear_selected ? SW_SHOW : SW_HIDE);
        EnableWindow(angle, linear_selected);
        const HWND invert = add_control(dialog, L"BUTTON", state->vietnamese
            ? L"Đảo vùng mặt nạ" : L"Invert mask",
            BS_AUTOCHECKBOX | WS_TABSTOP, 16, 333, 378, 22, kInvertId, font);
        SendMessageW(invert, BM_SETCHECK, state->inverted ? BST_CHECKED : BST_UNCHECKED, 0);
        add_control(dialog, L"STATIC", state->editing_existing_node
            ? (state->vietnamese
                ? L"Giữ nguyên tham số và ID node; chỉ thay mặt nạ của chỉnh sửa cuối."
                : L"Keeps the node ID and adjustment; only its mask changes.")
            : (state->vietnamese
                ? L"Tọa độ theo ảnh sau crop/xoay. Góc 0° chạy trái→phải; góc dương xoay theo chiều kim đồng hồ. Radial giảm từ tâm ra biên."
                : L"Coordinates follow the cropped/rotated raster. 0° fades left-to-right; positive angles rotate clockwise. Radial fades center-to-edge."),
            SS_LEFT | SS_NOPREFIX, 16, 358, 378, 39, kHelpId, font);
        add_control(dialog, L"BUTTON", state->vietnamese ? L"Áp dụng" : L"Apply",
            BS_DEFPUSHBUTTON | WS_TABSTOP, 215, 402, 82, 27, IDOK, font);
        add_control(dialog, L"BUTTON", state->vietnamese ? L"Hủy" : L"Cancel",
            BS_PUSHBUTTON | WS_TABSTOP, 306, 402, 82, 27, IDCANCEL, font);
        SetFocus(GetDlgItem(dialog, kExposureId));
        return FALSE;
    }
    if (!state) return FALSE;
    if (message == WM_COMMAND && LOWORD(wparam) == kShapeId &&
        HIWORD(wparam) == CBN_SELCHANGE) {
        const bool linear_selected = SendDlgItemMessageW(
            dialog, kShapeId, CB_GETCURSEL, 0, 0) == 1;
        ShowWindow(GetDlgItem(dialog, kAngleId + 100),
                   linear_selected ? SW_SHOW : SW_HIDE);
        ShowWindow(GetDlgItem(dialog, kAngleId),
                   linear_selected ? SW_SHOW : SW_HIDE);
        EnableWindow(GetDlgItem(dialog, kAngleId), linear_selected);
        return TRUE;
    }
    if (message == WM_COMMAND && LOWORD(wparam) == IDOK) {
        const LRESULT shape_index = SendDlgItemMessageW(
            dialog, kShapeId, CB_GETCURSEL, 0, 0);
        if (shape_index < 0 || shape_index > 2) {
            SetDlgItemTextW(dialog, kHelpId, state->vietnamese
                ? L"Chọn một kiểu mặt nạ hợp lệ."
                : L"Select a valid mask shape.");
            return TRUE;
        }
        const bool exposure_valid = state->editing_existing_node ||
            (read_number(dialog, kExposureId, state->exposure_ev) &&
             state->exposure_ev >= -10.0 && state->exposure_ev <= 10.0);
        const bool valid = exposure_valid &&
            read_number(dialog, kLeftId, state->left) &&
            read_number(dialog, kTopId, state->top) &&
            read_number(dialog, kWidthId, state->width) &&
            read_number(dialog, kHeightId, state->height) &&
            read_number(dialog, kFeatherId, state->feather_px) &&
            read_number(dialog, kDensityId, state->density_percent) &&
            (shape_index != 1 ||
                (read_number(dialog, kAngleId,
                             state->gradient_angle_degrees) &&
                 state->gradient_angle_degrees >= -180.0 &&
                 state->gradient_angle_degrees <= 180.0)) &&
            state->left >= 0.0 && state->top >= 0.0 &&
            state->width > 0.0 && state->height > 0.0 &&
            state->left + state->width <= 1.0 &&
            state->top + state->height <= 1.0 && state->feather_px >= 0.0 &&
            state->density_percent >= 0.0 && state->density_percent <= 100.0 &&
            std::floor(state->density_percent) == state->density_percent;
        if (!valid) {
            SetDlgItemTextW(dialog, kHelpId, state->vietnamese
                ? L"Kiểm tra phạm vi, kích thước, feather và góc tuyến tính −180…180°."
                : L"Check bounds, feather, and linear angle range −180…180°.");
            return TRUE;
        }
        const document::RasterAdjustmentMaskKind shape_kinds[] = {
            document::RasterAdjustmentMaskKind::Rectangle,
            document::RasterAdjustmentMaskKind::LinearGradient,
            document::RasterAdjustmentMaskKind::RadialGradient};
        state->kind = shape_kinds[static_cast<size_t>(shape_index)];
        if (state->kind != document::RasterAdjustmentMaskKind::LinearGradient)
            state->gradient_angle_degrees = 0.0;
        state->inverted = SendDlgItemMessageW(
            dialog, kInvertId, BM_GETCHECK, 0, 0) == BST_CHECKED;
        state->accepted = true;
        EndDialog(dialog, IDOK);
        return TRUE;
    }
    if ((message == WM_COMMAND && LOWORD(wparam) == IDCANCEL) ||
        message == WM_CLOSE) {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

bool show_local_exposure_dialog(HWND owner, LocalExposureState& state) {
    struct EmptyDialog {
        DLGTEMPLATE dialog{};
        WORD menu{0};
        WORD klass{0};
        WORD title{0};
    } layout{};
    layout.dialog.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME |
        DS_CENTER | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
    layout.dialog.cdit = 0;
    layout.dialog.cx = 420;
    layout.dialog.cy = 450;
    return DialogBoxIndirectParamW(GetModuleHandleW(nullptr), &layout.dialog,
        owner, local_exposure_dialog_proc, reinterpret_cast<LPARAM>(&state)) == IDOK &&
        state.accepted;
}
#endif

std::optional<document::ObjectId> selected_bitmap_id(const NativeWindow& window) {
    const auto& selected = window.root().vector_controller().selection().selected_ids();
    if (selected.size() != 1) return std::nullopt;
    return selected.front();
}

bool is_maskable_adjustment(const document::RasterEditNode& node) noexcept {
    return std::visit([](const auto& parameters) {
        using T = std::decay_t<decltype(parameters)>;
        return !std::is_same_v<T, document::RasterCropParams> &&
               !std::is_same_v<T, document::RasterFlipParams> &&
               !std::is_same_v<T, document::RasterRotateParams> &&
               !std::is_same_v<T, document::RasterCloneStampParams>;
    }, node.parameters);
}

}  // namespace

void NativeWindow::edit_selected_bitmap_local_exposure_mask() {
#ifdef _WIN32
    if (!can_edit_selected_bitmap(true)) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Chọn một ảnh sRGB có nguồn được giữ lại để tạo mặt nạ"
            : "Select one sRGB bitmap with a preserved source to create a mask";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    const auto selected = selected_bitmap_id(*this);
    const auto snapshot = root().document().current_snapshot();
    if (!selected || !snapshot) return;
    const auto* object = snapshot->objects().lookup_ref(*selected);
    const auto* image = object ? std::get_if<document::ImageObject>(object) : nullptr;
    if (!image) return;
    const auto [width, height] = raster::raster_edited_pixel_dimensions(*image);
    if (width == 0 || height == 0) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Kích thước ảnh sau các node hình học không hợp lệ"
            : "Raster dimensions after geometric edit nodes are invalid";
        return;
    }
    LocalExposureState state;
    state.vietnamese = language_ == UILanguage::Vietnamese;
    if (!show_local_exposure_dialog(hwnd_, state)) return;

    uint64_t maximum_node_id = 0;
    uint64_t maximum_mask_id = 0;
    for (const auto& node : image->edit_graph.nodes) {
        maximum_node_id = (std::max)(maximum_node_id, node.id.value);
        if (node.mask)
            maximum_mask_id = (std::max)(maximum_mask_id, node.mask->id);
    }
    if (maximum_node_id == std::numeric_limits<uint64_t>::max() ||
        maximum_mask_id == std::numeric_limits<uint64_t>::max()) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Không còn mã node hoặc mặt nạ khả dụng"
            : "No raster node or mask ID is available";
        return;
    }
    document::RasterAdjustmentMask mask;
    mask.id = maximum_mask_id + 1;
    mask.width_px = width;
    mask.height_px = height;
    mask.density_percent = static_cast<uint8_t>(state.density_percent);
    mask.inverted = state.inverted;
    mask.kind = state.kind;
    mask.left = state.left;
    mask.top = state.top;
    mask.width = state.width;
    mask.height = state.height;
    mask.feather_px = state.feather_px;
    mask.gradient_angle_degrees = state.gradient_angle_degrees;
    if (mask.feather_px > (std::max)(width, height)) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Feather không được lớn hơn cạnh dài của ảnh"
            : "Feather cannot exceed the longest raster edge";
        return;
    }
    document::RasterEditNode node{
        document::RasterEditNodeId{maximum_node_id + 1}, true,
        document::RasterExposureParams{state.exposure_ev}};
    node.mask = std::move(mask);
    const auto result = root().document().execute_command(
        std::make_unique<document::SetRasterEditNodeCommand>(*selected,
                                                             std::move(node)));
    status_msg_ = result.success
        ? (language_ == UILanguage::Vietnamese
            ? "Đã tạo phơi sáng cục bộ có feather · Ctrl+Z hoàn tác"
            : "Local feathered Exposure mask created · Ctrl+Z to undo")
        : (language_ == UILanguage::Vietnamese
            ? "Không thể tạo mặt nạ phơi sáng cục bộ"
            : "Could not create local Exposure mask");
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::edit_selected_bitmap_adjustment_mask() {
#ifdef _WIN32
    if (!can_edit_selected_bitmap(true)) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Chọn một ảnh sRGB có nguồn được giữ lại để sửa mặt nạ"
            : "Select one sRGB bitmap with a preserved source to edit a mask";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    const auto selected = selected_bitmap_id(*this);
    const auto snapshot = root().document().current_snapshot();
    if (!selected || !snapshot) return;
    const auto* object = snapshot->objects().lookup_ref(*selected);
    const auto* image = object ? std::get_if<document::ImageObject>(object) : nullptr;
    if (!image || image->edit_graph.nodes.empty()) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Thêm một chỉnh sửa màu trước khi gắn mặt nạ"
            : "Add a color adjustment before attaching a mask";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    const auto& target = image->edit_graph.nodes.back();
    if (!is_maskable_adjustment(target)) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Node cuối không hỗ trợ mặt nạ; thêm hoặc chọn chỉnh sửa màu sau node hình học/Clone"
            : "The last node does not support masks; add a color adjustment after geometry or Clone Stamp";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (target.mask &&
        target.mask->kind == document::RasterAdjustmentMaskKind::Bitmap) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Mặt nạ bitmap được giữ nguyên; công cụ refine nét cọ chưa khả dụng"
            : "The painted bitmap mask is preserved; brush refinement is not available yet";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    const auto [width, height] = raster::raster_edited_pixel_dimensions(*image);
    if (width == 0 || height == 0) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Kích thước ảnh tại node chỉnh màu không hợp lệ"
            : "Raster dimensions at the adjustment node are invalid";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    LocalExposureState state;
    state.vietnamese = language_ == UILanguage::Vietnamese;
    state.editing_existing_node = true;
    if (target.mask) {
        state.left = target.mask->left;
        state.top = target.mask->top;
        state.width = target.mask->width;
        state.height = target.mask->height;
        state.feather_px = target.mask->feather_px;
        state.gradient_angle_degrees = target.mask->gradient_angle_degrees;
        state.density_percent = target.mask->density_percent;
        state.inverted = target.mask->inverted;
        state.kind = target.mask->kind;
    }
    if (!show_local_exposure_dialog(hwnd_, state)) return;

    uint64_t maximum_mask_id = 0;
    for (const auto& node : image->edit_graph.nodes)
        if (node.mask)
            maximum_mask_id = (std::max)(maximum_mask_id, node.mask->id);
    uint64_t mask_id = target.mask ? target.mask->id : maximum_mask_id + 1;
    if (mask_id == 0 || (!target.mask && maximum_mask_id ==
                                      std::numeric_limits<uint64_t>::max())) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Không còn mã mặt nạ khả dụng"
            : "No raster mask ID is available";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    document::RasterAdjustmentMask mask;
    mask.id = mask_id;
    mask.width_px = width;
    mask.height_px = height;
    mask.density_percent = static_cast<uint8_t>(state.density_percent);
    mask.inverted = state.inverted;
    mask.kind = state.kind;
    mask.left = state.left;
    mask.top = state.top;
    mask.width = state.width;
    mask.height = state.height;
    mask.feather_px = state.feather_px;
    mask.gradient_angle_degrees = state.gradient_angle_degrees;
    if (mask.feather_px > (std::max)(width, height)) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Feather không được lớn hơn cạnh dài của ảnh"
            : "Feather cannot exceed the longest raster edge";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    auto updated = target;
    updated.mask = std::move(mask);
    const auto result = root().document().execute_command(
        std::make_unique<document::SetRasterEditNodeCommand>(
            *selected, std::move(updated)));
    status_msg_ = result.success
        ? (language_ == UILanguage::Vietnamese
            ? "Đã cập nhật mặt nạ node chỉnh màu gần nhất · Ctrl+Z hoàn tác"
            : "Updated the last color adjustment mask · Ctrl+Z to undo")
        : (language_ == UILanguage::Vietnamese
            ? "Không thể cập nhật mặt nạ chỉnh màu"
            : "Could not update the color adjustment mask");
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

}  // namespace pikdraw::desktop
