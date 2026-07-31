"""Deterministic safe migration-island grouping."""

from __future__ import annotations

from dataclasses import dataclass

from .annotations import Safety
from .call_graph import CallEdge


@dataclass(frozen=True)
class IslandNode:
    id: str
    safety: Safety
    priority: int = 0


@dataclass(frozen=True)
class MigrationIsland:
    id: str
    nodes: tuple[str, ...]
    frontier: tuple[CallEdge, ...]
    score: int


def group_islands(nodes: list[IslandNode], edges: list[CallEdge], max_nodes: int = 12) -> tuple[MigrationIsland, ...]:
    """Group safe connected units, never crossing an unsafe/bridge boundary."""
    by_id = {node.id: node for node in nodes}
    safe = {node.id for node in nodes if node.safety is Safety.SAFE}
    adjacency = {node: set() for node in safe}
    frontier: dict[str, list[CallEdge]] = {node: [] for node in safe}
    for edge in edges:
        if edge.caller not in safe:
            continue
        if edge.callee in safe:
            adjacency[edge.caller].add(edge.callee)
            adjacency[edge.callee].add(edge.caller)
        else:
            frontier[edge.caller].append(edge)
    components: list[list[str]] = []
    unseen = set(safe)
    while unseen:
        seed = min(unseen)
        stack, component = [seed], []
        unseen.remove(seed)
        while stack:
            current = stack.pop()
            component.append(current)
            for next_node in sorted(adjacency[current], reverse=True):
                if next_node in unseen:
                    unseen.remove(next_node)
                    stack.append(next_node)
        components.append(sorted(component))
    islands: list[MigrationIsland] = []
    for component in sorted(components, key=lambda group: group[0]):
        # Splitting by sorted names is deterministic. SCCs are kept intact by
        # adding every mutually reachable unit to a single block first.
        blocks = _scc_blocks(component, edges)
        chunks: list[list[str]] = [[]]
        for block in blocks:
            if chunks[-1] and len(chunks[-1]) + len(block) > max_nodes:
                chunks.append([])
            chunks[-1].extend(block)
        for index, chunk in enumerate(chunks, 1):
            chunk = sorted(chunk)
            node_set = set(chunk)
            cut_frontier = [edge for node in chunk for edge in frontier[node]]
            cut_frontier.extend(edge for edge in edges if edge.caller in node_set and edge.callee in safe - node_set)
            islands.append(MigrationIsland(
                id=f"{component[0]}#{index}", nodes=tuple(chunk),
                frontier=tuple(sorted(set(cut_frontier))),
                score=sum(by_id[node].priority for node in chunk) - 5 * len(cut_frontier),
            ))
    return tuple(sorted(islands, key=lambda island: (-island.score, island.id)))


def _scc_blocks(component: list[str], edges: list[CallEdge]) -> list[list[str]]:
    members, index, low, stack, on_stack, next_index, result = set(component), {}, {}, [], set(), 0, []
    graph = {node: [] for node in members}
    for edge in edges:
        if edge.caller in members and edge.callee in members:
            graph[edge.caller].append(edge.callee)

    def visit(node: str) -> None:
        nonlocal next_index
        index[node] = low[node] = next_index; next_index += 1; stack.append(node); on_stack.add(node)
        for target in sorted(graph[node]):
            if target not in index:
                visit(target); low[node] = min(low[node], low[target])
            elif target in on_stack:
                low[node] = min(low[node], index[target])
        if low[node] == index[node]:
            block = []
            while True:
                target = stack.pop(); on_stack.remove(target); block.append(target)
                if target == node: break
            result.append(sorted(block))
    for node in sorted(members):
        if node not in index: visit(node)
    return sorted(result, key=lambda block: block[0])
