# Kế hoạch quản lý màu thống nhất — C00–C09

Trạng thái cập nhật 21/10/2026: **triển khai một phần, chưa nghiệm thu đầy đủ C00–C09/AC-CM01–20**. C01 hiện có immutable ICC resource cho RGB/CMYK/Gray: kiểm tra header/model/tag bounds, quota 16 MiB, SHA-256 từ bytes bằng Windows CNG và registry lookup theo digest. Page có thể gắn resource bất biến qua command có Undo; native v15 khử trùng resource theo digest, giới hạn tổng 64 MiB và xác minh digest/space khi mở. Native v16 lưu basis màu của pixel preview raster; v17 lưu ICC binding cho fill/stroke, gradient stop, text fill và shadow paint; v18 lưu Dimension/Contour theo ObjectId cùng typed Contour paints, vẫn đọc v2–v17. Storage/Contour round-trip kiểm CMYK, Spot, alpha, overprint và ICC pass 3/3; Direct2D Spot contour pixel regression cũng pass. Settings có lệnh gán profile trang; canvas thử dùng ICC của trang đang mở và trở về profile preview chung nếu Windows CMM từ chối. Selection có Assign ICC giữ channel values và Convert ICC dùng transform theo từng nguồn. Hộp thoại màu giữ binding khi sửa cùng không gian RGB/CMYK bằng ICC transform phù hợp; nếu CMM không tạo được transform, Apply bị chặn. Paint preview tùy chỉnh vẫn có thể gần đúng. C02 hiện có report phân biệt ICC-managed/fallback/preset/no-op, document commands lưu summary và color dialog dùng conversion core cho RGB/CMYK; page/document/selection có lệnh Convert ICC theo transform với Undo; test ICC với SWOP cài trên máy đã chạy. Lệnh DPI hiệu ứng trang/tài liệu đã sửa kiểm tra no-op để không nhầm `color_mode_explicit` với `raster_dpi_explicit` và xét đủ mọi trang ở document scope; hồi quy xác nhận đổi sang DPI mặc định vẫn lưu metadata, document command không bỏ qua trang khác khi active page đã khớp, và Undo khôi phục trạng thái từng trang. Chuyển màu không còn mặc định giới hạn TAC ở 320% hoặc tự chọn Black Generation Pure K; caller phải chọn chính sách black/TAC rõ ràng. C06 có backend Validate TAC chỉ đọc và Fix TAC theo document/active page/selection, nhận output-condition ID cùng ngưỡng tường minh; Fix là một Undo, giữ K/alpha/overprint và nén tỷ lệ CMY, báo before/after, spot và raster chưa đánh giá. Settings > Color & ICC hiện có manual TAC preflight với scope, Check chỉ đọc, Fix riêng và keyboard focus; ngưỡng chưa gắn profile. Đây chỉ là TAC trên process CMYK paints, chưa có output preset registry, raster separation, ICC appearance guarantee hay gamut/colorimetric oracle. C03 sửa cache Spot theo alternate/alpha và giới hạn 4096 entry, nhưng cache này chưa được chứng minh là renderer production đang dùng. Raster decoder dùng WIC color context/transform để chuyển profile nguồn sang sRGB một lần nếu WIC hỗ trợ; ICC bytes và original source vẫn giữ, basis được ghi lại, còn trường hợp chưa chuyển được tạo fidelity issue. `test_pikdraw_interop_raster_image_decoder` kiểm profile sRGB iCCP, pixel basis và RGBA; worker adapter và native storage fidelity cũng pass. Raster writer TIFF/JPEG có chuyển pixel sang RGB ICC đầu ra trước khi có thể nhúng profile; `File > Export Raster…` có hộp thoại native đúng theo [phiếu UI](../../design/features/raster-export-20261021.md), nhưng kiểm visual runtime chưa xong. Pixel oracle ngoài sRGB, CMYK/Gray output và selection export raster còn mở. Paint-level custom ICC preview/runtime fidelity, raster preview management khi WIC từ chối profile, proof/gamut oracle, overprint compositor, interop color preservation và print release gate vẫn còn mở.

Liên kết: [kế hoạch tổng](../existing-feature-upgrade-20261020.md), [trình tự triển khai](implementation-plan.md), [đơn vị/schema](units-plan.md), [UI](../../design/features/editor-consistency-upgrade-20261020.md), [C8/C9 và vector](../../architecture/vector-resource-interoperability.md), [mục tiêu màu trước đó](../color-system-advancement-plan-20261020.md).

## 1. Phần giữ lại và bằng chứng cần sửa

Giữ RGB/CMYK/Spot/Gray, overprint metadata, lựa chọn đen/TAC đang có, lệnh chuyển document/page/objects với Undo, source ICC của raster, proof/gamut controls và route chọn đối tượng theo màu. Giữ toàn bộ source values khi preview hoặc chuyển monitor. Các đường cũ chuyển thành consumer/alias của service chung sau khi chứng minh tương đương.

Các phát hiện dưới đây đọc từ working tree ngày 20/10/2026, có local edits của công việc khác; không phải kết quả chạy app. Đọc lại diff trước sửa.

| ID | Evidence | Kết luận / sửa cần làm |
| --- | --- | --- |
| CM-F01 / P0 | `core/color/.../values/color_value.hpp`: variant nguồn + overprint, chưa có profile binding từng paint; page giữ profile name; raster metadata đã có ICC bytes | Không được nói “không có ICC”; cần bind profile có identity thật cho paint/working space, giữ raster provenance |
| CM-F02 / P0 | `profiles/profile_id.hpp`: equality tên+space, bỏ checksum; registry lookup theo tên; default CMYK registry ISO Coated v2 300%, default document SWOP | Tên không chứng minh bytes/condition; hai profile cùng tên khác bytes không được coi là một; hợp nhất defaults theo resource thực |
| CM-F03 / P0 | `preview/color_transform.cpp::convert_color_to_mode` và `native_style_color_model.hpp::source_color::convert` | UI dialog dùng approximate RGB/manual CMYK, batch có ICC; cùng input có thể ra khác. Một service, cùng request và report |
| CM-F04 / P0 | `core/document/src/commands/color_commands.cpp`: document no-change kiểm active page trước vòng tất cả page; page command có thể relabel profile cùng model; selection profile không vào transform | Repro mixed-page early no-op, same-model profile conversion và scope; không đổi nghĩa lệnh Convert đang có thành Assign |
| CM-F05 / P0 | Core convert trả ColorValue, fallback công thức; confirmation workflow có cảnh báo approximate; profile label truyền mặc định riêng với transform đã nạp | Không nói mọi fallback im lặng; cần report có chất lượng/actual profile, không ghi label SWOP cho transform profile khác |
| CM-F06 / P0 | `preview/icc_preview.cpp`: Windows CMM, relative colorimetric, destination system sRGB, slots RGB/CMYK | Có ICC transform thực; chưa là pipeline calibrated monitor/intent/BPC đầy đủ hoặc profile-aware trên từng nguồn |
| CM-F07 / P0 | Gamut dùng RGB→CMYK→RGB rồi distance RGB encoded >0.12; proof là round-trip này | Không gọi khoảng cách này DeltaE hay gamut oracle. Tách diagnostic khác màu proof và out-of-gamut có thuật toán/capability rõ |
| CM-F08 / P0 | Overprint flag/toggle và `simulate_overprint` helper; không tìm thấy consumer compositing trong renderer | Multiply RGB helper chưa chứng minh overprint ink/knockout; flag ON không được quảng bá như mô phỏng RIP đã đúng |
| CM-F09 / P0 | Dark RGB <=.02→PureK100 và force cả fill/stroke OP; RichBlack preset .4/.3/.3/1; same-CMYK early return bỏ qua TAC | Black/OP phải theo role/policy rõ; không auto đổi source OP chỉ vì gần đen; TAC không chỉ xử lý RGB mới chuyển |
| CM-F10 / P1 | `ColorPreviewCache`: spot hash name+tint, bỏ alternate/alpha; chưa thấy production consumer ngoài tests; IccPreview có cache riêng bounded 4096 | Sửa latent library key/limit; không gán nó là nguyên nhân viewport đã tái hiện. Audit cache nào thực sự được gọi |
| CM-F11 / P0 | `native_window_style_paint.cpp`: stop list/selected stop dùng `to_preview_rgba`, swatches khác dùng `color_preview_->convert`; GradientSampler dùng preview | Swatch và canvas có thể khác với actual ICC. Một projection màu, số source không đổi theo preview |
| CM-F12 / P0 | `gradient_sampler.cpp`, path/effect compositor: C9 encoded-sRGB blend/interpolation và linear-sRGB blur; preview hiện chạm trước compositing | Thiết kế vị trí source→working→view rõ; thêm monitor/proof không được vô tình đổi gradient/alpha/effect semantics |
| CM-F13 / P1 | Raster decoder giữ embedded ICC bytes; chưa thấy renderer tiêu thụ `icc_profile` tương ứng | Giữ bytes chưa chứng minh pixels được color-managed. Trace decode pixel basis/ICC transform/placement trước kết luận |
| CM-F14 / P1 | Color matching cùng model channels ±.005; spot name+tint; model filter ưu tiên fill, stroke chỉ khi không fill | Phân biệt same-source và same-appearance, fill/stroke/gradient scope. Kiểm consumer và policy khóa/ẩn trước đổi selection semantics |
| CM-F15 / P1 | SVG exporter có cảnh báo conversion RGB; PDF editable extraction trả projected color; locked PDF giữ bytes; raster writer TIFF/JPEG RGBA8 có RGB ICC conversion | Báo loss/capability đúng từng representation; nguyên bản giữ được không có nghĩa editable hoặc xuất separation đúng |
| CM-F16 / P1 | ICC tests có system sRGB; CMYK profile test có thể NOT_MEASURED; gamut/OP tests dùng heuristic/helper | Test helper pass và “khác naive formula” không là oracle accuracy, print proof hay compositor |

Tab switch hiện đã gán lại `color_preview` cho renderer đang active; không tạo task “sửa thiếu tab rebind” khi chưa có repro khác. Các const setters trong IccPreview dùng mutable state dưới mutex cần xem ownership/cache generations; chưa có bằng chứng data race để kết luận.

## 2. Những quyết định kỹ thuật bắt buộc

### 2.1. Dữ liệu nguồn, profile và hai loại lệnh

- Giữ ColorValue channels/alpha/overprint; thêm typed optional `ProfileResourceId` ở paint/color source binding phù hợp model hiện hành. Tránh nhét full ICC bytes/hash vào mỗi stop.
- Document resource dictionary giữ bytes immutable, SHA-256 theo bytes, space/class/version, origin và validation status. Name chỉ là label; cùng tên khác hash là khác resource.
- Working defaults có map RGB/CMYK/Gray ở document/page theo scope hiện tại; nguồn explicit profile ưu tiên hơn default. Untagged, explicit, imported và legacy-unresolved là state riêng.
- Legacy chỉ có tên profile không được tự gắn một file cùng tên trên máy rồi coi là nguồn đã biết. Gợi ý resolve có report; mở file không rewrite.
- Spot có stable identity, tên nguồn, tint, alternate color và alternate profile; giữ ink identity khác display alternate. Lab/DeviceN nếu chưa có model thì giữ provenance/unsupported report, không giả là RGB editable đầy đủ.
- **Assign profile:** giữ số channels, thay cách diễn giải/profile binding bằng command có Undo.
- **Convert profile/mode:** chuyển số để đạt mục tiêu appearance theo actual source/destination, scope và policy; same-model khác ICC cũng phải convert. Lệnh Convert document/page/selection đang có tiếp tục semantics chuyển số.
- UI phải cho thấy khác biệt Assign/Convert và số object/paint bị tác động. Không đổi geometry, text source, IDs hoặc target page ngoài scope.

### 2.2. Service và kết quả có thể kiểm chứng

~~~text
ColorTransformRequest {
  source_color/binding, destination_profile_resource, operation,
  intent, black_point_compensation, black_policy, overprint_policy,
  tac_policy, paint_role, scope, strictness, context_generation
}
ColorTransformResult {
  color/binding, quality {managed, approximate, unsupported, failed},
  actual_source/destination_hashes, engine/version, applied_policies,
  changed_fields, warnings/losses
}
ViewColorContext {
  working_space, proof_profile/options, display_profile,
  monitor_id, generation, transform_capabilities
}
~~~

Tên/interface trên là đề xuất, chưa phải API đã có. Batch command preflight **toàn bộ scope**, không chỉ active page; snapshot kết quả trước commit; lỗi/budget/stale không áp dụng một nửa. Không tạo Undo cho no-op thật. Report phân biệt profile missing/invalid và empty selection.

Source→working transform chuẩn bị màu cho C9. Gradient interpolation/blending/filter giữ contract đã công bố. Sau compositing mới áp view/proof/display ở vị trí đã có oracle; swatches có cùng source→working→view projection nhưng không trộn overlay diagnostic vào số nguồn. Spot/overprint có thể cần pipeline separations riêng theo C05; không ép proof mỗi stop thành tương đương proof final composite.

Rendering UI chrome không dùng document proof. Raster preview/source pixels ghi rõ encoded/linear basis; không áp ICC hai lần vào ảnh đã chuyển.

### 2.3. Engine, proof và giới hạn

Giữ Windows CMM adapter trước; khai báo capability intents/BPC/proof/gamut/profile classes có bằng chứng. [Microsoft CreateMultiProfileTransform](https://learn.microsoft.com/en-us/windows/win32/api/icm/nf-icm-createmultiprofiletransform) mô tả transform nhiều profile, không tự bảo đảm ứng dụng đã chọn đúng pipeline.

Nếu Windows API/integration không đáp ứng proof/BPC/gamut cần thiết, đánh giá LittleCMS qua cùng interface, pin release/hash và license trước tích hợp. Tham khảo [transicc](https://github.com/mm2/Little-CMS/blob/master/utils/transicc/transicc.c) và [tificc](https://github.com/mm2/Little-CMS/blob/master/utils/tificc/tificc.c); không triển khai engine thứ hai chỉ để có thêm checkbox. Output differences phải có engine/version và oracle, không ngầm thay toàn bộ màu khi nâng dependency.

- Display profile lấy đúng active monitor; fallback sRGB có status. Di chuyển cửa sổ/monitor đổi view generation, không dirty document.
- Soft proof cần actual output profile, proofing intent, display transform và những tùy chọn paper-white/black simulation mà engine đã hỗ trợ. Chưa có capability thì control giữ route nhưng disable hoặc label approximation rõ.
- Gamut warning dùng CMM gamut predicate nếu capability được kiểm, hoặc diagnostic proof-difference được **đặt tên đúng**. Nếu dùng DeltaE, cần PCS Lab/reference white/adaptation và thuật toán xác định; không đổi tên RGB norm thành DeltaE.
- Transform thiếu/fail là Unknown/Unavailable, không coi là in gamut hoặc proof success.
- BPC là lựa chọn xử lý black points, không cùng khái niệm Pure K preservation; tham khảo [ICC BPC](https://www.color.org/WP40-Black_Point_Compensation_2010-07-27.pdf). Assign, convert, view và proof là các boundary khác nhau theo [ICC](https://www.color.org/getting-started/).

### 2.4. Mực đen, TAC và overprint

TAC là tổng C+M+Y+K của process channels theo đơn vị phần trăm; giới hạn dựa trên output condition đã xác minh, không gọi 320% là chuẩn chung. [ECI offset profiles](https://eci.org/doku.php_id%3Den_colorstandards_offset.html) có các điều kiện/profile khác nhau, gồm biến thể giới hạn mực khác nhau.

- Giữ preset Pure K100/Rich Black hiện có; preset .4/.3/.3/1 là công thức cụ thể TAC 200%, không tự nhận phù hợp mọi máy/giấy.
- Black preservation có paint role (text/fill/stroke/effect), threshold và chính sách explicit; không force OP cả fill/stroke chỉ vì RGB gần đen.
- “Validate TAC” không sửa channels. “Fix TAC/Convert” là command riêng có preview/report, Undo, kể cả nguồn đã CMYK. Không khẳng định mọi ICC chứa một tag TAC có thể đọc trực tiếp.
- OP fill/stroke, knockout, white-overprint, spot tint và process ink cần model có oracle separations. RGB multiply giữ như approximate diagnostic nếu vẫn cần, label không đánh đồng mô phỏng RIP.
- Groups/masks/blends/DeviceN ngoài subset phải báo Unsupported/Approximate, giữ source/provenance. Không công bố toàn bộ PDF/X/separations chỉ vì một subset overprint đạt.
- Calibration máy in, giấy, RIP và physical print không được suy từ scalar transform tests.

## 3. Thực thi C00–C09

### C00 — baseline màu và ca tái hiện / P0

**Sở hữu:** tests hiện có của color/document/desktop/rendering; reference fixtures hợp lệ; docs capability. **Sau:** U00/U01.

1. Tạo fixtures có profile bytes hợp lệ, hash và license/provenance; generated fixture cũng phải có expected độc lập, không lấy output chính service làm oracle.
2. Repro document mixed pages: active page đã target, page khác chưa target; cả hai thứ tự active page. Repro cùng CMYK nhưng hai profile khác bytes.
3. So color dialog, selection/page/document conversion cho cùng input/profile/policy; so gradient swatch với canvas.
4. Trace OP toggle đến compositor, raster ICC đến pixels, cache hit/miss paths. Ghi supported/missing thay vì sửa test để hợp badge.
5. Ghi engine/profile hashes/backend/display/intent. Mandatory fixtures thiếu là gate fail; optional installed profile thiếu là NOT_MEASURED, không báo accuracy pass.

**Gate:** AC-CM01/02/03 và baseline AC-CM04/09/12/17. Không rewrite source chỉ để mở preview.

### C01 — tài nguyên profile và storage / P0

**Sở hữu:** core/color profiles, document paint/page/resources/commands/storage; phối hợp S02 và import/PDF local edits.

1. Implement identity/resources và explicit/untagged/legacy-unresolved states; reject malformed ICC, space mismatch, vượt size quota.
2. Check hash bytes và schema; profile name/space equality không thay hash equality. Không nạp file path từ document ngoài broker/policy.
3. Thêm binding phù hợp solid/gradient stop/text/effect/image; không bỏ alpha/OP/spot alternate. Deduplicate theo bytes và immutable ownership.
4. Đọc native version mới nhất (khảo sát v12), migration table, unknown-feature behavior; phối hợp một schema change đã chốt, không mặc định v13.
5. Save/reopen/Undo/clipboard giữ source profile bytes/binding; legacy mở không tự dirty. Metadata mới không làm exporter cũ lén bỏ profile.

**Tiến độ 21/10/2026:** `ProfileResource::create` nhận bytes thay vì path, giới hạn 16 MiB, kiểm kích thước/header `acsp`, color-space signature và bảng tag có bounds/alignment; SHA-256 được tính từ chính bytes bằng Windows CNG. Resource trả ra là `shared_ptr<const ...>` và `ProfileRegistry` đăng ký/tìm bằng digest, nên hai profile trùng tên nhưng khác bytes không thể phân giải nhầm qua identity. `test_pikdraw_color` kiểm digest known-answer, sai kích thước/header/model/tag và lookup registry; `test_pikdraw_icc_preview` còn nạp profile sRGB hệ thống và SWOP tùy máy qua resource validator. Đã chạy `.	ooling\native\build.ps1 -Test -TestTarget test_pikdraw_color,test_pikdraw_icc_preview -TestRegex '^(pikdraw_color_tests|test_pikdraw_icc_preview)$'`: **2/2 CTest pass**. Chưa kiểm ICC semantic validity bằng CMM, chưa có broker/import UI, binding cho document/page/paint, schema/storage migration, clipboard hoặc Undo; C01 vẫn mở.

**Bổ sung 21/10/2026:** `ProfileResource::load_file` nạp file theo quota thành bytes bất biến; `IccPreview` mở ICC từ `PROFILE_MEMBUFFER`, gọi CMM semantic validation và giữ resource sống cùng transform. NativeWindow truyền profile RGB/CMYK đã nạp và đăng ký identity theo digest. Hai color targets pass với sRGB và SWOP trên máy này; bindings document/page/paint, schema/storage, clipboard, broker/import UI và Undo vẫn mở.

**Gate:** AC-CM01/05/14/18. Model versioning phải đi trước consumers, tránh profile-name label giả.

### C02 — một conversion service và command scope / P0

**Sở hữu:** color transform service, color_commands, document_workflow_color, native_style_color_model/dialog và automation tương ứng. **Sau:** C01/U01; U02 consumer.

1. Tạo request/result, actual profiles/engine/quality; wrapping engine hiện có trước khi mở capability mới.
2. Thay UI manual conversion bằng service; preset/interactive/batch dùng một validator. Giữ fallback có report; strict production từ chối approximate thiếu profile.
3. Xóa early-return dựa chỉ trên model; cùng model khác profile xử lý thật. No-change đánh giá cả scope và paint bindings.
4. Fix document active-page early no-op; page/selection cùng scope contract. Không relabel same-model thành convert thành công.
5. Tách Assign ICC và Convert ICC explicit, giữ action cũ Convert channels; atomic transaction và one Undo; giữ source OP trừ policy người dùng chọn.
6. Matching source/appearance không được gọi transform trong menu can_execute; precompute/budget query khi user chạy.

**Gate:** AC-CM02/03/04/06/10/15.

**Tiến độ 21/10/2026:** `ProfileColorTransform` giữ một transform Windows CMM tái sử dụng theo resource bytes nguồn/đích, hỗ trợ RGB↔RGB, RGB↔CMYK và CMYK↔CMYK với bốn rendering intent. Convert giữ alpha/overprint; profile giống hệt trả no-op diagnostic, color space khác với source bị từ chối. `ConvertPageColorProfileCommand` yêu cầu page đang bind đúng ICC nguồn, chuyển fill/stroke/gradient stops/shadow/text và binding trong một command/Undo; Spot/Gray và embedded image ICC giữ nguyên. Menu Edit và profile manager gọi native workflow theo PageId; picker kiểm profile RGB/CMYK, chọn intent rõ ràng, xác nhận channel change và báo one-step Undo. Action disable khi thiếu ICC nguồn đã resolve hoặc tài liệu bị khóa. `ConvertDocumentColorProfileCommand` nhận transform cho từng profile nguồn khác nhau, preflight toàn bộ trang, bỏ qua trang đã ở đích, cập nhật binding còn lại trong một Undo và không commit một phần nếu trang thiếu ICC/paint sai model/transform thiếu. Edit > Convert Document ICC Profile gọi workflow, chọn một intent dùng chung, bật khi mọi trang có ICC nguồn resolve và giữ active PageId. Paint-level binding và selection profile conversion hiện đã có command/workflow: Assign giữ số màu; Convert preflight RGB/CMYK paints đã chọn theo ICC nguồn riêng, gắn ICC đích và hoàn tác bằng một Undo. Spot/Gray/ảnh giữ nguyên khi Convert. Windows CMM không cung cấp BPC qua API đang dùng nên UI báo unavailable. `test_pikdraw_document_commands`, `test_pikdraw_desktop_shell` và `test_pikdraw_desktop_actions` pass 3/3. Native walkthrough và oracle độc lập colorimetric chưa chạy.

**Bổ sung 21/10/2026:** Menu Convert Selection to RGB/CMYK và Convert Active Page to RGB/CMYK nay truyền effective page ICC preview vào cùng workflow; Convert Document RGB/CMYK truyền preview nguồn cấu hình chung. Status hiển thị số màu ICC-managed/fallback, chính sách đen/TAC và Undo. `test_pikdraw_document_color_workflow` phủ menu selection/page conversion, quality summary và Undo; pass 1/1. Document-wide mode conversion vẫn không thay thế Convert Document ICC Profile theo từng source profile.

### C03 — pipeline render, swatch, raster và cache / P0

**Sở hữu:** IccPreview/view context, renderer paint/gradient/image/effects, shell color paint, monitor handling. **Sau:** C01/C02; phối hợp U05/U06.

1. Trace và version source/working/filter/composite/view stages; preserve C9 encoded-sRGB blend và linear-sRGB blur hiện có trước khi bàn đổi working space.
2. Swatches/selected stop/palette/Properties/status sampling gọi cùng projection; source numeric controls không dùng proof pixels làm values.
3. Raster decode metadata ghi pixel basis; apply embedded ICC đúng một lần hoặc report chưa hỗ trợ. Giữ original bytes/high-bit metadata và alpha.
4. Bind display profile theo monitor; tab/page/options change bump view generation. Không làm dirty document hoặc rebuild geometry vì đổi monitor.
5. Cache key có source identity/channels/alpha/spot alternate/profile hashes, operation/options, engine version và generation; hash collision có equality, bounded bytes/entries, snapshot ownership.
6. Sửa ColorPreviewCache library nhưng không giả nó là production path; test cache đang dùng và cache public riêng. Kiểm 16-bit CMM channel quantization/tolerance.

**Bổ sung 21/10/2026:** preview nguồn RGB/CMYK dùng validated in-memory ICC resource trong Windows CMM; đổi preview profile không yêu cầu CMM tự mở lại source path. Page ICC binding/storage có regression; shell đã nối profile đang gắn vào page preview, nhưng native visual behavior chưa được kiểm. Raster decoder nay áp dụng WIC ICC transform sang sRGB đúng một lần nếu khả dụng, giữ nguồn/profile, lưu pixel basis; fallback tạo import fidelity issue. Shadow ribbon, color dialog và Spot palette swatches nay dùng cùng effective page `IccPreview` như selected stop/canvas; Spot route chưa có pixel regression. Proof/Gamut không giữ cờ bật khi profile hiệu lực thiếu transform CMYK; `test_pikdraw_desktop_shell` kiểm route không profile. Pixel sampling/native visual chưa chạy. Paint source binding, palette/status sampling, monitor/proof consistency và native visual/pixel-oracle validation cho raster export vẫn mở.

**Bổ sung 21/10/2026:** `AssignPageColorProfileCommand` gắn resource ICC theo đúng RGB/CMYK mode của page, giữ nguyên channel values và cho Undo/Redo. Native v15 lưu dictionary ICC dùng chung theo SHA-256, kiểm tra digest, color space, reference index, mỗi resource 16 MiB và tổng cộng 64 MiB; native v2–v14 tiếp tục mở với profile label chưa resolve. `test_pikdraw_document_commands`, `test_pikdraw_document_storage` và `test_pikdraw_document_storage_fidelity` pass 3/3, gồm binding mismatch reject, Undo/Redo, save/open bytes và dedup giữa nhiều trang. Settings có UI gán cho active page; canvas preview theo page ICC khi Windows CMM mở được profile. Native UI chưa được kiểm trực tiếp; paint-level resource binding có native v17 persistence, selection Assign/Convert và same-space color-dialog handling.

**Cập nhật 21/10/2026:** Canvas preview xử lý ICC gắn trực tiếp trên paint, chuyển RGB/CMYK nguồn sang display profile; proof colors qua CMYK profile cấu hình nếu có. Cache transform giới hạn 16 entry và 64 MiB profile bytes. Regression SWOP nguồn/page sRGB qua ICC unit, CPU pixel và Direct2D/CPU parity pass. `SpatialIndex` bỏ hash lookup thứ hai trên mỗi hit BVH bằng internal item indices, vẫn trả stable object IDs theo painter order. `test_pikdraw_icc_preview`, `test_pikdraw_rendering_pixels`, `test_pikdraw_rendering_culling` và full `test_pikdraw_rendering_rasterizer` pass 4/4 trong Debug. Release benchmark 10.000 objects đo 5,814 ms median (9 mẫu), budget 8,3 ms, cull 8.729 objects; Release gate được enforce riêng theo CMake config, Debug chỉ in diagnostic. Native visual walkthrough chưa chạy do helper capture timeout. Gamut warning vẫn là khoảng cách encoded RGB, không phải Delta E; monitor-specific display profile, proof/gamut oracle độc lập, overprint compositor, CMYK/Gray raster export và print gate còn mở.

**Bổ sung cache 21/10/2026:** ICC preview cache giữ tối đa 4.096 màu gần đây theo LRU thay vì xóa toàn bộ ở ngưỡng; đổi preview options vẫn invalidates cache. `ColorPreviewCache` dùng cùng giới hạn LRU và giờ so sánh đủ source channels, ICC digest, Spot alternate/alpha và overprint sau bước hash; regression tạo va chạm hash bằng cách đổi overprint xác nhận hai màu không dùng chung entry. `pikdraw_color_tests` và `test_pikdraw_icc_preview` pass 2/2. Cache `ColorPreviewCache` chưa được xác minh là consumer renderer; không tính nó là tối ưu viewport. `AC-CM19` còn mở cho invalidation theo tab/page/options và stress trên consumer production.

**Gate:** AC-CM07/08/09/12/16/19; CPU/HW/WARP khác biệt chỉ trong tolerance đã công bố.

**Bổ sung raster 21/10/2026:** ICC decode/import/IPC dùng chung budget 256 KiB; profile trong budget được giữ trọn. WIC chuyển pixel RGBA8 sang sRGB đúng một lần khi context hỗ trợ; basis được lưu qua IPC/native v16, còn profile không lưu được hoặc pixel chưa convert tạo fidelity issue. `test_pikdraw_interop_raster_worker_adapter`, `test_pikdraw_interop_raster_image_decoder`, native storage fidelity và desktop import pass. Raster writer chuyển RGB ICC nguồn (mặc định sRGB) sang RGB ICC đích theo strip giới hạn bộ nhớ; nếu tắt chuyển đổi, profile nhúng phải mô tả pixels caller đưa vào. TIFF kiểm alpha/profile bytes và transform sRGB→sRGB; JPEG lossy và alpha flatten theo background RGB profile đích. Raster có UI `File > Export Raster…`: active/all pages, TIFF/JPEG, 72/150/300/600 DPI, RGB profile đầu ra, render nền trắng và chuyển đổi profile từng trang qua ICC. Giới hạn render 16.384 px/cạnh, 32 MP, 128 MiB RGBA; batch ghi từng file và cho hủy sau file hiện tại. Test writer và app build pass; native visual walkthrough, export qua profile khác sRGB bằng pixel oracle độc lập, selection export và profile CMYK/Gray output chưa được nghiệm thu/hỗ trợ.

**Cập nhật raster ICC 21/10/2026:** Writer nay gọi bulk `ProfileColorTransform` theo strip thay cho WIC pixel transform; profile nhúng bị từ chối nếu không trùng bytes profile đích. `test_pikdraw_interop_raster_image_writer` pass với profile RGB có primaries khác sRGB: output raw TIFF khớp scalar path của cùng Windows CMM trong sai số tối đa 1 mức 8-bit và giữ alpha. Điều này đóng lỗi pixel conversion của writer và xác nhận bulk path; không phải oracle độc lập từ RIP/app khác. Pixel oracle độc lập, native visual walkthrough, selection export và profile CMYK/Gray output vẫn còn mở/chưa hỗ trợ.

**Bổ sung decoder readback 21/10/2026:** Cùng fixture còn đọc TIFF có gắn profile qua `RasterImageDecoder`/WIC về sRGB; sai số appearance tối đa quan sát được là 5/255 trên máy này, regression cho phép tối đa 8/255. C08 có bằng chứng consumer readback trên Windows nhưng AC-CM16 vẫn cần checker độc lập ngoài Windows CMM/RIP trước khi kết luận colorimetric.

**Bổ sung SVG ICC 21/10/2026:** `SvgExporter` chuyển RGB/CMYK paint có ICC riêng hoặc kế thừa page ICC sang sRGB chuẩn bằng `ProfileColorTransform`; ICC giống sRGB giữ nguyên channel. Nếu profile/CMM không khả dụng, SVG vẫn có fallback hiện hành nhưng `FidelityLossReport` cảnh báo rõ. CMYK/Spot/Gray và overprint tiếp tục báo mất separation/overprint; SVG không nhúng paint ICC. Hộp thoại hoàn tất export gom cảnh báo từ mọi trang batch, kèm PageId, tên trang/đối tượng. `test_pikdraw_interop_svg_exporter` kiểm profile RGB primaries khác sRGB, inheritance từ page, paint override, alpha và màu hex so với scalar transform Windows CMM. Bộ writer/raster/capability/action/E2E pass 5/5; sau đó regression mới về gom cảnh báo đa trang pass cùng writer 2/2. Đây là đối chiếu cùng CMM, không phải oracle màu độc lập; C08/C09 và native visual walkthrough vẫn mở.

### C04 — soft proof và gamut / P0 capability correctness

**Sở hữu:** CMM adapter/proof settings, diagnostic overlay, view actions/UI. **Sau:** C02/C03.

1. Publish capability và chất lượng thực; existing toggles giữ đường gọi, không badge ON nếu transform không hoạt động.
2. Implement profile chain/intents/BPC/paper-black theo engine support; missing proof profile hiện lý do, không dùng implicit tùy ý.
3. Tách “Proof Colors”, “Proof Difference” và “Gamut Warning”; chọn predicate/PCS algorithm có oracle, threshold config chỉ đúng loại metric.
4. Preview-only options không sửa source/channel/TAC/OP và không tạo Undo document. Persist preference theo scope rõ.
5. Dữ liệu CMYK/Gray/Spot/alpha/gradients/images có fixture; unsupported case có state explicit.

**Gate:** AC-CM07/10/11/19. Không nhận RGB distance .12 là DeltaE standard.

### C05 — overprint và separations subset / P0 tính đúng, P1 mở rộng

**Sở hữu:** core color ink semantics, render compositor, capability/report, UI. **Sau:** C01/C03/C04.

1. Đặc tả supported subset process/spot, knockout và role OP; giữ flags khi chưa preview được.
2. Implement/reference oracle cho overlapping process ink, pure K over color, white OP, fill khác stroke, spot tint và alpha theo contract.
3. Không thay alpha composition bằng RGB multiply rồi coi là OP. Unsupported masks/groups/blends không bị âm thầm “flatten đúng”.
4. View-mode overprint không sửa channels; export nguyên metadata chỉ khi writer capability đã xác nhận, nếu không loss report/reject strict.
5. Dùng separation reference/RIP fixture có license hoặc oracle ink analytic cho subset; physical printing là gate ngoài nếu được yêu cầu.

**Gate:** AC-CM12/13/20. Giữ route chức năng, báo đúng chất lượng khi subset chưa đủ.

### C06 — black/TAC theo output condition / P0 chính sách, P1 UI tích hợp

**Tiến độ 21/10/2026:** `validate_document_tac` và `FixDocumentTacCommand` hiện thực hóa backend tối thiểu. Output condition yêu cầu ID và giới hạn 100–400% do caller cung cấp; ID là nhãn report, chưa gắn hash ICC hoặc registry condition. Phạm vi scan vector paints gồm fill/stroke, gradient stops và drop shadow, kể cả object/layer ẩn hoặc effect tắt; spot và ImageObject được đếm là chưa đánh giá. Selection resolve trên active page. Fix giữ K/alpha/overprint, giảm CMY theo tỷ lệ và báo impact; không dùng matching ICC. Settings > Color & ICC hiện có scope Document/Page/Selection, ngưỡng thủ công, Check chỉ đọc, Fix riêng, stale-report guard, keyboard focus và tooltip. `test_pikdraw_document_commands` kiểm backend; `test_pikdraw_tac_settings_ui` kiểm nhập sai/đúng, focus, help, read-only revision, Fix một Undo và khôi phục màu nguồn. Export/output preset, raster separation, oracle ICC và profile binding vẫn mở.

**Sở hữu:** color policy, commands/preflight, output preset model và Properties. **Sau:** C02/C05 khi policy OP liên quan.

1. Tách black preserve, rich-black recipe, overprint và TAC; preset hiện có là lựa chọn có tên/thông số.
2. Role-based explicit policy, không cưỡng bức near-black thành K100/OP mặc định cho mọi source.
3. TAC validate chạy cả CMYK hiện có và converted paints, gradients/stops theo scope; image/process separation outside support phải báo chưa kiểm.
4. Fix TAC command nêu phương pháp preserving K/chroma/engine limitations; report before/after và appearance impact; không “fix” chỉ bằng relabel profile.
5. Output condition chưa có giới hạn kiểm chứng hiện Unknown; người dùng có thể nhập limit explicit có source/preset label, không gán universal standard.

**Gate:** AC-CM13/15/20; source thay chỉ bằng command.

### C07 — UI và chọn cùng màu / P1

**Sở hữu:** shell Color/Appearance/Gradient, workflow/menu/catalog/action registry; U02/U07. **Sau:** C02/C03, capability C04–C06.

1. Field tách Source Mode/Profile, Working Defaults, Proof Profile, Display Profile; display profile read-only status, không trộn với Assign.
2. Bar swatch/model/value thường dùng; Properties giữ toàn bộ options/metadata/report. Color dialog cũ reachable và gọi cùng binding.
3. Select Same cho phép scope Fill/Stroke/Any supported paint, source equality hoặc appearance comparison với profile/context/metric rõ. Giữ legacy ±.005 như preset có tên nếu cần tương thích.
4. Spot name+tint không đủ equality; đưa stable identity/alternate/profile/alpha và policy OP vào so sánh theo loại matcher. Không đổi tiêu chí chọn mà không UI/help.
5. Giữ actual shortcut Proof Ctrl+Alt+Y theo registry hiện tại; Ctrl+Y vẫn Undo counterpart Redo. Các shortcut khác lấy registry, không copy shortcut ứng dụng tham khảo.

**Bổ sung 21/10/2026:** `Select Same` hiện so sánh color space, profile binding và channels/alpha; Spot còn so sánh tên, tint, alternate RGBA và separation overprint, cùng paint-level fill/stroke overprint. Ngưỡng tương thích hiện tại vẫn là ±0,005. Regression tạo các Spot paint khác alternate alpha và từng cờ overprint, xác nhận chỉ reference và màu tương đương được chọn. `test_pikdraw_document_color_workflow` pass 1/1. Lệnh Any Fill or Stroke và kiểm thử workflow được bổ sung bên dưới; appearance matcher có metric/proof context và native UI walkthrough của C07 vẫn còn mở.

**Bổ sung C07 21/10/2026:** `Edit > Select Same > Fill or Stroke Color` đã được thêm vào menu Edit và context menu, chạy cùng một handler. Nó chọn đối tượng trên trang hiện hành khi bất kỳ fill, stroke hoặc gradient stop nào có source value khớp paint của đối tượng nguồn; so sánh giữ profile/alpha/Spot/overprint và tolerance ±0,005. Không tạo toolbar/toolbox/Properties cho lệnh chọn một lần; label menu và status hướng dẫn chọn đối tượng nguồn. Regression kiểm fill↔stroke, gradient stop, không khớp và giữ các lệnh Fill/Stroke/Fill & Stroke cũ. Native menu/focus walkthrough vẫn còn mở; matcher appearance theo proof context chưa có.

**Gate:** AC-CM04/09/10/15 và AC-UP-UX.

### C08 — interop/export/clipboard/automation / P1, chặn loss không báo P0

**Sở hữu:** từng format adapter/writer/capability report, native/clipboard/RPC. **Sau:** C01/C02/C03; giữ AC-IPA/AC-PDFP.

| Format/representation | Source fidelity cần báo | View/editability/export/production |
| --- | --- | --- |
| Native version hiện hành + migration | Channels/alpha/OP/spot/profile bytes/binding | Semantic round-trip; legacy unresolved không bị rewrite |
| SVG 1.0/1.1 static subset của adapter | Path/Rect presentation `opacity`, `fill-opacity`, `stroke-opacity` và Text `opacity`/`fill-opacity` map vào `Appearance`; phần trăm được nhận và clamp [0,1]. Group opacity, CSS style, gradient/stop opacity vẫn reject. RGB hex theo sRGB; ICC nguồn chuyển đổi có báo; CMYK/spot/OP separation không được giữ | Paint/page ICC RGB và CMYK được chuyển sang sRGB; report nêu profile/source semantics bị bỏ. Không hứa separation qua RGB SVG hoặc group compositing |
| DXF version/entity subset | Indexed/true color và units theo semantics DXF | Không suy ICC/spot support từ indexed color; production CNC dùng geometry/material gate riêng |
| PDF / AI PDF-compatible | Editable extraction projected colors khác nguyên bản ICCBased/Separation/DeviceN | View locked original, editable subset, metadata và export round-trip tách riêng; unsupported color spaces report |
| AI khác representation | Native adapter capability/worker | Không đổi extension hoặc ép parser PDF để nhận mọi AI |
| EPS/PostScript | Device/process/spot/transfer trong subset worker | Render thành công không chứng minh editable source separation; giữ report |
| CDR qua version/libcdr path | Source/converted representation và loss | SVG trung gian không giữ mọi ink/ICC/effect; provenance riêng |
| Raster embedded ICC | Original bytes/profile/source bit depth/pixel basis/DPI | Decoder pixels, preview transform và exporter ICC embedding kiểm riêng |
| TIFF/JPEG writer mới trong working tree | RGBA8/WIC, TIFF LZW/JPEG RGB 4:4:4, DPI/background; embedded ICC phải khớp pixel basis | RGB8 có ICC transform trong writer và nối vào Export Raster; independent oracle, CMYK/high-bit press output và selection export còn mở |
| Clipboard/automation | Typed color/profile resource refs hoặc payload brokered | Same command service, validation/revision/permissions; không nhận arbitrary path/trust profile name |

Mỗi adapter khai báo **version + representation + feature + limits** từ capability thực; Unknown khác Unsupported. UI report tách render/view, editability, source metadata, export round-trip và production. Các format không tham gia một thao tác phải ghi N/A có lý do; không ép mọi format qua một parser.

Export phải lấy source + explicit output condition, không chụp viewport đang proof/monitor. RGB raster pixels và embedded profile phải cùng basis; metadata alone không sửa màu. Atomic output, admission/quotas và stale guards giữ theo worker/export hiện có.

**Gate:** AC-CM14/16/17/18/20 cùng C8/C9 tests; PDF/X exporter mới ngoài capability hiện hành không tự coi hoàn tất.

21/10 bổ sung AC-CM14: `test_pikdraw_clipboard_color_ui` xác nhận clipboard native serialize/deserialize và paste sang document hiển thị inch vẫn giữ nguyên ICC resource bytes/digest, RGB channels + alpha/overprint, Spot tint/alternate alpha/overprint, cùng geometry mm. Undo/Redo khôi phục cùng ID và paints; NativeFormat round-trip sau paste vẫn giữ profile bytes và source colors. CTest mục tiêu pass 1/1. Đây là kiểm chứng native clipboard snapshot; SVG/text fallback, multi-object/multi-type selection và automation/RPC vẫn mở.

### C09 — oracle, hiệu năng và release

**Sở hữu:** tests/benchmarks hiện có, help/capability docs/evidence. **Sau:** các C đưa vào release.

1. Independent expected Lab/transform fixtures ghi engine/version/profilehash/intent/BPC/tolerance. Round-trip cùng engine không đủ chứng minh absolute accuracy.
2. Cache correctness và invalidation, memory quota, transform creation count, per-pixel/per-stop overhead; UI render pipeline không tạo transform từng frame.
3. Run scope tests trước; canonical app kiểm swatch/canvas/monitor/proof thực; separations và exported profile/pixels có checker độc lập.
4. RGB/CMYK/Spot/Gray, gradients/effects/text/images, alpha, multi-page/tab, save/reopen và Undo đều có coverage theo claim.
5. Tuyên bố giới hạn rõ: uncalibrated display, optional installed profiles, unsupported ink groups, RIP/physical print chưa đo. Không nâng trạng thái “industrial” từ một nhóm unit tests.

## 4. UI contract

Các action `P:` chưa có runtime; các ID đã đăng ký phải dùng cùng handler và enable state ở mọi surface. Icon dùng bộ SVG color/profile/proof hiện có hoặc asset mới cùng hệ qua U07; label+accessible name bắt buộc. Không thêm toolbox color tool riêng (N/A).

| Đường/surface | Identity/fields | Scope và hành vi |
| --- | --- | --- |
| Edit > Document Color Mode; Page Properties > Color | Giữ document_color_ui IDs; mode, target profile, policy | Convert channels explicit; Document/Page target rõ; review report → Apply một Undo |
| Edit > Assign Color Profile… | P: `action.color.assign_profile` | Giữ channels, đổi binding; text giải thích appearance có thể đổi |
| Edit > Assign ICC to Selection… | `action.color.assign_selection_profile` | Gán binding cho paint RGB/CMYK/Gray tương thích, giữ channel values; Spot và paint không tương thích được giữ nguyên |
| Edit > Convert Selection ICC… | `action.color.convert_selection_profile` | Chọn ICC đích + rendering intent; chuyển RGB/CMYK paints theo source binding riêng, preflight toàn selection, một Undo; Gray/Spot/raster không đổi |
| Edit > Convert Color Profile… | P: `action.color.convert_profile` | Source/destination actual profile, intent/BPC; same-model conversion có tác dụng |
| Appearance/Color dialog | `color.model`, channels, alpha, ICC binding, OP fill/stroke, spot/tint | Same-space RGB/CMYK edits giữ binding và dùng transform; thiếu transform thì chặn Apply. Properties hiển thị tên ICC; field số giữ source |
| View > Proof Colors / Gamut Warning / Overprint Preview | Giữ `action.view.proof_colors`, `action.view.gamut_warning`, `action.view.overprint_preview` | View-only; capability/quality/disabled reason; checked state tương ứng consumer thật |
| Properties > Color Management | Working defaults, proof setup, display status, missing profiles | Advanced luôn reachable; profile picker có label/space/hash tóm tắt, không lộ chi tiết engine vào workflow thường |
| Select > Same Color | Giữ route hiện có; source/appearance mode và paint scope | Help nêu tiêu chí/tolerance; không chạy transform toàn document trong paint menu |
| File > Export SVG… / Export Raster… | Hai UI đã có: SVG scope + save picker; Raster format/scope/DPI/JPEG quality/RGB profile/embed option | SVG subset và TIFF/JPEG capability giữ riêng theo writer; Raster không hỗ trợ selection. Không tạo panel export thứ ba; preflight/loss chỉ dùng report backend hiện có |

Tooltip/focus help ví dụ: “Assign: giữ số màu, đổi profile diễn giải”; “Convert: chuyển số sang profile đích”; selection conversion áp dụng toàn bộ process paints phù hợp bằng một Undo; “Proof chỉ đổi cách xem”; “Overprint preview hiện chỉ hỗ trợ …”. Status khi lỗi nêu profile/target và cách khắc phục, không chỉ “Failed”. Current Proof Ctrl+Alt+Y phải đọc từ registry khi hiển thị. Help usage 3–5 bước cho Assign/Convert/Proof; mở panel/focus không revision.

## 5. Nghiệm thu độc lập

Tất cả ca **chưa được chạy trong đợt plan này**. Sửa test hiện có; không tạo ảnh/log/fixture tạm trong repo nếu không là regression có giá trị.

| ID | Given | When | Then | Verify |
| --- | --- | --- | --- | --- |
| AC-CM01 | Hai ICC cùng name/space khác bytes; ICC invalid/mismatch | Register/bind/save/load | Identity theo bytes; invalid reject; không chọn nhầm profile | Profiles + storage tests, actual hashes |
| AC-CM02 | Active page đã target, page khác chưa; đảo active page | Convert document và Undo | Mọi page trong scope được xét; không early no-op; một transaction | Document command assertions all pages/IDs/revisions |
| AC-CM03 | RGB A→RGB B và CMYK A→CMYK B | Assign rồi Convert trong ca riêng | Assign giữ channels; Convert theo transform; label khớp bytes thực | Independent fixture + before/after bindings |
| AC-CM04 | Cùng source/profile/policy ở dialog, selection/page/document và RPC | Convert cùng target | Cùng service/result/report; không manual UI formula riêng | Desktop workflow + commands + automation |
| AC-CM05 | Legacy chỉ có profile name và mixed source bindings | Open rồi save explicit | Không tự resolve giả/dirty lúc mở; nguồn giữ, unresolved rõ | Storage legacy fixture, semantic diff |
| AC-CM06 | Missing profile, approximate fallback, strict output, job stale | Preflight/convert/apply | Quality rõ; strict reject; không apply một phần/stale | Commands/writer guards + actual report |
| AC-CM07 | Actual proof/display profiles, monitor đổi | Bật/tắt proof/chuyển monitor | View đúng profile/capability; source/Undo không đổi | Native monitor test + profile hashes; thiếu monitor ghi chưa đo |
| AC-CM08 | Alpha/gradient/blend/blur C9 fixtures | Đổi view profile và render HW/WARP/CPU | Working semantics giữ; view transform đúng stage; không double ICC | Pixel oracle và source semantic checks |
| AC-CM09 | Solid/stop/palette cùng color binding | So swatch/Properties/canvas qua proof modes | Cùng projection/tolerance; source values ổn định | Desktop color paint + render sampling |
| AC-CM10 | Gamut fixture trong/ngoài/unknown profile | Chạy warning/diagnostic | Metric/capability đúng; Unknown không là in gamut | CMM/PCS independent fixture, không RGB norm giả DeltaE |
| AC-CM11 | Paper-white/black/BPC option có/không capability | Bật option | Transform thật hoặc disable có lý do; không false ON | Adapter capability + native view tests |
| AC-CM12 | OP process/spot, fill/stroke, knockout, white OP | Composite overlapping objects | Ink subset đúng oracle; unsupported explicit; source giữ | Compositor/separation fixtures, không chỉ helper multiply |
| AC-CM13 | Near-black text/fill/stroke và existing OP | Convert với policy khác nhau | PureK/rich recipe/OP đúng lựa chọn; không force cả hai flags | Role-specific command/model tests |
| AC-CM14 | Native/clipboard giữ profile resources, spot, alpha, OP | Undo/Redo/save/reopen/copy giữa document | Numeric/source identity/provenance giữ; no profile-name substitution | Storage fidelity + clipboard semantic diff |
| AC-CM15 | Same CMYK vượt TAC, gradients và legacy color-match | Validate/fix/select same | Validate không sửa; fix có Undo/report; matching đúng mode/scope | Policy/document selection fixtures |
| AC-CM16 | Raster ICC/pixel basis, TIFF/JPEG output | Decode/view/export rồi inspect | Transform đúng một lần; embedded output ICC khớp pixels | Decoder/writer + checker độc lập; metadata alone không pass |
| AC-CM17 | SVG/PDF/AI/CDR/EPS/DXF representation fixtures | Import/edit/export theo capability | Report render/edit/source/export/production tách rõ, loss không giấu | Adapter version/subset + AC-IPA/AC-PDFP |
| AC-CM18 | Native schema mới và legacy, invalid resources, save R rồi edit R+1 | Read/write/atomic save | Migration đúng; saved R/dirty R+1; không mất data/profile | Storage/recovery/writer safety |
| AC-CM19 | Cache collision, spot alternate/alpha khác, tab/page/options đổi | Repeated preview rồi invalidation/stress | Kết quả đúng key; quota bounded; không stale cross-context | Production cache tests + memory/transform counts |
| AC-CM20 | Output condition và exported print subset | Preflight/export/separation validation | TAC/black/OP theo condition, giới hạn explicit; không tự chứng nhận press | Independent output checker; RIP/physical print ghi chưa nghiệm thu nếu chưa làm |

### Lệnh và trạng thái kiểm tra

Nhóm nền màu, dùng runner chuẩn:

~~~powershell
./tooling/native/build.ps1 -Test -TestTarget test_pikdraw_color,test_pikdraw_icc_preview,test_pikdraw_document_commands,test_pikdraw_document_color_workflow -TestRegex '^(pikdraw_color_tests|test_pikdraw_icc_preview|test_pikdraw_document_commands|test_pikdraw_document_color_workflow)$'
~~~

Lần đầu runner dừng trước configure/build/test vì lock của pwsh PID 28504. Sau khi phiên đó nhả lock, chạy lại đúng lệnh trên bằng runner chuẩn, cấu hình **Debug**: **4/4 CTest pass, 0 failed, tổng 5,06 giây** (color 0,03 s; ICC 0,07 s; document commands 0,65 s; workflow 4,27 s). LastTest.log xác nhận ca installed SWOP CMYK→sRGB đã chạy, không ghi NOT_MEASURED/SKIP trong lần này. Build có hai warning missing initializer preview_rgba trong test_commands.cpp hiện có; không sửa code ngoài phạm vi plan. Đây là baseline tests đang có trên working tree tại thời điểm chạy, chưa phải các AC-CM mới hoặc oracle gamut/overprint/print. Không chiếm/xóa build lock.

Khi triển khai, mở rộng thêm targets liên quan: `test_pikdraw_document_storage_fidelity`, `test_pikdraw_document_gradient_storage`, `test_pikdraw_style_dialog`, `test_pikdraw_rendering_gradient`, `test_pikdraw_rendering_pixels`, `test_pikdraw_rendering_effect_compositor`, `test_pikdraw_interop_raster_image_decoder`, `test_pikdraw_interop_svg_exporter`, `test_pikdraw_automation_rpc`. Lệnh `.\tooling\native\build.ps1 -Test -TestTarget test_pikdraw_interop_raster_image_writer -TestRegex '^test_pikdraw_interop_raster_image_writer$'` đạt **1/1 pass** với ICC sRGB hệ thống: TIFF lưu và decoder đọc lại đúng toàn bộ ICC bytes cùng pixels RGBA8. Đây chỉ là metadata embedding; không kiểm pixel transform hoặc production export wiring. CTest regex lấy đúng registration, không suy từ executable.

Kết quả đóng C09 phải chỉ rõ target/command/source revision, tests thực sự chạy, case skip/not_measured, native interactions và oracles. Unit tests không chứng minh monitor đã calibrated, RIP đúng hoặc bản in vật lý đạt.

