import hashlib
import importlib.util
import pathlib
import tempfile
import unittest

SOURCE = pathlib.Path(__file__).resolve().parents[3] / "tooling/spikes/spike_control.py"
SPEC = importlib.util.spec_from_file_location("spike_control", SOURCE)
control = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(control)

class ControlTests(unittest.TestCase):
    def envelope(self):
        return {"status": "SUCCESS", "structured_output": {"contract_review": "mm/double confirmed",
                "files": [{"path": "core/document/src/model.cpp", "content": "// prototype"}]}}

    def test_empty_and_denied_success_are_not_source_delivery(self):
        for envelope in [{"status": "SUCCESS", "response": ""},
                         {**self.envelope(), "denied_actions": [{"action": "command"}]},
                         {"status": "ERROR"}]:
            with self.subTest(envelope=envelope), self.assertRaises(ValueError):
                control.generation_payload(envelope, ["core/document"])

    def test_scoped_source_is_reviewable_but_not_automatically_applied(self):
        payload = control.generation_payload(self.envelope(), ["core/document"])
        self.assertEqual(payload["files"][0]["path"], "core/document/src/model.cpp")

    def test_path_escape_metadata_devices_and_prefix_collisions_are_rejected(self):
        paths = ["../core/document/x.cpp", "core/documentary/x.cpp", "C:/x.cpp",
                 "core/document/../x.cpp", "core/document/AGENTS.md", "core/document/.git/config.txt",
                 "core/document/NUL.cpp", "core/document/x.cpp.", "core\\document\\x.cpp",
                 "core/document/x.cpp:stream", "core/document/x.exe"]
        for path in paths:
            envelope = self.envelope()
            envelope["structured_output"]["files"][0]["path"] = path
            with self.subTest(path=path), self.assertRaises(ValueError):
                control.generation_payload(envelope, ["core/document"])

    def test_case_insensitive_duplicate_files_are_rejected(self):
        envelope = self.envelope()
        envelope["structured_output"]["files"].append({"path": "CORE/document/src/MODEL.cpp", "content": ""})
        with self.assertRaises(ValueError):
            control.generation_payload(envelope, ["core/document"])

    def test_disagreeing_provider_outputs_require_explicit_selection(self):
        envelope=self.envelope()
        for response in ['{"files":[]}', 'Updated source follows in a markdown block']:
            envelope["response"]=response
            with self.assertRaises(ValueError):
                control.generation_payload(envelope, ["core/document"])

    def receipt(self, root):
        artifact = root / "report.json"
        artifact.write_text('{"measured":true}', encoding="utf-8")
        digest = hashlib.sha256(artifact.read_bytes()).hexdigest()
        return {"schema_version": 1, "lanes": {lane: {
            "implementer": "gemini-" + lane, "reviewer": "independent-reviewer",
            "integrated_sha": "a" * 40,
            "gates": {gate: "pass" for gate in gates},
            "artifact": {"path": "report.json", "sha256": digest}}
            for lane, gates in control.REQUIRED_GATES.items()}}

    def test_partial_missing_review_and_tampered_artifact_block_advancement(self):
        with tempfile.TemporaryDirectory() as d:
            root = pathlib.Path(d)
            receipt = self.receipt(root)
            self.assertEqual(control.check_foundation_receipt(receipt, root), [])
            receipt["lanes"]["A"]["gates"]["rtx"] = "not_measured"
            self.assertIn("A/rtx: not accepted", control.check_foundation_receipt(receipt, root))
            receipt = self.receipt(root)
            receipt["lanes"]["B"]["reviewer"] = "GEMINI-B"
            self.assertIn("B: independent review missing", control.check_foundation_receipt(receipt, root))
            receipt = self.receipt(root)
            (root / "report.json").write_text("tampered", encoding="utf-8")
            self.assertTrue(any("checksum mismatch" in p for p in control.check_foundation_receipt(receipt, root)))

    def test_missing_receipt_and_artifact_path_escape_block_advancement(self):
        with tempfile.TemporaryDirectory() as d:
            root = pathlib.Path(d)
            self.assertTrue(control.check_foundation_receipt({}, root))
            receipt = self.receipt(root)
            receipt["lanes"]["C"]["artifact"]["path"] = "../outside.json"
            self.assertIn("C: unsafe artifact path", control.check_foundation_receipt(receipt, root))

if __name__ == "__main__":
    unittest.main()
