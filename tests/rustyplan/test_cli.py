from __future__ import annotations
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
CLI = ROOT / "tools/rustyplan.py"
class CliTest(unittest.TestCase):
    def test_verify_is_dry_run(self) -> None:
        result = subprocess.run([sys.executable, str(CLI), "verify", "src/deptran/raft/messages.hpp"], cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('"dry_run": true', result.stdout)
    def test_next_requires_annotation_before_migration(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source"; source.mkdir(); (source / "x.hpp").write_text("enum Mode { READY };\n", encoding="utf-8")
            result = subprocess.run([sys.executable, str(CLI), "next", "--source-dir", str(source)], cwd=ROOT, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("NEEDS_SAFETY_ANNOTATION", result.stdout)
if __name__ == "__main__": unittest.main()
