"""Package-level compatibility checks for the initial RustyPlan extraction."""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from rustyplan import inventory


class InventoryCompatibilityTest(unittest.TestCase):
    def test_package_scan_and_json_are_deterministic(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            source.mkdir()
            (source / "sample.hpp").write_text("enum Mode { READY };\n", encoding="utf-8")
            rows = inventory.scan(source, root)
            self.assertEqual(rows[0].name, "Mode")
            first, second = root / "first.json", root / "second.json"
            inventory.write_json(rows, first)
            inventory.write_json(rows, second)
            self.assertEqual(first.read_bytes(), second.read_bytes())
            payload = json.loads(first.read_text(encoding="utf-8"))
            self.assertEqual(payload["schema_version"], 1)
            self.assertEqual(payload["declarations"][0]["name"], "Mode")


if __name__ == "__main__":
    unittest.main()
