"""Deterministic human and Graphviz reports for RustyPlan findings."""

from __future__ import annotations

from .annotations import Safety
from .call_graph import CallEdge
from .islands import MigrationIsland


def dot(nodes: dict[str, Safety], edges: list[CallEdge], islands: tuple[MigrationIsland, ...] = ()) -> str:
    colors = {Safety.SAFE: "green", Safety.UNSAFE: "red", Safety.BRIDGE: "yellow", Safety.UNANNOTATED: "gray"}
    lines = ["digraph rustyplan {", "  rankdir=LR;"]
    for island in sorted(islands, key=lambda item: item.id):
        lines.append(f'  subgraph "cluster_{_escape(island.id)}" {{')
        lines.append(f'    label="{_escape(island.id)}";')
        for node in island.nodes:
            lines.append(f'    "{_escape(node)}";')
        lines.append("  }")
    for name, safety in sorted(nodes.items()):
        lines.append(f'  "{_escape(name)}" [style=filled, fillcolor={colors[safety]}];')
    for edge in sorted(set(edges)):
        label = "" if edge.kind == "direct" else f' [label="{_escape(edge.kind)}"]'
        lines.append(f'  "{_escape(edge.caller)}" -> "{_escape(edge.callee)}"{label};')
    return "\n".join(lines + ["}", ""])


def _escape(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"')
