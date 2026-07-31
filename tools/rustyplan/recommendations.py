"""Transparent, conservative migration recommendations."""

from __future__ import annotations

from dataclasses import dataclass
from enum import StrEnum

from .annotations import Safety


class Recommendation(StrEnum):
    MIGRATE_NOW = "MIGRATE_NOW"
    RESHAPE_THEN_MIGRATE = "RESHAPE_THEN_MIGRATE"
    PROBE_TRANSPILER = "PROBE_TRANSPILER"
    KEEP_CPP_BRIDGE = "KEEP_CPP_BRIDGE"
    DEFER_EXTERNAL_BOUNDARY = "DEFER_EXTERNAL_BOUNDARY"
    DEFER_CONCURRENCY_BOUNDARY = "DEFER_CONCURRENCY_BOUNDARY"
    DEFER_BINARY_LAYOUT = "DEFER_BINARY_LAYOUT"
    NEEDS_SAFETY_ANNOTATION = "NEEDS_SAFETY_ANNOTATION"
    ALREADY_MIGRATED = "ALREADY_MIGRATED"
    REVIEW_MANUALLY = "REVIEW_MANUALLY"


@dataclass(frozen=True)
class RecommendationResult:
    recommendation: Recommendation
    reasons: tuple[str, ...]


def recommend(*, safety: Safety, migrated: bool, signals: list[str], kind: str) -> RecommendationResult:
    if migrated:
        return RecommendationResult(Recommendation.ALREADY_MIGRATED, ("inline Rust/GEN region already covers it",))
    if safety is Safety.UNANNOTATED:
        return RecommendationResult(Recommendation.NEEDS_SAFETY_ANNOTATION, ("no explicit @safe, @unsafe, or @bridge annotation",))
    if safety is Safety.BRIDGE:
        return RecommendationResult(Recommendation.KEEP_CPP_BRIDGE, ("explicit @bridge annotation",))
    if safety is Safety.UNSAFE:
        return RecommendationResult(Recommendation.KEEP_CPP_BRIDGE, ("explicit @unsafe annotation",))
    signal_set = set(signals)
    if {"I/O-or-FFI", "void*", "va_list"} & signal_set:
        return RecommendationResult(Recommendation.DEFER_EXTERNAL_BOUNDARY, ("raw I/O, FFI, or erased-pointer boundary",))
    if "threading" in signal_set:
        return RecommendationResult(Recommendation.DEFER_CONCURRENCY_BOUNDARY, ("threading or synchronization boundary",))
    if "C-array" in signal_set:
        return RecommendationResult(Recommendation.DEFER_BINARY_LAYOUT, ("array or layout-sensitive representation",))
    if {"template", "operator-overload"} & signal_set:
        return RecommendationResult(Recommendation.PROBE_TRANSPILER, ("requires a focused transpiler emission probe",))
    if {"constructor-overloads", "inheritance", "virtual-dispatch", "mutable-state"} & signal_set:
        return RecommendationResult(Recommendation.RESHAPE_THEN_MIGRATE, ("shape/dispatch state should be separated before DSL conversion",))
    if kind in {"enum", "enum class", "using", "typedef", "struct", "class", "fn"}:
        return RecommendationResult(Recommendation.MIGRATE_NOW, ("explicitly safe with no known migration blocker",))
    return RecommendationResult(Recommendation.REVIEW_MANUALLY, ("declaration kind is not in the conservative quick-win set",))
