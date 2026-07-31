from __future__ import annotations
import sys
import unittest
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from rustyplan.annotations import Safety
from rustyplan.call_graph import CallEdge
from rustyplan.islands import IslandNode, group_islands

class IslandTest(unittest.TestCase):
    def test_safe_chain_and_unsafe_frontier(self) -> None:
        islands = group_islands([IslandNode("a", Safety.SAFE, 5), IslandNode("b", Safety.SAFE, 4), IslandNode("c", Safety.UNSAFE)], [CallEdge("a", "b", "direct"), CallEdge("b", "c", "direct")])
        self.assertEqual(islands[0].nodes, ("a", "b"))
        self.assertEqual(islands[0].frontier[0].callee, "c")
    def test_scc_stays_together_and_split_is_stable(self) -> None:
        nodes = [IslandNode(name, Safety.SAFE) for name in "abcd"]
        edges = [CallEdge("a", "b", "direct"), CallEdge("b", "a", "direct"), CallEdge("b", "c", "direct"), CallEdge("c", "d", "direct")]
        islands = group_islands(nodes, edges, max_nodes=2)
        self.assertIn(("a", "b"), [island.nodes for island in islands])
        self.assertEqual([island.nodes for island in islands], [island.nodes for island in group_islands(nodes, edges, max_nodes=2)])

if __name__ == "__main__": unittest.main()
