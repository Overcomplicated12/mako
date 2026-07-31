#!/usr/bin/env python3
"""Smoke tests for the guide-aware inline-Rust DSL inventory tool."""

from __future__ import annotations

import csv
import importlib.util
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
SCRIPT = HERE / "rust-dsl-inventory.py"

spec = importlib.util.spec_from_file_location("rust_dsl_inventory", SCRIPT)
assert spec and spec.loader
inventory = importlib.util.module_from_spec(spec)
sys.modules["rust_dsl_inventory"] = inventory
spec.loader.exec_module(inventory)


class RustDslInventoryTest(unittest.TestCase):
    def test_scans_risk_signals_and_orders_quick_wins(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source"
            source.mkdir()
            (source / "sample.hpp").write_text(
                """
enum PlainMode { READY, STOPPED };

struct PodConfig {
  int value;
};

class Stateful : public Base {
 public:
  Stateful();
  Stateful(int value);
  virtual void run();
 private:
  mutable int counter_;
};

template <typename T>
struct GenericBox { T value; };

#if RUSTYCPP_RUST
pub struct Migrated { value_: i32 }
#endif
/*RUSTYCPP:GEN-BEGIN id=sample.migrated version=1 rust_sha256=ignored*/
struct Migrated { int value_; };
/*RUSTYCPP:GEN-END id=sample.migrated*/
""",
                encoding="utf-8",
            )
            rows = inventory.scan(source, source.parent)
            by_name = {row.name: row for row in rows}

            self.assertEqual(by_name["PlainMode"].bucket, "trivial")
            self.assertEqual(by_name["PlainMode"].action, "migrate-now")
            self.assertEqual(by_name["Stateful"].bucket, "refactor-then-DSL")
            self.assertIn("virtual-dispatch", by_name["Stateful"].signals)
            self.assertIn("constructor-overloads", by_name["Stateful"].signals)
            self.assertEqual(by_name["GenericBox"].bucket, "needs-transpiler")
            self.assertEqual(by_name["Migrated"].status, "migrated")
            self.assertGreater(by_name["PlainMode"].priority, by_name["Stateful"].priority)

    def test_cli_writes_csv_and_markdown(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            source.mkdir()
            (source / "sample.hpp").write_text("enum Mode { READY };\n", encoding="utf-8")
            summary = root / "inventory.md"
            csv_path = root / "inventory.csv"
            result = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    "--source-dir",
                    str(source),
                    "--summary",
                    str(summary),
                    "--csv",
                    str(csv_path),
                ],
                check=False,
                capture_output=True,
                text=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            report = summary.read_text(encoding="utf-8")
            self.assertIn("RRR's Phase 0 declaration-span and LOC reporting", report)
            self.assertIn("Recommended Review Order", report)
            self.assertIn("Unmigrated LOC by File", report)
            with csv_path.open(newline="", encoding="utf-8") as stream:
                rows = list(csv.DictReader(stream))
            self.assertEqual(rows[0]["name"], "Mode")
            self.assertEqual(rows[0]["action"], "migrate-now")
            self.assertEqual(rows[0]["loc"], "1")

    def test_skips_imports_nested_rows_and_test_directories(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source"
            source.mkdir()
            (source / "sample.hpp").write_text(
                """
using namespace std;
using imported::Name;
using Alias = int;
typedef unsigned long Counter;
struct Outer {
  struct Inner { int value; };
  int total;
};
""",
                encoding="utf-8",
            )
            tests = source / "tests"
            tests.mkdir()
            (tests / "ignored.hpp").write_text("struct Ignored {};\n", encoding="utf-8")

            rows = inventory.scan(source, source.parent)
            by_name = {row.name: row for row in rows}

            self.assertEqual(set(by_name), {"Alias", "Counter", "Outer"})
            self.assertEqual(by_name["Outer"].loc, 4)


if __name__ == "__main__":
    unittest.main()
