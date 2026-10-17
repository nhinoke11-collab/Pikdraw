# PikDraw Core Typography

Trạng thái: backend Windows DirectWrite thật cho typography LTR, một font face cho mỗi run. Chưa phải toàn bộ typography chuyên nghiệp M1. Không sử dụng glyph ID bằng Unicode, width heuristic hoặc chữ nhật thay glyph.

| Tính năng | API | Implementation | Test |
| --- | --- | --- | --- |
| Font hệ điều hành, exact face/fallback warning | `shaping/font_resolver.hpp` | `shaping/font_resolver.cpp`, `shaping/system_font.*` | `shaping/test_shaping.cpp` |
| UTF-8 nghiêm ngặt, glyph IDs/clusters/advance/offset thật | `shaping/shaper.hpp` | `shaping/shaper.cpp` | NFC/NFD tiếng Việt, W/I, invalid UTF-8 |
| Multiline/CRLF/wrap/alignment/source byte offset | `layout/text_layout.hpp` | `layout/text_layout.cpp` | `layout/test_layout.cpp` |
| Caret tiếng Việt và hit-test theo glyph cluster | `layout/caret_navigation.hpp` | `layout/caret_navigation.cpp` | Roundtrip combining, CRLF, end-of-line |
| Contour glyph Bézier thật, lỗ chữ O | `layout/glyph_outline.hpp` | `layout/glyph_outline.cpp` | Hai contour kín có curve, space không có outline |

Dependencies: geometry; Windows system DirectWrite (`dwrite`) và các kiểu sink Direct2D trong Windows SDK. Không có dependency Qt/UI hoặc thư viện bên thứ ba mới. CMake công bố `PIKDRAW_TYPOGRAPHY_REAL_OUTLINES=1` chỉ khi build Windows thật. Platform khác báo unsupported bằng exception thay vì output mô phỏng.

`ShapedRun.font` là face thực được dùng; `requested_font`, `is_fallback`, `warning_message` giữ yêu cầu và cảnh báo để UI có thể hiển thị. Không ghi font thay thế vào Unicode/font nguồn của document. Font đăng ký bằng tên chỉ được xác nhận khi hệ điều hành thực sự có font đó; chưa hỗ trợ nạp file font riêng.

Layout dùng mm, baseline đầu tại `(0,0)` và trục +Y xuống; baseline dòng sau tăng +Y. Renderer có model +Y lên phải đổi trục tại boundary. `LayoutLine.source_byte_offset` và caret là offset byte UTF-8 toàn nguồn; CRLF tính như một xuống dòng, không thay đổi byte nguồn. Empty/trailing paragraph có line thực. Wrap giữ toàn bộ byte, có thể tràn khi một cluster rộng hơn khung.

Lỗi input/size/options trả `std::invalid_argument`; backend/lỗi font hoặc run mixed-font, RTL, sideways trả `std::runtime_error`. Justify, tab stops, bidirectional/mixed-face spans, full UAX #29 mọi script, font variations/embedding và persistence font assets chưa hỗ trợ. Caret boundary hiện gồm tiếng Việt/Latin combining, CRLF, variation selectors, emoji modifiers/ZWJ và regional pairs; không tuyên bố đầy đủ Unicode grapheme conformance. Run LTR có fallback glyph sang face khác bị từ chối rõ vì model chưa lưu span face. Shaping không sửa NFC/NFD nguồn.

Build/test thực tế: configure CMake Ninja với compiler Windows đã thiết lập; target `test_pikdraw_typography`, sau đó `ctest --test-dir BUILD -R '^pikdraw_typography_tests$' --output-on-failure`. Assertions test hoạt động cả Release/NDEBUG. Fixture dùng font Arial có sẵn của Windows, không sao chép/redistribute font trong repo.
