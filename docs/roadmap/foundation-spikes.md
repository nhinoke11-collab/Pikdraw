# Foundation spikes — lưu trữ

Tài liệu ngày 15/10/2026 mô tả ba spike đề xuất trước khi runtime editor hiện tại được xây dựng. Các mô tả Qt/Skia, M0, lane và chuỗi ps00–ps13 không phải trạng thái hay quy trình hiện hành; không dùng catalog cũ để dispatch task.

Runtime desktop hiện tại là C++20/Win32/GDI. Cấu trúc source và giới hạn sản phẩm xem tại [README](../../README.md), trạng thái task xem trên task board. Kiến trúc đích và các quyết định theo thời điểm nằm trong [architecture](../architecture/architecture.md) và [ADRs](../decisions/0001-foundation.md).

Các giả thuyết cũ về hiệu năng canvas và độ tin cậy import có tài liệu riêng: [kế hoạch canvas](canvas-performance-upgrade-20261016/README.md) và [kế hoạch import](import-resilience-20261016.md). Cả hai là kế hoạch, không chứng minh runtime đã đạt các mục tiêu.

[Kế hoạch post-spike](post-spike-implementation.md) và [catalog post-spike](post-spike-tasks.json) hiện chỉ giữ ghi chú lưu trữ.
