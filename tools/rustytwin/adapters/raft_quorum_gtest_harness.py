#!/usr/bin/env python3
"""Expose the focused Raft quorum gtest as a RustyTwin NDJSON harness."""

import json
import os
import subprocess
import sys


def main() -> int:
    test_binary = os.environ.get("RUSTYTWIN_RAFT_QUORUM_TEST")
    if not test_binary:
        print("set RUSTYTWIN_RAFT_QUORUM_TEST to test_raft_quorum", file=sys.stderr)
        return 2

    for line in sys.stdin:
        if not line.strip():
            continue

        operation = json.loads(line)
        if operation.get("op") != "run_raft_quorum_tests":
            print("unsupported Raft quorum operation", file=sys.stderr)
            return 2

        completed = subprocess.run(
            [test_binary, "--gtest_color=no"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )
        event = {
            "kind": "event",
            "step": operation["step"],
            "event": "return",
            "value": completed.returncode == 0,
        }
        print(json.dumps(event, separators=(",", ":")), flush=True)
        if completed.returncode:
            return completed.returncode

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
