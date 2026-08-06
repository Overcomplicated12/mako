#!/usr/bin/env python3
"""Plan conservative, explicit inline-Rust DSL migrations."""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from rustyplan.annotations import annotations_in_file
from rustyplan.configuration import load_config
from rustyplan.inventory import scan, write_csv, write_json, write_markdown
from rustyplan.recommendations import recommend
from rustyplan.reporting import dot
from rustyplan.verification import verify

ROOT = Path(__file__).resolve().parents[1]


def _rows(args):
    source = args.source_dir.resolve()
    rows = scan(source, ROOT)
    annotations = {}
    for file, lines in _group_lines(rows).items():
        annotations.update({(file, line): result for line, result in annotations_in_file(ROOT / file, lines).items()})
    return rows, annotations


def _group_lines(rows):
    grouped = {}
    for row in rows:
        grouped.setdefault(row.file, []).append(row.line)
    return grouped


def _candidate(row, annotation):
    outcome = recommend(safety=annotation.safety, migrated=row.status == "migrated", signals=row.signals, kind=row.kind)
    return {"id": f"{row.file}:{row.line}:{row.name}", "file": row.file, "line": row.line, "name": row.name, "safety": annotation.safety.value, "recommendation": outcome.recommendation.value, "reasons": list(outcome.reasons), "blockers": row.blockers, "priority": row.priority}


def command_scan(args):
    rows = scan(args.source_dir.resolve(), ROOT)
    if args.summary: write_markdown(rows, args.summary, args.csv, args.source_dir)
    if args.csv: write_csv(rows, args.csv)
    if args.json: write_json(rows, args.json)
    if args.dot:
        annotations = {}
        for file, lines in _group_lines(rows).items():
            annotations.update({(file, line): result for line, result in annotations_in_file(ROOT / file, lines).items()})
        nodes = {f"{row.file}:{row.line}:{row.name}": annotations[(row.file, row.line)].safety for row in rows}
        args.dot.parent.mkdir(parents=True, exist_ok=True)
        args.dot.write_text(dot(nodes, []), encoding="utf-8")
    print(json.dumps({"declarations": len(rows)}, sort_keys=True))


def command_next(args):
    rows, annotations = _rows(args)
    candidates = [_candidate(row, annotations[(row.file, row.line)]) for row in rows]
    candidates.sort(key=lambda item: (-item["priority"], item["file"], item["line"]))
    print(json.dumps(candidates[:args.limit], indent=2, sort_keys=True))


def command_explain(args):
    rows, annotations = _rows(args)
    found = [row for row in rows if row.name == args.target or f"{row.file}:{row.line}:{row.name}" == args.target]
    if not found: raise SystemExit(f"no declaration matches {args.target!r}")
    print(json.dumps(_candidate(found[0], annotations[(found[0].file, found[0].line)]), indent=2, sort_keys=True))


def command_prepare(args):
    print("1. Reshape commit: isolate boundaries and keep public behavior unchanged.")
    print("2. Migration commit: add #if RUSTYCPP_RUST, run --rewrite, then --check.")
    print("3. Build the focused target and run mapped tests.")


def command_verify(args):
    config = load_config(args.config)
    results = verify(ROOT, args.files, config, execute=args.execute, allow_dirty=args.allow_dirty)
    for result in results:
        print(json.dumps({"command": list(result.command), "returncode": result.returncode, "dry_run": result.returncode is None}, sort_keys=True))
        if result.output: print(result.output, end="")
    if any(result.returncode not in (None, 0) for result in results): raise SystemExit(1)


def command_review(args):
    print("review is revision-aware planning work; compare a revision with `git diff --name-only <base>...HEAD` before preparing targets")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__); sub = parser.add_subparsers(dest="command", required=True)
    scan_parser = sub.add_parser("scan"); scan_parser.add_argument("--source-dir", type=Path, required=True); scan_parser.add_argument("--summary", type=Path); scan_parser.add_argument("--csv", type=Path); scan_parser.add_argument("--json", type=Path); scan_parser.add_argument("--dot", type=Path); scan_parser.set_defaults(func=command_scan)
    for name, func in (("next", command_next), ("explain", command_explain)):
        item = sub.add_parser(name); item.add_argument("--source-dir", type=Path, required=True)
        if name == "next": item.add_argument("--limit", type=int, default=10)
        else: item.add_argument("target")
        item.set_defaults(func=func)
    prepare = sub.add_parser("prepare"); prepare.set_defaults(func=command_prepare)
    check = sub.add_parser("verify"); check.add_argument("files", nargs="*"); check.add_argument("--config", type=Path, default=ROOT / "tools/rustyplan.toml"); check.add_argument("--execute", action="store_true"); check.add_argument("--allow-dirty", action="store_true"); check.set_defaults(func=command_verify)
    review = sub.add_parser("review"); review.set_defaults(func=command_review)
    args = parser.parse_args(); args.func(args)


if __name__ == "__main__": main()
