# Kế hoạch Open, Import, Export và trao đổi vector phục vụ CNC

Ngày: 18/10/2026. Trạng thái: **kế hoạch triển khai; chưa bổ sung chức năng runtime**.
Task tài liệu: `plan-file-interop-cnc-20261018`. Source: `C:/Users/Administrator/Downloads/PikDraw`.
Baseline Git: `1847e1011b45c85cf2de24df937b28621c08b54c` cộng working tree hiện hữu tại thời điểm khảo sát; có thay đổi của task khác.
Tài liệu này bổ sung phạm vi UX, raster, export và CNC cho roadmap `import-resilience-20261016.md`; dùng khảo sát source mới bên dưới thay cho bảng hiện trạng cũ.

## 1. Phạm vi và kết quả người dùng cần

- **Open / Ctrl+O:** mở `.pikdraw`, PDF, CDR, AI, SVG, EPS, DXF và ảnh thành tài liệu mới, đúng kích thước/đơn vị; giữ tài liệu đang làm trong tab riêng.
- **Import / Ctrl+I:** nhập một hoặc nhiều file vào tài liệu hiện tại; chọn trang, kích thước, vị trí và chế độ giữ nội dung; có preview, Cancel và Undo.
- **Export / Ctrl+E:** JPG/JPEG, PNG, TIF/TIFF, SVG, EPS, DXF, PDF; chọn đối tượng, trang hiện tại, dải trang, toàn bộ trang hoặc mỗi đối tượng/trang một file.
- **Giữ hiệu ứng tối đa:** kiểm riêng hiển thị, chỉnh sửa, dữ liệu nguồn, xuất ngược và độ chính xác sản xuất. Phần chưa giữ được phải có chỉ báo và lựa chọn trước khi nhận/xuất.
- **CNC:** ưu tiên DXF và SVG có kích thước vật lý, đường cong chính xác, topology đúng; EPS/PDF bổ sung cho thiết bị/CAM đã được kiểm chứng.
- `dfx` trong yêu cầu được hiểu là **DXF**. File có đuôi `.dfx` chỉ được đề nghị nhận như DXF khi kiểm tra nội dung phù hợp; không tạo một format DFX mới.
- Ảnh mở rộng: BMP và WebP ở đợt sau; GIF chỉ ảnh tĩnh/frame được chọn. PSD/RAW/HEIF/AVIF, DWG, 3D CAD, G-code và điều khiển máy không nằm trong đợt này.
- AI/CDR trong phạm vi bắt buộc là **Open và Import**. Export AI/CDR gốc cần writer/SDK và gate riêng, không giả lập bằng đổi đuôi PDF/SVG. Lưu chỉnh sửa đầy đủ dùng `.pikdraw`.

Mục tiêu nghiệm thu là không mất nội dung hoặc đổi geometry âm thầm trong profile được công bố. Không thể cam kết mọi phiên bản AI/CDR, plugin và hiệu ứng độc quyền đều vừa giống tuyệt đối vừa sửa nguyên bản. Khi không có đường giữ nội dung đủ bằng chứng, phải báo giới hạn thay vì mở một bản thiếu chi tiết rồi thông báo thành công.

## 2. Hiện trạng đã kiểm tra trên source

Khảo sát dưới đây là đọc code/header/container, không phải chạy lại app, parser production, benchmark hay nghiệm thu fidelity.

| Thành phần hiện tại | Bằng chứng source | Khoảng thiếu cần xử lý |
| --- | --- | --- |
| Open native | `apps/desktop/src/shell/native_window_files.cpp`: filter `.pikdraw`, gọi `open_native` | Chưa có dispatcher mở định dạng ngoài; hiện hỏi bỏ tài liệu dirty rồi thay nội dung |
| Export UI | Cùng file có `export_svg_dialog`; `native_window_shortcuts.cpp` nối Ctrl+E vào dialog này | Chưa có dialog Export đa định dạng/phạm vi |
| Import và shortcut | `actions/action_id.hpp` đã khai báo `action.file.import`; menu/shortcut native chưa có luồng Import file tương ứng | Ctrl+I hiện gọi `toggle_font_italic`; phải giải quyết xung đột |
| Capability | `modules/interop/src/format_capability_matrix.cpp`, `modules/interop/README.md` | SVG static subset nhập/xuất prototype; DXF ASCII 2D chỉ nhập prototype; PDF/AI/CDR/EPS/PNG/JPEG bị từ chối; TIFF chưa có adapter |
| SVG | `svg_importer.cpp`, `svg_exporter.cpp` | Import chưa nhận opacity attributes; export có opacity nhưng từ chối effect nodes và blend khác Normal. Cần đóng cả hai chiều, không chỉ thêm filter vào hộp chọn file |
| DXF | `dxf_entity_parser.cpp`, `dxf_source_reader.cpp`, `dxf_importer.cpp` | Chỉ LINE/ARC/CIRCLE/LWPOLYLINE theo profile; cần units hợp lệ. POLYLINE/VERTEX legacy và export chưa có |
| Transport worker | `ipc_dto.hpp`, `platform/workers/src/worker_main.cpp` | Đã có wire v2, source handle, proposal stream và identity; tái sử dụng/kiểm chứng luồng hiện có. V1 vẫn có trần payload 16 MiB, v2 frame 1 MiB |
| Document | `core/document/include/pikdraw/document/snapshots/document_snapshot.hpp` | Snapshot hiện giữ một `Page`; đa trang thật là prerequisite cho chọn/xuất từng trang |
| Raster model | `core/document/include/pikdraw/document/model/object.hpp`: `ImageObject` | Đã có pixel RGBA và clip đơn; chưa đủ source bytes/ICC/bit depth/DPI/link/orientation/frame provenance cho nhập ảnh hoàn chỉnh |
| Effects/storage | `contracts/c9-appearance-effects.md`, `docs/project-state.json` | Native v5; basic effects cho Path/Text, chưa đủ group/layer isolation, masks và mọi ánh xạ effect ngoài |
| Production | `registry/modules.json`, `modules/production` | Module còn kế hoạch; chưa có chứng nhận đường CNC từ import hoặc export |

Runtime tiếp tục theo C++20/Win32/Direct2D 1.1/D3D11/WARP và SoftwareRasterizer hiện hành. Kế hoạch này không phụ thuộc việc chuyển Qt/Skia. Core không phụ thuộc ngược desktop; reader/vendor types không đi vào document model.

## 3. Bộ mẫu local và khoảng trống kiểm thử

Corpus người dùng chỉ định: thư mục `cdr` trong source. Không đưa file, tên khách hàng, nội dung, đường dẫn tuyệt đối riêng tư hoặc hash corpus vào Git. Khi triển khai, manifest ánh xạ fixture ID sang đường dẫn/hash nằm local và bị ignore; evidence dùng ID trung tính. Không upload mẫu lên dịch vụ chuyển đổi.

| Nhóm | Số file cấp gốc | Nhận định từ inventory chỉ đọc | Việc cần kiểm tiếp |
| --- | ---: | --- | --- |
| CDR | 4 | Đều có ZIP signature; chưa xác nhận version family và feature coverage | Đọc metadata bằng reader được chọn; so với CorelDRAW đúng build, từng trang/effect |
| AI | 1 | Có `%PDF-1.5` ở đầu file | Xác minh PDF representation; không suy ra đầy đủ dữ liệu authoring |
| PDF | 3 | Đầu file `%PDF-1.7` | Số trang, boxes, fonts, clip/transparency, ICC/spot và nội dung sản xuất |
| EPS | 2 | Header khai báo PostScript LanguageLevel 2; creator lần lượt Adobe Illustrator/CorelDRAW | Kiểm phần PostScript thật; thumbnail không thay nội dung vector |
| DXF | 1 | `$ACADVER=AC1003`; 298 POLYLINE, 19.319 VERTEX, 298 SEQEND; chưa thấy `$INSUNITS` trong cửa sổ header 32 KiB | Kiểm toàn header; hỗ trợ legacy chain và cho người dùng xác nhận units khi thiếu |
| JPG | 4 | JPEG signature | DPI/EXIF/ICC/orientation, CMYK JPEG nếu có |
| SVG | 0 ở cấp gốc; 1 smoke SVG trong `test-sources` | Chưa đủ mẫu SVG thực tế | Bổ sung tài nguyên có quyền dùng và fixtures tự tạo |

Tổng 15 file cấp gốc, khoảng 111,57 MiB; AI, một PDF và một EPS lớn hơn 16 MiB. Một CDR khoảng 1,56 MiB khai báo tổng dữ liệu ZIP giải nén khoảng 35,35 MiB: phải đo decoded/scene/preview peak, không dự toán RAM theo kích thước file nén.

Bổ sung fixture PNG alpha, TIFF RGB/CMYK/16-bit/nhiều frame, JPEG EXIF/CMYK, PDF nhiều trang/password/missing font, AI có và không có PDF, CDR theo version thực tế. SVG cần CSS/use/gradient/clip/mask/filter/text/ảnh; DXF cần R12 POLYLINE, R2000 LWPOLYLINE, bulge, blocks/inserts, spline/ellipse, OCS và đơn vị thiếu. Fixtures malformed, zip bomb, vòng tham chiếu và thiếu asset phải có kết quả mong đợi rõ.

## 4. Bài học từ tài liệu Adobe/Corel và quyết định thiết kế

Các URL được truy cập ngày 18/10/2026. Phần lựa chọn cụ thể của PikDraw bên dưới là thiết kế đề xuất, không phải claim runtime hoặc thông số bảo đảm của hãng.

- Adobe mô tả tùy chọn Create PDF Compatible File lưu thêm biểu diễn PDF; lưu legacy có thể mất tính năng và EPS có giới hạn transparency. Vì vậy AI-PDF, AI authoring và EPS converted phải là các hàng capability riêng. [Adobe Save artwork](https://helpx.adobe.com/illustrator/using/saving-artwork.html).
- Adobe tách ưu tiên appearance và editability khi xuất AutoCAD, cho chọn scale/units; SVG có độ chính xác tọa độ, ảnh nhúng/link; JPEG/PNG/TIFF có tùy chọn chất lượng riêng. PikDraw thêm profile CNC với geometry làm điều kiện bắt buộc. [Adobe Export artwork](https://helpx.adobe.com/illustrator/using/exporting-artwork.html).
- CorelDRAW tổ chức xuất theo đối tượng/trang, dải trang và danh sách tài sản có cấu hình lặp lại. PikDraw dùng dialog Export làm đường chính, cho lưu preset và batch thay vì buộc người dùng mở một dock mới. [Corel Export objects and pages](https://help.coreldraw.com/CorelDRAW/540111192/Documentation-Windows/CorelDRAW-en/CorelDRAW-Export-assets.html).
- CorelDRAW có chọn đơn vị khi nhập DXF, version/units/text khi xuất và nêu giữ layer. PikDraw yêu cầu xác nhận units nếu nguồn thiếu, tắt tự giảm node mặc định cho đường sản xuất. [Corel DWG/DXF](https://help.coreldraw.com/CorelDRAW/540111192/Documentation-Windows/CorelDRAW-en/CorelDRAW-AutoCAD-Drawing-Database-DWG-and-AutoCAD-Drawing-Interchange-Format-DXF.html).

## 5. Ma trận format/version/representation đích

Tất cả ô dưới là **phạm vi đề xuất cần gate**, không thay capability registry hiện tại. Mỗi hàng runtime phải có trạng thái riêng cho xem/render, editability, provenance, production và từng đích export; không dùng một bool Supported cho mọi mục đích.

| Format / representation | Open và Import đích | Hiển thị và chỉnh sửa | Metadata nguồn | Export / round-trip | Điều kiện sản xuất |
| --- | --- | --- | --- | --- | --- |
| PDF 1.4-1.7 trước; PDF 2.0 gate sau | Chọn một/dải/toàn bộ trang, CropBox/TrimBox/MediaBox | Native subset có oracle; phần phức tạp reference có renderer kiểm chứng hoặc chuyển đổi tường minh | Boxes, Rotate/UserUnit, fonts, colors/profile, OCG nếu có, source locator | PDF vector + raster có báo cáo; PDF/X gate riêng; không tái tạo authoring AI từ PDF | Chỉ geometry/print profile đã kiểm; trang scan không tự trở thành đường cắt |
| AI PDF-compatible | Nhận diện đúng representation, dùng adapter PDF có nhãn | Độ chỉnh sửa theo dữ liệu PDF hiện có | Giữ source bytes/hash và representation; không bịa layers/artboards/live effects | Xuất SVG/PDF/EPS/DXF theo capability; chưa có AI native round-trip | Kiểm geometry và scale riêng |
| AI legacy PostScript / AI không PDF | Nhánh legacy chỉ khi signature/profile interpreter hợp lệ; dạng khác research | Converted có report; phần authoring chưa đọc không gọi editable đầy đủ | Version nhận diện được; unknown giữ unknown | AI writer chưa thuộc scope | Bị chặn nếu geometry chưa qua kiểm |
| CDR ZIP/RIFF, version family lấy từ corpus | libcdr/librevenge spike, whitelist theo version + feature | Native khi semantics đúng; PowerClip/lens/mesh/blend/envelope chưa đạt dùng reference có bằng chứng hoặc từ chối | Giữ units/pages/order/source ID/fonts/colors và phần chưa map theo storage policy | Không claim ghi CDR gốc; .pikdraw lưu chỉnh sửa và provenance | Chỉ contours/units/transforms đã kiểm |
| EPS text/binary wrapper, Level 2/3 | PostScript worker riêng; có thể chuyển PDF qua job kế tiếp | Converted representation; giữ vector khi interpreter thực sự xuất vector; fallback raster có nhãn | BoundingBox/HiResBoundingBox, fonts, source/version/conversion chain | EPS vector theo profile; transparency cần flatten có lựa chọn; mỗi trang một file | Chỉ profile vector đã đối chiếu CAM; ảnh preview EPS không là đường cắt |
| SVG static 1.1 + từng feature SVG2 | XML/namespaces/CSS/units/tree/use; mở rộng theo feature | Path, text, group, gradient/clip trước; mask/filter/blend theo C9, không bỏ effect lặng lẽ | IDs, viewBox, sizes, style/source mapping và assets | Hai profile SVG Design/SVG CNC; import lại và mở trong app đích | SVG CNC chỉ path/shape hợp lệ, physical size và topology đạt |
| DXF ASCII legacy AC1003 và R12 trước, R2000 sau | Header/units/layer; POLYLINE-VERTEX-SEQEND và LINE/ARC/CIRCLE/LWPOLYLINE; blocks/spline theo gate sau | CAD geometry, không ép thành appearance effects | ACADVER, handles/layers, OCS/WCS, model/paper space, units và giả định đã xác nhận | Writer R12 AC1009 và R2000 AC1015 riêng; không hứa ghi lại AC1003 | Kiểm mm/inch, arc/bulge, closure/holes, duplicate/open edges và CAM thực tế |
| JPG/JPEG, PNG | Asset raster, embed mặc định, link khi chọn | Đổi vị trí/scale/crop; giữ alpha/profile/orientation; không tự trace | Bytes gốc, pixel size, DPI, EXIF/ICC và trạng thái link | Xuất từ snapshot gốc; test màu/alpha/dimensions; JPEG là lossy | Chỉ ảnh khắc nếu có workflow đã kiểm; không cho qua gate vector cut |
| TIF/TIFF 6.0 | Chọn frame/trang; RGB/gray 8-bit trước, CMYK/16-bit gate sau | Raster; giữ source bytes, không giả pipeline 8-bit là bảo toàn 16-bit | Compression, channel depth, DPI/profile/alpha/frame | TIFF lossless; đa frame khi model và encoder đã đạt; BigTIFF gate riêng | Tương tự raster; không là đường vector |
| BMP/WebP/GIF tĩnh | Đợt mở rộng sau bộ bắt buộc | Theo codec/profile được khóa; animation ngoài scope | Pixels/profile/frames được chọn | Chỉ format có encoder đã kiểm | Không tự cấp quyền vector cut |

PDF 2.0, CDR mới ngoài whitelist, AI không PDF hiện đại, SVG animation/foreignObject/script, DXF binary/3D/xref/hatch/dimension không hỗ trợ phải có diagnostic riêng. Việc một parser không báo lỗi không tự nâng hàng capability.

## 6. Kiến trúc và lựa chọn reader/writer

Luồng chung: nguồn read-only → nhận diện signature/version → reader riêng ngoài process → scene trung lập có provenance/report → host validation → staging preview/loss choices → một transaction document → snapshot bất biến → renderer/exporter theo đích.

- Tái sử dụng transport v2 đã có: source handle, bounded frame/queue, checksums, generation, strict base revision; đóng khoảng thiếu thay vì tạo protocol song song. V1 payload không dùng để nạp file lớn nguyên khối.
- Các adapter dùng chung **model và semantics sau normalize**: commands, units/transforms, painter order, color/text, undo và renderer. Mỗi format giữ parser, version routing và source mapping riêng; không ép tất cả qua SVG/PDF.
- C8 hiện là Proposed: phải hiện thực report/transaction/provenance đủ để UI sử dụng. C9 basic Path/Text không đủ để nhận mọi transparency/group/effect ngoài.
- Scene cần page collection/page IDs, layers/groups/order, path verbs/control points/arcs/closure/fill rule, text runs/fonts, source colors/ICC/spot, asset handles, clips/masks/effect scope và mapping source→native ID. Stable native IDs do host writer cấp/xác thực.
- `reference_only` cần model/storage/lifecycle riêng hoặc wrapper rõ trong model; không đánh dấu một ImageObject như vector chỉnh sửa được. Nguồn reference phải lưu đủ asset để mở lại; cache preview không là nguồn xuất.
- Source bytes, metadata, original ICC/depth và normalized editable objects cùng có version/schema/limits. Native v5 là hiện trạng; thay schema phải dùng version kế tiếp phù hợp, reader backward và migration fixtures, không tự ghi thêm field vào v5.
- Group opacity/isolation/backdrop có thể buộc giữ toàn group/trang reference để nhìn đúng. Không vẽ đồng thời native children và ảnh fallback của cùng nội dung; thay backdrop phải invalidation/re-render hoặc báo chưa hỗ trợ.
- Export chụp document ID/revision, page IDs, selection IDs, assets và options thành job bất biến. Khi người dùng tiếp tục sửa, file vẫn đại diện snapshot đã chọn; preview và receipt ghi revision đó, không trộn dữ liệu mới/cũ.
- Open mới chỉ publish tab sau staging thành công. Import áp dụng một transaction; malformed/cancel/crash/quota/stale không đổi revision, history, dirty state hoặc selection trước đó. Multi-file batch mặc định chuẩn bị hết rồi commit một lần; file bị loại phải do lựa chọn rõ trước commit.
- Output ghi temp cùng volume, xác minh, rồi atomic replace từng file. Batch tạo manifest kết quả; không hứa atomic cả thư mục. Cancel xóa temp của job, giữ file đã hoàn tất và báo rõ phần còn lại; không ghi đè nguồn Open/Import.

| Thành phần đề xuất | Hướng kỹ thuật và gate quyết định |
| --- | --- |
| Raster Windows | WIC built-in allowlist JPEG/PNG/TIFF/BMP trong worker, không tự dùng mọi codec bên thứ ba cài trên máy. WebP chỉ bật với decoder/encoder build đã khóa. WIC có codec nền nhưng mapping ICC/alpha/16-bit vẫn phải kiểm. [Microsoft WIC](https://learn.microsoft.com/windows/win32/wic/-wic-about-windows-imaging-codec) |
| PDF | PDFium candidate cho render và object extraction/writing subset, build tắt V8/JS/XFA, không chạy document actions. Spike precision/API/color/text/clip; API trả float/RGBA không đủ để suy ra geometry lớn hoặc nguồn CMYK chuẩn. Nếu thiếu dữ liệu, giới hạn capability hoặc chọn engine có coverage được đo. [PDFium public API](https://pdfium.googlesource.com/pdfium/+/refs/heads/main/public/fpdf_edit.h) |
| CDR | libcdr + librevenge candidate đã có trong registry; callback vào scene thay vì bắt buộc xuất SVG. Chưa pin version/build/license. Không lấy tên thư viện làm chứng cứ mọi CDR/effect đều đọc đúng; whitelist chỉ chốt sau spike/corpus. Trang upstream được thử tra cứu chưa cung cấp được nội dung trong phiên này |
| SVG | Giữ parser/adapter hiện tại làm nền để đánh giá; mở rộng XML/CSS/tree bằng parser có giới hạn hoặc thay phần không đạt. Nếu dùng libxml2, pin version/flags và chặn DTD/entities/XInclude/external loaders; XML parsing không tự giải quyết SVG semantics |
| DXF | Mở rộng built-in parser/writer 2D cho legacy và profile CNC; thư viện ngoài chỉ khi spike cần blocks/spline rộng hơn. Không mặc định đưa libdxfrw vào distribution khi license/ABI chưa audit |
| EPS | Ghostscript candidate trong process cách ly, SAFER cùng OS containment và broker assets; chuyển EPS→PDF→scene phải có provenance của hai bước. Pin artifact và kiểm điều kiện phân phối trước enable; upstream công bố AGPL hoặc commercial, chạy ngoài process không tự giải quyết nghĩa vụ. [Ghostscript releases/licensing](https://ghostscript.com/releases/gsdnld.html), [vector devices](https://ghostscript.readthedocs.io/en/latest/VectorDevices.html) |

Mỗi dependency phải có commit/tag, checksum, compiler/ABI, build flags, transitive notices, security update policy và reproducible package. Không tải/cài CorelDRAW/Illustrator hoặc dùng COM của chúng làm dependency bắt buộc. Máy có phần mềm nguồn hợp lệ có thể tạo oracle local; SDK thương mại chỉ là phương án khảo sát nếu reader mở không đạt bộ mẫu, không mặc định mua/cài.

## 7. Hợp đồng fidelity và giữ hiệu ứng

Report có parse outcome, feature coverage, appearance, editability, provenance, production và export target riêng theo từng phần tử/trang. Giữ source hash/size, format/version/representation, adapter build, source locator, units/tolerance, fonts/profile/assets, lựa chọn chuyển đổi và identity job theo C8.

| Nhóm nội dung | Điều kiện được nhận editable | Hành vi khi chưa đủ |
| --- | --- | --- |
| Geometry/transform | Đúng control points, arc sweep/bulge, fill rule, holes, reflection/nonuniform scale và đơn vị | Chặn production; approximation phải có tolerance và lựa chọn |
| Layer/group/page | Giữ đúng hierarchy/order/visibility/lock/page ownership; PDF OCG và SVG group không tự là layer tác giả | Tên/layer do PikDraw tổng hợp có nhãn; không bịa cấu trúc nguồn |
| Solid/gradient/color | Giữ RGB/CMYK/spot+tint/profile/intent; gradient stops/space/matrix đúng | RGB preview không thay nguồn in; không âm thầm đổi spot/overprint |
| Opacity/blend/blur/shadow | Đúng scope/order/alpha/blend space và backdrop; matching tên hiệu ứng chưa đủ | Reference có oracle, flatten/outline/raster có lựa chọn, hoặc từ chối |
| PowerClip/clip/mask/mesh/lens/envelope/live effects | Model, renderer, persistence và adapter đều có cùng semantics đã kiểm | Giữ reference ở phạm vi composite phù hợp; nếu không render đúng thì báo unsupported |
| Text/fonts | Giữ Unicode nếu có, glyph positions/shaping, identity font và layout; font subset không tự đủ để sửa | Chọn giữ appearance, outline hoặc substitute có preview; không đoán tiếng Việt từ outlines |
| Raster | Đúng pixel dimensions, straight/premultiplied alpha, orientation/DPI/ICC/depth/frame | Giữ bytes nguồn; downsample/chuyển depth là explicit conversion |

Ba chế độ người dùng: **Giữ diện mạo**, **Ưu tiên chỉnh sửa**, **Đường cắt CNC**. Giữ diện mạo không tự cho phép rasterize âm thầm; chọn chế độ chỉ xác định ưu tiên và cách trình bày loss. Hộp tóm tắt nêu phần bị đổi, preview vùng ảnh hưởng và lựa chọn giữ reference/chuyển đổi/hủy trước commit. Không có thay thế đạt yêu cầu thì giữ trạng thái unsupported.

Khi file có nguồn bị thiếu fonts/images/profile, không tự fetch URL hoặc dò cả ổ đĩa. Cho chọn tài nguyên qua broker, xác thực hash/trạng thái và chỉ nhúng theo quyền cho phép. Native save/reopen giữ nguồn cần thiết ngay cả khi file cũ được di chuyển; link-only có nhãn phụ thuộc và không render cache cũ như bản mới.

## 8. Luồng Open và Import

**Open:** Ctrl+O → chọn format/file → nhận diện → với PDF/CDR/AI có nhiều trang chọn trang/dải; với raster chọn frame/DPI khi cần; với DXF chọn units nếu thiếu → xem report/preview → mở tab mới. Các trang giữ khổ và tọa độ riêng, không xếp chồng vào một Page. File ngoài mở thành tài liệu chuyển đổi chưa có đường Save native; Ctrl+S lần đầu yêu cầu lưu `.pikdraw`, không ghi đè nguồn.

**Import:** Ctrl+I → chọn một/nhiều file → chọn pages/artboards/frame → embed/link (embed mặc định), chế độ fidelity → đặt vào trang hiện tại hoặc thêm trang từ nguồn → giữ kích thước thật mặc định; có scale/W/H khóa tỉ lệ → click đặt, nhập X/Y hoặc giữ tọa độ nguồn → Enter hoàn tất, Esc hủy. Preview placement có status hint, không tạo document objects trước commit. Chọn nhiều nguồn phải có chính sách xếp riêng/từng trang; không tự thay đổi scale để vừa trang.

PDF: cho chọn Trim/Crop/Media box, thông báo Rotate/UserUnit; file có password yêu cầu mật khẩu trong phiên, không lưu vào document/log. Không suy ra vector từ PDF scan; mở scan là ảnh/reference. AI nhiều artboard qua PDF chỉ cho chọn số trang thực có trong representation.

Raster: kích thước vật lý tính từ pixel/DPI đáng tin; khi thiếu DPI dùng mặc định app có nhãn, ví dụ 96 DPI, cho sửa trước đặt. Đây là giả định ảnh, không áp dụng để đoán đơn vị DXF. Đổi DPI/scale không resample ảnh gốc khi chỉ đặt.

DXF: hiện toàn extents và kích thước sau quy đổi; nếu `$INSUNITS` thiếu/unitless, bắt chọn 1 unit = mm/inch/... rồi preview; không cho qua profile CNC với đơn vị unknown. Giữ model coordinates, không dời hết geometry về 0 chỉ để hiển thị; fit view là camera. Paper space/3D ngoài profile có diagnostic và không tự chiếu sang 2D.

## 9. Export: phạm vi, trang và batch

| Tùy chọn chung | Hành vi đề xuất |
| --- | --- |
| Phạm vi | Đối tượng được chọn; trang hiện tại; dải `1,3-5`; tất cả trang; toàn bộ artwork kể cả ngoài trang |
| Đối tượng được chọn | Xuất gộp selection hoặc mỗi top-level object/group một file; không tách mọi child của group trừ khi chọn rõ; thiếu selection disable và nêu lý do |
| Vùng xuất | Theo khổ trang; theo visible bounds artwork/selection gồm stroke/effects; vùng tùy chọn. Chọn scope và bounds là hai thiết lập độc lập |
| Clip theo trang | Có preview; chỉ lấy phần giao với page bounds khi bật. Không tự thêm hình chữ nhật nền vào SVG/CNC |
| Đối tượng xuyên trang | Design có thể clip theo từng trang; CNC phải báo đường bị cắt, cho xuất nguyên đối tượng hoặc chuyển sang repair có preview; không tạo contour hở âm thầm |
| Nhiều trang | PDF: một PDF nhiều trang hoặc mỗi trang một PDF. JPG/PNG/SVG/EPS/DXF: từng trang một file. TIFF đa frame chỉ khi profile encoder đã đạt |
| Hidden/locked/guides | Mặc định theo artwork đang hiển thị; locked vẫn xuất nếu visible. Hidden/non-printing/guides/construction lines chỉ khi bật rõ; CNC mặc định loại guides/construction |
| Phụ thuộc | Thu thập fonts/clip/mask/effects/assets cần thiết; selection có blend phụ thuộc backdrop cần report hoặc xuất vùng composite tường minh, không tự lấy mất phần nền cần thiết |
| Tỉ lệ và gốc | 100% mặc định; W/H/scale và origin giữ tọa độ/chân trang/bounds đều rõ. CNC 1:1; thay scale cần xác nhận kích thước xem trước |
| Đặt tên | `{document}_{page:03}_{asset}_{preset}`; tên Unicode, lọc ký tự cấm/reserved names, tránh collision, có preview danh sách đường dẫn |
| Ghi đè | Chọn hỏi/đổi tên/ghi đè từng batch; không cho output trùng nguồn Open/Import. File đang khóa/hết đĩa trả kết quả rõ |
| Batch/preset | Preset theo format và mục đích; xuất nhiều kích thước/profile từ một snapshot; progress theo file, Cancel, retry file lỗi và receipt |

Mặc định Export mở **trang hiện tại**, 100%, bounds theo trang. Nếu có selection, tùy chọn xuất selection hiện rõ và người dùng chủ động chọn; không tự đổi phạm vi mỗi khi selection thay đổi. Khi mở dialog đã chụp selection, thay selection ở editor không âm thầm đổi job đang review.

### 9.1 Tùy chọn ảnh và nén

Các thông số sau là schema UI đích; chỉ hiển thị control khi encoder/profile thực sự hỗ trợ. Không suy ra WIC có codec là mọi bit depth/color model đều được giữ.

| Format | Nhóm tùy chọn và giới hạn |
| --- | --- |
| JPG/JPEG | Quality 1-100, default 90 cho preset xem trước; baseline/progressive khi encoder hỗ trợ; subsampling khi có; RGB/gray, CMYK gate riêng; không alpha, chọn matte nền rõ; ICC và metadata policy |
| PNG | Lossless compression level/effort, RGB/RGBA; transparent/solid background; 8-bit, 16-bit gate riêng; indexed palette là chuyển đổi giảm màu cần preview; không gán Quality kiểu JPEG |
| TIFF | None/LZW/Deflate nếu encoder có; RGB/gray và CMYK theo gate; 8/16-bit theo pipeline; alpha convention, DPI/ICC; JPEG-in-TIFF nếu thêm phải ghi lossy; không mặc định nén mất dữ liệu |
| BMP/WebP | BMP profile pixel format rõ; WebP lossless hoặc lossy quality chỉ sau khóa codec; ngoài milestone định dạng bắt buộc |
| Chung | Pixel W/H, aspect lock, DPI, 100/150/300/600 hoặc custom hợp lệ, anti-alias theo artwork/text, profile/intent, nền, metadata giữ hoặc loại thông tin riêng tư |

`pixels = round(mm × DPI / 25,4)`; preview hiển thị cả mm và pixel, cách làm tròn và kích thước ước lượng. Đổi DPI không được vô tình đổi kích thước CNC. Profile raster làm việc từ nguồn/snapshot, không chụp màn hình hay phóng cache preview 32 MP. Export lớn dùng tile/strip với halo hiệu ứng và encoder streaming, peak budget đo riêng.

Xuất nén ảnh có preview trước/sau và size **ước lượng**; target dung lượng là tùy chọn tối ưu có giới hạn quality/resolution, báo nếu không đạt, không âm thầm giảm kích thước. Trong PDF có downsample ảnh theo effective DPI, threshold và nén lossless/JPEG; không downsample mặc định ảnh/vector xuất CNC. JPEG luôn tạo từ asset gốc/snapshot thay vì nén lại bản JPEG trước.

### 9.2 Tùy chọn PDF, SVG và EPS cho thiết kế/in

- **PDF:** chọn version/profile, trang, page boxes/bleed/crop marks, nén/downsample ảnh, RGB/CMYK/source profile, font embedding/subset khi có quyền hoặc outline có lựa chọn; transparency/native vector khi writer có coverage. PDF/X-4, spot/overprint/output intent chỉ bật sau validator và RIP/print gate riêng. Không coi PDF render đẹp là separations đã đúng.
- **SVG Design:** physical W/H và viewBox, precision theo sai số mm, presentation attributes/inline CSS theo profile, embed/link ảnh, text hoặc outlines, metadata/minify. Minify không tự round tọa độ vượt tolerance; responsive là lựa chọn web, CNC luôn có kích thước vật lý. SVGZ là profile nén đợt sau với decompression budget.
- **EPS Design:** LanguageLevel/version profile, BoundingBox/HiResBoundingBox, text/outlines/fonts theo quyền, color profile thực hỗ trợ, ảnh nhúng, vector/raster balance và resolution khi flatten. Không có live transparency tổng quát; dialog phải nêu phần cần chuyển đổi. File một trang; export nhiều trang tạo nhiều EPS.
- Tất cả writer chạy preflight theo object/feature. Có lựa chọn có mất mát thì report trước ghi file. Nếu không có cách giữ theo lựa chọn người dùng, dừng export thay vì bỏ effect hoặc trả file rỗng.

## 10. Profile và gate riêng cho cắt CNC

DXF/SVG/EPS/PDF ở đây là file giao tiếp tới CAM/phần mềm máy. Chúng chưa quyết định feed, spindle, chiều sâu, gá đặt hay đường chạy dao. Nghiệm thu CAM/máy đích thuộc gate riêng; vector đúng trên màn hình chưa đủ.

| Profile đề xuất | Nội dung xuất | Quy tắc bắt buộc |
| --- | --- | --- |
| DXF CNC Legacy R12 / AC1009 | LINE, ARC, CIRCLE, POLYLINE/VERTEX/SEQEND; 2D Z=0, layer thao tác | Không phát LWPOLYLINE trong profile R12. R12 không dựa vào INSUNITS để giải quyết mọi CAM: ghi units trong receipt và buộc kiểm kích thước nhập CAM |
| DXF CNC R2000 / AC1015 | LWPOLYLINE + bulge/ARC/CIRCLE, SPLINE nếu CAM profile đã kiểm | `$INSUNITS` đúng, còn lại explicit conversion; giữ spline hoặc approximate theo tolerance do profile quyết định |
| SVG CNC | Paths/shapes sạch, physical W/H `mm`, viewBox nhất quán, text outlines có lựa chọn | Bỏ dependency CSS/font ngoài, expand use/transforms trên bản xuất đã kiểm; không script/filter/raster trong layer cắt; màu/layer theo máy đích |
| EPS CNC | Path vector phẳng và kích thước xác định | Không nhúng raster thay path, không dùng preview EPS làm nội dung; test importer CAM cụ thể |
| PDF Vector CNC | Vector trang đã xác định box/tỉ lệ | Không scan/raster-only, không fit-to-page; kiểm trục Y, unit point→mm, clip và calibration trong CAM |

Quy tắc geometry:

1. Dùng double mm trong scene/canonical geometry, giữ source units và chuỗi transform. DXF OCS/extrusion/INSERT phải chuyển đúng; nonuniform scale/reflection có thể đổi circle/arc thành ellipse hoặc đảo hướng, không ép lại thành arc sai.
2. Giữ arc/circle/spline chính xác nếu profile đích hiểu; nếu buộc tessellate thì adaptive, tolerance theo mm trên output transform. Không dùng cố định số segment hay tolerance theo pixel/zoom.
3. Khảo sát ngưỡng tổng `0,01 mm` cho profile CNC Fine: tổng budget import 0,002 + normalize/transform 0,001 + curve conversion 0,004 + serialization/reopen 0,003 mm. Đây là đề xuất cần chốt theo vật liệu/CAM/máy, chưa là độ chính xác đã đạt; dữ liệu nguồn thô hoặc API float không đủ thì không chứng nhận.
4. Kiểm Hausdorff/max curve deviation phù hợp, extents, chiều dài, tâm/bán kính, endpoints, area và topology. Contour count, holes, nesting và open/closed state không được thay ngoài conversion đã chọn; đúng bbox hoặc pixel diff không đủ.
5. Dọn duplicate/zero length, nối gaps, simplify/weld chỉ trên bản xuất hoặc command có Undo và diff preview. Mặc định chỉ **phát hiện**; không tự khép đường hở hoặc merge chi tiết gần nhau. Tolerance nối đường tách khỏi tolerance xấp xỉ curve.
6. Phân biệt **centerline cut** với **outline stroke**. Centerline là đường gốc, không tự expand stroke tạo hai đường cắt; silhouette/outline/filled-region chỉ khi chọn, có xử lý holes và report. Text outline dùng glyph geometry thật; thiếu font không xuất bản phông thay thế mà không có lựa chọn.
7. Preflight nêu open contours, self-intersections, duplicate/overlap, rất nhiều node, chi tiết nhỏ, units unknown, reference/raster, clip chưa resolve, unsupported spline, chiều cao Z và phần vượt page. User chọn layer Cut/Score/Engrave; layer/color mapping không tự đặt thông số máy.
8. Contour open có thể hợp lệ cho score/engrave nên gate dựa vào operation profile; cut silhouette đòi closed. Chiều winding và nested holes giữ theo CAM đích; đường cắt lỗ trước có thể là sorting gợi ý, không hứa CAM sẽ giữ thứ tự entity.
9. Kerf/tool compensation để CAM mặc định; nếu sau này thêm offset export phải ghi rõ và tránh bù hai lần. Không tự apply contour effect thành đường dao.
10. Mỗi trang là một file CNC mặc định; giữ gốc hoặc reset origin theo lựa chọn. Không gộp các trang cùng gốc gây chồng geometry; không tự nesting/resize để vừa phôi.

Chuẩn đối chiếu: [Autodesk LWPOLYLINE DXF](https://help.autodesk.com/cloudhelp/2026/ENU/AutoCAD-DXF/files/GUID-748FC305-F3F2-4F74-825A-61F04D757A50.htm), [SVG coordinate systems/units](https://www.w3.org/TR/SVG2/coords.html). Round-trip phải có consumer độc lập; không chỉ exporter và importer chung code cùng tự xác nhận.

## 11. Đặc tả UI, action, fields và trạng thái

Tuân thủ `docs/design/ui-ux-standard.md` và `docs/architecture/workspace-layout.md`. English mặc định, Vietnamese qua message ID; thuật ngữ trong tài liệu là tiếng Việt. Dùng UI native hiện hành, không tạo app web hay cửa sổ bố cục thứ hai.

| Bề mặt / action ID | Nhãn EN / VI, shortcut | Placement và trạng thái |
| --- | --- | --- |
| File > Open…, `action.file.open` đã có | Open… / Mở…, Ctrl+O | Standard toolbar tái dùng Open; hỗ trợ filter theo registry, không bật format chưa có capability |
| File > Import…, `action.file.import` đã có | Import… / Nhập…, Ctrl+I | Có tài liệu writable; không vào toolbox trái. Mở dialog rồi placement preview |
| File > Export…, `action.file.export` đã có | Export… / Xuất…, Ctrl+E | Thay menu/handler Export SVG thành Export chung; dùng cùng enable/preflight cho menu và shortcut |
| File > Export For > CNC…, `action.file.export_for.cnc` đề xuất | CNC… / Cắt CNC… | Mở cùng dialog với preset CNC; chưa gán shortcut mới |
| Properties > Source, `action.object.source_properties` đề xuất | Source / Nguồn | Khi chọn imported object: format, fidelity, source status, units, link/reference; mixed có trạng thái Mixed; không tự sửa source |
| Help > File Formats, `action.help.file_formats` đề xuất | File Formats / Định dạng tệp | Chỉ rõ profile/giới hạn, cách import/export và các lựa chọn mất mát |

Ctrl+I được dành cho Import trên editor theo yêu cầu, kể cả khi đang chọn TextObject. Bỏ binding Ctrl+I khỏi Italic và cập nhật menu/tooltip; Italic vẫn có menu Text và Properties. Không tự chuyển Italic sang Ctrl+Shift+I vì shortcut này đang có nhánh Intersection. Khi IME đang composition hoặc field số chưa hợp lệ, giữ focus/validation; không nuốt dữ liệu để mở dialog. Text draft hợp lệ kết thúc qua writer theo policy trước Import; kiểm thực tế luồng text editing.

Standard toolbar có thể thêm icon Import/Export nếu còn chỗ, dùng overflow khi hẹp. Đề xuất asset `file-import.svg`, `file-export.svg`, `file-export-cnc.svg` theo bộ icon repo, provenance/license cùng hệ; task kế hoạch chưa tạo assets. Không thêm tool vẽ mới. Property bar chỉ hiện mode/scale/X/Y/Apply/Cancel của phiên placement; Properties là nơi đầy đủ cho thông số nguồn/placement, hai bề mặt dùng chung draft. Export options ở dialog vì scope là job, không tạo Properties toàn cục dư thừa.

| Nhóm field | Scope/default/range và trạng thái |
| --- | --- |
| Format/preset | Export job, registry enum; unsupported disable có lý do; default format gần nhất đã dùng, phạm vi mặc định trang hiện tại |
| Page range | Source dialog hoặc export job; 1-based, không trang âm/ngoài tài liệu, deduplicate và thứ tự rõ; single-page ẩn/disable phần multi-page |
| Selection | Snapshot IDs lúc mở export; rỗng disable; locked có thể đọc/xuất, reference chỉ theo capability; mixed hiển thị diagnostic từng phần |
| Scale/W/H/X/Y | Placement/export draft; mm và %, finite, W/H > 0, aspect lock; mặc định 100%; giới hạn theo geometry/budget, error không tự clamp source |
| Pixels/DPI | Raster job, integer pixels > 0 và DPI finite > 0 trong budget đã đo; field không áp dụng cho vector pure thì ẩn |
| Quality/compression | Theo schema encoder; JPEG quality 1-100; PNG/TIFF lossless không hiện slider chất lượng lossy; không ghi tùy chọn unsupported vào preset |
| Units/tolerance | CNC job, unit enum và tolerance > 0 mm; unknown units chặn production; sửa tolerance chạy lại preflight/preview |
| Source/link/color | Read-only provenance hoặc explicit conversion draft; thiếu asset/profile có nhãn; chọn lại link tạo command khi Apply, không đổi bởi hover |

Tooltips/focus help ví dụ: “Import (Ctrl+I) / Nhập file vào bản vẽ hiện tại”, “Export (Ctrl+E) / Chọn định dạng và phạm vi xuất”. Placement hint: “Click để đặt. Enter hoàn tất. Esc hủy.” Export status: “Đang xuất 2/5 trang”; lỗi có vị trí control và cách sửa. UI người dùng không chứa parser class, protocol hay epoch/job IDs.

Dialog có các nhóm Phạm vi / Định dạng / Chất lượng hoặc Vector / Màu / Đích; Advanced thu gọn, preview và danh sách file xem được; Apply/Export/Cancel luôn truy cập được. Review/loss report có thể mở lại và chọn vùng ảnh hưởng. Tab order, keyboard focus, screen reader name, light/dark/high contrast và 1366×768/1920×1080 tại 100/125/150/200% thuộc nghiệm thu. Các lựa chọn dialog/preview không tạo Undo; Import Apply một Undo, export không sửa dirty state.

## 12. Chống lỗi, hủy tác vụ và tài nguyên

Tận dụng C6/C8 và worker host hiện hữu. Chỉ nhận handles/assets đã cấp; worker/parser không tự đọc sibling files, gọi mạng, chạy script/macro, PDF JS/actions hoặc process tùy ý. Linked assets do host broker chọn/cấp. Test OS denial riêng với positive control; Job Object memory/process limits chưa đủ chứng minh filesystem/network containment.

Budget gồm source/decompressed bytes, images/pixels/frames, fonts/glyphs, segments/entities/groups/depth/cycles, effect scratch, scene/staging/undo, cache/temp/disk/handles, worker count và wall time. Bắt đầu một job import nặng có hàng đợi hữu hạn; số cụ thể khóa sau spike trên corpus/máy tham chiếu, không gắn timeout/512 MiB tùy tiện để nhận file lớn.

Parse/decode/normalize/index/prepare/export cleanup nặng chạy nền, không block UI. Cancel đổi generation trước thu hồi; reject proposal/preview cũ sau edit/undo/close/reopen/tab switch. Commit kiểm document ID/epoch/strict revision; stale cần chạy lại, không tự rebase. Memory pressure giảm preview hoặc fail rõ, không giảm độ chính xác source/output để qua gate.

Các lỗi phải phân biệt: unsupported version/feature, thiếu font/link/profile, password, malformed, quota, timeout, worker failure, validation failure, permission/locked output, disk full và cancelled. Output hỏng không mang tên file hoàn tất. Log chỉ ID/size/status/duration, không chứa nội dung/mật khẩu/đường dẫn khách hàng.

## 13. Gói triển khai, dependencies và tiêu chí ra khỏi gói

Đây là backlog đề xuất, chưa tạo task implementation hay giao agent. Mỗi gói tạo/claim exact paths trước sửa; shared CMake/registry/contracts có phạm vi riêng để tránh xung đột. File code chuẩn 300-700 dòng, tối đa 800; tách domain thay vì nối tính năng vào file shell lớn.

| Gói | Phạm vi source dự kiến | Phụ thuộc / kết quả phải đạt |
| --- | --- | --- |
| F00 - Baseline và corpus | Manifest local ignored; `docs/quality` receipt schema mới; đọc registry/source | Khoá snapshot thực, ID corpus, oracle builds/settings/rights; DXF legacy và gaps có fixture. Không sửa trạng thái capability từ inventory |
| F01 - Hợp đồng và capability | C8/C9, C5/C6 phần liên quan, `contracts/fixtures`; `modules/interop/include/.../capabilities` và `format_capability_matrix.cpp` trong task consumer riêng | Sau F00; report hai chiều, pages/selection/asset/reference, export options versioned, CNC precision; consumer review document/rendering/interop/workers/UI |
| F02 - Model, multi-page, storage | `core/document` pages/assets/provenance/reference/commands/storage; native codec/test hiện có; cập nhật consumer theo lease riêng | F01; snapshot đa trang thật, Undo import, version/migration, save/reopen không cần đường nguồn; native v2-v5 vẫn đọc theo contract |
| F03 - Worker/dependency readiness | `platform/workers` broker/v2/staging/lifecycle; `cmake/PikDrawDependencies.cmake`, dependency manifests/notices | F01; tái dùng v2, audit pin/license/ABI cho từng engine; test nguồn >16 MiB và containment. F03 là prerequisite riêng cho mỗi adapter chưa sẵn sàng |
| F04 - Framework UI Open/Import/Export | Tách workflow file jobs/dialog/options/placement; sửa `native_window_files.cpp`, `native_window_shortcuts.cpp`, menu/action adapters; locales/help/icons | F01-F03; một dispatcher, Ctrl+O/I/E, report/cancel, export scope builder/snapshot; no-op/unsupported không báo thành công |
| F05 - Raster bắt buộc | Adapter raster mới trong `modules/interop`, worker codec allowlist; encoder/export surface; color/render asset consumers | F02-F04; JPG/PNG/TIFF baseline, alpha/ICC/DPI/orientation, compression, selection/page export. Depth/CMYK mở từng profile có oracle |
| F06 - SVG Design và CNC | `modules/interop/src/adapters/svg_*`, `writers/svg_exporter.cpp` tách options/preflight; geometry/renderer paths chỉ khi có lease | F02-F04; đóng opacity import↔export, tree/styles/units/gradient/clip và precision; two profiles, effects chưa map có lựa chọn |
| F07 - DXF legacy và writer CNC | `dxf_source_reader.cpp`, `dxf_entity_parser.cpp`, adapter/writer profile mới; `modules/production` preflight theo task riêng | F01-F04; đọc AC1003 POLYLINE/VERTEX fixture, explicit missing-unit choice; writer R12/R2000, arcs/bulge/layers; topology/tolerance và independent reopen |
| F08 - PDF và AI-PDF | Adapter PDF/AI routing mới; PDF worker/extraction/writer subset; export preflight và page UI | F02-F05, F03 PDF engine; mở mẫu lớn/multi-page, AI representation đúng, native/reference split; PDF export baseline; print/PDF-X gate sau |
| F09 - CDR | CDR adapter version profiles + reader worker, mapping/report fixtures | F02-F04, F03 CDR engine; cả 4 mẫu có report/preview so CorelDRAW, whitelist thực; PowerClip/lens/mesh không mất âm thầm; chưa có CDR writer |
| F10 - EPS và AI legacy | EPS recognizer, interpreter worker, chained PDF job, vector EPS writer/conversion/export preflight | F08 và gate dependency/license riêng; hai EPS local có oracle; vector CNC độc lập với ảnh fallback; không bypass sandbox để cứu sample |
| F11 - CNC consumer và release | `modules/production`, geometry regression hiện có, `tests/fixtures` redistributable, quality/tooling receipts; registry capability update sau nghiệm thu | F06-F10 theo format; xuất/mở trong CAM đích, 1:1, closed/holes và tolerance; app UX/DPI/cancel/save/reopen; công bố từng profile thực đạt |

Thứ tự ưu tiên giao hàng: F00-F04 nền → JPG/PNG/TIFF baseline + SVG/DXF CNC → PDF/AI-PDF → CDR → EPS/AI legacy sau gate công nghệ. Các định dạng bắt buộc đều có gói hoàn thành; trạng thái “đã nhận file” không đủ để đóng gói. Chốt thời lượng sau spike F00/F03/F08/F09/F10; chưa có dữ liệu để đưa deadline đáng tin cho mọi effect độc quyền.

Nâng fidelity là chuỗi nhỏ có gate: geometry/units/layer → gradient/text/fonts → clip/mask/group compositing → hiệu ứng phức tạp. Mở nhóm hiệu ứng nào thì cùng lúc phải có model, render, persistence và import/export evidence tương ứng, không chỉ thêm vào combo box.

## 14. Ca nghiệm thu và bằng chứng bắt buộc

Ca dưới là **chưa chạy trong task kế hoạch**. Khi triển khai phải có Given/When/Then/Verify cụ thể với fixture ID, build và oracle. Mở rộng tests hiện có thay vì thêm suite trùng chỉ để tăng số lượng.

| ID | Given / When | Then / Verify |
| --- | --- | --- |
| AC01 Open | Có tab dirty; Ctrl+O mở từng format bắt buộc | Tab cũ nguyên vẹn; tab mới chỉ publish sau thành công; khổ/units đúng; Ctrl+S không ghi đè nguồn; native shell E2E |
| AC02 Import | Có tài liệu/selection; Ctrl+I nhập file, đặt, Enter | Shortcut mở Import, không Italic; đúng vị trí/size, một Undo; undo/redo giữ IDs/assets/report; workflow và native UI tests |
| AC03 Cancel/stale | Job đang parse/place; Esc/cancel hoặc sửa/undo/đóng tab | Không nhận preview/proposal cũ, không đổi document/history/dirty ngoài lệnh người dùng; worker/workflow fault fixtures |
| AC04 Fidelity | PDF/AI/CDR/EPS có effect/missing font | So nguồn đúng build; unsupported có report và lựa chọn; không hiệu ứng bị biến thành Normal hoặc bỏ mất; oracle pixel + structure |
| AC05 PDF/AI pages | PDF nhiều trang/AI-PDF; chọn `1,3-5` và box | Đúng thứ tự/khổ/trang thực có, không xếp chồng; AI không PDF nhận đúng diagnostic; độc lập app nguồn |
| AC06 Raster | PNG alpha, JPG EXIF/ICC, TIFF frame/depth | Hướng, pixel size, mm/DPI, alpha/color đúng; 16-bit chưa đạt không tự gọi lossless toàn dữ liệu; decode/encode oracle |
| AC07 Selection | Có object/group chọn, stroke rộng/shadow/clip/blend | Chỉ scope đã chọn cùng dependencies; bounds/effect không bị cắt ngoài thiết lập; unsupported backdrop có report; ảnh render + object set |
| AC08 Page clipping | Object nằm trong/ngoài/xuyên hai trang | Theo page/selection/artwork bounds đúng; CNC không tạo contour hở âm thầm; test scoped geometry và native preview |
| AC09 Batch pages | 5 trang khác khổ; xuất dải/từng trang/PDF nhiều trang | Đúng số file/trang/names/size, không file rỗng, thứ tự rõ; reopen độc lập từng output |
| AC10 Compression | Cùng snapshot xuất JPEG quality, PNG lossless, TIFF LZW/Deflate | Đúng dimensions/options; lossless so pixels trong cùng color pipeline, JPEG đánh giá artifact/quality; size estimate không là cam kết |
| AC11 SVG self-round-trip | SVG opacity/gradient/transform/path có holes | Import→edit→save/reopen→export→reimport; semantics subset và units giữ; mở Corel/Illustrator/browser làm consumer bổ sung |
| AC12 DXF legacy mẫu | AC1003, 298 polyline, 19.319 vertices; units chưa xác định | Nhận chain không rơi vertex/closure; yêu cầu chọn units nếu thiếu; không sửa source; topology/extents so oracle nguồn và independent reader |
| AC13 CNC curves | Arc/circle/bulge/spline, reflection/scale, mm/inch | Xuất R12/R2000/SVG/EPS/PDF theo profile; max deviation trong budget, holes/closure đúng; không double cut do stroke expansion |
| AC14 CAM | Coupon 100×100 mm, circle/slot/hole/line/Bezier/text và mẫu thật | Import CAM đúng version/settings, đo 100 mm, holes/open score đúng, không fit-to-page; thử cắt coupon trước production, lưu evidence đo |
| AC15 Native persistence | Import rồi sửa/undo/redo/save; di chuyển bản sao nguồn/link | Reopen không tự parse ngoài sandbox, giữ page/assets/report/fidelity; link-only báo thiếu; storage migration fixtures |
| AC16 Failure/output | File >16 MiB, malformed/zip bomb/font bomb, worker crash, disk full/output locked | UI có thể hủy; không partial document/final-name file hỏng; hết temp/handles đúng ownership; worker/export integration |
| AC17 UI/accessibility | Empty/single/mixed/locked/reference, text/numeric/IME focus | Ctrl+I/E đúng context, labels/help/focus/disabled reason; dialog dùng được ở các window/DPI đã nêu; native capture và thao tác thật |
| AC18 Color/print | RGB/CMYK/spot/ICC/overprint/transparency vectors | Preview diff đúng profile; PDF print profile kiểm separations/RIP riêng; không suy ra từ RGBA hoặc pixel pass |

Đối chiếu appearance dùng render Adobe/Corel đúng phiên bản/settings khi được phép; JPG đi kèm hoặc thumbnail nhúng chỉ là tài liệu tham khảo cho đến khi biết cách tạo. Pixel/color/alpha diff ở nhiều scale/DPI kết hợp review vùng effect; ngưỡng theo feature/profile khóa trước so, không chọn lại ngưỡng để hợp thức hóa kết quả. Engine của importer không tự làm oracle duy nhất.

Performance đo trên máy tham chiếu đã ghi CPU/RAM/GPU/driver, hardware/WARP/CPU; thời gian từng file, UI stall, cancel latency, peak memory/temp/handles và lỗi. Có corpus repeat/cancel/batch và kiểm không rò tài nguyên. Các mốc latency hoặc tốc độ chưa đo không được đưa lên UI/release notes như cam kết.

Tests liên quan sẵn có để tái dùng: `test_pikdraw_interop_capabilities`, `test_pikdraw_interop_svg_adapter`, `test_pikdraw_interop_svg_exporter`, `test_pikdraw_interop_dxf_adapter`, các `*_worker_adapter`, `test_pikdraw_worker_v2_integrity`, `test_pikdraw_worker_svg_import`, `test_pikdraw_worker_dxf_import`, `test_pikdraw_workers_live_containment`, `test_pikdraw_document_storage_fidelity`, `test_pikdraw_desktop_shell` và geometry/color/rendering suites sở hữu. Những test mới chỉ đăng ký khi hành vi chưa được suite hiện có bảo vệ.

Ví dụ chạy đúng mục tiêu khi có implementation: `tooling/native/build.ps1 -Test -TestTarget test_pikdraw_interop_dxf_adapter -TestRegex '^test_pikdraw_interop_dxf_adapter$'`. Tên test/target phải đối chiếu CMake snapshot lúc chạy. Task chỉ sửa tài liệu không cần build lại app; doc links/encoding/whitespace và task harness là kiểm tra phù hợp.

## 15. Điều kiện công bố hỗ trợ và các quyết định còn phải chốt

Một profile chỉ được bật khi đúng snapshot tích hợp có: dependency lock/notices; reader/writer feature catalog; report/no-silent-loss; fixtures geometry/appearance/text/color/provenance; import-edit-undo-save-reopen-export; fault/cancel/stale/containment; native UX; consumer độc lập và known limits. Profile CNC cần thêm CAM/version/settings và tolerance/topology receipt, chưa thử máy thì ghi rõ chưa nghiệm thu cắt thực tế.

Các quyết định kỹ thuật còn mở có gói chịu trách nhiệm: F01 chốt scene/pages/report/precision schema; F02 chọn schema storage tiếp theo; F03 pin dependencies/codec/security budgets; F08 xác nhận API PDF đủ precision/color; F09 chốt CDR version/feature whitelist; F10 chốt interpreter/writer và quyền phân phối; F11 chọn CAM/máy đích và tolerance theo công việc. Những việc này không cản hoàn thành kế hoạch nhưng là gate trước khi bật chức năng tương ứng.

Trong đợt khảo sát này: đã đọc source/contracts/UX, kiểm header/container của bộ mẫu và tham khảo Adobe/Corel cùng tài liệu kỹ thuật nêu trong bài. **Chưa chạy mở bộ mẫu trong PikDraw, chưa so ảnh/effect, chưa chạy export round-trip hoặc cắt CNC; chưa có cơ sở công bố định dạng mới đã được hỗ trợ.**
