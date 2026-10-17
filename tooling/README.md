# PikDraw harness

Status: implemented coordination tooling.

Responsibility: Codex task board, disjoint leases in one primary source, bounded context and source-snapshot handoffs. New worktrees and non-Codex tasks are rejected; legacy records remain readable.
Windows launcher: `./tooling/pikdraw.ps1 doctor` finds Python 3.10+ locally; `PIKDRAW_PYTHON` overrides discovery. No installation or network calls.
Dependency authority: `registry/modules.json` (no module dependencies).

Public interfaces must be versioned where consumed across processes/plugins.
Do not depend on desktop UI. See the scoped AGENTS.md before a change.

Claim before editing: this preserves task-owned source bytes/hashes in `.agent/snapshots/` and records `source_baseline`. Handoff compares against that baseline and writes a new `source_snapshot`; inherited dirty changes are not attributed to the task. It includes additions/deletions/untracked files without committing or changing the index. The board lock coordinates metadata; it cannot prevent direct filesystem edits.
Each manifest maps repository paths to SHA-256 and gives `files_directory` for the preserved bytes. Earlier local snapshots without that field store bytes directly beside the manifest. Ignored files are excluded from task snapshots; legacy migration must inventory and preserve needed ignored data separately.

Native builds use `native/build.ps1`, one exclusive `.agent/native-build.lock` and `build/win-dev/apps/desktop/pikdraw.exe`. An active lock must finish before another build/test; after a crash verify the owner stopped before manually removing the lock. CMake rejects alternate build trees and linked sources; direct CMake commands can bypass the runner lock, so sessions must use the runner.

`native/package.ps1` builds the Release desktop, MCP sidecar and COM local server through the `pikdraw-package` target under the same native build lock, invokes CPack after those dependencies finish, then verifies required package files and hashes all three executables against the canonical build outputs. It refuses to start while the canonical desktop executable is running.
