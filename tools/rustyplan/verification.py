"""Dry-run-first verification command construction and execution."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import subprocess

from .configuration import RustyPlanConfig


@dataclass(frozen=True)
class VerificationResult:
    command: tuple[str, ...]
    returncode: int | None
    output: str = ""


def verification_commands(project_root: Path, files: list[str], config: RustyPlanConfig) -> list[list[str]]:
    transpiler = config.transpiler or "third-party/rusty-cpp/target/release/rusty-cpp-transpiler"
    commands = [[str(project_root / transpiler), "inline-rust", "--check", "--files", *files]] if files else []
    for file in files:
        commands.extend([command.split() for command in config.tests.get(file, ())])
    return commands


def working_tree_dirty(project_root: Path) -> bool:
    return bool(subprocess.run(["git", "status", "--porcelain"], cwd=project_root, capture_output=True, text=True, check=True).stdout.strip())


def verify(project_root: Path, files: list[str], config: RustyPlanConfig, *, execute: bool, allow_dirty: bool) -> list[VerificationResult]:
    commands = verification_commands(project_root, files, config)
    if not execute:
        return [VerificationResult(tuple(command), None) for command in commands]
    if working_tree_dirty(project_root) and not allow_dirty:
        raise RuntimeError("refusing --execute with unrelated dirty files; pass --allow-dirty to override")
    results = []
    for command in commands:
        result = subprocess.run(command, cwd=project_root, capture_output=True, text=True)
        results.append(VerificationResult(tuple(command), result.returncode, result.stdout + result.stderr))
        if result.returncode:
            break
    return results
