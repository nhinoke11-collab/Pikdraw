# 01 — Tham chiếu chính thức và baseline PikDraw

[Kế hoạch chính](../image-editing-color-print-20261024.md). Tra cứu trực tiếp ngày **24/10/2026**.
Chỉ dùng tài liệu hãng/tổ chức hoặc tác giả nghiên cứu; không lấy bài quảng cáo bên thứ ba làm
bằng chứng thuật toán, tốc độ, tương thích định dạng hoặc chuẩn màu.

## 1. Mốc phiên bản và các điểm mới nên học

| Sản phẩm | Mốc đã xác minh | Chắt lọc cho PikDraw |
| --- | --- | --- |
| Photoshop desktop | Release notes 27.11, tháng 9/2026; trang cập nhật 05/10/2026 [S01] | Adjustment có mask, sửa lại được; Light, Color and vibrance, Clarity/Dehaze/Grain; quick actions là lối vào cùng mô hình sửa |
| Illustrator desktop | Release notes 30.8, tháng 8/2026 [S02]; What's New cập nhật 14/10/2026 [S03] | Links/relink, quản lý ảnh đặt trong tài liệu vector, profile khi place; kiểm soát output/separations; không dùng Illustrator làm chuẩn đầy đủ cho retouch ảnh |
| CorelDRAW/PHOTO-PAINT 2026 | September Update 27.2; trang Updates ghi phát hành 14/09/2026 [S04–S06] | Adjustments không phá hủy, local mask, histogram, preset; Mask from Subject/background removal; tích hợp ảnh với tài liệu vector và workflow proof |

Tính năng hiện hành có thể tồn tại từ các bản trước. Ngày cập nhật Help không đồng nghĩa ngày
ra mắt tính năng. Không gán mọi tính năng dưới đây thành “mới trong 2026”; không coi Beta,
thuê bao hoặc model AI của hãng là capability/license đã có trong PikDraw.

### Những hành vi chuyển thành yêu cầu thiết kế

| Tham chiếu | Hành vi đã được tài liệu hóa | Quyết định PikDraw |
| --- | --- | --- |
| Photoshop [S07–S11] | Adjustment layer, mask, nhóm Light/Color/Clarity, xử lý sắc độ và màu | Một graph tham số theo ảnh, Properties chung; mask theo node; có basic/advanced trong cùng section |
| Corel PHOTO-PAINT [S12–S14] | Adjustments docker, local adjustment, preset có nhiều filter, reorder effect | Giữ node ID, reorder có Undo; copy/paste recipe theo scope; preset khác profile ICC |
| Corel/Photoshop [S15–S17] | Channels, Channel Mixer, Selective Color | Xem composite/kênh riêng; chọn rõ channel edit target; mixer và selective có semantics riêng |
| CorelDRAW/Photoshop [S18–S21] | Proof theo profile thiết bị; intent, preserve numbers, hard proof, app/driver quản lý màu | Tách ProofSetup/OutputCondition; một owner chuyển màu; lưu preset và run metadata |
| Illustrator/Corel/Photoshop [S22–S24] | Process/spot separations, overprint, giữ/convert spot, PDF output | Đầu ra có plate model, output intent và report; không tạo tách màu từ screenshot |
| ICC/Fogra [S25–S28] | Profile/characterisation theo điều kiện in; dữ liệu đo và proof có yêu cầu riêng | Profile hash, vật liệu/mực/chế độ in, so reference/measurement; không tuyên bố chuẩn từ tên preset |
| CIEDE2000/CGATS [S29–S30] | Công thức cần bộ vector kiểm chứng; dữ liệu đo có format/metadata | Parser subset rõ, patch ID, điều kiện đo, test vector độc lập |
| Little CMS [S31–S32] | CMM có BPC, proof/gamut, nhiều kiểu pixel; release 2.19 được công bố 17/04/2026 | Ưu tiên adapter CMM cho pixel 16/float và proof; phải kiểm license/tag/build trước tích hợp |

Từ tham chiếu workflow **suy ra thiết kế PikDraw**, không suy ra Adobe/Corel dùng tile size,
cache, kernel, scheduler hoặc cách lưu graph được đề xuất trong plan này.

## 2. Baseline đã đối chiếu source, không chạy lại runtime

| Vùng | Nguồn đã đọc | Hiện trạng cần kế thừa / khoảng trống |
| --- | --- | --- |
| Registry | `registry/modules.json` | Raster in_progress, phụ thuộc document/color; production planned. Core/rendering không phụ thuộc module raster trực tiếp |
| Asset/cache | `modules/raster/README.md`, `src/{asset_store,tile_cache,memory_budget}.cpp`, CMake | Có source store/cache/budget và test targets; README nêu desktop/tile decode/evaluator còn cần tích hợp |
| ImageObject | `core/document/include/pikdraw/document/model/object.hpp` | Có `pixels_rgba`/shared pixels, metadata nguồn, optional asset descriptor và edit graph; preview hiện là RGBA8, không phải master CMYK/16 bit |
| Graph | `core/document/include/pikdraw/document/model/raster_edit_graph.hpp` | Schema 1, ordered nodes có ID; Crop/Flip/Rotate/Exposure; descriptor encoded length 64 bit. Chưa có mask/retouch/channel node hoặc typed pixel planes |
| Command | `core/document/include/pikdraw/document/commands/raster_edit_commands.hpp` | `SetRasterEditNodeCommand` cập nhật node cùng ID thành một Undo; cần nối đủ UI/evaluator/export và thêm reorder/remove/mask semantics |
| CMM | `core/color/{include/pikdraw/color,src}/preview/profile_color_transform.*` | RGB/CMYK process transform; bốn intent; RGB8 và process U16LE có batch RGB↔CMYK cùng alpha; `create` từ chối BPC, Gray/Lab chưa được backend này hỗ trợ |
| Proof/gamut | `core/color/include/pikdraw/color/preview/icc_preview.hpp` | Có proof/gamut/overprint options. Gamut được ghi rõ là RGB round-trip diagnostic; overprint reserved, chưa là compositor bản tách màu |
| Profile UI | `apps/desktop/src/shell/native_window_color_profiles.cpp` | Có Assign/Convert; thông báo conversion hiện giữ pixels ảnh/spot/gray, BPC chưa khả dụng; không nhận lệnh màu tài liệu là conversion ảnh đã hoàn chỉnh |
| TAC | `core/color/include/pikdraw/color/preview/color_transform.hpp` | `TACOutputCondition`, helper và summary; process paints, raster/spot chưa đánh giá. RichBlack enum/preset không thay output condition |
| Storage | `core/document/include/pikdraw/document/storage/native_format.hpp`, `docs/architecture/native-pr-format.md` | Payload v23, container v23/v24; source/descriptor/graph lưu được ở phạm vi hiện hành, save/load vẫn có full-buffer; streaming/index/dedup còn mở |
| UI contracts | `docs/design/ui-ux-standard.md`, `workspace-layout.md`, feature template | 3–5 control trên bar; Properties đủ trường; action mở panel khác action sửa; EN mặc định, VI catalog; keyboard/IME/DPI cần kiểm |
| Test targets | CMake của raster/color/document/desktop | Có các suite hiện hành liệt kê ở tài liệu 06; sự tồn tại test không chứng minh phần mới đã pass |

`docs/project-state.json` hiện cũng ghi hạn chế TAC, proof/gamut oracle, separations và in thật.
Đây là audit source đang dirty ngày 24/10; không lấy snapshot cũ để xóa phần đã được triển khai,
không nâng trạng thái từ một khai báo field/action thành tính năng dùng được.

## 3. Phần tiếp nối các plan trước

| Nguồn sở hữu | Plan này tiếp tục | Ranh giới |
| --- | --- | --- |
| [Ảnh lớn](../raster-import-large-assets-20261021.md) | Asset source, link/embed, tile/LOD, streaming, full-res export | Không tạo asset store thứ hai, không hạ yêu cầu ảnh lớn thành proxy-only |
| [Màu hiện hành](../existing-feature-upgrade-20261020/color-management-plan.md) | Profile resource, Assign/Convert, intent, preview, TAC/black semantics | Thêm pixel formats và proof/print jobs qua cùng color service |
| [C4](../../../contracts/c4-color-typography.md) / [C9](../../../contracts/c9-appearance-effects.md) | Giữ source màu, alpha, blend/filter working spaces có phiên bản | RGB ảnh mới không được tự đổi semantics gradient/vector/effect đã lưu |
| [C8](../../../contracts/c8-import-fidelity.md) / [vector interop](../../architecture/vector-resource-interoperability.md) | Format/version/representation, report, recoverable original | Giữ riêng render/editability/provenance/source export/production |

## 4. Danh mục nguồn

Mỗi mã dưới đây là một tài liệu cụ thể. Đường dẫn trực tiếp cho phép kiểm lại ngày/nội dung
khi triển khai; bản ghi này không thay thế toàn văn tiêu chuẩn ISO hoặc cấp chứng nhận.

- **[S01]** [Photoshop desktop release notes](https://helpx.adobe.com/photoshop/desktop/whats-new/photoshop-on-desktop-release-notes.html) — 27.11, cập nhật 05/10/2026.
- **[S02]** [Illustrator desktop release notes](https://helpx.adobe.com/illustrator/desktop/new-features/release-notes.html) — 30.8, cập nhật 31/08/2026.
- **[S03]** [Illustrator What's New](https://helpx.adobe.com/illustrator/desktop/new-features/whats-new.html) — cập nhật 14/10/2026; quản lý links và các cập nhật hiện hành.
- **[S04]** [CorelDRAW Updates](https://www.coreldraw.com/en/support/updates/) — September 2026 Update 27.2, 14/09/2026.
- **[S05]** [CorelDRAW September 2026 Help](https://help.coreldraw.com/CorelDRAW/540111192/Documentation-Windows/CorelDRAW-en/CorelDRAW-CorelDRAW-Graphics-Suite-September-2026-Update.html) — PowerTRACE và các workflow AI mới; trace là nhánh vector riêng.
- **[S06]** [CorelDRAW March 2026 Help](https://help.coreldraw.com/CorelDRAW/540111192/Documentation-Windows/CorelDRAW-en/CorelDRAW-CorelDRAW-Graphics-Suite-Marcht-2026-Update.html) — background removal và Mask from Subject; không hứa chất lượng suy luận nếu chưa đo.
- **[S07]** [Photoshop: Adjust image color and tone](https://helpx.adobe.com/photoshop/desktop/adjust-color/color-corrections/adjust-image-color-and-tone.html) — adjustment layers/Camera Raw, 30/09/2026.
- **[S08]** [Photoshop: Adjustments panel](https://helpx.adobe.com/photoshop/desktop/adjust-color/color-corrections/apply-color-adjustments-with-adjustments-panel.html) — Properties/mask/clipping, 06/10/2026.
- **[S09]** [Photoshop: Light](https://helpx.adobe.com/photoshop/desktop/create-manage-layers/color-adjustment-fill-layers/adjust-image-lighting-with-light.html) — exposure, contrast, highlights/shadows, whites/blacks, 09/09/2026.
- **[S10]** [Photoshop: Color and vibrance](https://helpx.adobe.com/photoshop/desktop/create-manage-layers/color-adjustment-fill-layers/correct-color-balance-with-color-and-vibrance.html) — temperature/tint/vibrance/saturation, 23/02/2026.
- **[S11]** [Photoshop: Clarity and Dehaze](https://helpx.adobe.com/uk/photoshop/desktop/create-manage-layers/color-adjustment-fill-layers/adjust-contrast-with-clarity-and-dehaze.html) — adjustment có thể sửa lại, 16/03/2026.
- **[S12]** [PHOTO-PAINT 2026: Adjustments docker](https://help.coreldraw.com/PHOTO-PAINT/540111192/Documentation-Windows/Corel-PHOTO-PAINT-en/Corel-PHOTO-PAINT-Adjustments-docker.html) — local adjustment và mask.
- **[S13]** [PHOTO-PAINT 2026: Adjustment presets](https://help.coreldraw.com/PHOTO-PAINT/540111192/Documentation-Windows/Corel-PHOTO-PAINT-en/Corel-PHOTO-PAINT-Apply-manage-and-create-adjustment-presets.html) — nhiều filter và quản lý preset.
- **[S14]** [PHOTO-PAINT 2026: Special effects](https://help.coreldraw.com/PHOTO-PAINT/540111192/Documentation-Windows/Corel-PHOTO-PAINT-en/Corel-PHOTO-PAINT-Work-with-special-effects.html) — stack, reorder, vùng áp dụng, before/after.
- **[S15]** [PHOTO-PAINT 2026: Color channels](https://help.coreldraw.com/PHOTO-PAINT/540111192/Documentation-Windows/Corel-PHOTO-PAINT-en/Corel-PHOTO-PAINT-Color-channels.html) — component, split/combine, calculations.
- **[S16]** [Photoshop: Channel Mixer](https://helpx.adobe.com/photoshop/desktop/adjust-color/selective-color-adjustments/mix-color-channels-with-channel-mixer.html) — output/source channels, total và preset, 30/09/2026.
- **[S17]** [Photoshop: Selective Color](https://helpx.adobe.com/photoshop/desktop/adjust-color/selective-color-adjustments/make-selective-color-adjustments.html) — màu đích, Relative/Absolute, slider CMYK trên RGB, 30/09/2026.
- **[S18]** [CorelDRAW 2026: Soft proofing](https://help.coreldraw.com/CorelDRAW/540111192/Documentation-Windows/CorelDRAW-en/CorelDRAW-Soft-proofing.html) — proof profile, preserve numbers, gamut, export/print proof.
- **[S19]** [Photoshop: Print with color management](https://helpx.adobe.com/photoshop/desktop/print/print-with-color-management.html) — app/driver owner, profile giấy/mực, 04/10/2026.
- **[S20]** [Photoshop: Print a hard proof](https://helpx.adobe.com/photoshop/desktop/print/print-a-hard-proof.html) — profile điều kiện mô phỏng khác profile máy in proof, 04/10/2026.
- **[S21]** [CorelDRAW 2026: Print colors accurately](https://help.coreldraw.com/CorelDRAW/540111192/Documentation-Windows/CorelDRAW-en/CorelDRAW-Print-colors-accurately.html) — tuyến GDI RGB/Gray và PostScript, preserve numbers.
- **[S22]** [Illustrator: Print color separations](https://helpx.adobe.com/illustrator/using/printing-color-separations.html) — process/spot plates, overprint, giới hạn preview; Help cập nhật 17/07/2024 và vẫn hiện hành khi tra cứu.
- **[S23]** [CorelDRAW 2026: PDF color management](https://help.coreldraw.com/CorelDRAW/540111192/Documentation-Windows/CorelDRAW-en/CorelDRAW-PDF-color-management-options.html) — native/output model, profile, spot/overprint.
- **[S24]** [Photoshop: Print color separations](https://helpx.adobe.com/photoshop/desktop/print/print-color-separations.html) — kênh CMYK/spot và giới hạn tuyến in, 10/10/2026.
- **[S25]** [ICC: sRGB encoding](https://registry.color.org/rgb-registry/srgb) và [sRGB profiles](https://registry.color.org/rgb-registry/srgbprofiles) — transfer function, white point, profile variant; không đồng nhất mọi RGB.
- **[S26]** [ICC Profile Registry](https://registry.color.org/profile-registry/) — condition, TAC và separation khác nhau giữa profile.
- **[S27]** [Fogra characterisation data](https://fogra.org/en/downloads/work-tools/characterisation-data) — reference data, profile khác separation; điều khoản dữ liệu khác quyền dùng control strip.
- **[S28]** [Fogra MediaWedge](https://fogra.org/en/shop/mediawedge-cmyk) và [Prepress certification](https://fogra.org/en/certification/prepress-technology) — contract proof/validation print có điều kiện và license riêng.
- **[S29]** [Sharma, Wu, Dalal: CIEDE2000 và supplementary tests](https://hajim.rochester.edu/ece/sites/gsharma/ciede2000/) — test vector cho công thức, không phải preset dung sai bản in.
- **[S30]** [X-Rite: CxF, Annex A về CGATS 17](https://www.xrite.com/-/media/xrite/files/literature/misc/c/cxf_standard_en.pdf) — cấu trúc trao đổi số đo; tham chiếu cấu trúc, không nhận hỗ trợ toàn bộ CxF/CGATS.
- **[S31]** [Little CMS engine](https://littlecms.com/color-engine/) và [release 2.19](https://www.littlecms.com/blog/2026/04/17/lcms2-2.19/) — khả năng CMM và mốc phiên bản để đánh giá tích hợp.
- **[S32]** [Little CMS tutorial](https://www.littlecms.com/LittleCMS2.18%20tutorial.pdf) — API proof/gamut/BPC; tutorial 2.18 cần đối chiếu headers của tag được pin khi code.
- **[S33]** [PDF Association: Technical requirements of PDF/X](https://pdfa.org/technical-side-and-requirements-of-pdfx) và [PDF/X key facts](https://pdfa.org/pdfx-the-key-facts/?highlight=PDF%2FA) — OutputIntent, embedded assets/fonts, phân biệt X-1a/X-4; PDF/X không tự quy định mọi ngưỡng PPI/bleed của công việc.
- **[S34]** [Fogra: Paper testing](https://fogra.org/en/testing/materials-environment/paper-testing) — điều kiện đo, backing, M0/M1/M2; metadata đo cần lưu cùng kết quả.

Các mã [Sxx] trong bộ plan tra tại danh mục này. Phần được đề xuất cho PikDraw, đặc biệt
range control, kernel, cache, hiệu năng và ngưỡng nghiệm thu, là quyết định thiết kế riêng.
