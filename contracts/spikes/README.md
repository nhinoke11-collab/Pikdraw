# Hợp đồng khảo sát foundation v1

Đặc tả cho ba spike, chưa là runtime API/storage schema đã chấp nhận. Agents A/B/C phải review semantics trước code, ghi nhận vào report; nếu mâu thuẫn thì báo leader và dừng phần phụ thuộc. Không tự sửa contracts.

[foundation-v1.json](foundation-v1.json) thống nhất mm/double, transforms, lifecycle, stale jobs, save và IPC limits. Vectors có expected values độc lập. Chỉ cùng JSON shape chưa đủ để pass.

Ngưỡng/performance/corpus đầy đủ ở [roadmap](../../docs/roadmap/foundation-spikes.md). Parser một subset hoặc một empty window không chứng minh toàn spike. Các gate chưa đo phải giữ not_measured.
