# PikDraw document

Status: Implemented in Milestone M1 (ps07-document & ps08-storage).

Responsibility:
- Monotonic unique ObjectId generator (`model/object_id.hpp`).
- Document objects: PathObject, TextObject (Vietnamese text with FontIdentity), GroupObject, Layer, Page (20m billboard baseline).
- Copy-On-Write structural sharing object map (`snapshots/persistent_map.hpp`).
- Immutable document snapshots consumed by concurrent readers (`snapshots/document_snapshot.hpp`).
- Single logical writer with transactional command execution (`commands/command.hpp`, `commands/transaction_writer.hpp`).
- Undo/Redo history with monotonic revision bump and history budget (`history/undo_history.hpp`).
- Asynchronous worker proposal validation with strict base revision & epoch checks (`async/async_proposal.hpp`).
- Native `.pikdraw` persistence container with magic `PKDR`, CRC32 checksum, and versioning (`storage/native_format.hpp`).
- Atomic save via temporary file replace and crash recovery journaling (`storage/save_recovery.hpp`).

Dependencies: `core/geometry`, `core/color`, `core/typography`. Zero UI dependencies.
Tests: 4 test suites passing (`test_pikdraw_document_commands`, `test_pikdraw_document_snapshots`, `test_pikdraw_document_async`, `test_pikdraw_document_storage`).
