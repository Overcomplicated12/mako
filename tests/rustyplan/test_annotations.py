from __future__ import annotations

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from rustyplan.annotations import Safety, annotation_above
from rustyplan.recommendations import Recommendation, recommend


class AnnotationTest(unittest.TestCase):
    def test_annotation_values_and_gaps(self) -> None:
        for value in ("safe", "unsafe", "bridge"):
            result = annotation_above([f"// @{value}", "void f();"], 1)
            self.assertEqual(result.safety, Safety(value))
        self.assertEqual(annotation_above(["void f();"], 0).safety, Safety.UNANNOTATED)
        self.assertEqual(annotation_above(["// @safe", "", "// explanation", "void f();"], 3).safety, Safety.SAFE)

    def test_unannotated_is_not_direct_migration(self) -> None:
        result = recommend(safety=Safety.UNANNOTATED, migrated=False, signals=[], kind="fn")
        self.assertEqual(result.recommendation, Recommendation.NEEDS_SAFETY_ANNOTATION)

    def test_safe_risky_shapes_are_explainable(self) -> None:
        result = recommend(safety=Safety.SAFE, migrated=False, signals=["constructor-overloads"], kind="class")
        self.assertEqual(result.recommendation, Recommendation.RESHAPE_THEN_MIGRATE)
        binary = recommend(safety=Safety.SAFE, migrated=False, signals=["C-array"], kind="struct")
        self.assertEqual(binary.recommendation, Recommendation.DEFER_BINARY_LAYOUT)


if __name__ == "__main__":
    unittest.main()
