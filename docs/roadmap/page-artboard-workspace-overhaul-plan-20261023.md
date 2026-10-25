# Đại tu Page/Artboard, canvas và vùng đáy editor

Ngày: 23/10/2026. **Đã triển khai Page/Artboard cơ bản; nghiệm thu hiển thị và một số workflow còn mở.**
Phần mở rộng của [kế hoạch Select/toolbox](select-tool-toolbox-interaction-plan-20261023.md).
Snap theo cạnh/tâm/khoảng cách và đường gióng Artboard tuân theo
[kế hoạch Snap/Smart Guides](snap-smart-guides-overhaul-plan-20261023.md), dùng chung context và transform của tài liệu này.
Kế thừa mục tiêu Page/Artboard trong [roadmap import đa trang](import-font-pages-artboards-20261020.md),
nhưng dùng source hiện tại để xác định capability và version, không sao chép snapshot cũ thành trạng thái hôm nay.

## 1. Yêu cầu và quyết định sản phẩm

1. Có hai chế độ hiển thị trong Settings: **Trang — tập trung** và **Artboard — tổng quan**.
   Người dùng có thể làm việc trên vùng canvas/pasteboard rộng hoặc xem nhiều thiết kế cùng lúc.
2. Có nút **thêm, xóa, nhân đôi**, điều hướng đầu/trước/sau/cuối, tên và thứ tự Page/Artboard dễ tìm.
3. Artboard là vùng xuất của artwork dùng chung trong một scene; không chỉ là thumbnail của các trang độc lập.
4. Hai chế độ dùng cùng tài liệu, IDs, selection, commands và Undo. Đổi chế độ xem không tự chuyển đổi artwork,
   hợp nhất page roots, nhân bản object hoặc thay kết quả export.
5. Import/Open, lưu native, export theo trang/artboard và các tool canvas phải hiểu đúng context.
6. **Bỏ hiển thị Theme, GPU và X/Y con trỏ ở status bar.** Giữ Theme trong Settings;
   thông tin backend thuộc diagnostics; nhập X/Y của artwork tiếp tục ở property bar/Properties.

Ảnh 1 được dùng làm tham khảo cho navigator tên trang + số trang + nút thêm và palette tách biệt.
Ảnh 2 xác định ba cụm cần bỏ ở footer hiện tại. Không lưu hai ảnh tạm vào repository hoặc đưa vào package.

### Tham khảo chính thức

- [CorelDRAW — Page views](https://product.corel.com/help/CorelDRAW/Documentation-Windows/CorelDRAW-en/CorelDRAW-Page-views.html):
  một trang/nhiều trang, điều hướng bằng tab và sắp trang; Corel cũng có multipage view, không đồng nhất Corel với chỉ một trang.
- [CorelDRAW — Add, duplicate and delete pages](https://product.corel.com/help/CorelDRAW/540111148/CorelDRAW-en/CorelDRAW-Add-duplicate-and-delete-pages.html):
  quản lý trang qua các thao tác rõ ràng, có chèn trước/sau.
- [Illustrator — Rearrange artboards](https://helpx.adobe.com/ca/illustrator/desktop/create-manage-artboards/organize-manage-artboards/rearrange-artboards.html):
  bố trí hàng/cột, khoảng cách và tùy chọn di chuyển artwork cùng artboard.
- [Illustrator — Export artwork](https://helpx.adobe.com/illustrator/using/exporting-artwork.html):
  phạm vi artboard/range và xuất theo vùng; không đồng nhất export với ảnh màn hình.

Đã tra ngày 23/10/2026. Các quyết định về model, defaults và thao tác cụ thể dưới đây thuộc PikDraw;
không tuyên bố tương đương toàn bộ hai sản phẩm tham khảo.

## 2. Kiểm tra source hiện tại

| Miền | Có trong source | Khoảng thiếu cần xử lý |
| --- | --- | --- |
| Document | `DocumentPage` có PageId, name, layout, page-local objects, PDF source và import reports; snapshot nhiều trang | Chưa có Artboard riêng trong struct này; không được giả trang độc lập thành scene artwork dùng chung. |
| Page commands | `page_commands.hpp` có Add/Remove/Resize/Reorder/Duplicate; Remove giữ ít nhất một trang và chọn trang kế cận | Tái dùng commands; bổ sung rename/chọn nhiều/context UI khi cần. |
| Menu | `native_window_menus.hpp:514–518` có trước/sau/thêm/nhân đôi/xóa; handler ở `native_window_menu_actions.cpp` | Thiếu navigator theo ảnh và state thống nhất giữa menu/navigator/Properties. |
| Navigation/camera | `finish_page_action` gọi `fit_page_to_canvas()` khi đổi trang | Không ghi đè zoom/pan người dùng mỗi lần chuyển; thêm Fit Active/Fit All độc lập. |
| Canvas | `native_window_canvas.cpp` render snapshot và kẻ bounds bằng `snapshot->page()` | Cần layout nhiều scene/region, hit test, culling và overlays tương ứng; không chỉ loop vẽ nhiều hình chữ nhật. |
| Native storage | `native_format.hpp:58` hiện trỏ `kCurrentFormatVersion` tới LayerProperties, tức **v22** | Lấy version mới tiếp theo khi triển khai. Roadmap 20/10 còn ghi v9/v10 là lịch sử, không phải nền hiện tại. |
| Import | Workflow có `ImportedPageContent`; PDF/AI page-set hiện đã có đường nhiều trang trong capability matrix | Giữ mapping nguồn và PageId; thêm region/context, không làm lại parser đa trang theo snapshot cũ. |
| Export | SVG có active/selection/all page path; Raster UI có active/all, lấy danh sách page trước khi chạy | Mở rộng selected/range/artboard bounds và schema target chung; không giả Raster hiện có range/artboard. |
| Footer | `native_window_paint_status.cpp:128–172` vẽ tọa độ/Theme/GPU; hint bắt đầu x=695 | Gỡ ba cụm và thu hồi không gian; layout responsive, không để khoảng trắng 695 DIP. |
| Tooltip/footer hit | `native_window_tooltips_guides.cpp:680–692` có tọa độ/Theme và zoom; chrome click có zoom ở `967–979` | Xóa tooltip/focus/hit cũ thuộc ba cụm, giữ zoom bằng metrics chung; hiện không thấy Theme/GPU click action trong đoạn footer này. |

Các kết luận là source audit. App đang mở chưa được điều khiển trong lượt này.
Tài liệu dependency chuẩn vẫn là `registry/modules.json`; core/document/rendering không import desktop.

## 3. Model: phân biệt Page, Scene, Artboard và View

### 3.1. Kiến trúc được chọn

- **Page/scene:** giữ `DocumentPage` và PageId làm đơn vị nội dung độc lập hiện có.
  Đặt tên Scene trong hợp đồng để chỉ không gian artwork của một page; chưa tạo một SceneId thứ hai nếu PageId đủ định danh.
- **Artboard:** thêm record có ArtboardId ổn định, owner PageId, tên Unicode, rect mm trong scene, thứ tự,
  liên kết optional với page bounds và metadata nguồn. Nhiều artboard của cùng scene nhìn cùng object map.
- **Workspace placement:** ánh xạ scene-local → workspace-world để bố trí các scene độc lập trên canvas tổng quan.
  Không cộng offset vào từng object mỗi frame và không flatten hierarchy.
- **View:** mode, camera, active PageId/ArtboardId, focus, hover và các lựa chọn tạm.
  Mode/camera thay đổi không làm dirty document, không tăng revision hoặc thêm Undo.
- **Document edits:** geometry/name/order của trang/artboard và vị trí scene được lưu là command có Undo.
  Phân biệt rõ camera/view layout tạm với action “Sắp xếp và lưu bố cục”; không trộn hai loại vào một nút.

Mỗi page cũ nhận một default artboard liên kết page bounds khi migration.
Đổi page size cập nhật artboard còn liên kết; sửa riêng artboard phải tách liên kết có chủ đích.
Trang PDF khóa vẫn là nguồn tham chiếu khóa; không biến bytes/preview của nó thành artwork editable.

**Ranh giới quan trọng:** tổng quan nhiều page-scene độc lập và nhiều artboard dùng chung scene là hai tình huống khác nhau.
Overview của pages là bước tích hợp có ích, nhưng chưa đạt phần Artboard nếu thiếu shared object map, vùng chồng và command/export.
Không tự hợp nhất các scene khi người dùng bật chế độ Artboard. Chuyển artwork giữa scenes là command riêng có mapping layer/reference.

### 3.2. Phạm vi artwork và canvas

- Pasteboard là vùng vẽ thật thuộc scene đang active: object ngoài page/artboard được giữ, chọn, sửa, Undo và lưu/mở lại.
- Canvas rộng dùng double và camera-relative origin trước khi đổi sang float; vẫn tuân giới hạn số học/geometry hiện có.
  Không hứa canvas vô hạn hoặc tăng giới hạn bằng cách chỉ bỏ clamp.
- Trong overview, một scene được dựng **một lần**; artboard backgrounds/borders/labels là view overlays.
  Object cắt qua hai artboard không được vẽ/composite hai lần trên màn hình.
- Artwork không bị cắt ở mép artboard khi đang thiết kế, trừ view “Preview vùng xuất” bật rõ ràng.
  Export mới áp dụng clip bounds được chọn; bóng/blur ngoài bounds có policy clip/bleed minh bạch.
- Culling xét bounds artwork gồm stroke/effect và workspace placement; không loại object chỉ vì nằm ngoài page rect.
- Hàng/cột/khoảng cách mặc định là presentation; command sắp artboard đổi rect thật có preview và một Undo.

## 4. Hai chế độ và Settings

| Chế độ | Canvas và điều hướng | Quy tắc dữ liệu |
| --- | --- | --- |
| Trang — tập trung | Một Page/scene đang làm việc, navigator tên trang như ảnh 1; pasteboard rộng quanh trang; camera nhớ theo page | Không sửa artwork ở page khác; shared artboard của scene vẫn được giữ trong model, chọn vùng active qua navigator/Properties. |
| Artboard — tổng quan | Hiện artboards của scene và các scene khác đã bố trí trong workspace; Fit All/fit active; nhãn tên/số rõ; pan/zoom liên tục | Artwork cùng scene dùng chung; click chuyển scene có policy tool và context thật; không giả cùng ownership vì cùng màn hình. |

Settings dùng `Settings > Preferences > Canvas & Pages` trong dialog hiện có:

| Field | Scope / mặc định / áp dụng |
| --- | --- |
| Chế độ mở mặc định | App; `Trang — tập trung`; chỉ tài liệu mở mới nếu không có workspace state đã lưu. |
| Chế độ tài liệu đang xem | View workspace của tab; đổi ngay sau khi gesture/input xử lý thành công; không dirty/Undo. Có entry `View > Page View` và `View > Artboard Overview` dùng cùng action. |
| Bố trí tổng quan | View preset hàng/cột/lưới/tự do, khoảng hở dùng đơn vị trang active; default lưới, gap 20 mm là quyết định PikDraw cần thử UX. Không tự di chuyển object. |
| Nhớ góc nhìn theo trang | App preference, mặc định bật; lần đầu fit vùng active, lần sau khôi phục camera. Fit All không sửa camera riêng của từng page. |
| Hiện tên/viền artboard | App/view, bật; không làm xuất tên/viền vào file. |
| Di chuyển artwork cùng artboard | Tool preference, mặc định tắt; hiện rõ trên Properties, không tự bật khi đổi chế độ. |

Mode không phải tên file format và không phụ thuộc shortcut profile Corel/Illustrator.
Không tự thay size, DPI, ICC, đơn vị, thứ tự xuất hoặc capability chỉ vì đổi chế độ hiển thị.

## 5. Navigator, thêm/xóa và công cụ Artboard

### 5.1. Footer theo ảnh tham chiếu

```text
Canvas / pasteboard
[đầu][trước] [2 / 3] [sau][cuối] [+][-] [Page 1][Page 2][Page 3] [overflow]
Palette hiện có, nếu workspace đang bật
Status: hint công cụ / selection / cảnh báo cần xử lý                         Fit  Zoom
```

Navigator là một vùng quản lý trang gọn khoảng 24–28 DIP trong footer; không là thanh thuộc tính mới.
Lý do tách khỏi status: tên trang dài, nhiều tab và thao tác thêm/xóa phải truy cập được, không đẩy mất hint công cụ.
Tính cả vùng này trong `ShellGeometry/canvas_rect`, rulers, hit test, popup anchors và dock; không vẽ đè artwork.
Giữ palette và document tabs hiện có. **Document tab khác Page tab**; nút `+` phải có tooltip đúng “Tài liệu mới” hoặc “Thêm trang/artboard”.

- Ở Page view, navigator liệt kê pages; ở Artboard overview liệt kê artboards với scene/page cha khi cần phân biệt.
- Bấm tên chỉ activate, không đổi tool hoặc selection sang object đầu tiên. Double-click tên sửa tên Unicode.
- Có context menu Insert Before/After, Duplicate, Rename, Delete, Properties và Export Target.
- Drag tab reorder thay thứ tự logic/export; kéo khung trên canvas thay tọa độ. Hai thao tác có tên và Undo riêng.
- Tên dài ellipsis + tooltip/focus đầy đủ; nhiều mục dùng horizontal scroll/overflow; thao tác đầu/cuối vẫn truy cập được.
- Dùng Up/Down hoặc Left/Right trong navigator, Enter activate, Esc trả canvas; không để phím Delete xóa artwork
  khi focus đang ở page/artboard control. Click vùng footer không xuyên xuống Select.

### 5.2. Commands và semantics

| Thao tác | Quyết định |
| --- | --- |
| Add Page | Chèn sau active page; mặc định kế thừa size/unit/color/DPI của trang active qua policy tạo trang; blank artwork; PageId/default ArtboardId mới; một Undo. |
| Delete Page | Xóa page cùng nội dung thuộc page bằng command hiện có; nhãn/xác nhận khi có nội dung phải nêu “Xóa trang và N đối tượng”; Undo khôi phục IDs/metadata. Trang cuối disable với lý do. |
| Duplicate Page | Clone artwork/hierarchy bằng API hiện có, IDs object/page mới, asset immutable có thể chia sẻ; không sửa bản gốc; kiểm liên kết dimension/text/clip/effect. |
| Add Artboard | Tạo vùng mới trong scene đang active; label nút ghi rõ Artboard; size từ active region, đặt cạnh vùng active theo gap; không tạo Page độc lập hoặc duplicate artwork. |
| Delete Artboard | Xóa vùng xuất, **giữ artwork**; không chọn object rồi Delete thay thế. Giữ ít nhất một artboard của scene ở phiên bản đầu, disable mục cuối kèm lý do. |
| Duplicate Artboard | Mặc định chỉ nhân vùng; “Nhân cả artwork” là lựa chọn riêng và clone qua command, không dùng hai artboard trỏ tới bản clone giả như cùng scene. |
| Move/Resize Artboard | Mặc định đổi rect, không scale/move artwork. “Di chuyển cả artwork” tác động mỗi top-level root đủ điều kiện đúng một lần; preview và một Undo. |
| Tập artwork đi cùng | Default top-level roots nằm hoàn toàn trong rect nguồn theo geometry bounds; root cắt biên không tự chuyển, hiện số mục bị bỏ qua. Group/PowerClip di chuyển nguyên root; nhiều vùng chồng deduplicate IDs. |
| Reorder | Chỉ đổi thứ tự danh sách/export, không thay z-order artwork, tọa độ khung hoặc scene ownership. |
| Move artwork sang scene khác | Action rõ ràng, transaction nguồn+đích; layer/source metadata và liên kết phải validate. Không chuyển ownership âm thầm khi object cắt qua artboard. |

Artboard Tool có canvas handles cho tạo/move/resize và label vùng; Select giữ vai trò chọn artwork.
Đổi active artboard không đồng nghĩa activate Artboard Tool; click trống không tự quay Quick Shapes.
Ẩn tool trong toolbox vẫn truy cập được qua menu. Chỉ thêm slot khi action/capability đã có, không dựng nút giả.

### 5.3. Phiếu UI

| Bề mặt | Menu/action/giá trị |
| --- | --- |
| Page actions | Giữ `Layout > Add Page / Duplicate Page / Delete Page / Previous Page / Next Page`; nâng thành semantic ActionIds `action.layout.page.*` nơi chưa có, menu/navigator dùng cùng command. |
| Artboard actions | `Layout > Artboards > New / Duplicate / Rename / Delete / Arrange`; IDs `action.layout.artboard.*`. `Tools > Artboard Tool` / `action.tool.artboard`; Shift+O kích hoạt công cụ. |
| View actions | `View > Page View`, `View > Artboard Overview`, `View > Fit Active`, `View > Fit All`; IDs `action.view.page_mode`, `action.view.artboard_mode`, `action.view.fit_active`, `action.view.fit_all`. |
| Properties | `Layout > Page Setup…` focus Page; `Object > Properties` khi chọn khung focus Artboard; `Window > Properties` mở dock khi đóng. Chia General/Size/Placement/Export; action mở panel không thêm Undo. |
| Property bar | Tool Artboard: X/Y/W/H và nút Properties; name/order cùng Add/Duplicate/Delete/Arrange/Export trong dock. Object Select vẫn thao tác artwork. |
| Fields | W/H dương hữu hạn trong giới hạn geometry; X/Y dùng đơn vị hiện hành và cho tọa độ âm trong giới hạn; Name Unicode không dùng làm authority; order integer; mixed/locked/source-reference hiển thị đúng. |
| Scope | Size/position/name/order là document edits; active region/mode/camera là view; mặc định view/tool là app preference; page unit/color/DPI theo page, không tự đổi theo region. |
| SVG/icon | Tái dùng icon add/delete/duplicate/navigation; artboard outline + handles theo hệ icon hiện có. IconId khác ToolId; nguồn/license asset trong registry, painter phải dùng asset thật. |
| Tooltip/focus | Hai dòng, tên + binding thật, tác dụng. Delete Page ghi “Xóa trang cùng nội dung”; Delete Artboard ghi “Xóa vùng xuất, giữ artwork”. |
| Status/help | Artboard: “Kéo khung để đổi vùng xuất · Artwork giữ nguyên · Esc hủy”; khi move-content bật, hint đổi tương ứng. Hướng dẫn 3–5 bước trong feature docs, không nhồi lên property bar. |
| Không áp dụng | Không thêm theme/backend switch vào footer; không panel diagnostics mặc định; không đổi toàn bộ bố cục/menu top-level hoặc xây mock HTML. |

## 6. Select/toolbox, tọa độ và lifecycle

Edit context phải gồm document identity/epoch, PageId của scene, optional ArtboardId,
scope group/PowerClip, revision và interaction generation; không suy đích chỉ từ `active_page()` ở lúc pointer-up/job hoàn tất.

- Resolve hit theo chuỗi: chrome/footer → artboard handle nếu tool Artboard → scene/object → scope picker → canvas trống.
  Chồng artboards trong cùng scene không nhân object hits. Resolve scene/object từ vị trí thật, không từ tab cũ.
- Nhãn/viền vùng là target kích hoạt region; artwork vẫn dùng Select/Node/Text. Artboard selection và object selection là hai kiểu context,
  không nhét ArtboardId vào ObjectId rồi gọi DeleteObject.
- Trong cùng scene, object có thể nằm ngoài mọi artboard hoặc giao nhiều vùng; marquee/transform không bị giới hạn bởi active artboard.
- Multi-scene selection phải mang PageId từng target. Move/copy hàng loạt chỉ enable khi command hỗ trợ atomic context nhiều scene;
  group/PowerClip không hỗ trợ xuyên scene thì disable có lý do, không đổi ownership để chạy cho qua.
- Page view ruler origin theo active page/region; overview có workspace origin và chuyển rõ sang local region cho field Artboard.
  Display origin không sửa tọa độ nguồn; source→scene→workspace→screen dùng cùng transform cho render, pick, snap, overlays và import placement.
- Chuyển page/mode đang kéo phải chạy policy trong kế hoạch Select: validate field/IME, finish/cancel/suspend đúng tool;
  nếu bị chặn giữ view và active IDs. Không dùng đổi view để né bảo vệ Pen một điểm/PDF content lock.
- Đổi artboard trong cùng scene khi idle không hủy selection artwork vô cớ; chuyển scene lọc selection theo scope,
  không giữ IDs cũ như đang thuộc page mới. Khôi phục camera chỉ sau transition thành công.
- Context mất hiệu lực khi delete scene/region hoặc undo thay snapshot: refresh Properties/snap/culling/cache;
  event/job mang generation cũ không được commit vào page/region mới.

## 7. Import/Open và export phải dùng cùng vùng đích

### 7.1. Import/Open

- Open tạo document với đúng page-set đã chọn, giữ trang rỗng, thứ tự, kích thước/rotation/source mapping và page reports.
  Mỗi page được cấp default artboard liên kết; chế độ xem chỉ quyết định cách bố trí lúc mở.
- Import có lựa chọn rõ: **Đặt artwork vào scene hiện tại** hoặc **Thêm thành các trang**;
  nguồn nhiều trang phải chọn pages/range hoặc placement group rõ, không đè lẫn nhiều page vào cùng tọa độ.
- “Tạo các artboard trong scene hiện tại” cần command normalize từng source page bằng placement matrix,
  validate layer/clip/font/effect và phát hành toàn bộ bằng một transaction; không chỉ đổi label page thành artboard.
- Capture target PageId/ArtboardId và options generation khi bắt đầu. Đổi active page không tự chuyển đích job;
  xóa đích/stale revision phải reject/cancel theo policy host, không fallback sang page đang active.
- Không dùng số trang PDF representation làm số artboard authoring AI. Metadata unknown vẫn unknown.

| Format/representation và version profile hiện tại | Ánh xạ dự kiến | Gate riêng cần giữ |
| --- | --- | --- |
| PDF qua PDFium pinned, page-set subset | PageId cho từng source page; artboard mặc định theo box đã normalize | Page boxes/rotation/UserUnit, blank pages, names, source locator và intent editable/reference; font/ICC/effect không tự đầy đủ vì có nhiều trang. |
| AI PDF-compatible page-set | Như PDF, ghi đúng representation | Không khai private authoring artboards/shared scene nếu reader chưa lấy được. AI authoring/legacy cần adapter riêng mới có thể giữ authoring layout. |
| CDR RIFF/ZIP qua libcdr 0.1.9, SVG normalization | Giữ subset page/group/clip thực sự reader phát được | Capability hiện vẫn bounded một trang; đa trang CDR là dependency adapter riêng nếu muốn hỗ trợ, không tuyên bố hoàn tất bằng UI artboard. |
| EPS DSC allowlist; DOS EPS qua Ghostscript/PDF | Vùng theo BoundingBox hoặc page representation đã kiểm | Không bịa nhiều artboards nguồn; giữ cảnh báo mất feature/metadata và dependency Ghostscript. |
| SVG subset qua SVG adapter | viewBox/physical size → vùng mặc định; artwork normalize như hiện có | Metadata nhiều trang không chuẩn phải có adapter/profile riêng; không đếm group thành artboard. |
| DXF 2D subset qua DXF adapter | Geometry/units giữ nguyên; vùng mặc định hoặc bounds theo lựa chọn | Không tự coi model/paper-space layouts là artboards đã hỗ trợ; sản xuất DXF giữ gate độc lập. |

Adapters theo format/version vẫn riêng; sau normalize dùng cùng document commands/edit/render.
Tuân [hướng dẫn vector](../architecture/vector-resource-interoperability.md), [C8](../../contracts/c8-import-fidelity.md),
[C9](../../contracts/c9-appearance-effects.md). Không tải fixture mạng không rõ quyền hoặc nâng capability từ việc UI trông đúng.

### 7.2. Export

- `ExportTarget` cần PageId/SceneId, optional ArtboardId, bounds mm, source snapshot revision, clipping/bleed,
  DPI/ICC/background và thứ tự; schema có version. Cả SVG và Raster dùng cùng resolver target.
- UI chọn **Trang hiện tại / Trang đã chọn / Tất cả trang / Dải trang / Artboard hiện tại / Artboards đã chọn / Tất cả artboards / Selection**;
  writer chỉ hiện các scope nó thực hiện được. Có preview tên/kích thước/số file trước xuất.
- Đổi view không đổi selection export đã chốt; export dùng một immutable snapshot. Edit tiếp sau không lọt vào giữa batch.
- Artboards chồng có thể cùng chứa một artwork trong hai output khác nhau; đó là hai lần render theo vùng xuất,
  không phải hai object lưu trong document. Pasteboard không tự xuất trừ Selection/Artwork Bounds được chọn.
- SVG tạo viewBox/clip/translation từ vùng đích, giữ kích thước vật lý. Raster tính pixel từ bounds mm × DPI,
  dùng renderer/source chất lượng xuất, không dùng preview canvas/thumbnail/cache đã giảm chất lượng.
- PDF khóa để in tiếp tục lấy source page đúng index/box và color policy đã có; không mở khóa hoặc reshape nội dung để bố trí overview.
  Artboard edit làm thay crop/source semantics ngoài capability phải disable hoặc yêu cầu conversion rõ ràng theo workflow sẵn có.
- Định dạng không chứa nhiều trang xuất nhiều file theo order ổn định, tên Unicode được sanitize/chống trùng;
  báo overwrite trước, staging/atomic publish/cancel không để báo thành công batch thiếu file.
- Không suy PDF/CDR/AI writer từ importer. Writer chưa có thì scope tương ứng disabled có lý do.
  Tách appearance, editability, source metadata, round-trip và production; export ảnh đẹp không chứng minh CNC/spot separations.

## 8. Dọn status bar đúng phạm vi

1. Gỡ tọa độ con trỏ, Theme badge và GPU/backend badge trong `paint_status_bar`.
2. Gỡ mô tả tooltip, focus/accessibility node và hit region tương ứng nếu có; cập nhật test để không còn vùng tương tác vô hình.
3. Tính lại layout: hint từ vùng trái còn lại, selection/cảnh báo ưu tiên, zoom/fit bên phải;
   dùng shared metrics cho paint/click/tooltip, không giữ các mốc x=215/390/535/695.
4. Giữ thông tin proof/color có ý nghĩa đang được dùng, nhưng bố trí gọn và ưu tiên cảnh báo khi hẹp;
   không mở rộng yêu cầu thành xóa preview màu hay đổi renderer.
5. Theme vẫn chỉnh trong Settings; backend diagnostics chỉ hiển thị theo yêu cầu ở bề mặt diagnostics phù hợp.
   Tọa độ object/numeric HUD cần cho thao tác chính xác vẫn giữ; chỉ bỏ tọa độ con trỏ thường trực dưới đáy.
6. Chuột trỏ vào vùng ba badge cũ không hiện tooltip hoặc tác động tới tool. Keyboard navigation bỏ qua controls đã gỡ.

## 9. Các gói triển khai và đường dẫn

| Thứ tự | Việc thực hiện | Source dự kiến | Gate |
| --- | --- | --- | --- |
| W0 | Sửa Select P0 và gỡ ba cụm status; footer metrics chung | Các file Select trong plan chính; `native_window_paint_status.cpp`, `native_window_tooltips_guides.cpp`, `native_window_chrome_clicks.cpp`, `status_bar_model.*` | Click trống vẫn Pick; không badge/tooltip/hit cũ; hint và zoom không đè nhau. |
| W1 | Navigator + page CRUD đầy đủ, Properties, camera state | Mới `apps/desktop/src/shell/native_window_page_navigation.*`; menus/actions, `workspace_state.*`, `shell_geometry.*`, page commands/tests | Nút/menu/context cùng state và Undo; khác document tab; add/delete cuối/undo/camera. |
| W2 | Model Artboard, scene placement, persistence/migration | `core/document/.../model/document_page.hpp`; mới `model/artboard.hpp`, `commands/artboard_commands.hpp/.cpp`; snapshots/document/storage; CMake | Shared artwork không duplicate; IDs/rect/order/provenance tồn tại sau save/reopen; reader cũ reject schema mới rõ. |
| W3 | Overview renderer/picking/snap/overlays và Artboard Tool | `core/rendering/include/pikdraw/rendering/renderer.hpp`, `core/rendering/src/renderer.cpp`, rendering context/cache; mới `apps/desktop/src/tools/artboard_tool_controller.*`; native canvas/selection | Một render mỗi scene, artwork ngoài khung, vùng chồng, precision/culling và cancellation đúng. |
| W4 | Settings/view mode + Properties + đồng bộ các tool | settings/workspace store hiện có; `native_window_paint_settings_modal.cpp`, `native_window_settings.cpp`, header/action catalog, tool adapters | Đổi view không dirty/Undo; giữ active IDs/camera/context; không commit draft sai scene. |
| W5 | Import target mapping và ExportTarget chung | `document_workflow_import.cpp`, `native_window_import.cpp`, import/page commands; `modules/interop/.../export/*`, SVG/Raster shell/writers | Source mapping/atomic import; active/selected/range/all/artboard export đúng bounds/snapshot. |
| W6 | Nghiệm thu UI và tích hợp | Test hiện có của document/rendering/vector/interop/desktop; cập nhật feature docs, CMake khi có source mới | Các AC trọng yếu bên dưới có bằng chứng thực; dọn output tạm. |

Kiểm lại paths thực tế trước sửa; không tự tạo một renderer thứ hai vì tên file khác dự kiến.
Chia file theo domain, tối đa 2.000 dòng; mở rộng target test hiện có, tránh test `.cpp` dùng một lần.
Làm tuần tự các hợp đồng model/storage/renderer trước consumers; không tạo worktree/nhánh/task lease.
Không commit kèm local edits không thuộc thay đổi. Mỗi gói mới chỉ được đánh dấu xong theo gate thực tế;
W1 nhiều tab hoặc W3 nhiều khung không thay cho W2/W5 Artboard hoàn chỉnh.

## 10. Nghiệm thu bắt buộc

| AC | Given / When | Then / oracle |
| --- | --- | --- |
| PA-01 | Ba pages khác kích thước, tên tiếng Việt; dùng nút đầu/trước/sau/cuối, tab và menu | Cùng PageId active, fields/unit đúng; không đổi tool/geometry; không lẫn document tabs. |
| PA-02 | Thêm/chèn/đổi tên/reorder/duplicate/delete Page, Undo/Redo | Size/unit/color/DPI, order, IDs và hierarchy đúng; duplicate IDs mới; Undo xóa khôi phục IDs cũ; trang cuối không xóa được. |
| PA-03 | Xóa Artboard chứa artwork; duplicate vùng có/không artwork | Artwork được giữ khi xóa vùng; duplicate/move-content theo đúng lựa chọn; một Undo; không xóa nhầm selection object. |
| PA-04 | Bật Page ↔ Artboard view 20 lần rồi save/reopen | Object count/IDs/source geometry/color/hash không đổi vì mode; mode/camera restore theo workspace policy; document edits vẫn round-trip. |
| PA-05 | Hai artboards chồng nhau trong cùng scene, object và shadow đi qua cả hai | Màn hình không compositing hai lần; Pick một object ID; xuất từng vùng có crop đúng, shadow không bị nhân đôi. |
| PA-06 | Object ngoài mọi vùng, tọa độ âm; pan xa rồi zoom fit | Chọn/sửa/Undo/lưu/mở lại được; không bị clamp vào page; fit active/all/artwork có phạm vi rõ; precision không trôi. |
| PA-07 | Move/resize/rearrange artboards, có root giao biên và root thuộc nhiều vùng | Mặc định chỉ đổi rect; move-content đúng tập roots và mỗi ID một lần; Preview/Esc không sửa source; reorder không đổi z-order. |
| PA-08 | Pen/Text/Node/Gradient/Contour/Knife/Dimension/PowerClip đang draft | Chuyển scene/mode hoặc xóa đích: policy đồng nhất với plan Select; no wrong-page commit, no lost IME, stale/capture loss bị xử lý. |
| PA-09 | Overview nhiều scenes; click label/artwork, marquee, multi-target command | Hit scene/context đúng; artboard khác object selection; unsupported cross-scene group disable rõ; không tự đổi ownership. |
| PA-10 | PDF/AI page-set có trang rỗng/xoay/khổ khác, CDR/SVG/EPS/DXF fixtures hợp lệ | Đúng số representation pages, size/transform/order/source metadata; không giả artboard authoring; object editable/reference đúng capability. |
| PA-11 | Import đang chạy; chuyển trang, reorder hoặc xóa target | Đích job giữ PageId đã chốt; stale/deleted target reject atomically; không import sang active page khác. |
| PA-12 | Export active/selected/range/all pages và artboards, đổi view khi job chạy | Đúng số file/order/bounds/mm/DPI/ICC, snapshot nhất quán; không export pasteboard ngoài lựa chọn; cancel/failure có kết quả đúng. |
| PA-13 | PDF source khóa để in; xem overview/đổi trang/xuất raster | Source bytes/index/box giữ nguyên; tool writes bị chặn; output lấy nguồn chất lượng xuất, không canvas preview; không tự mất lock. |
| PA-14 | Native legacy document và native schema mới, Undo/save/reopen/recovery | Migration từ version hỗ trợ hiện tại, không dùng v9 giả làm latest; stable IDs/regions/provenance/units; corruption/dangling owner bị reject. |
| PA-15 | Status bar mới, pointer/keyboard đi qua vị trí badge cũ | Không Theme/GPU/tọa độ/tooltip/focus cũ; hint/zoom/proof còn dùng được; không click xuyên canvas. |
| PA-16 | 1280×720/1366×768, 100/125/150/200% DPI, light/dark, nhiều tên dài | Navigator overflow/focus và zoom/hint không chồng; canvas rect/ruler/palette/dock/hit thống nhất; tooltip đọc đủ tên. |
| PA-17 | 1/10/100/500 vùng; nhiều artwork nhưng phần lớn ngoài viewport | Đo input→visible, frame p50/p95, RAM cold/warm và cache khi pan/zoom/switch; culling đúng oracle, không build full-resolution bitmap cho mọi vùng. |

PA-17 dùng baseline cùng fixture/máy trước sửa; mục tiêu regression không quá 10% p95 trên fixture tương đương,
và phản hồi tương tác quan sát được không bị đứng do arrange/render/import. Đây là mục tiêu cần đo, chưa có số chứng minh.
Nếu không đạt, ghi số đo và phần chưa đạt thay vì suy “mượt” từ GPU badge hay render unit test.

Test tập trung mở rộng `test_desktop_shell*.cpp`, `test_canvas_view.cpp`, `test_svg_dxf_import.cpp`,
document `tests/commands/test_commands.cpp`, storage tests, vector selection/input/gesture,
interop `tests/writers/test_export_job.cpp`, `test_svg_exporter.cpp`, `test_raster_image_writer.cpp` và rendering pixel/culling tests.
Tên/path consumer cần kiểm lại trước thêm test; test HWND/pointer + UI hiển thị là gate riêng với unit logic.

Lệnh dự kiến, chỉ chạy subset đúng thay đổi qua runner chuẩn:

```powershell
.\tooling\native\build.ps1 -Test -TestTarget test_pikdraw_desktop_shell,test_pikdraw_canvas_view -TestRegex '^test_pikdraw_(desktop_shell|canvas_view)$'
.\tooling\native\build.ps1 -Test -TestTarget test_pikdraw_document_commands -TestRegex '^test_pikdraw_document_commands$'
.\tooling\native\build.ps1 -Test -TestTarget test_pikdraw_document_storage,test_pikdraw_document_storage_fidelity -TestRegex '^test_pikdraw_document_storage(_fidelity)?$'
.\tooling\native\build.ps1 -Test -TestTarget test_pikdraw_rendering_camera,test_pikdraw_rendering_culling,test_pikdraw_rendering_pixels -TestRegex '^test_pikdraw_rendering_(camera|culling|pixels)$'
.\tooling\native\build.ps1 -Test -TestTarget test_pikdraw_interop_export_job,test_pikdraw_interop_svg_exporter,test_pikdraw_interop_raster_image_writer -TestRegex '^test_pikdraw_interop_(export_job|svg_exporter|raster_image_writer)$'
.\tooling\native\build.ps1 -Test -TestTarget test_pikdraw_desktop_import -TestRegex '^test_pikdraw_desktop_import$'
.\tooling\native\build.ps1 -Target pikdraw
```

Các target trên đã đối chiếu CMake; chọn command/rendering subset theo code bị chạm, không gọi scoped tests là full CTest.
Các lệnh trên là baseline trước triển khai; xem trạng thái hiện hành ở mục 11. Không suy từ test subset
thành full CTest, visual acceptance hoặc xác nhận hiệu năng.

## 11. Trạng thái triển khai — 2026-10-23

Page/Artboard document context và shell đã có: Artboard mặc định, ID/tên/bounds lưu trong native
document, migration, CRUD/duplicate/order command có Undo, footer chuyển Page/Artboard theo chế độ,
Settings Page Focus/Artboard Overview, view fitting, cùng vùng export SVG/Raster theo Artboard đang chọn.
Import tiếp tục commit vào PageId đã chốt trong job; Artboard không giữ bản sao artwork riêng.

Đợt này nối Artboard Tool chuyên dụng vào Toolbox, canvas và Properties: kéo tạo/move/resize, handles,
X/Y/W/H trên property bar, rename và chỉnh bounds trong dock, Add/Duplicate/Delete/Reorder/Export từ
cùng dock. Artboard create/move/resize dùng chung Snap config; resize bắt cạnh đang kéo và giữ cạnh neo.
`test_pikdraw_artboard_tool_ui` xác nhận Toolbox → drag → property-bar X → Undo trên HWND;
`test_pikdraw_artboard_snap_ui` xác nhận cạnh Artboard bắt mép object và Undo giữ bounds ban đầu;
`test_pikdraw_drawing_tool_controller` kiểm chuẩn hóa drag, move/resize, minimum size và giới hạn canvas.
Status bar không còn tọa độ con trỏ, Theme hay GPU badge; page navigation nằm ở hàng riêng.

Kiểm chứng hiện tại: 12/12 test liên quan Snap/Select/Artboard/Quick Shapes/Pen/Node qua trong lượt
2026-10-23. Chưa có pixel oracle trực quan trên canonical app, pixel oracle Raster export, save/reopen
bằng app, mọi DPI, benchmark, move-content/bleed, hay batch export selected/range/all Artboards. Artboard
hiện chia sẻ artwork theo Page; đây là giới hạn model có chủ ý, không phải từng board chứa bản sao.
