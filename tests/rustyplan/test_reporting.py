from __future__ import annotations
import sys
import unittest
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from rustyplan.annotations import Safety
from rustyplan.call_graph import CallEdge
from rustyplan.reporting import dot
class ReportingTest(unittest.TestCase):
    def test_dot_is_deterministic_and_colored(self) -> None:
        first = dot({"safe": Safety.SAFE, "bridge": Safety.BRIDGE}, [CallEdge("safe", "bridge", "direct")])
        self.assertEqual(first, dot({"bridge": Safety.BRIDGE, "safe": Safety.SAFE}, [CallEdge("safe", "bridge", "direct")]))
        self.assertIn("fillcolor=green", first); self.assertIn("fillcolor=yellow", first)
if __name__ == "__main__": unittest.main()
