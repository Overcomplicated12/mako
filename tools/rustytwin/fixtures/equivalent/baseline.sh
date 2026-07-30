#!/usr/bin/env bash
exec python3 "$(dirname "$0")/../fixture_harness.py" equivalent baseline "$@"
