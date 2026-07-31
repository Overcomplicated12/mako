#!/usr/bin/env python3
"""Guide-aware inventory for inline-Rust DSL migrations.

This combines the Raft scanner's inline-Rust/generated-region coverage with
the RRR inventory's declaration spans, LOC reporting, and Phase 0 buckets. It
is not a C++ parser: it finds likely migration units, records the guide's
common blockers, and ranks review candidates with an explainable heuristic.

Example:
  python3 tools/rust-dsl-inventory.py --source-dir src/deptran/raft \
      --summary /tmp/raft-dsl-inventory.md --csv /tmp/raft-dsl-inventory.csv
"""

from __future__ import annotations

import argparse
import csv
import re
from collections import Counter
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE_SUFFIXES = {".h", ".hh", ".hpp", ".cc", ".cpp", ".cxx"}
TEST_DIRECTORY_PREFIXES = ("tests_", "test_")

DECL_RE = re.compile(
    r"^(?P<prefix>pub\s+)?(?P<kind>class|struct|enum(?:\s+class)?|union|using|typedef)\s+"
    r"(?P<name>[A-Za-z_][A-Za-z0-9_]*)"
)
FN_RE = re.compile(r"^pub\s+fn\s+(?P<name>[A-Za-z_][A-Za-z0-9_]*)", re.MULTILINE)
RUST_DECL_RE = re.compile(
    r"\b(?:pub\s+)?(?:struct|enum|trait)\s+(?P<name>[A-Za-z_][A-Za-z0-9_]*)"
)
TYPEDEF_RE = re.compile(r"^typedef\b.*\b(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*;")

BLOCKER_PATTERNS = (
    ("void*", re.compile(r"\bvoid\s*\*")),
    ("va_list", re.compile(r"\bva_list\b")),
    ("C-array", re.compile(r"\b(?:std::)?(?:array|CArray)\s*<")),
    ("template", re.compile(r"\btemplate\s*<")),
    ("operator-overload", re.compile(r"\boperator\s*(?:<<|>>|\[\]|\(\))")),
    ("raw-pointer", re.compile(r"\b[A-Za-z_][A-Za-z0-9_:<>]*\s*\*\s*[A-Za-z_]")),
    ("threading", re.compile(r"\b(?:std::)?(?:mutex|recursive_mutex|condition_variable|thread|atomic)\b")),
    ("I/O-or-FFI", re.compile(r"\b(?:open|read|write|close|fstat|memcpy|mmap|syscall|extern\s+\"C\")\b")),
    ("Rc-by-value", re.compile(r"\brusty::Rc\s*<[^>]+>\s+[A-Za-z_][A-Za-z0-9_]*\s*[,)={]")),
)

RISK_WEIGHTS = {
    "void*": 35,
    "va_list": 40,
    "C-array": 20,
    "template": 20,
    "operator-overload": 15,
    "raw-pointer": 12,
    "threading": 18,
    "I/O-or-FFI": 20,
    "Rc-by-value": 35,
    "virtual-dispatch": 20,
    "inheritance": 15,
    "constructor-overloads": 10,
    "mutable-state": 10,
    "static-state": 10,
    "nested-type": 8,
    "large-body": 10,
}


@dataclass
class Declaration:
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


def rust_regions(text: str) -> list[str]:
    """Return inline-Rust regions without attempting to parse their contents."""
    regions: list[str] = []
    start = 0
    while True:
        begin = text.find("#if RUSTYCPP_RUST", start)
        if begin < 0:
            return regions
        end = text.find("#endif", begin)
        if end < 0:
            regions.append(text[begin:])
            return regions
        regions.append(text[begin:end])
        start = end + len("#endif")


def without_generated_regions(text: str) -> str:
    """Blank generated regions while keeping original line numbers stable."""
    masked: list[str] = []
    in_generated = False
    for line in text.splitlines(keepends=True):
        if line.startswith("/*RUSTYCPP:GEN-BEGIN"):
            in_generated = True
        if in_generated:
            masked.append("".join("\n" if char == "\n" else " " for char in line))
        else:
            masked.append(line)
        if line.startswith("/*RUSTYCPP:GEN-END"):
            in_generated = False
    return "".join(masked)


def declaration_end(lines: list[str], start: int) -> int:
    """Return the 0-based end line of a declaration using brace depth.

    This is the same deliberately small technique used by the RRR inventory.
    It handles comments, but does not claim to parse C++ strings, macros, or
    every initializer form. A single-line alias, DSL function, or forward
    declaration ends on its opening line.
    """
    depth = 0
    saw_open = False
    in_block_comment = False
    for index in range(start, len(lines)):
        line = lines[index]
        column = 0
        while column < len(line):
            pair = line[column : column + 2]
            if in_block_comment:
                if pair == "*/":
                    in_block_comment = False
                    column += 2
                    continue
                column += 1
                continue
            if pair == "/*":
                in_block_comment = True
                column += 2
                continue
            if pair == "//":
                break
            if line[column] == "{":
                depth += 1
                saw_open = True
            elif line[column] == "}":
                depth -= 1
                if saw_open and depth == 0:
                    return index
            column += 1
        if index == start and not saw_open and ";" in line:
            return index
    return len(lines) - 1


def blocker_names(text: str) -> list[str]:
    return [name for name, pattern in BLOCKER_PATTERNS if pattern.search(text)]


def has_template_prefix(lines: list[str], start: int) -> bool:
    """Recognize the conventional standalone `template <...>` prefix.

    Like the RRR inventory, this intentionally handles the common form rather
    than trying to parse arbitrary C++ template declarations.
    """
    previous = start - 1
    while previous >= 0 and not lines[previous].strip():
        previous -= 1
    return previous >= 0 and bool(re.match(r"^\s*template\s*<", lines[previous]))


def risk_signals(
    kind: str, name: str, body: str, blockers: list[str], loc: int
) -> list[str]:
    signals = list(blockers)
    if re.search(r"\bvirtual\b", body):
        signals.append("virtual-dispatch")
    if kind in {"class", "struct"} and re.search(
        rf"\b(?:class|struct)\s+{re.escape(name)}\s*:", body
    ):
        signals.append("inheritance")
    if kind in {"class", "struct"}:
        constructors = re.findall(rf"\b{re.escape(name)}\s*\(", body)
        if len(constructors) > 1:
            signals.append("constructor-overloads")
    if re.search(r"\bmutable\b", body):
        signals.append("mutable-state")
    if re.search(r"\bstatic\b", body):
        signals.append("static-state")
    # Do not treat the declaration's own introducer as a nested type.
    nested_body = "\n".join(body.splitlines()[1:])
    if re.search(
        r"\b(?:class|struct|enum)\s+[A-Za-z_][A-Za-z0-9_]*\s*[{:]", nested_body
    ):
        signals.append("nested-type")
    if loc > 120:
        signals.append("large-body")
    return sorted(set(signals))


def classify(
    kind: str, rust_covered: bool, signals: list[str]
) -> tuple[str, str, str, str]:
    """Return bucket, status, next action, and guide-oriented phase."""
    if rust_covered:
        return "already-DSL", "migrated", "verify-generated-region", "complete"
    if any(signal in signals for signal in ("void*", "va_list", "I/O-or-FFI")):
        return "boundary-review", "unmigrated", "isolate-or-probe-unsafe-boundary", "floor-review"
    if any(signal in signals for signal in ("template", "operator-overload", "C-array")):
        return "needs-transpiler", "unmigrated", "probe-current-transpiler", "probe"
    if kind in {"enum", "enum class", "using", "typedef"} and not signals:
        return "trivial", "unmigrated", "migrate-now", "quick-wins"
    if kind in {"class", "struct", "union"}:
        if signals:
            return "refactor-then-DSL", "unmigrated", "reshape-first", "reshape"
        return "trivial", "unmigrated", "migrate-now", "quick-wins"
    return "boundary-review", "unmigrated", "review-manually", "floor-review"


def risk_score(signals: list[str], status: str) -> int:
    if status == "migrated":
        return 0
    return min(100, sum(RISK_WEIGHTS.get(signal, 0) for signal in signals))


def risk_label(score: int) -> str:
    if score <= 15:
        return "low"
    if score <= 40:
        return "medium"
    return "high"


def priority(bucket: str, status: str, score: int) -> int:
    """Rank likely next units without pretending this is a dependency graph."""
    if status != "unmigrated":
        return 0
    bucket_bonus = {
        "trivial": 100,
        "refactor-then-DSL": 60,
        "needs-transpiler": 30,
        "boundary-review": 0,
    }.get(bucket, 0)
    return max(0, bucket_bonus - score)


def relative_path(path: Path, project_root: Path) -> str:
    try:
        return str(path.relative_to(project_root))
    except ValueError:
        return str(path)


def is_test_source(path: Path, source_dir: Path) -> bool:
    """Use the RRR inventory's convention of excluding test directories."""
    parts = path.relative_to(source_dir).parts[:-1]
    return any(part == "tests" or part.startswith(TEST_DIRECTORY_PREFIXES) for part in parts)


def scan(source_dir: Path, project_root: Path = ROOT) -> list[Declaration]:
    """Inventory source declarations under ``source_dir`` recursively."""
    rows: list[Declaration] = []
    for path in sorted(source_dir.rglob("*")):
        if (
            not path.is_file()
            or path.suffix not in SOURCE_SUFFIXES
            or is_test_source(path, source_dir)
        ):
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        regions = rust_regions(text)
        rust_names = {
            match.group("name")
            for region in regions
            for match in RUST_DECL_RE.finditer(region)
        }
        rust_names.update(
            match.group("name") for region in regions for match in FN_RE.finditer(region)
        )
        lines = without_generated_regions(text).splitlines()
        seen: set[tuple[str, str]] = set()
        index = 0
        while index < len(lines):
            number = index + 1
            line = lines[index]
            match = DECL_RE.match(line.strip())
            if match:
                kind, name = match.group("kind"), match.group("name")
                if kind == "typedef":
                    typedef_match = TYPEDEF_RE.match(line.strip())
                    if not typedef_match:
                        index += 1
                        continue
                    name = typedef_match.group("name")
            else:
                fn_match = FN_RE.match(line.strip())
                if not fn_match:
                    typedef_match = TYPEDEF_RE.match(line.strip())
                    if not typedef_match:
                        index += 1
                        continue
                    kind, name = "typedef", typedef_match.group("name")
                else:
                    kind, name = "fn", fn_match.group("name")
            # `using namespace x;` and `using a::b;` are imports, not aliases.
            if kind == "using" and not re.match(
                r"^using\s+[A-Za-z_][A-Za-z0-9_]*\s*=", line.strip()
            ):
                index += 1
                continue
            key = (kind, name)
            if key in seen:
                index += 1
                continue
            # A semicolon only means a forward declaration when there is no
            # opening brace. Keep compact definitions such as `enum X { Y };`.
            if (
                kind not in {"using", "typedef", "fn"}
                and line.strip().endswith(";")
                and "{" not in line
            ):
                index += 1
                continue
            seen.add(key)
            end_index = declaration_end(lines, index)
            body = "\n".join(lines[index : end_index + 1])
            loc = end_index - index + 1
            blockers = blocker_names(body)
            if has_template_prefix(lines, number - 1) and "template" not in blockers:
                blockers.append("template")
            signals = risk_signals(kind, name, body, blockers, loc)
            bucket, status, action, phase = classify(kind, name in rust_names, signals)
            score = risk_score(signals, status)
            rows.append(
                Declaration(
                    file=relative_path(path, project_root),
                    line=number,
                    end_line=end_index + 1,
                    loc=loc,
                    kind=kind,
                    name=name,
                    bucket=bucket,
                    status=status,
                    blockers=blockers,
                    signals=signals,
                    risk_score=score,
                    risk=risk_label(score),
                    action=action,
                    phase=phase,
                    priority=priority(bucket, status, score),
                    rust_blocks=len(regions),
                )
            )
            # A nested declaration belongs to the enclosing class/struct's
            # migration unit, so do not duplicate it as an independent row.
            index = end_index + 1
    return rows


def write_csv(rows: list[Declaration], path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream)
        writer.writerow(
            [
                "file",
                "line",
                "end_line",
                "loc",
                "kind",
                "name",
                "bucket",
                "status",
                "blockers",
                "signals",
                "risk_score",
                "risk",
                "action",
                "phase",
                "priority",
                "rust_blocks",
            ]
        )
        for row in rows:
            writer.writerow(
                [
                    row.file,
                    row.line,
                    row.end_line,
                    row.loc,
                    row.kind,
                    row.name,
                    row.bucket,
                    row.status,
                    ", ".join(row.blockers) or "none",
                    ", ".join(row.signals) or "none",
                    row.risk_score,
                    row.risk,
                    row.action,
                    row.phase,
                    row.priority,
                    row.rust_blocks,
                ]
            )


def write_markdown(rows: list[Declaration], path: Path, csv_path: Path, source_dir: Path) -> None:
    buckets = Counter(row.bucket for row in rows)
    risks = Counter(row.risk for row in rows if row.status == "unmigrated")
    blockers = Counter(blocker for row in rows for blocker in row.blockers)
    candidates = sorted(
        (row for row in rows if row.status == "unmigrated" and row.priority > 0),
        key=lambda row: (-row.priority, row.risk_score, row.file, row.line),
    )
    files = len({row.file for row in rows})
    manual_rows = [row for row in rows if row.status == "unmigrated"]
    manual_loc = sum(row.loc for row in manual_rows)
    per_file_loc = Counter()
    for row in manual_rows:
        per_file_loc[row.file] += row.loc
    largest_manual = sorted(manual_rows, key=lambda row: (-row.loc, row.file, row.line))
    lines = [
        f"# Inline-Rust DSL Inventory: {source_dir.name}",
        "",
        "Generated by `tools/rust-dsl-inventory.py`. It uses Raft's inline-Rust and GEN coverage convention plus RRR's Phase 0 declaration-span and LOC reporting. Test directories are excluded. This is a conservative textual triage report, not a C++ parser or an automatic migration decision.",
        "",
        "## Regeneration",
        "",
        "```bash",
        f"python3 tools/rust-dsl-inventory.py --source-dir {source_dir} --summary {path} --csv {csv_path}",
        "```",
        "",
        "## Scope",
        "",
        f"- Files scanned: {files}",
        f"- Declarations scanned: {len(rows)}",
        f"- Unmigrated declaration LOC: {manual_loc}",
        f"- Already DSL-covered: {buckets.get('already-DSL', 0)}",
        f"- Unmigrated low-risk units: {risks.get('low', 0)}",
        f"- Unmigrated medium-risk units: {risks.get('medium', 0)}",
        f"- Unmigrated high-risk units: {risks.get('high', 0)}",
        "",
        "## Buckets",
        "",
        "| Bucket | Count | Default action |",
        "| --- | ---: | --- |",
        f"| `already-DSL` | {buckets.get('already-DSL', 0)} | Verify generated regions and tests |",
        f"| `trivial` | {buckets.get('trivial', 0)} | Migrate as a quick win |",
        f"| `refactor-then-DSL` | {buckets.get('refactor-then-DSL', 0)} | Make a separate reshape commit first |",
        f"| `needs-transpiler` | {buckets.get('needs-transpiler', 0)} | Probe the current transpiler before declaring a floor |",
        f"| `boundary-review` | {buckets.get('boundary-review', 0)} | Isolate or retain the C++ boundary |",
        "",
        "## Blocker Histogram",
        "",
        "| Signal | Count |",
        "| --- | ---: |",
    ]
    if blockers:
        lines.extend(f"| `{name}` | {count} |" for name, count in sorted(blockers.items()))
    else:
        lines.append("| none detected | 0 |")
    lines += [
        "",
        "## Recommended Review Order",
        "",
        "The score is a transparent heuristic, not a dependency graph. Review source locations, then migrate quick wins before reshape work; probe recurring syntax blockers in isolation before treating them as permanent.",
        "",
        "| Rank | Declaration | Bucket | Risk | Action | Why |",
        "| ---: | --- | --- | --- | --- | --- |",
    ]
    for rank, row in enumerate(candidates[:15], 1):
        why = ", ".join(row.signals) or "no guide-listed blocker"
        lines.append(
            f"| {rank} | `{row.name}` (`{row.file}:{row.line}`) | `{row.bucket}` | `{row.risk}` ({row.risk_score}) | `{row.action}` | {why} |"
        )
    if not candidates:
        lines.append("| - | No ranked unmigrated candidates | - | - | - | - |")
    lines += [
        "",
        "## Largest Unmigrated Units",
        "",
        "| Declaration | LOC | Bucket | Signals |",
        "| --- | ---: | --- | --- |",
    ]
    for row in largest_manual[:15]:
        signals = ", ".join(row.signals) or "none"
        lines.append(
            f"| `{row.name}` (`{row.file}:{row.line}`) | {row.loc} | `{row.bucket}` | {signals} |"
        )
    if not largest_manual:
        lines.append("| No unmigrated declarations | 0 | - | - |")
    lines += [
        "",
        "## Unmigrated LOC by File",
        "",
        "| File | Declaration LOC |",
        "| --- | ---: |",
    ]
    for file, loc in sorted(per_file_loc.items(), key=lambda item: (-item[1], item[0])):
        lines.append(f"| `{file}` | {loc} |")
    if not per_file_loc:
        lines.append("| No unmigrated files | 0 |")
    lines += [
        "",
        "## Per-Declaration Detail",
        "",
        "| File | Lines | LOC | Kind | Declaration | Bucket | Risk | Action | Signals |",
        "| --- | --- | ---: | --- | --- | --- | --- | --- | --- |",
    ]
    for row in rows:
        signals = ", ".join(row.signals) or "none"
        lines.append(
            f"| `{row.file}` | {row.line}-{row.end_line} | {row.loc} | `{row.kind}` | `{row.name}` | `{row.bucket}` | `{row.risk}` ({row.risk_score}) | `{row.action}` | {signals} |"
        )
    lines += [
        "",
        "## Interpretation",
        "",
        "`needs-transpiler` means a current transpiler probe is warranted; it does not mean the declaration is a permanent floor. `boundary-review` marks unsafe, I/O, or FFI-shaped code that should be isolated or intentionally retained in C++ unless a focused design and transpiler probe establish a safe DSL boundary.",
        "",
        "Use this report to choose review order, then follow the guide's reshape-then-migrate cadence: keep C++ reshapes and DSL conversion in separate commits, regenerate GEN regions, build, and run the focused test.",
        "",
        "The span finder is brace-counted and intentionally does not understand all C++ syntax. Confirm declaration boundaries, ownership/lifetime, and dependency order during review; this tool only makes that review queue visible.",
        "",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--summary", type=Path, required=True)
    parser.add_argument("--csv", type=Path, required=True)
    args = parser.parse_args()
    source_dir = args.source_dir.resolve()
    if not source_dir.is_dir():
        parser.error(f"--source-dir is not a directory: {args.source_dir}")
    rows = scan(source_dir)
    write_csv(rows, args.csv)
    write_markdown(rows, args.summary, args.csv, args.source_dir)
    print(f"wrote {args.summary}")
    print(f"wrote {args.csv}")
    print(f"scanned {len(rows)} declarations")


if __name__ == "__main__":
    main()
