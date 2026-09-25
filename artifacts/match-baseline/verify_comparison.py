#!/usr/bin/env python3
"""Validate the frozen legal benchmark captures, then summarize their timings."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


inputs = json.loads((HERE / "benchmark-inputs-sha256.json").read_text())
for name, expected in inputs.items():
    assert sha(ROOT / name) == expected, f"Benchmark input changed: {name}"

report = {
    "schema": "cinderline.match_scale_comparison.v1",
    "scope": "Single sequential before/after portable CPU wall-time captures on one Mac; no renderer, transport or device claim.",
    "reference_mobile_units_per_player": 100,
    "reference_crew_per_player": 184,
    "workloads": [],
}
for label, players in (("two-player", 2), ("four-player", 4)):
    captures = []
    for revision in ("before", "after"):
        path = HERE / f"{label}-{revision}.json"
        data = json.loads(path.read_text())
        assert data["success"] and data["prepared"] and data["deterministic_repeat"]
        assert data["players"] == players and not data["errors"]
        assert data["profile_enabled"] and data["snapshots_enabled"]
        assert not data["economy_bypass"] and not data["debug_spawn_used"]
        assert not data["supported_device_capacity_claim"]
        assert data["commands"]["rejected"] == 0
        assert len(data["teams"]) == players
        for key in ("state_hash", "trajectory_hash", "recording_hash"):
            assert data[key] == data[f"repeat_{key}"]
        for team in data["teams"]:
            assert sum(team["prepared_units"].values()) == 100
            assert team["prepared_supply"] == team["prepared_capacity"] == 184
            march = team["march"]
            assert march["expected"] == march["alive"] == march["arrived"] == 80
            for key in ("pending_valid_order", "navigation_exhausted", "missing", "goal_changed"):
                assert march[key] == 0
        assert sum(team["combat_delta"]["damage"] for team in data["teams"]) > 0
        assert sum(team["combat_delta"]["lost"] for team in data["teams"]) > 0
        assert [phase["name"] for phase in data["phases"]] == ["preparation", "march", "combat"]
        for phase in data["phases"]:
            assert phase["ticks"] > 0 and phase["profile_ms"]
            snap = phase["snapshots"]
            assert snap["batches"] == phase["ticks"] // 2
            assert snap["messages"] == snap["batches"] * players
            assert 0 < snap["message_bytes"]["max"] <= snap["message_byte_limit"] == 1048576
        captures.append(data)

    before, after = captures
    for key in ("state_hash", "trajectory_hash", "recording_hash", "preparation_ticks",
                "final_tick", "recorded_commands", "navigation"):
        assert before[key] == after[key], f"{label} changed {key}"
    assert before["commands"]["accepted_by_type"] == after["commands"]["accepted_by_type"]
    for left, right in zip(before["teams"], after["teams"]):
        for key in ("prepared_units", "final_units", "march", "combat_delta"):
            assert left[key] == right[key], f"{label} team changed {key}"

    phases = []
    for left, right in zip(before["phases"], after["phases"]):
        for key in ("batches", "messages", "message_bytes", "all_seats_batch_bytes",
                    "visible_entities", "visible_effects"):
            assert left["snapshots"][key] == right["snapshots"][key], f"{label} snapshot changed {key}"
        phases.append({
            "name": left["name"], "ticks": left["ticks"],
            "step_ms_before": left["step_ms"], "step_ms_after": right["step_ms"],
            "step_p99_reduction_percent": 100 * (1 - right["step_ms"]["p99"] / left["step_ms"]["p99"]),
            "movement_economy_p99_ms_before": left["profile_ms"]["movement_economy"]["p99"],
            "movement_economy_p99_ms_after": right["profile_ms"]["movement_economy"]["p99"],
            "snapshot_all_seats_p95_ms_after": right["snapshots"]["all_seats_batch_cpu_ms"]["p95"],
            "snapshot_max_message_bytes": right["snapshots"]["message_bytes"]["max"],
        })
    report["workloads"].append({
        "players": players, "preparation_ticks": after["preparation_ticks"],
        "final_tick": after["final_tick"], "state_hash": after["state_hash"],
        "trajectory_hash": after["trajectory_hash"], "recording_hash": after["recording_hash"],
        "recorded_commands": after["recorded_commands"],
        "army_arrivals": [team["march"]["arrived"] for team in after["teams"]],
        "combat_units_lost": sum(team["combat_delta"]["lost"] for team in after["teams"]),
        "navigation": after["navigation"], "phases": phases,
        "files_sha256": {f"{label}-{rev}.json": sha(HERE / f"{label}-{rev}.json") for rev in ("before", "after")},
    })

report["success"] = True
(HERE / "comparison.json").write_text(json.dumps(report, indent=2) + "\n")
print("PASS: both paid-army workloads preserve every tick, command, arrival, combat result and snapshot payload distribution.")
