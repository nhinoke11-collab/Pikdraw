# Contract C7: Actions, native UI và ngôn ngữ

Version: 1.2.0 (đồng bộ tài liệu; không đổi action IDs/runtime).
Audited: 2026-10-24, source đang làm việc.
Producer: `apps/desktop`. Consumers: native shell, tool/workflow controllers và automation bindings liên quan.
Status: Win32 Native Shell; Qt/QSS/.ui/.rcc chưa phải giao diện runtime.

## 1. Action identity và khả năng thực thi

Action dùng stable string `action.<domain>.<verb>[.<qualifier>]`. Tên hiển thị/ngôn ngữ/shortcut
không phải identity. Giữ ID khi đổi label, menu hoặc icon; đổi semantics cần review consumer.

`ActionId` là danh mục tên, không phải danh sách feature đã chạy. Một action cần được register,
có handler/enable-state đúng context và được gọi từ UI thực tế. Placeholder/status message hoặc
một constant không chứng minh cloud/print/plugin/feature tương ứng đã hoàn thành.

| Nhóm có binding trong source | Ví dụ và nơi kiểm tra |
| --- | --- |
| File/document | `action.file.new`, `.open`, `.save`, `.save_as`, `.import`, `.export`; kiểm workflow và native menu handlers. Design save/open dùng `.pr` theo C5. |
| Edit | `action.edit.undo`, `.redo`, `.cut`, `.copy`, `.paste`, `.delete`; phải dùng document command hoặc clipboard workflow tương ứng. |
| Tool | `action.tool.node_edit`, `.line`, `.polyline`, `.pen`, `.contour` có native registration; kiểm controller và đường pointer/key trước claim hành vi. |
| Snap panel | `action.tool.snap_panel` hiện bind F3 để mở/đóng panel, không phải shortcut snap-master theo đề xuất cũ. |
| Appearance/Properties | Các entry hiện hành đi qua workflow/controller; danh mục action không tự cấp scope group/layer mà model chưa hỗ trợ. |

Line/Polyline hiện register L/Shift+L; Pen/Node không có default shortcut trong đoạn native
registration đó. Không sao chép F-key từ mockup hoặc doc cũ thành binding đã xác minh.
Shortcut runtime còn phụ thuộc remapping, focus và tool session.

## 2. Command routing và UI surfaces

Menu, toolbox/toolbar, contextual property bar và Properties phải gọi chung semantics/validation/
document scope và Undo. Mở panel/chọn tool là interaction state; thay thuộc tính source là
document command. Multi-selection cần phân biệt mixed/disabled/unsupported; không ghi một giá trị
mặc định đè lên toàn selection khi chỉ mở UI.

Feature spec ghi menu path, action ID, toolbox/toolbar, bar/Properties, phạm vi/trạng thái field,
SVG icon, label, tooltip/focus help, status hint và hướng dẫn dùng. Mục không áp dụng ghi rõ.
Áp [UI/UX standard](../docs/design/ui-ux-standard.md), [workspace layout](../docs/architecture/workspace-layout.md)
và [feature UI template](../docs/design/feature-ui-template.md). Tài liệu này không đổi bố cục UI.

`ActionDispatcher` có register/dispatch, enable-state và shortcut map. Register/remap có logic
giải quyết trùng binding; không giả định mọi tool key đi qua map này, vì native pointer/key
handlers còn xử lý draft, text/IME và numeric fields. Luồng tương đương phải được kiểm tra cùng nhau.

## 3. Focus, modifiers và draft

Text/IME/numeric controls sở hữu phím trong scope của mình; canvas chỉ nhận chord đúng context.
Không nuốt AltGr/OS-reserved chords. Reserved Save As Ctrl+Shift+S phải được phân biệt với modifier
đang giữ trong gesture. Một chord không được kích hoạt hai edit ngoài ý muốn.

Snap preferences/direction/masks không dirty document; guide/geometry đã commit thì theo C2.
Screen acquisition radius theo DIP, source/numeric constraints theo mm/C1. Tắt snap acquisition
không tự xóa numeric locks hoặc hình học đã xác nhận. Draft cancel không tạo document Undo.

Drawing semantics và các profile modifier được đặc tả tại [drawing semantics](drawing/semantics.json)
và [drawing README](drawing/README.md). Các đề xuất như Ctrl/Shift constrain theo profile,
Alt center/break handles, temporary Node và held-S bypass phải đối chiếu controller/shortcut
handler trước khi công bố. Đây không phải xác nhận mọi tool đã triển khai toàn bộ profile.
Pan/zoom trong draft cũng cần evidence lifecycle theo C2/C3.

## 4. Development reload: giới hạn thực tế

Runtime hiện là Win32/C++ với native shell/rendering. `UIReloadController` còn tồn tại như helper
kiểm chuỗi QSS/manifest, generation và handler-count; nó không chứng minh desktop parse Qt `.ui`,
dựng/swap Qt widget tree hoặc đóng gói `.rcc` trong Release. Các file mockup/Qt/QSS là tư liệu hoặc
prototype. F5 hot reload toàn giao diện và giữ nguyên mọi state chưa phải bảo đảm của runtime.

Thay đổi native layout/paint/controller đi qua quy trình build native hiện hành. Nếu phát triển
reload thật, phải có rollback layout, focus/IME, selection, camera, draft/history và stale-handler
gates riêng; không lấy thành công của helper làm evidence UX.

## 5. Ngôn ngữ

`UILanguage` và native labels/callbacks hỗ trợ tiếng Việt/English. `set_language` có đường cập nhật
shell; nhiều chuỗi vẫn viết trực tiếp ở source. Các catalog trong `locales/` không phải bằng chứng
toàn bộ text/tooltip/modal được nạp từ JSON hoặc đổi ngôn ngữ đồng bộ ở mọi control.

Label ngắn và Unicode đúng; action ID/units/file schema giữ nguyên khi đổi ngôn ngữ. Chỉ công bố
locale coverage sau audit reachable surfaces, fallback, text clipping, tooltip và focus/IME.

## 6. Automation và feature tương lai

MCP/COM dùng semantic service và document guards, không click theo pixel/đọc label để đoán action.
Xem [automation quickstart](../docs/development/automation-quickstart.md). Action ID UI và tool name
automation là hai API có mapping tường minh, không được tự biến mọi action thành callable write.
Nesting/CNC/3D/raster/AI dùng cùng workflow/Properties khi có scope phù hợp, dependency theo registry;
plugin loader hoặc commercial licensing chưa được xác nhận chỉ từ menu/catalog.

## 7. Source authority và kiểm chứng

- [Action IDs](../apps/desktop/include/pikdraw/desktop/actions/action_id.hpp), [dispatcher](../apps/desktop/src/actions/action_dispatcher.cpp).
- [Native registration](../apps/desktop/src/shell/native_window.cpp), [menu actions](../apps/desktop/src/shell/native_window_menu_actions.cpp), [shortcut handling](../apps/desktop/src/shell/native_window_shortcuts.cpp).
- [Reload helper](../apps/desktop/src/development/ui_reload_controller.cpp).
- Regression liên quan: [desktop shell](../apps/desktop/tests/test_desktop_shell.cpp), [drawing UI](../apps/desktop/tests/test_drawing_tool_ui.cpp), [Pen native shell](../apps/desktop/tests/test_pen_native_shell.cpp).

Source có binding không thay thế kiểm tra thao tác native. Lượt này cập nhật tài liệu, không chạy
lại UI/native tests hoặc tuyên bố mọi menu/shortcut đạt acceptance.
