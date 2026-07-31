"""Optional project configuration, using only Python's stdlib ``tomllib``."""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
import tomllib


@dataclass(frozen=True)
class RustyPlanConfig:
    tests: dict[str, tuple[str, ...]] = field(default_factory=dict)
    build_dir: str | None = None
    transpiler: str | None = None
    rustytwin_module: str | None = None


def load_config(path: Path | None) -> RustyPlanConfig:
    if path is None or not path.exists():
        return RustyPlanConfig()
    raw = tomllib.loads(path.read_text(encoding="utf-8"))
    tests_raw = raw.get("tests", {})
    if not isinstance(tests_raw, dict):
        raise ValueError("[tests] must be a TOML table mapping files to test commands")
    tests: dict[str, tuple[str, ...]] = {}
    for source, commands in tests_raw.items():
        if isinstance(commands, str):
            tests[str(source)] = (commands,)
        elif isinstance(commands, list) and all(isinstance(command, str) for command in commands):
            tests[str(source)] = tuple(commands)
        else:
            raise ValueError(f"tests.{source} must be a string or string array")
    verify = raw.get("verify", {})
    if not isinstance(verify, dict):
        raise ValueError("[verify] must be a TOML table")
    twin = raw.get("rustytwin", {})
    if not isinstance(twin, dict):
        raise ValueError("[rustytwin] must be a TOML table")
    return RustyPlanConfig(
        tests=tests,
        build_dir=verify.get("build_dir"),
        transpiler=verify.get("transpiler"),
        rustytwin_module=twin.get("module"),
    )
