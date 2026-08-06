# RustyPlan

RustyPlan is a conservative planning tool for incremental migrations from C++
to Mako's inline-Rust DSL. It does not change source code, regenerate GEN
blocks, stage files, or create commits. Its role is to make migration scope,
explicit safety decisions, dependency boundaries, and verification commands
visible before a migration starts.

The tool has two compatible entry points:

- `tools/rust-dsl-inventory.py` is the established inventory command. Use it
  when a stable Markdown/CSV inventory is all that is needed.
- `tools/rustyplan.py` adds planning subcommands, annotations, deterministic
  JSON/DOT output, optional Clang AST call-graph support, and dry-run
  verification.

All tooling uses Python's standard library only. Python 3.11+ is required for
the built-in TOML reader; the repository's Python 3.12 is supported.

## Quick start

Run a baseline scan of a module. The four outputs are deterministic for the
same source revision and tool version.

```bash
python3 tools/rustyplan.py scan \
  --source-dir src/masstree \
  --summary /tmp/masstree-inventory.md \
  --csv /tmp/masstree-inventory.csv \
  --json /tmp/masstree-inventory.json \
  --dot /tmp/masstree-inventory.dot
```

Read the Markdown report first. It contains declaration spans, size, blocker
signals, current inline-Rust/GEN coverage, and the original inventory
heuristic. CSV and JSON are useful for review tooling; DOT is plain Graphviz
text and does not require Graphviz to be installed.

The committed Masstree baseline is in
`docs/migration/rustycpp/inventories/`.

## Legacy inventory command

This interface remains supported and requires `--source-dir`, `--summary`,
and `--csv`:

```bash
python3 tools/rust-dsl-inventory.py \
  --source-dir src/deptran/raft \
  --summary /tmp/raft-dsl-inventory.md \
  --csv /tmp/raft-dsl-inventory.csv
```

Pass `--json` to additionally write a versioned machine-readable report:

```bash
python3 tools/rust-dsl-inventory.py \
  --source-dir src/deptran/raft \
  --summary /tmp/raft-dsl-inventory.md \
  --csv /tmp/raft-dsl-inventory.csv \
  --json /tmp/raft-dsl-inventory.json
```

The scanner is intentionally not a C++ parser. It finds likely declaration
units using brace-counted spans, excludes `tests`/`test_*` directories, and
recognizes `#if RUSTYCPP_RUST` and generated `RUSTYCPP:GEN` regions. Treat its
rank as a review queue, not an automatic migration decision.

## Safety annotations

RustyPlan never infers that code is safe. Add one explicit comment immediately
above the declaration being reviewed:

```cpp
// @safe
struct VoteRequest { /* value-style migration candidate */ };

// @bridge: owns an rrr RPC boundary
class RaftCommo { /* retain the boundary in C++ */ };

// @unsafe: raw storage representation
void copy_bytes(void* destination, const void* source);
```

Blank lines and explanatory `//` comments may appear inside that annotation
block, but any source line ends it. Multiple safety annotations on the same
block are an error. An unannotated declaration is reported as
`NEEDS_SAFETY_ANNOTATION`; it is never proposed for direct migration.

Use the planning queue and a targeted explanation:

```bash
python3 tools/rustyplan.py next --source-dir src/masstree --limit 20
python3 tools/rustyplan.py explain --source-dir src/masstree hashcode_t
```

Recommendation meanings are deliberately conservative:

| Result | Meaning |
| --- | --- |
| `MIGRATE_NOW` | Explicitly safe and no known shape/blocker signal. |
| `RESHAPE_THEN_MIGRATE` | Separate construction, inheritance, dispatch, or mutable state first. |
| `PROBE_TRANSPILER` | Test the syntax in an isolated scratch source before committing to a port. |
| `KEEP_CPP_BRIDGE` | Explicit unsafe/bridge boundary stays C++. |
| `DEFER_*_BOUNDARY` | External I/O, concurrency, or binary-layout boundary needs focused design work. |
| `NEEDS_SAFETY_ANNOTATION` | Review has not recorded an explicit safety decision yet. |
| `ALREADY_MIGRATED` | Existing inline-Rust/GEN coverage was detected. |

## Compile databases and call graphs

The `rustyplan.call_graph.ClangCallGraphProvider` accepts a standard
`compile_commands.json`. It executes the compilation command with Clang's JSON
AST dump, extracts direct call and constructor edges, and caches AST output by
source content, command line, and Clang version.

```bash
PYTHONPATH=tools python3 - <<'PY'
from pathlib import Path
from rustyplan.call_graph import ClangCallGraphProvider

provider = ClangCallGraphProvider(Path("build-phase8/compile_commands.json"))
result = provider.graph_for(Path("src/deptran/raft/service.cc"))
if result.available:
    for edge in result.edges:
        print(edge.caller, "->", edge.callee, edge.kind)
else:
    print(result.detail)
PY
```

There is deliberately no regex fallback. If a translation unit has no command,
Clang is unavailable, or the recorded command fails, the result explicitly
reports unavailable dependency data rather than pretending the call graph is
empty. The cache is placed beside the compile database in `.rustyplan-cache/`;
it should remain untracked.

## Migration islands

`rustyplan.islands.group_islands()` groups explicitly safe units linked by
direct calls. Calls from safe code to unsafe/bridge/unannotated code are kept as
frontier edges, not absorbed into the island. Strongly connected components
remain together; large islands split deterministically at component boundaries.

The DOT output uses green for `@safe`, red for `@unsafe`, yellow for `@bridge`,
and gray for unannotated nodes. Generated/DSL-covered declarations should be
shown with a blue border by consumers that enrich the base DOT graph.

## Configuration and focused tests

`tools/rustyplan.toml` is optional. It maps source files to focused test
commands and can override the transpiler path:

```toml
[verify]
transpiler = "third-party/rusty-cpp/target/release/rusty-cpp-transpiler"

[tests]
"src/deptran/raft/messages.hpp" = [
  "ninja -C build-phase8 test_raft_messages",
  "./build-phase8/test_raft_messages",
]

[rustytwin]
module = "raft_helpers"
```

The optional `rustytwin.module` value records the intended RustyTwin module;
RustyPlan does not require RustyTwin to plan or verify a normal migration.

## Verification workflow

Always begin with a dry run:

```bash
python3 tools/rustyplan.py verify src/deptran/raft/messages.hpp
```

It prints the exact transpiler check and configured test commands without
running them. To execute those commands, require an explicit opt-in:

```bash
python3 tools/rustyplan.py verify --execute src/deptran/raft/messages.hpp
```

Execution refuses a dirty Git worktree by default. If the dirty files are
intentional and in scope, acknowledge that explicitly:

```bash
python3 tools/rustyplan.py verify --execute --allow-dirty \
  src/deptran/raft/messages.hpp
```

For an actual migration, keep the two commits separate:

1. **Reshape:** isolate ownership, I/O, threading, binary representation, or
   virtual-dispatch boundaries in C++ while preserving behavior.
2. **Migrate:** author the `#if RUSTYCPP_RUST` block, regenerate, and test it.

The transpiler is the authority on generated drift:

```bash
third-party/rusty-cpp/target/release/rusty-cpp-transpiler \
  inline-rust --rewrite --files src/deptran/raft/messages.hpp

third-party/rusty-cpp/target/release/rusty-cpp-transpiler \
  inline-rust --check --files src/deptran/raft/messages.hpp
```

Never hand-edit a `rust_sha256` marker to silence `--check`: a mismatch means
the generated block is stale. Re-run `--rewrite`, inspect the output, then
build and run the focused test.

## Tests

Run the legacy compatibility and RustyPlan unit tests from the repository root:

```bash
python3 -m unittest tools/test_rust_dsl_inventory.py
python3 -m unittest discover -s tests/rustyplan -v
```

The Clang integration test skips with a clear reason only when Clang is absent.
