# Localization

**Trạng thái:** Đặc tả; chưa có runtime localization, locale catalogs hoặc catalog validator.

- Giao diện English trước; Vietnamese là locale tiếp theo. Settings là vị trí thiết kế để chọn locale khi runtime được triển khai.
- Stable action IDs nối menu, shortcut, toolbar, Properties và trợ giúp với chuỗi dịch. UI strings không lưu vào dữ liệu tài liệu.
- Đổi ngôn ngữ chỉ đổi nhãn UI; không đổi Unicode/artwork, đơn vị, mã màu, stable IDs, schema hay dữ liệu sản xuất.
- Cách tổ chức catalog và schema còn chờ task triển khai. Chưa coi `locales/manifest.json`, `locales/<locale>/*.json` hoặc `LocaleService` là hiện có.

Đọc [đặc tả localization](../../docs/architecture/localization.md) và [Contract C7](../../contracts/c7-actions-ui.md). Không đăng ký module runtime `core/localization` cho đến khi kiến trúc loader/host và dependency được xác nhận.
