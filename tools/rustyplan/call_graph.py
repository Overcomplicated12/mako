"""Direct C++ call edges extracted from Clang's JSON AST.

This module intentionally has no textual-call-graph fallback.  Consumers can
distinguish unavailable dependency information from an empty call graph.
"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import subprocess
from typing import Protocol

from .compile_db import CompileCommand, load_compile_db


@dataclass(frozen=True, order=True)
class CallEdge:
    caller: str
    callee: str
    kind: str


@dataclass(frozen=True)
class CallGraphResult:
    edges: tuple[CallEdge, ...]
    available: bool
    detail: str = ""


class CallGraphProvider(Protocol):
    def graph_for(self, source: Path) -> CallGraphResult: ...


class NoCallGraphProvider:
    def __init__(self, detail: str = "no --compile-db was provided") -> None:
        self.detail = detail

    def graph_for(self, source: Path) -> CallGraphResult:
        return CallGraphResult((), False, self.detail)


class ClangCallGraphProvider:
    def __init__(self, compile_db: Path, cache_dir: Path | None = None) -> None:
        self.commands = load_compile_db(compile_db)
        self.cache_dir = cache_dir or compile_db.parent / ".rustyplan-cache"

    def graph_for(self, source: Path) -> CallGraphResult:
        source = source.resolve()
        command = self.commands.get(source)
        if command is None:
            return CallGraphResult((), False, f"no compile command for {source}")
        try:
            ast = self._ast(command)
        except (OSError, ValueError, subprocess.SubprocessError) as error:
            return CallGraphResult((), False, f"Clang AST unavailable: {error}")
        return CallGraphResult(tuple(sorted(_edges_from_ast(ast))), True)

    def _ast(self, command: CompileCommand) -> dict[str, object]:
        args = _ast_command(command)
        tool_identity = subprocess.run([args[0], "--version"], capture_output=True, text=True, check=True).stdout
        digest = hashlib.sha256(
            (command.file.read_text(encoding="utf-8", errors="replace") + "\0" + "\0".join(args) + "\0" + tool_identity).encode()
        ).hexdigest()
        cache = self.cache_dir / f"{digest}.json"
        if cache.exists():
            return json.loads(cache.read_text(encoding="utf-8"))
        result = subprocess.run(args, cwd=command.directory, capture_output=True, text=True, check=True)
        ast = json.loads(result.stdout)
        self.cache_dir.mkdir(parents=True, exist_ok=True)
        cache.write_text(json.dumps(ast, sort_keys=True), encoding="utf-8")
        return ast


def _ast_command(command: CompileCommand) -> list[str]:
    """Remove compile/output switches and request a JSON AST from Clang."""
    result = [command.arguments[0]]
    skip_next = False
    source = str(command.file)
    for arg in command.arguments[1:]:
        if skip_next:
            skip_next = False
            continue
        if arg in {"-c", "-o", "-MF", "-MT", "-MQ"}:
            skip_next = arg != "-c"
            continue
        if arg == source or Path(arg).resolve() == command.file if not arg.startswith("-") else False:
            continue
        result.append(arg)
    return result + ["-Xclang", "-ast-dump=json", "-fsyntax-only", source]


def _edges_from_ast(ast: dict[str, object]) -> set[CallEdge]:
    edges: set[CallEdge] = set()

    def visit(node: object, caller: str | None = None) -> None:
        if not isinstance(node, dict):
            return
        kind = node.get("kind")
        current = caller
        if kind in {"FunctionDecl", "CXXMethodDecl", "CXXConstructorDecl"} and node.get("name"):
            current = str(node["name"])
        if current and kind in {"CallExpr", "CXXMemberCallExpr", "CXXConstructExpr"}:
            referenced = _referenced_name(node)
            edges.add(CallEdge(current, referenced or "<external-or-unresolved>", "constructor" if kind == "CXXConstructExpr" else "direct"))
        for child in node.get("inner", []):
            visit(child, current)

    visit(ast)
    return edges


def _referenced_name(node: dict[str, object]) -> str | None:
    def descend(value: object) -> str | None:
        if not isinstance(value, dict):
            return None
        reference = value.get("referencedDecl")
        if isinstance(reference, dict) and reference.get("name"):
            return str(reference["name"])
        for child in value.get("inner", []):
            found = descend(child)
            if found:
                return found
        return None
    return descend(node)
