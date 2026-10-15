"""Leader-side validation; provider SUCCESS is not implementation acceptance."""
from __future__ import annotations
import argparse
import hashlib
import json
import pathlib
import re
import sys

REQUIRED_GATES = {
    "A": {"intel_onboard", "amd_onboard", "rtx", "software", "render_correctness", "recoverable_device_loss", "memory"},
    "B": {"precision_topology", "transactions_history", "stale_rejection", "snapshot_consistency", "save_recovery", "retention"},
    "C": {"format_subset_fidelity", "editability_output", "ipc_validation", "process_faults", "os_containment", "licenses"},
}
RESERVED = {"con", "prn", "aux", "nul", *(f"com{i}" for i in range(1, 10)), *(f"lpt{i}" for i in range(1, 10))}
ALLOWED_EXTENSIONS = {".h", ".hpp", ".cpp", ".cc", ".cxx", ".cmake", ".txt", ".md", ".json"}

def generation_payload(envelope: dict, owned_paths: list[str]) -> dict:
    if envelope.get("status") != "SUCCESS" or envelope.get("denied_actions"):
        raise ValueError("Provider failed or required an action denied by its permissions; no success assumed.")
    payload = envelope.get("structured_output")
    response = envelope.get("response", "")
    if isinstance(payload, dict) and isinstance(response, str) and response.strip():
        try:
            response_payload = json.loads(response)
        except ValueError as exc:
            raise ValueError("Ambiguous response/structured_output; leader must select and record a reviewed source candidate") from exc
        if response_payload != payload:
            raise ValueError("Response differs from structured_output; explicit source selection required")
    if not isinstance(payload, dict):
        if not isinstance(response, str) or not response.strip():
            raise ValueError("Empty SUCCESS is not source delivery.")
        payload = json.loads(response)
    files = payload.get("files")
    if not isinstance(files, list) or not 1 <= len(files) <= 30:
        raise ValueError("Expected 1..30 scoped source files.")
    roots = [p.casefold().rstrip("/") for p in owned_paths]
    seen = set()
    total = 0
    for item in files:
        path = item.get("path")
        content = item.get("content")
        if not isinstance(path, str) or not isinstance(content, str):
            raise ValueError("Each file needs a string path and content.")
        rel = pathlib.PurePosixPath(path)
        if not path or rel.is_absolute() or ":" in path or "\\" in path or any(p in {"", ".", ".."} for p in path.split("/")):
            raise ValueError(f"Unsafe relative path: {path}")
        if any(p.casefold().split(".")[0] in RESERVED or p.endswith((" ", ".")) for p in rel.parts):
            raise ValueError(f"Unsafe Windows component: {path}")
        key = path.casefold()
        if key in seen or not any(key.startswith(r + "/") for r in roots):
            raise ValueError(f"Duplicate or out-of-scope path: {path}")
        if rel.name.casefold() == "agents.md" or any(p.casefold() in {".git", ".agent", ".github"} for p in rel.parts):
            raise ValueError("Generated code cannot rewrite instructions or coordination/security metadata.")
        if rel.suffix.casefold() not in ALLOWED_EXTENSIONS:
            raise ValueError(f"Not an allowed source/document extension: {path}")
        size = len(content.encode("utf-8"))
        if size > 256 * 1024:
            raise ValueError("Individual generated source exceeds the review bound.")
        total += size
        seen.add(key)
    if total > 1024 * 1024:
        raise ValueError("Generated output exceeds the review bound.")
    if not isinstance(payload.get("contract_review"), str) or not payload["contract_review"].strip():
        raise ValueError("Missing consumer contract review.")
    return payload

def check_foundation_receipt(receipt: dict, artifact_root: pathlib.Path) -> list[str]:
    """Receipt is written by the leader after real review, never by feature agents."""
    problems = []
    if receipt.get("schema_version") != 1:
        problems.append("unknown receipt version")
    lanes = receipt.get("lanes", {})
    if not isinstance(lanes, dict):
        return problems + ["invalid lanes"]
    for lane, required in REQUIRED_GATES.items():
        data = lanes.get(lane)
        if not isinstance(data, dict):
            problems.append(f"{lane}: missing acceptance")
            continue
        implementer = data.get("implementer")
        reviewer = data.get("reviewer")
        if not implementer or not reviewer or str(implementer).casefold() == str(reviewer).casefold():
            problems.append(f"{lane}: independent review missing")
        if not re.fullmatch(r"[0-9a-f]{40}", str(data.get("integrated_sha", ""))):
            problems.append(f"{lane}: integrated SHA missing")
        gates = data.get("gates", {})
        if not isinstance(gates, dict):
            problems.append(f"{lane}: invalid gate results")
        else:
            for gate in sorted(required):
                if gates.get(gate) != "pass":
                    problems.append(f"{lane}/{gate}: not accepted")
        artifact = data.get("artifact", {})
        if not isinstance(artifact, dict):
            problems.append(f"{lane}: invalid artifact")
            continue
        raw_path = artifact.get("path", "")
        rel = pathlib.PurePosixPath(str(raw_path))
        if not raw_path or rel.is_absolute() or ".." in rel.parts or "\\" in str(raw_path) or ":" in str(raw_path):
            problems.append(f"{lane}: unsafe artifact path")
            continue
        target = (artifact_root / str(raw_path)).resolve()
        if not target.is_relative_to(artifact_root.resolve()) or not target.is_file():
            problems.append(f"{lane}: artifact missing or path escape")
            continue
        digest = hashlib.sha256(target.read_bytes()).hexdigest()
        if digest != artifact.get("sha256"):
            problems.append(f"{lane}: artifact checksum mismatch")
    return problems

def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="backslashreplace")
    parser = argparse.ArgumentParser()
    subs = parser.add_subparsers(dest="command", required=True)
    generated = subs.add_parser("inspect-generation")
    generated.add_argument("--result", type=pathlib.Path, required=True)
    generated.add_argument("--owned-path", action="append", required=True)
    gate = subs.add_parser("check-foundation")
    gate.add_argument("--receipt", type=pathlib.Path, required=True)
    gate.add_argument("--artifact-root", type=pathlib.Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == "inspect-generation":
            payload = generation_payload(json.loads(args.result.read_text(encoding="utf-8")), args.owned_path)
            print(json.dumps({"ok": True, "requires_source_review": True,
                              "contract_review": payload["contract_review"],
                              "files": [{"path": f["path"], "bytes": len(f["content"].encode("utf-8"))} for f in payload["files"]],
                              "limitations": payload.get("limitations", [])}, ensure_ascii=False))
            return 0
        receipt = json.loads(args.receipt.read_text(encoding="utf-8"))
        problems = check_foundation_receipt(receipt, args.artifact_root)
        print(json.dumps({"ok": not problems, "next": "ps00-accept-spikes" if not problems else "spike-remediation-or-missing-evidence",
                          "problems": problems}, ensure_ascii=False))
        return 0 if not problems else 2
    except (OSError, ValueError, TypeError) as exc:
        print(json.dumps({"ok": False, "error": str(exc)}, ensure_ascii=False))
        return 2

if __name__ == "__main__":
    raise SystemExit(main())
