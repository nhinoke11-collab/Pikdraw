# Kế hoạch chi tiết nhóm hiệu ứng cơ bản

> Các trạng thái có ngày trong tài liệu này là lịch sử của feature. Runtime, định dạng và workflow hiện hành ở docs/START.md, docs/project-state.json và AGENTS.md; không dùng task/lease/handoff làm điều kiện triển khai.

Ngày: 17/10/2026. Task: `basic-effects-detailed-plan-20261017`.
Trạng thái: **đã có lát cắt native cho các hiệu ứng cơ bản; còn thiếu một số điều kiện nghiệm thu và chưa sẵn sàng phát hành**.
Baseline khảo sát: HEAD `11512046d23fac08059ecd37382179283b4530a0` cùng local source tại thời điểm task claim. Handoff lưu snapshot của hai tài liệu được sửa; đây không phải chứng nhận toàn bộ source/build đang dirty.

Phạm vi: Object/Fill/Stroke Opacity, sáu Blend Mode cơ bản, Gaussian Blur và Drop Shadow gồm Shadow Blend riêng. Tái dùng [đặc tả Opacity/Properties](../design/features/object-opacity-and-properties.md), [C9](../../contracts/c9-appearance-effects.md), [ADR 0009](../decisions/0009-appearance-effect-graph.md) và [hướng dẫn tài nguyên vector](../architecture/vector-resource-interoperability.md). C9/ADR vẫn Proposed; kế hoạch này không tự chuyển trạng thái chúng.

## Trạng thái tích hợp hiện tại · 17/10/2026

Source root đã có model `Appearance`, command/Undo, native v4 storage, CPU renderer cho Object/Fill/Stroke Opacity, sáu Object Blend Mode, Gaussian Blur và Drop Shadow; Drop Shadow schema 1.1 có sáu Shadow Blend mode riêng. Chọn lại Blur/Shadow khi node đã có sẽ mở chế độ Edit trên node hiện có, giữ ID/thông số và không tăng revision; chỉ khi chưa có mới thêm mặc định. Shadow Blend khác Normal chỉ hợp lệ khi Shadow là node bật cuối cùng; node disabled phía sau được phép. Full Debug CTest mới nhất sau khi khôi phục baseline serial AVX/SSE2 đạt 80/80; focused Release cho 14 target effects/interop/desktop đạt 14/14. `tooling/native/build.ps1 -Test` hoàn tất build mặc định trước CTest, nhưng automated build/test không thay thế nghiệm thu OS thủ công. Direct OS interaction, DPI/accessibility và tương thích file nguồn vẫn chưa được xác nhận.

Task Shadow Blend chính đã hoàn tất; task culling schema 1.1 vẫn có record riêng ở trạng thái `in_progress` dưới lease khác, dù culling test nằm trong bộ full Debug 80/80 pass. BF00/BF00b và một số slice model/render/UI còn ở `review`, cần xác minh snapshot và cập nhật handoff trước khi đóng toàn bộ chuỗi. Scope interop hiện vẫn là loss guard: SVG import chưa nhận appearance opacity/filter/blend; regression mới xác nhận `opacity`, `fill-opacity`, `stroke-opacity` trên Path/Rect/Text bị từ chối và không trả object; SVG export bảo toàn phần opacity được hỗ trợ nhưng từ chối effect nodes và blend ngoài Normal. AI, CDR, PDF và EPS hiện `UnsupportedReject`; không có claim round-trip/fidelity với Illustrator hay CorelDRAW.

UI hiện có Object/Fill/Stroke Opacity, Object Blend, chỉnh sửa Blur/Shadow và Shadow Blend cho đối tượng đủ điều kiện. Appearance toolbox group mở Properties hoặc chọn/thêm Blur/Shadow; `Properties > Style` có draft Preview/Apply/Cancel. Shadow Blend control dùng cùng workflow/Undo và bị chặn khi stack vi phạm vị trí cuối. Compositor có bounded output cache tối đa 262,144 pixel/4 MiB và giữ preflight atomic; đường convolution SSE2 dùng kernel đối xứng, so parity với scalar trong tolerance `4e-6`. Blur giảm đáng kể nhưng baseline Release mới nhất có median p50/p95 55.0/67.8 ms, chưa đạt 33.3 ms; measured vector capacity cho Blur là 5.65 MiB và cho Drop Shadow là 8.90 MiB trên các fixture tương ứng; Shadow latency chưa có A/B được kiểm soát. Thử nghiệm bỏ kiểm tra biên và thử nghiệm hai-lane đều không chứng minh tăng tốc nên đã bị từ chối. Full Debug CTest mới nhất sau restore đạt 80/80; full Release build mới nhất dừng trước CTest tại target Contour do undefined symbol `get_object_contour`; focused Release 14 target effects/interop/desktop đạt 14/14 CTest. Benchmark và giới hạn ở [report](../reviews/basic-effects-performance-20261017.md). Automated tests không thay thế kiểm tra OS thủ công. DPI/accessibility và vendor-format fidelity vẫn chưa nghiệm thu; không có claim tương thích CorelDRAW hay Illustrator.

## 1. Kết quả và phạm vi phát hành

| Đợt | Chức năng hoàn chỉnh cần đạt | Phạm vi |
| --- | --- | --- |
| A | Object Opacity; tiếp theo Fill/Stroke Opacity; model-command-render-native save/undo và UI đồng bộ | Path; Text chỉ khi backend outline thật sẵn sàng và test glyph đạt. Text chưa có stroke thì không hiện Stroke Opacity. |
| B | Blend Mode: Normal, Multiply, Screen, Overlay, Darken, Lighten | Toàn đối tượng, blend với backdrop đúng painter order; chưa có mode riêng cho từng fill/stroke hoặc group pass-through. |
| C | Gaussian Blur chỉnh sửa lại được, Sigma X/Y, bật/tắt/xóa node | Nội dung Path/Text đã render đúng; units nguồn, transform và bounds của effect có kiểm chứng. |
| D | Drop Shadow chỉnh lại được, offset X/Y, Sigma, màu, shadow opacity và Shadow Blend riêng | Normal dùng chuỗi node hiện có. Shadow Blend khác Normal đọc backdrop painter-order nên Shadow phải là node bật cuối; object Blend áp lên source phía trên bóng, Object Opacity áp đúng một lần lên toàn kết quả. Giữ source vector. |

Mỗi đợt chỉ bật UI sau khi model, renderer, command, codec, loss guard và acceptance tương ứng đạt. Không chờ cả bốn đợt mới có một luồng dùng hoàn chỉnh.

Group/Image, masks, clipping nguồn chưa hỗ trợ, isolation/knockout/pass-through, nhiều fill/stroke, inner shadow, glow, spread/choke, bevel, lens và mode ngoài sáu mode trên thuộc các đợt riêng. GroupOpacity không được giả bằng cách nhân opacity xuống từng child. Native effect không tự bật AI/CDR/PDF/EPS import hoặc source-format export.

## 2. Baseline khảo sát trước khi code (17/10/2026)

Các hàng sau ghi nhận source tại lúc lập kế hoạch; chúng không mô tả trạng thái runtime hiện tại. Dùng phần “Trạng thái tích hợp hiện tại” ở trên và capability matrix trong source để tra cứu năng lực mới nhất.

| Source đã đọc | Kết luận dùng để thiết kế |
| --- | --- |
| [object.hpp](../../core/document/include/pikdraw/document/model/object.hpp) | Path/Text có màu alpha; chưa có thuộc tính opacity độc lập hoặc effect stack. Group/Image có kiểu model nhưng không có nghĩa renderer đã hỗ trợ. |
| [style_tool.hpp](../../modules/vector/include/pikdraw/vector/gestures/style_tool.hpp) | `StylePatch` có fill/stroke/width/cờ bật tắt, chưa có appearance patch. Đã có luồng cập nhật nhiều object nguyên tử; cần giữ semantics đó. |
| [path_paint.cpp](../../core/rendering/src/raster/path_paint.cpp) | Fill và stroke đang vẽ trực tiếp lên target. Nhân riêng alpha từng màu không tạo ra Object Opacity đúng tại vùng fill/stroke chồng nhau. |
| [frame_target.hpp](../../core/rendering/include/pikdraw/rendering/frame_target.hpp) | Target public là straight-alpha RGBA8; compositor/effect scratch cần quy ước chuyển đổi rõ, không đổi layout buffer ngầm. |
| [software_rasterizer.cpp](../../core/rendering/src/software_rasterizer.cpp) | Text đi qua glyph outline khi có `PIKDRAW_TYPOGRAPHY_REAL_OUTLINES`; lỗi render trả diagnostic. Group/Image chưa có nhánh render hoàn chỉnh. Target và CPU work có guardrail, chưa có ngân sách riêng cho effects. |
| [native_format.hpp](../../core/document/include/pikdraw/document/storage/native_format.hpp), [object_codec.cpp](../../core/document/src/storage/object_codec.cpp) | Codec nguồn hiện tại là v3, có đường đọc v2; chưa encode effect nodes. Cần audit byte layout và migration trước khi thêm dữ liệu. Nhãn v2 trong tài liệu trạng thái cũ không thay kết luận từ source. |
| [native_window_menus.hpp](../../apps/desktop/src/shell/native_window_menus.hpp) | Effects có các entry roadmap, gồm Drop Shadow, hiện disable. Cần tái dùng entry đúng và chỉ enable handler thật. |
| [Interop README](../../modules/interop/README.md) | Capability giới hạn theo subset; effect import/export chưa được chứng nhận bằng tài liệu này. |

Đây là đọc source, chưa chạy build/test/native UX ở task lập kế hoạch. Khi bắt đầu code phải kiểm lại snapshot và ownership.

## 3. Model, commands và native storage đề xuất

Đề xuất một `Appearance` có schema version, gắn với object; không lưu cùng giá trị ở cả field cũ lẫn một node khác:

- `object_opacity`, `fill_opacity`, `stroke_opacity`: finite `[0,1]`, mặc định `1`; giữ nguyên alpha/channel/model/ICC/overprint của source color.
- `object_blend_mode`: stable ID, mặc định `normal`; enum/ID không theo index UI hoặc tên dịch.
- `blend_space`: mặc định authored v1 là `srgb_encoded_v1`; `filter_space` của node authored v1 là `linear_srgb_v1`. Lưu rõ semantics; không đổi working space chỉ vì chuyển theme hoặc ICC preview.
- `effects`: danh sách có thứ tự, mỗi node có stable instance ID, type ID, schema version, enabled và typed parameters. Gaussian Blur dùng schema 1.0; Drop Shadow dùng schema 1.0 cho Normal hoặc 1.1 khi lưu Shadow Blend.
- Mỗi node có coordinate basis `object_local_mm` và support policy có version. Halo theo pixel, ROI và cache là dữ liệu dẫn xuất, không lưu thành source params. Source-defined filter region phải giữ riêng units/stage nếu có; authored v1 tính vùng tự động theo policy. Biểu diễn `document_space` hoặc graph nhập khẩu khác basis cần renderer/mapping riêng trước khi nhận editable.
- Blur giữ `sigma_x_mm`, `sigma_y_mm`; Shadow giữ `dx_mm`, `dy_mm`, sigma X/Y, source color, shadow opacity và `blend_mode` riêng. Shadow schema 1.0 mặc định Normal; schema 1.1 lưu một trong sáu mode. Spread bằng 0; nguồn có mode/spread khác chưa được map exact.
- Stack authored v1 là chuỗi node một input: đầu vào node sau là kết quả node trước. Render DAG/cache có thể biên dịch từ chuỗi này; không nhận arbitrary executable graph hoặc callback từ importer.

Giới hạn node count, bytes, dimension, sigma-derived halo và checked arithmetic do host policy quản lý. Giá trị finite hợp lệ về model vẫn cần admission cho request render cụ thể. Unknown required node/version phải tạo unsupported outcome; không load rồi lưu bỏ node. Opaque data không có reference renderer đủ bằng chứng không trở thành “hiển thị đúng”.

Commands dự kiến: set appearance fields; add/remove/enable/move effect; update typed params. Mỗi command kiểm toàn bộ targets, lock/read-only/type/range và expected object/node IDs trước khi ghi. Multi-selection all-or-nothing; no-op không tạo lịch sử mới; undo/redo khôi phục params, IDs, order và source colors. Presenter chỉ giữ draft; không sửa model trực tiếp.

**Storage đã tích hợp:** native container v4 với appearance schema 1.0 và node schema major 1; Shadow node minor 1 nối stable blend ID sau payload schema 1.0. Reader v2/v3 migration áp default `1/1/1/Normal/empty stack`; explicit save ghi v4, không ghi xuống v3 làm mất appearance. Reader từ chối unknown-required. Regression lưu/mở lại kiểm schema 1.0 mặc định và Shadow 1.1 riêng.

## 4. Thuật toán và thứ tự render đề xuất

### 4.1 Một nguồn semantics

```text
Source geometry/text + source color alpha
  -> fill/stroke opacity và tổng hợp SourceGraphic trên nền trong suốt
  -> effects theo thứ tự, trong object-local space
  -> object transform sang document/view
  -> object opacity áp đúng một lần lên cả RGB premul và alpha
  -> object blend mode + source-over với backdrop
  -> output target/viewport clip
```

SourceGraphic không chứa màu trang/background. Stroke có thể overlap fill nhưng Object Opacity chỉ nhân sau khi tổng hợp. Opacity 0 vẫn giữ geometry/IDs và khả năng chọn theo semantics hit-test nguồn; không đổi `visible`, không xóa object.

Shadow Blend khác Normal là ngoại lệ backdrop-aware: evaluate bóng cuối cùng với target pixel thực bên dưới object, composite source ở trên bằng Object Blend, rồi nội suy toàn kết quả với backdrop ban đầu theo Object Opacity. Vì node effect sau đó sẽ cần nhận pixel đã phụ thuộc backdrop, mọi node bật sau Shadow làm stack sai semantics và bị từ chối; node disabled không tham gia.

Appearance mặc định `1/1/1/Normal/empty stack` giữ fast path hiện có và baseline output. Effect path có precision/conversion đã định, không quantize lại sau từng node; thay đổi sampling/antialiasing phải có reference review thay vì tự gọi là chất lượng tương đương.

Internal scratch đề xuất float32 RGBA premultiplied, source parameters/units vẫn binary64. Chuyển đổi có tên giữa scratch và target RGBA8 straight; alpha 0 có RGB scratch bằng 0. Blur xử lý linear-sRGB; blend v1 xử lý sRGB encoded. Khi đổi space phải unpremultiply an toàn, đổi transfer function rồi premultiply lại; không áp gamma trực tiếp lên RGB đã premultiply. Source CMYK/spot/ICC/overprint vẫn nguyên trong model; preview RGB không chứng nhận print separations hoặc CMYK blend của ứng dụng nguồn.

### 4.2 Object Opacity và Blend Mode

Với source premultiplied `(cs, as)` và opacity `o`: `(cs, as) <- (o*cs, o*as)`. Với backdrop `(cb, ab)`, `Cs/Cb` là màu straight trong blend space:

```text
ao = as + ab*(1-as)
co = as*(1-ab)*Cs + as*ab*B(Cb,Cs) + (1-as)*ab*Cb
```

`co` là RGB premultiplied. Unpremultiply chỉ khi alpha khác 0. Đối chiếu [W3C Compositing and Blending](https://www.w3.org/TR/compositing-1/#blending).

| Mode | Hàm B theo từng channel trong `[0,1]` |
| --- | --- |
| Normal | `Cs` |
| Multiply | `Cb*Cs` |
| Screen | `Cb + Cs - Cb*Cs` |
| Overlay | `2*Cb*Cs` nếu `Cb <= 0.5`, ngược lại `1-2*(1-Cb)*(1-Cs)` |
| Darken | `min(Cb,Cs)` |
| Lighten | `max(Cb,Cs)` |

Dùng cùng hàm reference cho pixel vectors và renderer; test nền alpha 0/0.5/1. Thuật toán blend phải tính phần backdrop tham gia, không coi pixel trong suốt như màu đen opaque. Tính full float rồi quantize một lần ở boundary. Tolerance và test oracle độc lập được khóa BF00, không lấy chính implementation làm expected output.

### 4.3 Gaussian Blur

Đề xuất Gaussian separable hai pass X/Y trong filter-local space. `sigma_px = sigma_mm * pixels_per_local_mm`; kernel `K(i)=exp(-i*i/(2*sigma_px*sigma_px))`, normalize tổng kernel; finite support authored v1 `r=ceil(4*sigma_px)`. Sigma bằng 0 là identity theo từng trục. Support 4-sigma là lựa chọn PikDraw cần lưu semantic version và đo sai số; không tự coi tương đương mọi vendor radius.

Convolve RGB premul và alpha trên nền ngoài source bằng transparent zero; cấp halo đầy đủ, không renormalize riêng tại mép source gây sai năng lượng. Alpha-only path của Shadow dùng cùng kernel. Không blur RGB hidden dưới alpha 0 để tránh viền màu. UI gọi tham số là `Sigma`, giải thích ngắn trong help; map `stdDeviation` SVG theo units/basis đã kiểm. Đối chiếu [W3C Gaussian Blur](https://www.w3.org/TR/filter-effects-1/#feGaussianBlurElement).

Chi phí reference xấp xỉ `W*H*(2*rx+2*ry+2)` lần tích lũy/channel; admission dựa cả công việc và bytes. Large sigma có preview downsample được gắn quality state; source sigma không đổi. Final/settled render phải đạt quality đã công bố hoặc trả lỗi. Không âm thầm dùng box blur/approximation cho output final.

### 4.4 Drop Shadow

Pipeline: alpha input node -> Gaussian blur -> offset -> tint/multiply shadow alpha -> đặt phía sau input. Đề xuất phù hợp mô hình [W3C Drop Shadow](https://www.w3.org/TR/filter-effects-1/#feDropShadowElement); mapping vendor vẫn cần fixture riêng.

```text
ah = shifted_blurred_alpha * shadow_opacity * shadow_color_alpha
ch = ah * preview(shadow_color).rgb
result = input source-over shadow
```

Mask lấy sau source color alpha và fill/stroke opacity, trước object opacity. Object opacity áp sau toàn stack để tránh nhân hai lần vào shadow. SourceGraphic không bị rasterize trở lại model. Node sau có thể nhận cả shadow từ node trước, nên đổi Blur/Shadow order có ý nghĩa và phải được save/undo đúng.

Với Shadow Blend khác Normal, áp blend mode được chọn cho nhánh bóng với backdrop bên dưới object trước khi ghép source; Object Blend tiếp tục áp cho source phía trên. Object Opacity pha nhóm hoàn chỉnh với backdrop ban đầu đúng một lần. Sáu mode dùng các hàm ở mục 4.2, encoded sRGB; fixture kiểm riêng pixel bóng ngoài source và pixel source.

Preset authored đề xuất: màu đen RGB alpha 1; shadow opacity 50%; `dx=+2 mm`, `dy=-2 mm`, sigma X/Y `1 mm`, mode Normal. Theo C1, local +X sang phải, +Y hướng lên; preset nghiêng xuống phải khi transform identity. Vendor dùng Y-down phải đổi basis và dấu tại adapter, không đổi dấu tùy theo UI.

### 4.5 Transform, bounds và clipping

Effect authored v1 chạy trong object-local frame rồi mới apply object transform. Renderer chọn sampling từ singular values của view*object transform, xử lý non-uniform scale, rotation, reflection và shear qua sampling ảnh premul đúng frame; không dùng một sigma màn hình cố định cho mọi transform. Singular/invalid transforms có diagnostic theo C1. Nếu triển khai chưa đạt shear/reflection thì capability của tổ hợp đó vẫn bị chặn, không làm tròn về uniform scale.

Blur bounds = input bounds inflate support X/Y. Shadow bounds = union của input với blurred bounds dịch offset; stack truyền bounds lần lượt. Support thực dùng `r_px / pixels_per_local_mm`, có rounding và footprint resampling/AA bảo thủ; không chỉ lấy 4-sigma mm rồi cắt phần pixel lẻ. Map hull/bounds sang world trước culling. Phải xét object ngoài viewport nhưng shadow lọt vào viewport. Giữ geometry/source bounds riêng cho hit-test và production, không dùng shadow bounds làm đường cắt.

V1 chỉ nhận clipping semantics đã có end-to-end. Clip/mask nguồn khác thứ tự clip/effect/opacity của pipeline này cần explicit graph stages và gate riêng; chưa đạt thì adapter báo reference/conversion/unsupported.

## 5. Bộ nhớ, cache và preview

- Scratch tính cả source, hai pass, halo, transformed output và các node còn sống; float RGBA cần 16 byte/pixel trước overhead. Checked sizes và reservation diễn ra trước cấp phát; tránh full-document surface cho artwork vài chục mét.
- ROI truyền ngược qua stack: inverse transform output ROI, nới kernel support, dịch ngược Shadow offset, lấy input cần thiết; Shadow lấy union của nhánh input trực tiếp với nhánh mask dịch ngược/nới halo. Crop sau khi đủ input/halo; tiled và full-frame cùng oracle, không lộ seam.
- Cache key có source/object/node revisions hoặc content identity, params, order, scope, transform, camera sampling grid, quality, profile/working-space và backend generation. Final blend cache có backdrop dependency; source mask cache không giữ backdrop không cần thiết.
- Dirty/damage gồm cả old/new effect output và dependency phía sau; đổi màu shadow có thể dùng lại mask/blur, đổi source phải invalidate đúng. Xóa/undo/disable phải xóa ghost pixels và resource reservations.
- Preview có document ID/epoch/base revision, target/node IDs, selection/tool session, effect job generation, camera/profile generation. Đổi field nhanh coalesce request; stale/cancel/close không nhận kết quả cũ. Writer vẫn revalidate trước commit.
- Không tăng revision/Undo khi kéo slider hoặc mở Properties. Thả slider/Enter tạo một command; Escape/capture loss/đổi document hủy draft và resource candidate. Multi-selection không được commit một phần nếu target lỗi.
- BF00 phải đo peak bytes, work/cancel latency và p50/p95 trên máy tham chiếu, chốt policy node/scratch/job limits và ghi số đo. Guardrail hiện tại của renderer không tự là ngân sách effects hoặc bằng chứng 60 FPS. GPU không thuộc bản cơ bản này.

## 6. Thiết kế UI và hướng dẫn dùng

Áp dụng [chuẩn UX/UI](../design/ui-ux-standard.md) và [workspace](../architecture/workspace-layout.md). Runtime có Appearance toolbox, Properties > Style, Effects menu cho Blur/Shadow và Clear Effects. Clear Effects xóa toàn bộ filter nodes của selection đủ điều kiện bằng một command/Undo. Toolbox có đường bàn phím bắt đầu từ Tab/Shift+Tab. Chọn Blur/Shadow sẽ focus node cùng loại đã có để Edit, không ghi document; chỉ thêm mặc định khi mọi target đều chưa có. Kéo tay nắm Drop Shadow xem trước thay đổi offset rồi commit một lần khi thả chuột. Properties có draft Preview/Apply/Cancel cho node mới và sáu-mode Shadow Blend control cho node hiện có.

### 6.1 Menu và bề mặt

| Bề mặt | Hành vi/ID đề xuất |
| --- | --- |
| `Object > Properties` | Mở/focus cùng dock `Appearance`; navigation action `view.properties.appearance`, không tạo document command. `Window > Properties` bật/tắt cùng dock. |
| `Appearance > Transparency` | Object/Fill/Stroke Opacity và Object Blend. Setters `object.appearance.set-opacity`, `.set-fill-opacity`, `.set-stroke-opacity`, `.set-blend-mode`; bar và dock dùng chung setters. |
| `Effects > Blur > Gaussian Blur…` | `MenuActionId::EffectsGaussianBlur`; focus node Blur đầu tiên nếu đã có, nếu chưa có thì thêm default trong một Undo. Nút Properties Draft Blur dành cho node mới. |
| `Effects > Shadow > Drop Shadow…` | `MenuActionId::EffectsDropShadow`; focus node Shadow đầu tiên nếu đã có, nếu chưa có thì thêm default trong một Undo. Shadow mới giới hạn một node/đối tượng; drag handle edit offset hiện có bằng một Undo. |
| `Effects > Clear Effects` | `effects.clear`: đã nối menu với workflow; chỉ bật khi toàn selection là Path/Text không khóa và có ít nhất một node. Xóa filter nodes trong một Undo; giữ Object/Fill/Stroke Opacity và Blend. |
| Properties `Appearance > Effects` | Ordered node list; enabled, select/edit, remove, move up/down. IDs `effects.set-enabled`, `.update-parameters`, `.remove`, `.move`. Không tạo dock riêng cho từng effect. |
| Properties `Appearance > Effects > Draft Blur/Shadow` | Tạo candidate node mới chưa persist; `Preview` bật/tắt render tạm, `Apply` commit một command, `Cancel`/Esc bỏ. Draft gắn với document ID/epoch/revision và selection IDs. Shadow handle dùng edit draft của node hiện tại và bảo toàn ID. |
| Context property bar | Selection hợp lệ hiện Object Opacity và Blend gọn; khi đang sửa một node có thể thay nhóm phụ bằng Sigma hoặc Offset cần thiết. Chi tiết luôn có trong Properties; không thêm hàng bar mới. |
| Toolbox/standard toolbar | Nhóm `Appearance` hiện có icon chính mở `Properties > Style`; Tab/Shift+Tab chuyển focus qua các nhóm, focus ring hiện ở nhóm đang chọn, mũi tên hoặc `Alt+Down` mở flyout Blur/Shadow có điều kiện theo selection. Up/Down đổi hàng; Enter/Space kích hoạt; Esc đóng/trả focus. Đây là lối vào nhóm thuộc tính, không phải công cụ thao tác hình học. BF10/BF11 có acceptance tự động và ảnh EN/VI; kiểm tra OS keyboard/DPI thủ công vẫn mở. |
| Dialog/context menu/shortcut | Dùng dock hiện có, không dialog cho từng con số. Context menu chỉ alias nếu cần. Chưa gán shortcut mới; dùng menu, focus keyboard, Enter/Escape và Undo hiện hành. |

Draft Blur/Shadow có Preview, Apply, Cancel trong phần Effects của dock. Preview dùng candidate snapshot tách khỏi persistent document; nó không tăng revision hoặc tạo Undo. Draft mới thêm node khi Apply; draft chỉnh node giữ nguyên ID. Apply kiểm tra lại document, source revision và selection rồi commit bằng một command nguyên tử; Cancel/Esc hủy candidate. Numeric fields nhận dấu phẩy hoặc dấu chấm, Tab chuyển trường, Enter xác nhận trường hiện hành. Document/selection/revision đổi sẽ loại draft cũ. Toolbox flyout, Effects menu và nút Add/Edit trong Properties dùng cùng quy tắc focus-existing-or-add-once. Bấm xóa/reorder và Clear Effects là command riêng.

### 6.2 Fields, defaults và scope

| Field ID / EN / VI | Kiểu, default, units | Scope, vị trí và validation |
| --- | --- | --- |
| `property.object.opacity` / Opacity / Độ mờ | 0–100%, default 100%; hiển thị 0,1% theo locale, source `[0,1]` | Bar + Transparency; áp một lần cho object sau effects; Mixed khi nhiều values. |
| `property.fill.opacity` / Fill Opacity / Độ mờ phần tô | 0–100%, default 100% | Transparency/Fill; Text chỉ fill; không ghi đè source color alpha. |
| `property.stroke.opacity` / Stroke Opacity / Độ mờ nét | 0–100%, default 100% | Transparency/Stroke cho Path có capability; Text stroke N/A. |
| `property.object.blend-mode` / Blend / Hòa trộn | Sáu stable modes ở mục 4.2, default Normal | Bar + Transparency; unsupported không tự đổi Normal. |
| `property.effect.blur.sigma-x/y` / Sigma X/Y / Sigma X/Y | mm hữu hạn, `>=0`; preset 1 mm; linked X/Y mặc định | Properties; khi linked, sửa một giá trị đặt cả hai; unlink không đổi giá trị; source vẫn hai sigma. Không lưu pixel hoặc zoom. |
| `property.effect.shadow.offset-x/y` / Offset X/Y / Lệch X/Y | mm hữu hạn trong miền C1; preset +2/-2 mm | Properties và nhóm nhanh khi node active; dấu theo +Y-up; không lưu angle/distance thứ hai gây lệch sync. |
| `property.effect.shadow.opacity` / Shadow Opacity / Độ mờ bóng | 0–100%, preset 50% | Riêng shadow; Object Opacity vẫn riêng. |
| `property.effect.shadow.color` / Color / Màu | Source ColorValue; preset RGB đen alpha 1 | Tái dùng color editor/capability hiện có; giữ color model/source alpha. |
| `property.effect.shadow.blend-mode` / Shadow Blend / Hòa bóng lên nền | Sáu mode stable; default Normal; riêng Shadow node schema 1.1 | Properties > Style > Effects, ngay dưới Shadow Opacity; không đưa sáu nút lên property bar. Bật mode khác Normal chỉ khi Shadow là node bật cuối. |
| `property.effect.enabled` / Enabled / Bật | bool, default true | Node được chọn; toggle không xóa params/ID. |
| Order / Thứ tự; Remove / Xóa | Vị trí node và action | Move/remove qua stable node IDs, validation lại targets; không sửa trực tiếp vector UI. |

NaN, Inf, giá trị âm cho sigma, opacity ngoài range hoặc scalar/graph vượt miền được báo ngay, không clamp source âm thầm. Giới hạn scratch/work là admission của request; workflow giữ draft và giải thích khi request không thể render. Model không phụ thuộc renderer. Nếu zoom/profile mới làm request vượt budget sau commit, renderer báo trạng thái chưa render đầy đủ và giữ source, không bỏ effect khỏi hình mà báo hoàn tất.

### 6.3 Selection, Mixed và trạng thái

| Ngữ cảnh | Quy tắc |
| --- | --- |
| Không document/selection | Không dựng full form effects trên Properties; trạng thái ngắn yêu cầu chọn đối tượng; menu sửa disable. Window > Properties vẫn mở được. |
| Một Path eligible | Bật các field theo capability, hiện stack và node editor theo nhu cầu. |
| Một Text | Bật sau test real glyph; Stroke field N/A; font fallback có warning riêng, không coi là authoring fidelity. |
| Nhiều Path/Text eligible | Scalar opacity/blend có common/Mixed; add effect cùng params tạo node ID riêng cho mỗi object và một Undo. |
| Multi-selection có stack khác nhau | V1 không ghép node theo index/ID giữa objects. Scalar vẫn dùng chung; per-node edit/reorder yêu cầu chọn một object, có lý do rõ. |
| Có locked/read-only/Group/Image/unsupported | Disable toàn thao tác atomic, nêu lý do; không bỏ qua target lỗi rồi báo thành công. |
| Draft/IME/focus field | Giữ input draft; shortcut toàn app không cướp chữ/số; selection/document đổi hủy đúng draft cũ; stale result bị loại. |

### 6.4 Icons, help và usage

Đề xuất SVG sprite `apps/desktop/resources/icons/effects-basic.svg`, đồng bộ viewBox/stroke/theme/focus token với bộ icon hiện có. Symbols: Gaussian Blur (độ mềm nhiều vòng), Shadow (hình + bóng lệch), visibility, remove, move up/down; có tên truy cập đọc được, không sao chép biểu tượng hãng. Scalar fields không cần icon riêng. Tên menu bỏ nhãn roadmap khi handler được enable thật; labels/hints qua cơ chế localization hiện có, stable IDs không dịch.

| Trợ giúp | EN / VI đề xuất |
| --- | --- |
| Tooltip Opacity | `Set overall object opacity, including effects.` / `Đặt độ mờ toàn đối tượng, gồm cả hiệu ứng.` |
| Tooltip Blend | `Blend the object with artwork beneath it.` / `Hòa đối tượng với nội dung bên dưới.` |
| Tooltip Blur | `Set Gaussian sigma in local source units.` / `Đặt sigma Gaussian theo đơn vị nguồn cục bộ.` |
| Tooltip Shadow | `Create or edit a shadow from the object's alpha.` / `Thêm hoặc sửa bóng từ alpha đối tượng.` |
| Tooltip Shadow Blend | `Blend the shadow with artwork beneath it. Non-Normal modes require Shadow last in the enabled stack.` / `Hòa bóng với nội dung bên dưới. Mode khác Normal yêu cầu Shadow ở cuối stack đang bật.` |
| Hint dưới khi nhập | `Enter apply · Esc cancel · Ctrl+Z undo` / `Enter áp dụng · Esc hủy · Ctrl+Z hoàn tác`. Chỉ ghi binding đã kiểm trên app. |
| Hint add-effect draft | `Document unchanged · Preview to inspect · Apply to save` / `Chưa đổi tài liệu · Preview để xem · Áp dụng để lưu`. |
| Hint opacity 0 | `Transparent object remains selectable.` / `Đối tượng trong suốt vẫn chọn được.` |

Hướng dẫn ngắn: (1) chọn Path/Text hợp lệ; (2) chỉnh Opacity/Blend trên bar hoặc mở Object > Properties; (3) thêm Blur/Shadow từ Effects, chỉnh Sigma/Offset/màu trong cùng dock và xem Preview; (4) Apply để thêm, chọn node để sửa/tắt/xóa/reorder; (5) Ctrl+Z hoàn tác, save/reopen PikDraw để giữ editable source. Bar hẹp vẫn mở đủ fields qua Properties. Help chi tiết phải giải thích Sigma, local axes, effect order, Object Opacity khác Shadow Opacity và loss khi export.

## 7. Ma trận định dạng cần lập khi triển khai

Tất cả hàng dưới đây là **mục tiêu/gate**, không phải capability vừa được triển khai. Mỗi hàng cần source version/representation/feature/scope/parameter/working-space/adapter build và outcomes C8 riêng.

| Format/path | Đích ánh xạ | Gate hoặc giới hạn bắt buộc |
| --- | --- | --- |
| Native PikDraw v4 đề xuất | Appearance + ordered typed nodes, source values/provenance | Migration v2/v3, required versions, checksum/limits, save/recovery/reopen và Undo graph. |
| SVG static subset | `opacity`, `fill-opacity`, `stroke-opacity`; blend/filter subset sau gate | Kiểm inheritance/group wrappers, filterUnits/primitiveUnits, `stdDeviation`, filter region, color-interpolation-filters, transforms và clip order. Mở rộng parser/worker DTO/host validation cùng lúc; chưa có mapping thì export reject/loss report trước ghi. |
| PDF và AI PDF-compatible | Chỉ biểu diễn PDF đã whitelist: transparency, blend, soft mask, raster appearance nếu có | Tách appearance preview với editable effects; AI PDF không suy ra Illustrator live-effect params/authoring stack. Cần PDF adapter riêng đã đạt gate. |
| AI authoring data | Ánh xạ scope/order/params theo đúng source version, nếu parser có dữ liệu | Adapter/authoring parser hiện là nghiên cứu; không enable từ việc có native effects hoặc SVG pass. |
| CDR | Uniform transparency, merge mode và effect params theo version-family catalog | Không map radius/name trực tiếp; nguồn lens/feather/blur/shadow khác semantics phải reference/conversion/unsupported đủ bằng chứng. |
| EPS/PostScript | Converted representation với report, nếu converter/license/security đạt | Không hứa giữ live transparency/effect stack sau conversion; không coi EPS như SVG hoặc AI authoring. |
| DXF | Giữ source CAD entities/units/layers/blocks trong subset | Không map Shadow/Blur thành contour hoặc đường cắt. Appearance thiếu semantics cần issue hoặc conversion rõ; không suy production geometry từ hình preview. |

**Release guard:** ngay khi thêm node/opacity mới vào native model, mọi writer/worker proposal hiện có phải khai báo có encode được hay không. Trước khi SVG mapping mới đạt, workflow export có appearance ngoài subset phải chặn hoặc trình conversion/loss đã xác nhận, không âm thầm bỏ field. Native defaults giữ output cũ. Các file tải từ mạng đi broker/worker/provenance theo C8 và hướng dẫn vector; feature plan không cấp mạng cho parser.

## 8. Tasks triển khai đề xuất và dependency

BF00/BF00b và BF01–BF08 đã có task records; các trạng thái này là lịch sử, đối chiếu source hiện tại; không dùng task board. Các path giao nhau như compositor, workflow, Properties và test shell được làm tuần tự trong source gốc. Task `basic-effects-shadow-blend-20261017` bổ sung schema, render, storage, UI và nghiệm thu Shadow Blend; task culling tách riêng chỉ sở hữu `culling.hpp` cùng test culling.

| Task | Dependency và kết quả cần đạt | Owned paths đề xuất; gate |
| --- | --- | --- |
| **BF00 — Semantics/schema/reference** | C9 consumer review; khóa basic profile, local basis, order, clipping scope, finite support, blend/filter spaces, vectors và limits | `contracts/c9-appearance-effects.md`, `docs/decisions/0009-appearance-effect-graph.md`, `tests/fixtures/reference-fixtures/effects-basic.json`, kế hoạch này. C3/C4/C5 đọc review và đồng bộ ở BF00b trước code; C9/ADR giữ Proposed tới lúc đó. |
| **BF00b — C3–C5 consumer/storage alignment** | BF00; đối chiếu consumer document/render/color/storage/interop/desktop, giải quyết C5 đang mô tả codec v2 trong khi source dùng v3; khóa migration v3→v4, required schema, limits/recovery/checksum và effect bounds/color clauses | `contracts/c3-rendering.md`, `contracts/c4-color-typography.md`, `contracts/c5-storage.md`, `contracts/c9-appearance-effects.md`, `docs/decisions/0009-appearance-effect-graph.md`. Gate: semantics/migration nhất quán và có byte-level checklist trước BF01; không tuyên bố runtime. |
| **BF01 — Model/command/storage** | BF00 + BF00b; Appearance/node IDs, atomic patches và native codec/migration | `core/document/include/pikdraw/document/model/object.hpp`, `core/document/include/pikdraw/document/model/validation.hpp`, `core/document/include/pikdraw/document/effects/effect_stack.hpp` (mới, dùng path P22), `core/document/include/pikdraw/document/commands/appearance_commands.hpp` (mới), `core/document/src/commands/appearance_commands.cpp` (mới), `core/document/include/pikdraw/document/storage/native_format.hpp`, `core/document/src/storage/byte_codec.hpp`, `core/document/src/storage/native_format.cpp`, `core/document/src/storage/object_codec.cpp`, `core/document/src/storage/appearance_codec.cpp` (mới), `core/document/CMakeLists.txt`, tests commands/storage hiện có. Gate: default/migration/unknown-required/limits, stable IDs, Undo/reopen. |
| **BF02 — Compositor/Opacity và loss guard** | BF01; source surface, F/S/Object alpha, target conversion, bounds và export denial khi chưa mapping | `core/rendering/src/software_rasterizer.cpp`, `core/rendering/include/pikdraw/rendering/frame_target.hpp` (chỉ nếu đổi API có review), `core/rendering/src/raster/path_paint.cpp`, `core/rendering/src/raster/path_stroke.cpp`, `core/rendering/src/effects/object_compositor.hpp/.cpp` (mới), `core/rendering/include/pikdraw/rendering/culling.hpp`, rendering CMake/tests pixels/culling; `modules/interop/src/writers/svg_exporter.cpp`, capabilities và exporter test. Gate: overlap alpha analytic, transparent target, source color unchanged, effect-bearing output không bị bỏ field. |
| **BF03 — Properties/Opacity end-to-end** | BF02; đợt A dùng được | `apps/desktop/include/pikdraw/desktop/shell/native_window.hpp`, `apps/desktop/include/pikdraw/desktop/actions/action_id.hpp`, `apps/desktop/src/presenters/object_style.cpp`, `apps/desktop/src/presenters/inspector_presenter.cpp`, `apps/desktop/src/shell/native_window_menus.hpp`, `native_window_paint.cpp`, `native_window_style_events.cpp`, `native_window_events.cpp` trong cùng thư mục shell, desktop tests shell/actions/editor_tools. Gate: menu/bar/dock cùng state/commands, Mixed, draft/cancel, source save/reopen. Exact localization paths kiểm lại trước claim. |
| **BF04 — Blend subset** | BF02/BF03; đợt B | `core/rendering/src/effects/blend_modes.hpp/.cpp` (mới), object compositor, rendering tests/CMake; phần field/enum/menu/Properties đã claim lại tuần tự. Gate: sáu hàm B với transparent backdrop, painter order, ICC preview source intact, export mode ngoài subset bị chặn. |
| **BF05 — Gaussian CPU và editor** | BF00–BF04, bounded surfaces/preview tokens; đợt C | `core/rendering/src/effects/gaussian_blur.hpp/.cpp` (mới, path P23), `effect_graph.cpp`, `effect_cache.cpp` (mới, path P24), rendering CMake, `core/rendering/tests/effects/test_effects.cpp` (mới), shell/presenter/resource SVG và desktop tests được claim riêng theo source. Gate: impulse/step/transparent color, X/Y, transform, halo/tile seam, budget/cancel. |
| **BF06 — Drop Shadow và editor** | BF05; đợt D | `core/rendering/src/effects/drop_shadow.cpp` (mới, path P24), graph/cache/compositor, tests effects; UI/editor/assets theo BF03/BF05 claim lại tuần tự. Gate: source alpha, tint/offset/sigma, order, offscreen source, object opacity áp một lần và không ghost. |
| **BF07 — SVG effect mapping đã kiểm chứng** | Từng feature BF02/BF04/BF05/BF06 tương ứng; C8/C6 boundary review | `modules/interop/src/adapters/svg_importer.cpp`, `svg_worker_adapter.cpp`, `svg_import_record_codec.cpp`, `modules/interop/include/pikdraw/interop/ipc_dto.hpp`, codec/worker validator tương ứng, `modules/interop/src/writers/svg_exporter.cpp`, capability headers/matrix, C6/C8 và fixtures/tests liên quan. Chốt exact DTO/host paths sau đọc source; không mở chỉ parser mà quên host validation. Gate: adversarial schema+bounded import, attribute/graph semantic round-trip. AI/CDR/PDF/EPS adapter là task riêng ngoài BF07. |
| **BF08 — Tích hợp/nghiệm thu** | Các đợt được chọn đã đạt gate; source snapshot còn khớp | Tests E2E/editor tools, regression fixtures chuẩn, `docs/project-state.json`, tài liệu này và evidence cần review. Gate: đúng exe canonical, native screenshots, source save/reopen/export report, Release có assertion còn hiệu lực, p95/peak/cancel và nêu phần chưa đo. |
| **Shadow Blend extension — `basic-effects-shadow-blend-20261017`** | Native Shadow 1.0 giữ Normal; Shadow 1.1 thêm sáu blend modes, lưu/mở lại, backdrop compositing, UI guard cho node bật cuối và reselect-existing Edit | Contracts C3–C5/C9, ADR0009, effect model/validation/codec, object compositor/culling, DocumentWorkflow/Properties, independent reference fixture và focused acceptance tests. Gate: 6 backdrop vectors, source remains above shadow, object opacity once, invalid active order rejects without partial target write, reactivation preserves ID/parameters/revision. |

Các path viết dạng `name.hpp/.cpp` ở bảng là hai file đề xuất; khi tạo task phải khai báo từng path thật, không dùng glob. Không sửa file ngoài task để né ownership hoặc test fail.

BF00 kế thừa semantics C9; BF05/BF06 chuẩn bị CPU reference sau khi review ranh giới C3/resources. Điều đó không bỏ các gate của nhánh GPU và không đổi backend chỉ để làm hiệu ứng cơ bản.

## 9. Ma trận nghiệm thu theo rủi ro

Full Debug CTest mới nhất trên source sau restore đạt 80/80; focused Release 14 target effects/interop/desktop đạt 14/14 CTest. Lượt `tooling/native/build.ps1 -Test` mặc định hoàn tất build trước khi chạy CTest, nhưng CTest không chứng minh runtime UX, hiệu năng mục tiêu, round-trip định dạng ngoài hay kiểm thử accessibility/DPI thủ công.

| ID | Given / When / Then | Verify đề xuất |
| --- | --- | --- |
| **AC-BF-01 Opacity overlap** | Given fill/stroke opaque chồng nhau; When Object Opacity=50%; Then alpha vùng chồng trên target trong suốt là 0,5, không tăng thành 0,75. Màu alpha 0,5 với object 0,5 ở vùng chỉ fill cho alpha 0,25. | Analytic pixel oracle; rendering pixels/effects test. |
| **AC-BF-02 Source preservation** | Given RGB/CMYK/spot/gray có alpha/overprint và editable path IDs; When đổi opacity/blend/filters rồi Undo/Redo/save/reopen; Then source channels, IDs, text/font và hierarchy còn nguyên; output theo graph đã lưu. | Document commands/storage fidelity; so data và pixel riêng. |
| **AC-BF-03 Blend** | Given source/backdrop có alpha 0/0,5/1; When chọn sáu modes; Then kết quả theo công thức reference và painter order, backdrop alpha 0 cho màu source đúng. | Numeric hand vectors cho từng mode và representative alpha cases, không snapshot tự sinh từ implementation. |
| **AC-BF-04 Blur kernel** | Given impulse/step và pixel alpha 0 chứa màu khác; When sigma=0 hoặc X/Y khác nhau; Then identity đúng, kernel normalized/symmetric theo support đã khóa và không sinh viền màu từ hidden RGB. | CPU reference, independent convolution/oracle, kiểm alpha và RGB riêng. |
| **AC-BF-05 Transform/units** | Given node local-mm, object có scale/rotation/reflection/shear; When đổi zoom/DPI hoặc transform; Then source sigma/offset không đổi do camera; effect world shape đúng basis. Singular transform trả diagnostic. | Geometry transform oracle + render comparisons ở sampling grid đã ghi. |
| **AC-BF-06 Shadow alpha** | Given input alpha 0,5, shadow opacity 0,5, color alpha 1, object opacity 0,5; When sigma=0 và offset tách vùng; Then alpha vùng chỉ shadow là 0,125; bóng không có hình chữ nhật background. | Analytic pixels và native scene đối chiếu. |
| **AC-BF-07 Bounds/order** | Given shadow của object ngoài viewport đi vào viewport và stack Blur/Shadow; When pan/reorder/disable/delete; Then không cull mất bóng, order có output riêng và không ghost; geometry sản xuất không đổi thành effect bounds. | Culling/effects regression + structural geometry oracle. |
| **AC-BF-08 Atomic/Mixed** | Given nhiều targets với scalar khác nhau hoặc một target locked/unsupported; When set field/add node; Then Mixed đúng, valid targets commit một Undo; mixed-invalid targets không đổi object nào. | Commands + desktop acceptance. |
| **AC-BF-09 Preview/stale** | Given slider/draft và async job cũ; When Escape, selection/document/profile/camera đổi, undo hoặc close; Then stale preview không thay frame/source, candidate resources được nhả, revision/undo chỉ đổi khi commit hợp lệ. | Lifecycle/generation tests với delayed result. |
| **AC-BF-10 Codec/limits** | Given v2/v3 source hợp lệ hoặc v4 unknown-required/corrupt/over-budget; When open/save/recovery; Then default migration đúng, required data không mất và reject giữ file/document cũ; old reader reject v4. | Storage fixtures/negative paths và atomic-save existing suite. |
| **AC-BF-11 Resource/seams** | Given tiled/full-frame render và request sigma/area lớn; When render/cancel/cache evict; Then halo không seam trong tolerance công bố, không overflow/allocate vô hạn, budget failure có diagnostic và không âm thầm bỏ node. | Effects tests; peak/work/cancel trace đúng scene/hardware/build. |
| **AC-BF-12 UI/context** | Given no selection, single, mixed, locked, Text backend thiếu hoặc dock đóng/bar hẹp; When gọi menu, nhập locale number/IME, mở lại Properties; Then đúng enable state, đủ params/hints, navigation không đổi revision; field bar/dock đồng bộ. | Native shell/editor tests + manual focus/keyboard/DPI theo khả năng máy. |
| **AC-BF-13 Format loss** | Given native effect ngoài SVG writer subset hoặc AI/CDR/PDF/EPS/DXF chưa có mapping; When import/export theo workflow; Then đúng capability/diagnostic/conversion trước commit/ghi file, không mất opacity/filter hoặc đổi mode âm thầm; SVG export bị từ chối phải giữ nguyên byte file đích đã tồn tại. | `test_pikdraw_interop_svg_adapter`, `test_pikdraw_interop_svg_exporter` và `test_pikdraw_vertical_slice_e2e`. |
| **AC-BF-14 SVG semantics** | Given SVG có units, color-interpolation, filter region, source alpha và transform trong catalog đã hỗ trợ; When import-edit-native-save/reopen-export; Then graph/params/alpha/appearance giữ theo gate; ngoài catalog vẫn reject/loss policy. | Independent SVG render oracle + structural comparison, chỉ sau BF07. |
| **AC-BF-15 Text/native evidence** | Given Text glyph thật và Path cùng effect; When render/edit/save/reopen; Then text vẫn edit được, font/fallback status đúng và effect tác động trên glyph alpha thật. | Typography/render E2E; ảnh native không thay data assertion. |
| **AC-BF-16 Add-effect draft lifecycle** | Given một Path/Text hợp lệ và draft Blur/Shadow; When preview, sửa số, apply, cancel hoặc selection/document/revision đổi; Then preview không sửa persistent state, Apply hợp lệ tạo đúng một revision/Undo, Cancel trả lại source và stale draft bị từ chối. | `test_pikdraw_effect_draft`; Debug CTest 1/1 passed. Sync renderer only; delayed async cancellation chưa được kiểm chứng. |
| **AC-BF-17 Shadow Blend + Edit existing** | Given một object có Shadow đã chỉnh, When đổi Shadow Blend, chọn lại Drop Shadow, hoặc thử đặt sau node bật khác; Then mode dùng backdrop thật, source vẫn ở trên, node/ID không nhân bản, object opacity tác động toàn kết quả một lần, stack sai bị chặn và revision chỉ tăng cho thay đổi thật. | `test_pikdraw_rendering_effect_compositor`, `test_pikdraw_document_storage_fidelity`, `test_pikdraw_effect_draft`, `test_pikdraw_rendering_culling`; latest full Debug CTest 80/80. |

## 10. Lệnh và bằng chứng hiện có / còn thiếu

**Verification status:** Debug build và CTest đạt 80/80, bao gồm storage, rendering, effects, UI shell và interop. Focused Release build của 14 target storage/effects/interop/desktop đạt 14/14 CTest. Xem chi tiết số đo tại [performance report](../reviews/basic-effects-performance-20261017.md).

```powershell
# Focused effects, storage, desktop, interop and workflow targets (Debug)
.\tooling\native\build.ps1 -Test -TestTarget test_pikdraw_document_storage_fidelity,test_pikdraw_rendering_effect_compositor,test_pikdraw_rendering_culling,test_pikdraw_rendering_blend_operator,test_pikdraw_rendering_gaussian_blur,test_pikdraw_rendering_drop_shadow,test_pikdraw_rendering_float_scratch_paint,test_pikdraw_effect_draft,test_pikdraw_effect_render_warning,test_pikdraw_desktop_shell,test_pikdraw_vertical_slice_e2e,test_pikdraw_interop_svg_exporter,test_pikdraw_interop_svg_adapter -BuildOnly
.\tooling\native\build.ps1 -TestsOnly -TestRegex '^(test_pikdraw_document_storage_fidelity|test_pikdraw_rendering_effect_compositor|test_pikdraw_rendering_culling|test_pikdraw_rendering_blend_operator|test_pikdraw_rendering_gaussian_blur|test_pikdraw_rendering_drop_shadow|test_pikdraw_rendering_float_scratch_paint|test_pikdraw_effect_draft|test_pikdraw_effect_render_warning|test_pikdraw_desktop_shell|test_pikdraw_vertical_slice_e2e|test_pikdraw_interop_svg_exporter|test_pikdraw_interop_svg_adapter)$'
# Full registered CTest suite, after all test targets are built
.\tooling\native\build.ps1 -TestsOnly
.\tooling\native\build.ps1 -TestsOnly -Configuration Release
```

App path: `build/win-dev/apps/desktop/pikdraw.exe`. Các kiểm thử tự động xác minh GDI paint path và handler, không thay thế kiểm tra OS thủ công hoặc DPI/accessibility.

BF11 Clear Effects and keyboard-handler coverage, BF12 Preview/Apply/Cancel, BF13 reactivation/edit-existing, and BF17 Shadow Blend coverage passed in the earlier 79/79 Debug/Release runs and the latest 80/80 full Debug run. The Shadow Blend test adds independent six-mode backdrop values, source-over-shadow ordering, object-opacity-once, schema 1.1 save/reopen, invalid stack rejection with atomic target behavior, Properties click, and reselect-existing ID/revision checks. This does not establish all-platform OS interaction.

The [performance report](../reviews/basic-effects-performance-20261017.md) records the SSE2 and AVX comparisons: AVX runtime dispatch preserves parity but is slightly slower on the measured i7-3537U, and both paths remain above the 33.3 ms interaction target. The bounded two-lane experiment was rejected after it failed to show a useful same-fixture win. A controlled scalar A/B and total process/allocator peak remain open; actual present timing, cancel latency, Text glyph effect behavior, DPI/accessibility, and external-format fidelity also remain open. Native PikDraw v4 persistence is exercised; SVG import does not accept appearance effects, SVG export rejects effect nodes/non-Normal object blend, and PDF/AI/CDR/EPS have no adapters. No external format compatibility or round-trip claim follows from these native tests.

## 11. Tiêu chí bàn giao và quyết định còn cần khóa

BF00 phải ghi kết quả consumer review cho appearance schema/native-v4 byte layout, local transform semantics, working-space conversion, 4-sigma support/tolerance, quotas và vectors. Nếu nguồn/version chưa có fixture hoặc app nguồn để tạo oracle, hàng compatibility vẫn unknown/unsupported; không tự làm đầy bảng bằng giả định.

Mỗi đợt báo kết quả trực tiếp với paths thay đổi, source baseline/snapshot, list ca nghiệm thu, command/kết quả thật và giới hạn. Cập nhật project-state chỉ khi tích hợp capability đã kiểm chứng; gỡ entry disable/roadmap ở UI chỉ cho handler đạt gate. Giữ hồi quy hữu ích, dọn test/output tạm theo AGENTS. Code model, storage, opacity/blend compositor, Blur/Shadow, Properties, toolbox, Clear Effects và keyboard-only handler đã tồn tại trong source. Add-effect draft có kiểm thử riêng; symmetric SSE2 convolution giảm đáng kể CPU time nhưng chưa đạt target 33.3 ms; AVX dispatch giữ parity nhưng chậm hơn SSE2 trên CPU đã đo. Thử nghiệm bounded parallel bị từ chối do không cho thấy lợi ích; tiếp tục tối ưu thuật toán và đo scratch. Vendor-format fidelity còn mở. BF07 không làm SVG import hay export tự động bảo toàn effects nếu representation/semantics chưa được kiểm chứng.
