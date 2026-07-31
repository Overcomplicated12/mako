"""Explicit migration-safety annotations.

RustyPlan deliberately does not infer safety.  Only a nearby source comment is
authoritative; missing or contradictory comments remain review work.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import StrEnum
import re
from pathlib import Path


class Safety(StrEnum):
    SAFE = "safe"
    UNSAFE = "unsafe"
    BRIDGE = "bridge"
    UNANNOTATED = "unannotated"


ANNOTATION_RE = re.compile(r"^\s*//\s*@(?P<value>safe|unsafe|bridge)(?:\b|[-:])")


@dataclass(frozen=True)
class Annotation:
    safety: Safety
    line: int


@dataclass(frozen=True)
class AnnotationResult:
    annotation: Annotation | None
    warnings: tuple[str, ...] = ()
    errors: tuple[str, ...] = ()

    @property
    def safety(self) -> Safety:
        return self.annotation.safety if self.annotation else Safety.UNANNOTATED


def annotation_above(lines: list[str], declaration_line: int) -> AnnotationResult:
    """Read comments immediately preceding a 0-based declaration line.

    Blank lines and explanatory ``//`` comments are accepted as part of the
    annotation block.  Any non-comment source line stops the search, which
    prevents an annotation from accidentally leaking across declarations.
    """
    found: list[Annotation] = []
    index = declaration_line - 1
    while index >= 0:
        text = lines[index]
        stripped = text.strip()
        if not stripped:
            index -= 1
            continue
        if not stripped.startswith("//"):
            break
        match = ANNOTATION_RE.match(text)
        if match:
            found.append(Annotation(Safety(match.group("value")), index + 1))
        index -= 1
    if len(found) > 1:
        return AnnotationResult(
            None,
            errors=(
                "multiple safety annotations immediately precede declaration "
                f"at line {declaration_line + 1}",
            ),
        )
    if found:
        return AnnotationResult(found[0])
    return AnnotationResult(None)


def annotations_in_file(path: Path, declaration_lines: list[int]) -> dict[int, AnnotationResult]:
    """Return annotation outcomes keyed by 1-based declaration line."""
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    return {line: annotation_above(lines, line - 1) for line in declaration_lines}
