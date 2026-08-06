"""Compatibility API for the established DSL inventory scanner.

The scanner stays deliberately textual: RustyPlan's later Clang-backed call
graph is additive and must never change legacy inventory results.
"""

from __future__ import annotations

import importlib.util
import sys
from functools import lru_cache
from pathlib import Path
from types import ModuleType


@lru_cache(maxsize=1)
def _legacy() -> ModuleType:
    """Load the legacy entry point without relying on the caller's sys.path."""
    script = Path(__file__).resolve().parents[1] / "rust-dsl-inventory.py"
    spec = importlib.util.spec_from_file_location("_rustyplan_legacy_inventory", script)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load legacy inventory from {script}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def scan(source_dir: Path, project_root: Path | None = None):
    """Return legacy-compatible inventory rows for ``source_dir``."""
    legacy = _legacy()
    return legacy.scan(source_dir, legacy.ROOT if project_root is None else project_root)


def write_csv(rows, path: Path) -> None:
    _legacy().write_csv(rows, path)


def write_markdown(rows, path: Path, csv_path: Path, source_dir: Path) -> None:
    _legacy().write_markdown(rows, path, csv_path, source_dir)


def write_json(rows, path: Path) -> None:
    _legacy().write_json(rows, path)
