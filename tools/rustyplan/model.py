"""Stable data types shared by RustyPlan commands."""

from __future__ import annotations

from dataclasses import dataclass, asdict


SCHEMA_VERSION = 1


@dataclass(frozen=True)
class Declaration:
    """A conservative, source-spanned C++ migration unit."""

    file: str
    line: int
    end_line: int
    loc: int
    kind: str
    name: str
    bucket: str
    status: str
    blockers: list[str]
    signals: list[str]
    risk_score: int
    risk: str
    action: str
    phase: str
    priority: int
    rust_blocks: int

    @property
    def id(self) -> str:
        return f"{self.file}:{self.line}:{self.kind}:{self.name}"

    def to_json(self) -> dict[str, object]:
        return asdict(self) | {"id": self.id}
