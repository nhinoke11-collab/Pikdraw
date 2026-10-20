#include "pikdraw/desktop/shell/native_window.hpp"
#include "native_window_menus.hpp"
#include "pikdraw/desktop/shell/native_window_shortcuts_settings.hpp"
#include "pikdraw/vector/selection/selection_model.hpp"
#include "pikdraw/document/commands/layer_commands.hpp"
#include "pikdraw/document/model/object.hpp"
#include <algorithm>
#include <variant>

namespace pikdraw::desktop {

#ifdef _WIN32

HMENU NativeWindow::build_context_menu(const std::vector<document::ObjectId>& selection, bool /*is_canvas_hit*/) const {
    HMENU root_menu = CreatePopupMenu();
    const bool is_vi = (language_ == UILanguage::Vietnamese);
    const auto snapshot = root().document().current_snapshot();

    // =========================================================================
    // CASE 1: Empty Canvas (Canvas / Workspace Context Menu)
    // =========================================================================
    if (selection.empty()) {
        AppendMenuW(root_menu, MF_STRING, static_cast<UINT_PTR>(MenuActionId::EditPaste),
                    is_vi ? L"Dán (Paste)\tCtrl+V" : L"Paste\tCtrl+V");
        AppendMenuW(root_menu, MF_STRING, static_cast<UINT_PTR>(MenuActionId::EditPasteInPlace),
                    is_vi ? L"Dán tại chỗ (Paste in Place)\tCtrl+Shift+V" : L"Paste in Place\tCtrl+Shift+V");
        AppendMenuW(root_menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(root_menu, MF_STRING, static_cast<UINT_PTR>(MenuActionId::EditSelectAll),
                    is_vi ? L"Chọn tất cả (Select All)\tCtrl+A" : L"Select All\tCtrl+A");
        AppendMenuW(root_menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(root_menu, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ViewFitPage),
                    is_vi ? L"Vừa khung nhìn (Fit Page)\tF" : L"Fit Page to Window\tF");
        AppendMenuW(root_menu, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ViewZoom100),
                    is_vi ? L"Tỷ lệ 100% (Zoom 100%)\t1" : L"Zoom 100%\t1");
        AppendMenuW(root_menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(root_menu, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ViewToggleGuidelines),
                    is_vi ? L"Ẩn/Hiện đường gióng (Guidelines)\tCtrl+;" : L"Toggle Guidelines\tCtrl+;");
        AppendMenuW(root_menu, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ViewClearGuidelines),
                    is_vi ? L"Xóa hết đường gióng (Clear Guidelines)\tCtrl+Alt+;" : L"Clear All Guidelines\tCtrl+Alt+;");
        AppendMenuW(root_menu, MF_STRING,
                    static_cast<UINT_PTR>(MenuActionId::ViewSnapGrid),
                    is_vi ? L"Hít vào lưới (Snap to Grid)" : L"Snap to Grid");
        AppendMenuW(root_menu, MF_STRING,
                    static_cast<UINT_PTR>(MenuActionId::ViewSnapObjects),
                    is_vi ? L"Hít vào đối tượng (Snap to Objects)" : L"Snap to Objects");
        AppendMenuW(root_menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(root_menu, MF_STRING, static_cast<UINT_PTR>(MenuActionId::EditSelectAllCmyk),
                    is_vi ? L"Chọn tất cả đối tượng CMYK" : L"Select All CMYK Objects");
        AppendMenuW(root_menu, MF_STRING, static_cast<UINT_PTR>(MenuActionId::EditSelectAllRgb),
                    is_vi ? L"Chọn tất cả đối tượng RGB" : L"Select All RGB Objects");

        HMENU hColor = CreatePopupMenu();
        AppendMenuW(hColor, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ColorConvertActivePageToCmyk),
                    is_vi ? L"Chuyển trang hiện tại sang CMYK" : L"Convert Active Page to CMYK");
        AppendMenuW(hColor, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ColorConvertActivePageToRgb),
                    is_vi ? L"Chuyển trang hiện tại sang RGB" : L"Convert Active Page to RGB");
        AppendMenuW(hColor, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hColor, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ColorConvertDocumentToCmyk),
                    is_vi ? L"Chuyển toàn bộ file sang CMYK" : L"Convert Entire Document to CMYK");
        AppendMenuW(hColor, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ColorConvertDocumentToRgb),
                    is_vi ? L"Chuyển toàn bộ file sang RGB" : L"Convert Entire Document to RGB");
        AppendMenuW(root_menu, MF_POPUP, reinterpret_cast<UINT_PTR>(hColor),
                    is_vi ? L"Hệ màu & Chuyển đổi (Color Mode)" : L"Color Mode & Conversion");

        AppendMenuW(root_menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(root_menu, MF_STRING, static_cast<UINT_PTR>(MenuActionId::LayoutPageSetup),
                    is_vi ? L"Thiết lập trang in… (Page Setup…)" : L"Page Setup…");
        return root_menu;
    }

    // =========================================================================
    // CASE 2: Multi-Selection Context Menu (2+ objects) - Corel & SketchUp style
    // =========================================================================
    if (selection.size() >= 2) {
        // Group & PowerClip
        AppendMenuW(root_menu, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectGroup),
                    is_vi ? L"Nhóm đối tượng (Group Objects)\tCtrl+G" : L"Group Objects\tCtrl+G");
        AppendMenuW(root_menu, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectPowerClipMake),
                    is_vi ? L"Tạo mặt nạ cắt / PowerClip (Make Clipping Mask)\tCtrl+7" : L"Make Clipping Mask / PowerClip\tCtrl+7");
        AppendMenuW(root_menu, MF_SEPARATOR, 0, nullptr);

        // Shaping / Booleans Submenu (CorelDRAW Weld/Trim/Intersect & SketchUp Solid Tools)
        HMENU hShaping = CreatePopupMenu();
        AppendMenuW(hShaping, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectBooleanUnion),
                    is_vi ? L"Hàn / Hợp nhất (Weld / Union)" : L"Weld / Union");
        AppendMenuW(hShaping, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectBooleanDifference),
                    is_vi ? L"Cắt tỉa / Hiệu (Trim / Difference)" : L"Trim / Difference (Target − Source)");
        AppendMenuW(hShaping, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectBooleanIntersection),
                    is_vi ? L"Lấy phần giao (Intersect)" : L"Intersect");
        AppendMenuW(hShaping, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectBooleanExclude),
                    is_vi ? L"Loại trừ phần giao (Exclude / Simplify)" : L"Exclude / Simplify");
        AppendMenuW(hShaping, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectBooleanReverseDifference),
                    is_vi ? L"Hiệu ngược (Reverse Difference)" : L"Reverse Difference (Source − Target)");
        AppendMenuW(root_menu, MF_POPUP, reinterpret_cast<UINT_PTR>(hShaping),
                    is_vi ? L"Tạo hình (Shaping / Booleans)" : L"Shaping / Booleans");

        // Align & Distribute Submenu (CorelDRAW Style: L, C, R, T, E, B, P)
        HMENU hAlign = CreatePopupMenu();
        AppendMenuW(hAlign, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectAlignLeft),
                    is_vi ? L"Căn lề trái (Align Left)\tL" : L"Align Left\tL");
        AppendMenuW(hAlign, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectAlignCenter),
                    is_vi ? L"Căn giữa ngang (Center Horizontally)\tC" : L"Align Center Horizontally\tC");
        AppendMenuW(hAlign, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectAlignRight),
                    is_vi ? L"Căn lề phải (Align Right)\tR" : L"Align Right\tR");
        AppendMenuW(hAlign, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectAlignTop),
                    is_vi ? L"Căn đỉnh (Align Top)\tT" : L"Align Top\tT");
        AppendMenuW(hAlign, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectAlignMiddle),
                    is_vi ? L"Căn giữa dọc (Center Vertically)\tE" : L"Align Center Vertically\tE");
        AppendMenuW(hAlign, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectAlignBottom),
                    is_vi ? L"Căn đáy (Align Bottom)\tB" : L"Align Bottom\tB");
        AppendMenuW(hAlign, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectCenterPage),
                    is_vi ? L"Căn giữa trang (Center to Page)\tP" : L"Center to Page\tP");
        AppendMenuW(hAlign, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hAlign, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectDistributeHorizontal),
                    is_vi ? L"Phân bổ khoảng cách ngang đều nhau" : L"Distribute Horizontal Spacing");
        AppendMenuW(hAlign, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectDistributeVertical),
                    is_vi ? L"Phân bổ khoảng cách dọc đều nhau" : L"Distribute Vertical Spacing");
        AppendMenuW(root_menu, MF_POPUP, reinterpret_cast<UINT_PTR>(hAlign),
                    is_vi ? L"Căn chỉnh & Phân bổ (Align & Distribute)" : L"Align & Distribute");

        // Order Submenu (CorelDRAW Z-Order)
        HMENU hOrder = CreatePopupMenu();
        AppendMenuW(hOrder, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectOrderToFront),
                    is_vi ? L"Lên trên cùng trang (To Front of Page)\tShift+PgUp" : L"To Front of Page\tShift+PgUp");
        AppendMenuW(hOrder, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectOrderToBack),
                    is_vi ? L"Xuống dưới cùng trang (To Back of Page)\tShift+PgDn" : L"To Back of Page\tShift+PgDn");
        AppendMenuW(hOrder, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectOrderForwardOne),
                    is_vi ? L"Lên một lớp (Forward One)\tCtrl+PgUp" : L"Forward One\tCtrl+PgUp");
        AppendMenuW(hOrder, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectOrderBackwardOne),
                    is_vi ? L"Xuống một lớp (Backward One)\tCtrl+PgDn" : L"Backward One\tCtrl+PgDn");
        AppendMenuW(hOrder, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hOrder, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectOrderReverse),
                    is_vi ? L"Đảo ngược thứ tự các đối tượng (Reverse Order)" : L"Reverse Order");
        AppendMenuW(root_menu, MF_POPUP, reinterpret_cast<UINT_PTR>(hOrder),
                    is_vi ? L"Thứ tự lớp (Order)" : L"Order");

        // Transform Submenu (SketchUp & Corel Flip / Rotate)
        HMENU hTrans = CreatePopupMenu();
        AppendMenuW(hTrans, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectFlipHorizontal),
                    is_vi ? L"Lật ngang qua tâm (Flip Horizontal)\tCtrl+Shift+H" : L"Flip Horizontal\tCtrl+Shift+H");
        AppendMenuW(hTrans, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectFlipVertical),
                    is_vi ? L"Lật dọc qua tâm (Flip Vertical)\tCtrl+Shift+V" : L"Flip Vertical\tCtrl+Shift+V");
        AppendMenuW(hTrans, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectRotate90CW),
                    is_vi ? L"Xoay 90° thuận chiều (Rotate 90° CW)\tCtrl+]" : L"Rotate 90° CW\tCtrl+]");
        AppendMenuW(hTrans, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectRotate90CCW),
                    is_vi ? L"Xoay 90° ngược chiều (Rotate 90° CCW)\tCtrl+[" : L"Rotate 90° CCW\tCtrl+[");
        AppendMenuW(root_menu, MF_POPUP, reinterpret_cast<UINT_PTR>(hTrans),
                    is_vi ? L"Biến đổi (Transform)" : L"Transform");

        AppendMenuW(root_menu, MF_SEPARATOR, 0, nullptr);

        // Clipboard & Basic Actions
        AppendMenuW(root_menu, MF_STRING, static_cast<UINT_PTR>(MenuActionId::EditCut),
                    is_vi ? L"Cắt (Cut)\tCtrl+X" : L"Cut\tCtrl+X");
        AppendMenuW(root_menu, MF_STRING, static_cast<UINT_PTR>(MenuActionId::EditCopy),
                    is_vi ? L"Sao chép (Copy)\tCtrl+C" : L"Copy\tCtrl+C");
        AppendMenuW(root_menu, MF_STRING, static_cast<UINT_PTR>(MenuActionId::EditDuplicate),
                    is_vi ? L"Nhân bản (Duplicate)\tCtrl+D" : L"Duplicate\tCtrl+D");
        AppendMenuW(root_menu, MF_STRING, static_cast<UINT_PTR>(MenuActionId::EditDelete),
                    is_vi ? L"Xóa (Delete)\tDel" : L"Delete\tDel");
        AppendMenuW(root_menu, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectLock),
                    is_vi ? L"Khóa các đối tượng (Lock Objects)" : L"Lock Objects");
        AppendMenuW(root_menu, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectHide),
                    is_vi ? L"Ẩn các đối tượng (Hide Objects)" : L"Hide Objects");

        AppendMenuW(root_menu, MF_SEPARATOR, 0, nullptr);
        HMENU hSelectSame = CreatePopupMenu();
        AppendMenuW(hSelectSame, MF_STRING, static_cast<UINT_PTR>(MenuActionId::EditSelectSameFillColor),
                    is_vi ? L"Cùng màu tô (Same Fill Color)" : L"Same Fill Color");
        AppendMenuW(hSelectSame, MF_STRING, static_cast<UINT_PTR>(MenuActionId::EditSelectSameStrokeColor),
                    is_vi ? L"Cùng màu viền (Same Stroke Color)" : L"Same Stroke Color");
        AppendMenuW(hSelectSame, MF_STRING, static_cast<UINT_PTR>(MenuActionId::EditSelectSameFillAndStroke),
                    is_vi ? L"Cùng màu tô & viền (Same Fill & Stroke)" : L"Same Fill & Stroke");
        AppendMenuW(hSelectSame, MF_STRING, static_cast<UINT_PTR>(MenuActionId::EditSelectSameColorSpace),
                    is_vi ? L"Cùng hệ màu (Same Color Space)" : L"Same Color Space");
        AppendMenuW(root_menu, MF_POPUP, reinterpret_cast<UINT_PTR>(hSelectSame),
                    is_vi ? L"Chọn cùng mẫu (Select Same)" : L"Select Same");

        HMENU hColor = CreatePopupMenu();
        AppendMenuW(hColor, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ColorConvertSelectionToCmyk),
                    is_vi ? L"Chuyển các đối tượng chọn sang CMYK" : L"Convert Selection to CMYK");
        AppendMenuW(hColor, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ColorConvertSelectionToRgb),
                    is_vi ? L"Chuyển các đối tượng chọn sang RGB" : L"Convert Selection to RGB");
        AppendMenuW(hColor, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hColor, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ColorConvertActivePageToCmyk),
                    is_vi ? L"Chuyển trang hiện tại sang CMYK" : L"Convert Active Page to CMYK");
        AppendMenuW(hColor, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ColorConvertActivePageToRgb),
                    is_vi ? L"Chuyển trang hiện tại sang RGB" : L"Convert Active Page to RGB");
        AppendMenuW(hColor, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ColorConvertDocumentToCmyk),
                    is_vi ? L"Chuyển toàn bộ file sang CMYK" : L"Convert Entire Document to CMYK");
        AppendMenuW(hColor, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ColorConvertDocumentToRgb),
                    is_vi ? L"Chuyển toàn bộ file sang RGB" : L"Convert Entire Document to RGB");
        AppendMenuW(root_menu, MF_POPUP, reinterpret_cast<UINT_PTR>(hColor),
                    is_vi ? L"Chuyển hệ màu (Convert Color Mode)" : L"Convert Color Mode");

        AppendMenuW(root_menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(root_menu, MF_STRING, static_cast<UINT_PTR>(MenuActionId::EditDeselect),
                    is_vi ? L"Bỏ chọn (Deselect)\tEsc" : L"Deselect\tEsc");
        return root_menu;
    }

    // =========================================================================
    // CASE 3: Single Object Context Menu (Context-aware Smart Menu)
    // =========================================================================
    const auto id = selection.front();
    const bool is_group = const_cast<NativeWindow*>(this)->workflow().is_group(id);
    const bool is_powerclip = const_cast<NativeWindow*>(this)->workflow().is_clipping_container(id);
    bool is_text = false;
    bool is_path = false;
    bool is_locked = false;
    bool lock_contents = false;

    if (auto obj_opt = snapshot->objects().get(id)) {
        std::visit([&](const auto& item) {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, document::TextObject>) {
                is_text = true;
                is_locked = item.locked;
            } else if constexpr (std::is_same_v<T, document::PathObject>) {
                is_path = true;
                is_locked = item.locked;
            } else if constexpr (std::is_same_v<T, document::GroupObject>) {
                is_locked = item.locked;
                if (item.clip.has_value()) {
                    lock_contents = item.clip->lock_contents_to_frame;
                }
            } else {
                is_locked = item.locked;
            }
        }, *obj_opt);
    }

    // 1. PowerClip / Group / Isolation Smart Actions
    if (is_powerclip) {
        AppendMenuW(root_menu, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectIsolationEnter),
                    is_vi ? L"Chỉnh sửa nội dung PowerClip (Edit Contents)\tEnter" : L"Edit PowerClip Contents\tEnter");
        AppendMenuW(root_menu, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectPowerClipRelease),
                    is_vi ? L"Trích xuất nội dung (Extract Contents)\tCtrl+Alt+7" : L"Extract Contents / Release\tCtrl+Alt+7");

        HMENU hFit = CreatePopupMenu();
        AppendMenuW(hFit, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectPowerClipFitCenter),
                    is_vi ? L"Căn giữa nội dung (Center Contents)" : L"Center Contents");
        AppendMenuW(hFit, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectPowerClipFitProp),
                    is_vi ? L"Vừa khít giữ tỉ lệ (Fit Proportionally)" : L"Fit Proportionally");
        AppendMenuW(hFit, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectPowerClipFillProp),
                    is_vi ? L"Phủ đầy khung (Fill Proportionally)" : L"Fill Proportionally");
        AppendMenuW(root_menu, MF_POPUP, reinterpret_cast<UINT_PTR>(hFit),
                    is_vi ? L"Căn chỉnh nội dung (Fit Contents)" : L"Fit Contents");

        AppendMenuW(root_menu, (lock_contents ? MF_CHECKED : MF_UNCHECKED) | MF_STRING,
                    static_cast<UINT_PTR>(MenuActionId::ObjectPowerClipToggleLock),
                    is_vi ? L"Khóa nội dung vào khung (Lock Contents to Frame)" : L"Lock Contents to Frame");
        AppendMenuW(root_menu, MF_SEPARATOR, 0, nullptr);
    } else if (is_group) {
        AppendMenuW(root_menu, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectIsolationEnter),
                    is_vi ? L"Chui vào sửa nhóm (Edit Group)\tEnter" : L"Edit Group (Enter)");
        AppendMenuW(root_menu, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectUngroup),
                    is_vi ? L"Rã nhóm (Ungroup)\tCtrl+Shift+G" : L"Ungroup\tCtrl+Shift+G");
        AppendMenuW(root_menu, MF_SEPARATOR, 0, nullptr);
    } else if (is_in_isolation_mode()) {
        AppendMenuW(root_menu, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectIsolationExit),
                    is_vi ? L"Thoát chế độ cô lập (Exit Isolation Mode)\tEsc" : L"Exit Isolation Mode\tEsc");
        AppendMenuW(root_menu, MF_SEPARATOR, 0, nullptr);
    }

    if (!is_powerclip) {
        AppendMenuW(root_menu, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectPowerClipPlaceInside),
                    is_vi ? L"Đặt vào trong khung… (Place Inside Frame…)" : L"Place Inside Frame…");
    }

    // 2. Type-specific actions (Text / Curves)
    if (is_text) {
        AppendMenuW(root_menu, MF_STRING, static_cast<UINT_PTR>(MenuActionId::TextEdit),
                    is_vi ? L"Soạn thảo văn bản (Edit Text)\tF8" : L"Edit Text\tF8");
        AppendMenuW(root_menu, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectConvertToCurves),
                    is_vi ? L"Chuyển thành đường cong (Convert to Curves)\tCtrl+Q" : L"Convert to Curves\tCtrl+Q");
        AppendMenuW(root_menu, MF_SEPARATOR, 0, nullptr);
    } else if (is_path) {
        AppendMenuW(root_menu, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectConvertToCurves),
                    is_vi ? L"Chuyển thành đường cong (Convert to Curves)\tCtrl+Q" : L"Convert to Curves\tCtrl+Q");
        AppendMenuW(root_menu, MF_SEPARATOR, 0, nullptr);
    }

    // 3. Order Submenu
    HMENU hOrder = CreatePopupMenu();
    AppendMenuW(hOrder, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectOrderToFront),
                is_vi ? L"Lên trên cùng trang (To Front of Page)\tShift+PgUp" : L"To Front of Page\tShift+PgUp");
    AppendMenuW(hOrder, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectOrderToBack),
                is_vi ? L"Xuống dưới cùng trang (To Back of Page)\tShift+PgDn" : L"To Back of Page\tShift+PgDn");
    AppendMenuW(hOrder, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectOrderForwardOne),
                is_vi ? L"Lên một lớp (Forward One)\tCtrl+PgUp" : L"Forward One\tCtrl+PgUp");
    AppendMenuW(hOrder, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectOrderBackwardOne),
                is_vi ? L"Xuống một lớp (Backward One)\tCtrl+PgDn" : L"Backward One\tCtrl+PgDn");
    AppendMenuW(root_menu, MF_POPUP, reinterpret_cast<UINT_PTR>(hOrder),
                is_vi ? L"Thứ tự lớp (Order)" : L"Order");

    // 4. Transform Submenu (Flip / Rotate)
    HMENU hTrans = CreatePopupMenu();
    AppendMenuW(hTrans, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectFlipHorizontal),
                is_vi ? L"Lật ngang qua tâm (Flip Horizontal)\tCtrl+Shift+H" : L"Flip Horizontal\tCtrl+Shift+H");
    AppendMenuW(hTrans, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectFlipVertical),
                is_vi ? L"Lật dọc qua tâm (Flip Vertical)\tCtrl+Shift+V" : L"Flip Vertical\tCtrl+Shift+V");
    AppendMenuW(hTrans, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectRotate90CW),
                is_vi ? L"Xoay 90° thuận chiều (Rotate 90° CW)\tCtrl+]" : L"Rotate 90° CW\tCtrl+]");
    AppendMenuW(hTrans, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectRotate90CCW),
                is_vi ? L"Xoay 90° ngược chiều (Rotate 90° CCW)\tCtrl+[" : L"Rotate 90° CCW\tCtrl+[");
    AppendMenuW(hTrans, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectRotate180),
                is_vi ? L"Xoay 180° (Rotate 180°)" : L"Rotate 180°");
    AppendMenuW(root_menu, MF_POPUP, reinterpret_cast<UINT_PTR>(hTrans),
                is_vi ? L"Biến đổi (Transform)" : L"Transform");

    AppendMenuW(root_menu, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectCenterPage),
                is_vi ? L"Căn giữa trang giấy (Center to Page)\tP" : L"Center to Page\tP");

    AppendMenuW(root_menu, MF_SEPARATOR, 0, nullptr);

    // 5. Clipboard & Basic Actions
    AppendMenuW(root_menu, MF_STRING, static_cast<UINT_PTR>(MenuActionId::EditCut),
                is_vi ? L"Cắt (Cut)\tCtrl+X" : L"Cut\tCtrl+X");
    AppendMenuW(root_menu, MF_STRING, static_cast<UINT_PTR>(MenuActionId::EditCopy),
                is_vi ? L"Sao chép (Copy)\tCtrl+C" : L"Copy\tCtrl+C");
    AppendMenuW(root_menu, MF_STRING, static_cast<UINT_PTR>(MenuActionId::EditDuplicate),
                is_vi ? L"Nhân bản (Duplicate)\tCtrl+D" : L"Duplicate\tCtrl+D");
    AppendMenuW(root_menu, MF_STRING, static_cast<UINT_PTR>(MenuActionId::EditDelete),
                is_vi ? L"Xóa (Delete)\tDel" : L"Delete\tDel");

    if (is_locked) {
        AppendMenuW(root_menu, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectUnlock),
                    is_vi ? L"Mở khóa đối tượng (Unlock Object)" : L"Unlock Object");
    } else {
        AppendMenuW(root_menu, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectLock),
                    is_vi ? L"Khóa đối tượng (Lock Object)" : L"Lock Object");
    }
    AppendMenuW(root_menu, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectHide),
                is_vi ? L"Ẩn đối tượng (Hide Object)" : L"Hide Object");

    AppendMenuW(root_menu, MF_SEPARATOR, 0, nullptr);
    HMENU hSelectSame = CreatePopupMenu();
    AppendMenuW(hSelectSame, MF_STRING, static_cast<UINT_PTR>(MenuActionId::EditSelectSameFillColor),
                is_vi ? L"Cùng màu tô (Same Fill Color)" : L"Same Fill Color");
    AppendMenuW(hSelectSame, MF_STRING, static_cast<UINT_PTR>(MenuActionId::EditSelectSameStrokeColor),
                is_vi ? L"Cùng màu viền (Same Stroke Color)" : L"Same Stroke Color");
    AppendMenuW(hSelectSame, MF_STRING, static_cast<UINT_PTR>(MenuActionId::EditSelectSameFillAndStroke),
                is_vi ? L"Cùng màu tô & viền (Same Fill & Stroke)" : L"Same Fill & Stroke");
    AppendMenuW(hSelectSame, MF_STRING, static_cast<UINT_PTR>(MenuActionId::EditSelectSameColorSpace),
                is_vi ? L"Cùng hệ màu (Same Color Space)" : L"Same Color Space");
    AppendMenuW(root_menu, MF_POPUP, reinterpret_cast<UINT_PTR>(hSelectSame),
                is_vi ? L"Chọn cùng mẫu (Select Same)" : L"Select Same");

    HMENU hColor = CreatePopupMenu();
    AppendMenuW(hColor, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ColorConvertSelectionToCmyk),
                is_vi ? L"Chuyển đối tượng này sang CMYK" : L"Convert Object to CMYK");
    AppendMenuW(hColor, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ColorConvertSelectionToRgb),
                is_vi ? L"Chuyển đối tượng này sang RGB" : L"Convert Object to RGB");
    AppendMenuW(hColor, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hColor, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ColorConvertActivePageToCmyk),
                is_vi ? L"Chuyển trang hiện tại sang CMYK" : L"Convert Active Page to CMYK");
    AppendMenuW(hColor, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ColorConvertActivePageToRgb),
                is_vi ? L"Chuyển trang hiện tại sang RGB" : L"Convert Active Page to RGB");
    AppendMenuW(hColor, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ColorConvertDocumentToCmyk),
                is_vi ? L"Chuyển toàn bộ file sang CMYK" : L"Convert Entire Document to CMYK");
    AppendMenuW(hColor, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ColorConvertDocumentToRgb),
                is_vi ? L"Chuyển toàn bộ file sang RGB" : L"Convert Entire Document to RGB");
    AppendMenuW(root_menu, MF_POPUP, reinterpret_cast<UINT_PTR>(hColor),
                is_vi ? L"Chuyển hệ màu (Convert Color Mode)" : L"Convert Color Mode");

    AppendMenuW(root_menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(root_menu, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectProperties),
                is_vi ? L"Thuộc tính đối tượng… (Properties…)" : L"Properties…");

    return root_menu;
}

bool NativeWindow::show_context_menu(int screen_x, int screen_y, int canvas_x, int canvas_y) {
    if (!hwnd_) return false;

    // 1. Smart Hit-Testing & Auto-Selection (CorelDRAW & SketchUp style)
    const auto snapshot = root().document().current_snapshot();
    const auto world_pt = canvas_to_world(canvas_x, canvas_y);
    const double zoom = root().renderer().camera().zoom();
    const double tol_mm = (zoom > 0.0) ? (4.0 / zoom) : 1.0;
    auto hit_id = vector::SelectionModel::hit_test(world_pt, *snapshot, tol_mm);
    auto& sel_model = root().vector_controller().selection();
    auto current_selected = sel_model.selected_ids();

    const bool is_hit = hit_id.has_value();
    if (is_hit) {
        const bool already_selected = std::find(current_selected.begin(), current_selected.end(), *hit_id) != current_selected.end();
        if (!already_selected) {
            sel_model.clear();
            sel_model.select(*hit_id, vector::SelectionMode::Replace);
            current_selected = sel_model.selected_ids();
            InvalidateRect(hwnd_, nullptr, FALSE);
            UpdateWindow(hwnd_);
        }
    } else {
        if (!current_selected.empty()) {
            sel_model.clear();
            current_selected.clear();
            InvalidateRect(hwnd_, nullptr, FALSE);
            UpdateWindow(hwnd_);
        }
    }

    // 2. Build the context menu
    HMENU hMenu = build_context_menu(current_selected, is_hit);
    if (!hMenu) return false;

    // 3. Track Popup Menu
    const int action = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                                      screen_x, screen_y, 0, hwnd_, nullptr);
    DestroyMenu(hMenu);

    // 4. Dispatch the action
    if (action > 0) {
        execute_menu_action(action);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    return false;
}

#endif // _WIN32

} // namespace pikdraw::desktop
