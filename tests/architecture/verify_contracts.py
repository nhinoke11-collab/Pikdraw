"""PikDraw Contract & Oracle Verification Script

Validates shared contracts C1-C7, reference fixtures, and quality oracles.
"""

from __future__ import annotations

import json
import os
import sys
from pathlib import Path


def find_repo_root() -> Path:
    current = Path(__file__).resolve().parent
    while current.parent != current:
        if (current / "registry" / "modules.json").exists():
            return current
        current = current.parent
    raise RuntimeError("Repository root could not be located.")


def verify_contracts(root: Path) -> list[str]:
    issues: list[str] = []
    contracts_dir = root / "contracts"
    if not contracts_dir.exists():
        return ["contracts directory missing"]

    required_contracts = [
        "c1-geometry.md",
        "c2-document.md",
        "c3-rendering.md",
        "c4-color-typography.md",
        "c5-storage.md",
        "c6-workers.md",
        "c7-actions-ui.md",
    ]

    for c in required_contracts:
        path = contracts_dir / c
        if not path.is_file():
            issues.append(f"Missing required contract: {c}")
        else:
            content = path.read_text(encoding="utf-8")
            if len(content.strip()) < 50:
                issues.append(f"Contract {c} content is suspiciously short (< 50 chars)")
            if not content.startswith("# Contract C"):
                issues.append(f"Contract {c} missing standard '# Contract C' header")

    return issues


def verify_fixtures_and_oracles(root: Path) -> list[str]:
    issues: list[str] = []
    fixtures_dir = root / "docs" / "quality" / "reference-fixtures"
    oracles_dir = root / "docs" / "quality" / "oracles"

    required_fixtures = [
        "geometry-transform-vectors.json",
        "document-snapshot-vectors.json",
        "worker-ipc-envelope.json",
    ]

    for f in required_fixtures:
        path = fixtures_dir / f
        if not path.is_file():
            issues.append(f"Missing reference fixture: {f}")
        else:
            try:
                data = json.loads(path.read_text(encoding="utf-8"))
                if not isinstance(data, dict):
                    issues.append(f"Fixture {f} must be a JSON object")
            except Exception as e:
                issues.append(f"Fixture {f} invalid JSON: {e}")

    required_oracles = [
        "geometry-oracle.json",
        "color-conversion-oracle.json",
    ]

    for o in required_oracles:
        path = oracles_dir / o
        if not path.is_file():
            issues.append(f"Missing quality oracle: {o}")
        else:
            try:
                data = json.loads(path.read_text(encoding="utf-8"))
                if not isinstance(data, dict):
                    issues.append(f"Oracle {o} must be a JSON object")
            except Exception as e:
                issues.append(f"Oracle {o} invalid JSON: {e}")

    return issues


def main() -> int:
    root = find_repo_root()
    print(f"Verifying contracts and oracles in: {root}")
    contract_issues = verify_contracts(root)
    fixture_issues = verify_fixtures_and_oracles(root)
    all_issues = contract_issues + fixture_issues

    if all_issues:
        print(f"FAILED: Found {len(all_issues)} issue(s):", file=sys.stderr)
        for issue in all_issues:
            print(f"  - {issue}", file=sys.stderr)
        return 1

    print("SUCCESS: 7/7 contracts and all reference fixtures/oracles verified.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
