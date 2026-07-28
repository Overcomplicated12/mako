#!/usr/bin/env bash

# Verify that one Raft leader can keep multiple AppendEntries RPCs in flight to
# each follower.  This reuses testPreferredReplicaLogReplication so the cluster
# setup and correctness checks stay identical to run_test_log_replication.sh;
# the additional assertion is the per-follower pipeline high-water mark.

set -u

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
cd "$REPO_ROOT"

NAMES=("localhost" "p1" "p2" "p3" "p4")
FOLLOWER_IDS=(1 2 3 4)
LOG_DIR="logs/test_append_entries_pipeline"
TEST_EXECUTABLE="build/testPreferredReplicaLogReplication"
PIPELINE_WIDTH="${MAKO_RAFT_TEST_PIPELINE_WIDTH:-4}"
MIN_EXPECTED_INFLIGHT="${MAKO_RAFT_TEST_MIN_INFLIGHT:-$PIPELINE_WIDTH}"
TEST_TIMEOUT_SEC="${MAKO_RAFT_TEST_TIMEOUT_SEC:-30}"
DEFAULT_MIN_DATA_INFLIGHT=3
if (( PIPELINE_WIDTH < DEFAULT_MIN_DATA_INFLIGHT )); then
  DEFAULT_MIN_DATA_INFLIGHT="$PIPELINE_WIDTH"
fi
MIN_EXPECTED_DATA_INFLIGHT="${MAKO_RAFT_TEST_MIN_DATA_INFLIGHT:-$DEFAULT_MIN_DATA_INFLIGHT}"

if (( PIPELINE_WIDTH < 2 )); then
  echo "ERROR: MAKO_RAFT_TEST_PIPELINE_WIDTH must be at least 2" >&2
  exit 2
fi
if (( MIN_EXPECTED_INFLIGHT < 2 || MIN_EXPECTED_INFLIGHT > PIPELINE_WIDTH )); then
  echo "ERROR: MAKO_RAFT_TEST_MIN_INFLIGHT must be in [2, PIPELINE_WIDTH]" >&2
  exit 2
fi
if (( MIN_EXPECTED_DATA_INFLIGHT < 2 || MIN_EXPECTED_DATA_INFLIGHT > PIPELINE_WIDTH )); then
  echo "ERROR: MAKO_RAFT_TEST_MIN_DATA_INFLIGHT must be in [2, PIPELINE_WIDTH]" >&2
  exit 2
fi
if [[ ! -x "$TEST_EXECUTABLE" ]]; then
  echo "ERROR: $TEST_EXECUTABLE is missing or not executable" >&2
  echo "Build it with: ninja -C build -j4 testPreferredReplicaLogReplication" >&2
  exit 2
fi

# The reused workload submits one entry every 10ms.  A 100ms heartbeat creates
# enough backlog to fill the window, while one-entry payloads make each occupied
# slot represent a distinct AppendEntries range.
export MAKO_RAFT_APPEND_MAX_INFLIGHT="$PIPELINE_WIDTH"
export MAKO_RAFT_APPEND_BATCH_MAX_ENTRIES=1
export MAKO_RAFT_HEARTBEAT_INTERVAL_US=100000
export MAKO_RAFT_APPEND_PIPELINE_TRACE=1

mkdir -p "$LOG_DIR"
find "$LOG_DIR" -maxdepth 1 -type f -name '*.log' -delete

PIDS=()
cleanup() {
  local pid
  for pid in "${PIDS[@]}"; do
    if kill -0 "$pid" 2>/dev/null; then
      kill "$pid" 2>/dev/null || true
    fi
  done
  for pid in "${PIDS[@]}"; do
    wait "$pid" 2>/dev/null || true
  done
}
trap cleanup EXIT INT TERM

echo "AppendEntries per-follower pipeline test"
echo "  pipeline width:       $PIPELINE_WIDTH"
echo "  required high-water:  $MIN_EXPECTED_INFLIGHT"
echo "  required data depth:  $MIN_EXPECTED_DATA_INFLIGHT"
echo "  logs:                 $LOG_DIR"
echo

for name in "${NAMES[@]}"; do
  "$TEST_EXECUTABLE" "$name" >"$LOG_DIR/$name.log" 2>&1 &
  PIDS+=("$!")
  echo "Started $name (pid=$!)"
done

deadline=$((SECONDS + TEST_TIMEOUT_SEC))
while true; do
  all_done=1
  for pid in "${PIDS[@]}"; do
    if kill -0 "$pid" 2>/dev/null; then
      all_done=0
      break
    fi
  done
  if (( all_done )); then
    break
  fi
  if (( SECONDS >= deadline )); then
    echo "FAIL: replicas did not finish within ${TEST_TIMEOUT_SEC}s" >&2
    exit 1
  fi
  sleep 1
done

process_failure=0
for pid in "${PIDS[@]}"; do
  if ! wait "$pid"; then
    process_failure=1
  fi
done
PIDS=()

if (( process_failure )); then
  echo "FAIL: at least one replica exited unsuccessfully" >&2
  exit 1
fi

correctness_failure=0
for name in "${NAMES[@]}"; do
  log_file="$LOG_DIR/$name.log"
  if grep -q 'PASS: All 25 logs replicated successfully!' "$log_file"; then
    echo "PASS: $name applied 25/25 logs"
  else
    echo "FAIL: $name did not report 25/25 replicated logs" >&2
    correctness_failure=1
  fi
done
if (( correctness_failure )); then
  exit 1
fi

leader_log=""
for name in "${NAMES[@]}"; do
  candidate="$LOG_DIR/$name.log"
  if grep -Eq 'Logs submitted:[[:space:]]+25' "$candidate"; then
    leader_log="$candidate"
    break
  fi
done
if [[ -z "$leader_log" ]]; then
  echo "FAIL: could not identify the log-submitting leader" >&2
  exit 1
fi

echo
echo "Pipeline evidence from $leader_log:"
pipeline_failure=0
for follower_id in "${FOLLOWER_IDS[@]}"; do
  read -r max_inflight max_data_inflight data_range_count < <(
    awk -v follower="$follower_id" '
      /\[APPEND_PIPELINE_HIGH_WATER\]/ && $0 ~ ("follower=" follower " ") {
        observed = 0
        for (i = 1; i <= NF; ++i) {
          if ($i ~ /^in_flight=[0-9]+\/[0-9]+$/) {
            split($i, parts, "[=/]")
            observed = parts[2] + 0
            if (observed > max) max = observed
          }
          if ($i ~ /^newest_range=\([0-9]+,[0-9]+\]$/) {
            range = $i
            sub(/^newest_range=\(/, "", range)
            sub(/\]$/, "", range)
            split(range, bounds, ",")
            if (bounds[2] + 0 > bounds[1] + 0) {
              data_ranges++
              if (observed > max_data) max_data = observed
            }
          }
        }
      }
      END { print max + 0, max_data + 0, data_ranges + 0 }
    ' "$leader_log"
  )

  data_evidence="$(
    awk -v follower="$follower_id" '
      /\[APPEND_PIPELINE_HIGH_WATER\]/ && $0 ~ ("follower=" follower " ") {
        for (i = 1; i <= NF; ++i) {
          if ($i ~ /^newest_range=\([0-9]+,[0-9]+\]$/) {
            range = $i
            sub(/^newest_range=\(/, "", range)
            sub(/\]$/, "", range)
            split(range, bounds, ",")
            if (bounds[2] + 0 > bounds[1] + 0 && printed < 2) {
              print
              printed++
            }
          }
        }
      }
    ' "$leader_log"
  )"

  if (( max_inflight < MIN_EXPECTED_INFLIGHT )); then
    echo "FAIL: follower $follower_id reached only $max_inflight/$PIPELINE_WIDTH in flight" >&2
    pipeline_failure=1
  elif (( max_data_inflight < MIN_EXPECTED_DATA_INFLIGHT || data_range_count < 2 )); then
    echo "FAIL: follower $follower_id did not overlap enough log-bearing ranges " \
         "(data depth=$max_data_inflight, ranges=$data_range_count)" >&2
    pipeline_failure=1
  else
    echo "PASS: follower $follower_id reached $max_inflight/$PIPELINE_WIDTH in flight" \
         "with log-bearing depth $max_data_inflight"
    while IFS= read -r evidence_line; do
      echo "      $evidence_line"
    done <<<"$data_evidence"
  fi
done

if (( pipeline_failure )); then
  exit 1
fi

first_data_send_line="$(
  grep -n '\[APPEND_PIPELINE_TRACE\] phase=send request_id=[0-9]* kind=data' "$leader_log" \
    | head -1 | cut -d: -f1 || true
)"
if [[ -z "$first_data_send_line" ]]; then
  echo "FAIL: no log-bearing AppendEntries send trace was recorded" >&2
  exit 1
fi

first_data_request_id="$(
  awk -v target="$first_data_send_line" '
    NR == target {
      for (i = 1; i <= NF; ++i) {
        if ($i ~ /^request_id=[0-9]+$/) {
          split($i, parts, "=")
          print parts[2]
          exit
        }
      }
    }
  ' "$leader_log"
)"
if [[ -z "$first_data_request_id" ]]; then
  echo "FAIL: could not extract the first data request ID" >&2
  exit 1
fi

first_data_reply_line="$(
  grep -n "\[APPEND_PIPELINE_TRACE\] phase=reply_received request_id=$first_data_request_id " \
    "$leader_log" | head -1 | cut -d: -f1 || true
)"
if [[ -z "$first_data_reply_line" ]]; then
  echo "FAIL: callback arrival for request $first_data_request_id was not recorded" >&2
  exit 1
fi

echo
echo "Callback-overlap proof for follower 1:"
awk -v first="$first_data_send_line" -v reply="$first_data_reply_line" \
    -v request_id="$first_data_request_id" '
  NR >= first && NR < reply &&
      /\[APPEND_PIPELINE_TRACE\] phase=send request_id=[0-9]+ kind=data follower=1/ {
    print "  " $0
    sends++
  }
  NR == reply && $0 ~ ("phase=reply_received request_id=" request_id " ") {
    print "  " $0
  }
  END {
    if (sends < 2) exit 1
  }
' "$leader_log" || {
  echo "FAIL: fewer than two follower-1 data RPCs were sent before the first callback arrived" >&2
  exit 1
}

echo
echo "PASS: multiple AppendEntries RPCs were concurrently in flight to every follower"
echo "PASS: multiple follower-1 data RPCs were sent before the first callback arrived"
echo "PASS: all five replicas applied all 25 log entries"
echo "Detailed logs: $LOG_DIR"

trap - EXIT INT TERM
exit 0
