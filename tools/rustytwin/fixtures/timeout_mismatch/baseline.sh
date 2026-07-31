#!/usr/bin/env bash
exec python3 "$(dirname "$0")/../fixture_harness.py" timeout_mismatch baseline "$@"
