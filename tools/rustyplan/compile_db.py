"""Compilation-database lookup for optional Clang analysis."""

from __future__ import annotations

from dataclasses import dataclass
import json
from pathlib import Path
import shlex


@dataclass(frozen=True)
class CompileCommand:
    directory: Path
    arguments: tuple[str, ...]
    file: Path


def load_compile_db(path: Path) -> dict[Path, CompileCommand]:
    entries = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(entries, list):
        raise ValueError("compile_commands.json must contain an array")
    commands: dict[Path, CompileCommand] = {}
    for entry in entries:
        if not isinstance(entry, dict) or "file" not in entry or "directory" not in entry:
            continue
        directory = Path(entry["directory"])
        raw_arguments = entry.get("arguments")
        arguments = raw_arguments if isinstance(raw_arguments, list) else shlex.split(entry.get("command", ""))
        if not arguments:
            continue
        source = Path(entry["file"])
        if not source.is_absolute():
            source = directory / source
        commands[source.resolve()] = CompileCommand(directory, tuple(arguments), source.resolve())
    return commands
