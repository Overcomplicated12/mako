#!/usr/bin/env python3
"""Small NDJSON harness used to exercise RustyTwin itself.

Arguments select a fixture profile and role. Real Mako adapters will replace
these scripts later; keeping them dependency-free makes the v0 workflow easy
to run on any developer machine with Python 3.
"""

import json
import sys
import time


def normal_value(operation):
    args = operation.get("args", {})
    op = operation["op"]
    if op == "majority_count":
        return args.get("replicas", 0) // 2 + 1
    if op == "is_memory_ack":
        return args.get("ack_type") == "memory"
    if op == "snapshot_supports_compression":
        return args.get("compression") in {"none", "lz4", "zstd"}
    if op == "command_payload_present":
        return bool(args.get("present"))
    if op == "command_kind_is":
        return args.get("kind") == args.get("expected_kind")
    if op == "recovery_cleanup_predicate":
        return args.get("log_index", 0) > args.get("commit_index", 0)
    return None


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: fixture_harness.py <profile> <role>")

    profile, role = sys.argv[1:]
    if profile == "crash_mismatch" and role == "candidate":
        print("candidate fixture crashed", file=sys.stderr)
        raise SystemExit(17)
    if profile == "timeout_mismatch" and role == "candidate":
        time.sleep(5)
        return

    for line in sys.stdin:
        if not line.strip():
            continue
        operation = json.loads(line)
        event = {
            "kind": "event",
            "step": operation["step"],
            "event": "return",
            "value": normal_value(operation),
            "elapsed_ms": 1 if role == "baseline" else 99,
        }
        if profile == "return_mismatch" and role == "candidate" and operation["step"] == 2:
            event["value"] = False
        if profile == "state_mismatch" and role == "candidate" and operation["step"] == 2:
            event["event"] = "state"
            event["state"] = "candidate-only"
            del event["value"]
        print(json.dumps(event, separators=(",", ":")), flush=True)


if __name__ == "__main__":
    main()
