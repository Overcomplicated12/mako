from __future__ import annotations

import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from rustyplan.call_graph import ClangCallGraphProvider, NoCallGraphProvider


class CallGraphTest(unittest.TestCase):
    def test_no_provider_is_explicit(self) -> None:
        result = NoCallGraphProvider().graph_for(Path("missing.cc"))
        self.assertFalse(result.available)

    @unittest.skipUnless(shutil.which("clang++") or Path("/usr/bin/clang++-22").exists(), "Clang unavailable")
    def test_clang_ast_direct_edge(self) -> None:
        compiler = shutil.which("clang++") or "/usr/bin/clang++-22"
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "sample.cc"
            source.write_text("void callee() {}\nvoid caller() { callee(); }\n", encoding="utf-8")
            database = root / "compile_commands.json"
            database.write_text(json.dumps([{"directory": str(root), "command": f"{compiler} -std=c++23 -c {source} -o sample.o", "file": str(source)}]), encoding="utf-8")
            graph = ClangCallGraphProvider(database, root / "cache").graph_for(source)
            self.assertTrue(graph.available, graph.detail)
            self.assertTrue(any(edge.caller == "caller" and edge.callee == "callee" for edge in graph.edges), graph.edges)


if __name__ == "__main__":
    unittest.main()
