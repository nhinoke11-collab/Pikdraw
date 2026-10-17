# Nghiệm thu Gradient theo luồng sử dụng

Ngày: 17/10/2026. **Toàn bộ ca dưới đây đang planned/chưa chạy.** Đây là catalog theo rủi ro, không
lệnh bắt mỗi agent chạy tất cả. G01–G11 chọn ca đúng ownership trong [plan](../roadmap/gradient-20261017.md);
UI theo [phiếu](../design/features/gradient.md); cấu trúc theo [chuẩn nghiệm thu](feature-acceptance.md).
Targets/file test Gradient là đề xuất cần G00 đăng ký trước build. Không lưu source/output tạm sau kiểm;
giữ regression test/fixture an toàn có giá trị và report cần review.

## AC-G01 — Giá trị paint và hard edges

Given: Linear 3 stops tại 0/0,5/1, một phiên bản có hai stops cùng offset 0,5; IDs khác nhau.
When: Sample tại endpoints, midpoint, ngay trái/đúng/ngay phải hard edge; thêm/move/delete/reverse hai lần.
Then: Đúng thứ tự tie/midpoint theo plan; không NaN/zero division; IDs cũ giữ, thêm ID mới, Reverse hai lần
phục hồi đúng payload; đổi topology reset midpoint chỉ các interval quy định.
Verify: G01 `core/document/tests/model/test_gradient_paint.cpp`; analytic expected values riêng, không
gọi cùng production sampler để tính expected. Boundary 2/256/257 stops, duplicate/zero ID, infinity,
invalid color/range bị reject. Model test không chứng minh pixels của compositor.

## AC-G02 — Một Undo cho batch, không có partial writes

Given: Hai path khác paint và revision r; thêm tình huống một path locked, ID không tồn tại, một paint invalid.
When: Apply batch hợp lệ; Undo/Redo; apply no-op; thử từng batch lỗi.
Then: Apply một Undo, revision tăng monotonic qua undo/redo; từng đối tượng giữ source geometry/name/colors
không bị patch; lỗi giữ toàn document/history/dirty; no-op không dirty hoặc thêm Undo.
Verify: G01 `core/document/tests/commands/test_gradient_commands.cpp`, qua writer/command public consumer.

## AC-G03 — Alpha, profile và opacity scopes

Given: RGB đỏ alpha 0 → xanh alpha 1, stop opacity 100%; bản khác fill opacity 50%, object opacity 50%;
hai background trắng/đen và một fixture CMYK/spot có source channels/profiles ghi rõ.
When: Render sample t=0,5 trước/sau đổi session preview profile; save/mở lại.
Then: Với method v1, straight RGB=(0,5;0;0,5), alpha=0,5 trước opacity; premultiply sau sample.
Fill/object alpha scopes mỗi cái nhân đúng một lần; đổi profile chỉ đổi preview/cache, source channels/IDs
không đổi. CMYK/spot preview không được ghi thành print proof hoặc source RGB.
Verify: G03 `test_gradient_pixels.cpp` + scalar reference bằng tay cho RGB/alpha; G02 structural asserts.
Pixel compare ở interior tách quantization RGBA8 khỏi float scratch; không ép 1 tolerance cho mọi target.

## AC-G04 — Local geometry và numeric equivalence

Given: Path 100×60 mm, Linear nằm ngang, Radial ellipse tâm; object dịch/rotate/nonuniform scale/flip;
thêm large-world origin và một matrix singular.
When: Cùng điểm source được sample qua camera/zoom khác; sửa Angle/Length/Center bằng Properties;
node edit làm đổi bounds rồi gọi Fit.
Then: Paint theo object, không trôi theo camera; node edit trước Fit giữ matrix; Fit đổi riêng matrix và
Undo được. Singular edit reject không fallback solid. Bar/numeric/canvas cho cùng matrix và cùng pixels.
Verify: G01 matrix semantic vectors, G03 analytic q=inverse(M)*p, G06/G10 real UI nhập số + drag tương ứng.
Case skew nhập khẩu kiểm không decomposed-edit làm mất skew. Đây không chứng minh mọi extreme coordinate.

## AC-G05 — Native v5, legacy, malformed và save revision

Given: Fixtures v2/v3/v4 solid; v5 gradient có source color, hard edges/midpoint/spread, stable IDs;
malformed records gồm CRC, length/truncation, unknown kind/version, huge count/overflow và duplicate IDs.
When: Load → sửa Gradient → save v5 → reopen → Undo/Redo; nhận malformed input; save r khi edit r+1 xảy ra.
Then: Legacy giữ semantics solid, v5 giữ đầy đủ editable payload và Appearance; malformed reject atomic;
save dùng snapshot r và không clear dirty r+1. Old v4-only reader từ chối v5, không mở bằng solid fallback.
Verify: G02 `core/document/tests/storage/test_gradient_storage.cpp`; bounded temporary bytes trong test,
không phá fixture gốc; G10 app open/save/reopen ở canonical exe. Crash durability ngoài ca này.

## AC-G06 — Winding, clipping và effect scratch parity

Given: Path có lỗ NonZero/EvenOdd, open subpath implicit close, stroke solid riêng; Gradient alpha;
bản có Blur/Shadow và object opacity/blend được runtime hỗ trợ, fractional offset và ROI origin khác 0.
When: Render direct và float scratch/no-op effect rồi Blur/Shadow; edit/delete paint sau cached frame.
Then: Fill chỉ covered region; lỗ/clip/stroke không sai; cùng local Gradient giữa direct/scratch trong
precision target; effects không bị solid fallback, alpha không nhân hai lần; cache/damage không giữ ảnh cũ.
Verify: G03 `test_gradient_compositor.cpp`, existing pixel/float scratch/effect compositor regression.
Không sửa kernel/oracle task blur khác để đạt pass; error vì budget trả incomplete diagnostic.

## AC-G07 — Repeat/Reflect, giới hạn và cache

Given: Linear ramp chuẩn; points t=-2,-1,-0,25,0,0,25,1,1,25,2; radial ellipse và nhiều paints IDs khác;
paint cùng ID được thay nội dung bởi revision mới, profile generation mới.
When: Sample ba spreads; evict cache 16 MiB đề xuất; đưa input vượt count/work/matrix budget.
Then: Theo công thức plan, negative wraps và endpoints nhất quán; cache không trả old pixels, active
snapshot không dangling; request quá quota reject rõ, không skip paint hay unbounded allocations.
Verify: G03 analytic expected table + controlled allocations/cache accounting; G10 đo peak scratch/cache
thực, không dùng working set toàn process làm bằng chứng riêng cho renderer-owned scratch.

## AC-G08 — Gesture, cancel và stale preview

Given: Một path solid/gradient đang được chọn, committed revision r; controller/canvas cùng draft token.
When: Drag → pointer-up; drag thứ hai → Esc/capture lost; Properties sửa nhiều field; đổi selection,
Undo/tool/document hoặc source revision trước kết quả preview cũ trả về.
Then: Drag đầu đúng một commit; các hủy không thay source/dirty/history; stale preview không publish và
không Apply được. Shift 15° đúng source axes. Click dưới ngưỡng drag không áp Gradient ngầm.
Verify: G04 tool tests, G05 `test_gradient_tool_controller.cpp` dùng delayed result; G10 thao tác native.
Controller pass không xác minh Windows pointer capture/keyboard dispatch thật.

## AC-G09 — Properties luôn truy cập và đồng bộ bar

Given: Dock đóng, bar hẹp, một path hợp lệ; riêng một ca batch gradient cùng topology nhưng mixed colors.
When: Menu Object > Fill > Gradient…; sửa Position trên bar, Opacity/Midpoint/Spread trong dock;
đổi focus hai nơi; đóng/mở dock rồi Apply; mở panel mà không sửa.
Then: Focus đúng section; mọi field cùng draft, mixed không giả zero hoặc lấy object đầu; đóng dock giữ
draft và vẫn Esc được; Apply atomic một Undo. Chỉ mở/focus panel không dirty/Undo hoặc tự bật Gradient.
Verify: G06 native shell routing test + G10 screenshot và keyboard sequence ở app thật; ghi size/DPI/theme.

## AC-G10 — Keyboard, IME, validation và accessibility

Given: Gradient panel với numeric focus, active stop list, số chưa hợp lệ, UI EN và VI.
When: Tab/Shift+Tab, arrows chọn stop, thêm/xóa qua nút, nhập/copy/paste số+đơn vị, dùng IME,
Esc ở field rồi Esc ở draft, đọc tên control qua accessibility inspector.
Then: Không vô tình xóa object hoặc canvas shortcut khi nhập; số invalid ở field, source giữ nguyên;
Delete khi còn 2 stops disabled rõ; focus nhìn được; ramp/stop/type/numeric có accessible role/name/value.
Đổi UI language không đổi artwork/color/source IDs/units. Có đường tới Help không cần hover/F1 giả.
Verify: G06 event regression + G10 native Windows/inspector. Nếu không kiểm được IME/screen reader,
ghi unverified cụ thể; tooltip string không thay accessibility evidence.

## AC-G11 — Trạng thái selection và layout

Given: Không doc; doc không selection; single path; batch same/mixed topology; locked path; Text/group/bitmap;
1366×768 và 1920×1080, scales 100/125/150/200%, light/dark/high contrast trong môi trường có thể kiểm.
When: Mở tool/panel, chuyển context, dùng cửa sổ hẹp, invalid matrix/range, render quota failure.
Then: Enable/reason đúng phiếu; batch incompatible không partial apply; action Apply same gradient cần
chủ đích và mỗi object có matrix riêng. Không clipping label/unit/Apply/Cancel hoặc font bị thu nhỏ.
Error/busy/mixed/None khác nhau và không báo success khi thiếu paint.
Verify: G06 summary-state assertions; G10 ma trận manual chỉ các ca có rủi ro distinct, ghi tổ hợp chưa chạy.
Không nhân toàn ma trận trạng thái × DPI × theme nếu không đổi kết luận.

## AC-G12 — Consumers, defaults và production geometry

Given: Gradient path; solid creation defaults; hai đối tượng để Boolean với style donor đã xác định;
clipboard/duplicate/array/Contour workflow có runtime; original IDs và geometry checksum.
When: Copy/paste, duplicate/array, transform, Boolean/Contour, đổi fill solid qua palette,
Undo/Redo, tạo shape mới và save/reopen; thử nút Export SVG trước mốc SVG Gradient.
Then: Clone giữ paint/source channels nhưng remap paint/stop IDs; style donor giữ Gradient; palette solid
thay Gradient có chủ đích, Undo khôi phục; selection-rỗng không đổi defaults. Geometry/units/path IDs
chỉ đổi theo chính lệnh geometry, không vì paint. Không tạo toolpath từ màu chuyển sắc.
Trước G08/G11, SVG exporter từ chối Gradient rõ ràng, không lấy remembered solid để ghi file sai.
Verify: G09 `test_gradient_consumers.cpp` qua public app consumers và clone tests hiện có; G10 flow native.
Nếu workflow geometry có giới hạn thật, ghi capability đó, không mở rộng bằng task Gradient.

## AC-G13 — SVG normalize vào model chung

Given: SVG 1.1 self-authored/licensed fixtures linear/centered radial, local defs/refs, offsets/hard stops,
units userSpaceOnUse/objectBoundingBox, nonuniform gradientTransform, opacity và spreads; manifest source
app/build/profile/hash. Fixtures CSS ngoài subset, off-center focus và unsupported method riêng.
When: Import qua worker/host staging → edit stop → Undo/Redo → native save/reopen.
Then: Eligible subset có cùng canonical paint/commands/renderer, preserve source locator/representation;
không mất transform/alpha; unsupported rows báo loss trước commit hoặc reject, không fallback solid.
Verify: G07 adapter + worker consumer tests, G11 compare structural/analytic + source-app reference render.
Pin expected SVG normalization theo W3C, không gọi Illustrator/Corel semantic khi chưa fixture app đó.

## AC-G14 — Import input không tin cậy

Given: Missing/cyclic defs, external URL, script/animation, oversized stops/defs/record, malformed offset,
unknown enum từ worker; import base r bị thay đổi và delayed result sau Cancel/close.
When: Submit worker output vào host hoặc file input qua entry point thật.
Then: Không fetch external source; quotas/depth/cycles/finite/schema/capability kiểm trước cấp phát/commit;
malformed hoặc stale reject atomic, committed snapshot/history giữ nguyên, cleanup staging đúng generation.
Verify: G07 host-validation/worker codec tests. Các tests không chứng minh OS sandbox containment;
containment theo C6 là evidence riêng, không báo an toàn parser chỉ từ protocol pass.

## AC-G15 — Export đúng subset và không mất màu âm thầm

Given: Native Linear/Radial centered, midpoint 50%, sRGB colors/spread representable; các bản midpoint
khác 50%, CMYK/spot, unsupported effects/blend và method future/unknown được đưa vào negative vectors.
When: Export SVG → import SVG lại → mở trong named reference app; request export unsupported exact.
Then: Exact subset giữ appearance/geometry/stops/alpha theo precision metric đã đo và cấu trúc editable
phù hợp, provenance/IDs mapping có report. Unsupported export reject hoặc conversion choice tường minh
chỉ khi pipeline đã có, không ghi solid fallback/pretend native round-trip. Source original không overwrite.
Verify: G08 exporter assertions và G11 source app reopen evidence; pixel pass không thay editability.
AI/CDR round-trip vẫn unsupported dù SVG mở được trong các ứng dụng đó.

## AC-G16 — Báo cáo mốc và performance

Given: Canonical app build từ integrated source snapshot có hashes, hardware/OS/configuration ghi rõ;
fixtures: một gradient lớn, 1.000 paths nhỏ, ramp 2/16/256 stops, radial transformed và effect case riêng.
When: Chạy warm-up rồi tối thiểu 40 samples cho từng workload quyết định và solid control cùng session;
đo source redraw/offscreen, cache/scratch; đo present/input-to-visible nếu có dụng cụ thực tế.
Then: Report p50/p95, sample count, CPU time, memory attribution, checksum, cold/warm boundaries và
khác biệt solid; không đổi CPU p95 thành FPS. Budget target chưa đạt ghi thật; present/GPU/input chưa đo
để unmeasured. Không claim print/CNC/color fidelity hoặc cả UI pass từ harness/build.
Verify: G10 benchmark/report native, G11 report SVG riêng. G00 cập nhật project-state sau review hashes;
docs-only task hiện tại không chạy benchmark, build hay CTest và không đổi runtime capability.

## Mẫu evidence/handoff thực thi

```text
Task / owner / dependency snapshot:
Source baseline / integrated snapshot hash / exact changed paths:
AC ID / fixture ID / source app build + color profile / environment:
Command or manual steps:
Expected / observed / result (passed, failed, blocked, unverified):
Artifact path + hash / reviewer next action:
Limits: rendering | editability | source metadata | export | production | UX | performance
```

Native DoD: AC-G01–G12 và G16 theo scope; SVG DoD thêm G13–G15 và G16 source-app evidence.
Không đóng task runtime bằng cách chỉ sửa trạng thái ca trong tài liệu; cần evidence đúng source snapshot.
