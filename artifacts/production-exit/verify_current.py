#!/usr/bin/env python3
"""Validate this bounded pass against its frozen source and retained receipts."""
from pathlib import Path
import hashlib
import json
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def require(condition, message):
    if not condition:
        raise SystemExit(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_json(name):
    return json.loads((HERE / name).read_text(encoding="utf-8-sig"))


inputs = read_json("current-source-sha256.json")
reviewed_drift = read_json("reviewed-input-drift.json")
require(set(reviewed_drift) == {"Cinderline.uproject", "scripts/unreal.sh"},
        "Unexpected reviewed input exception")
for name, expected in inputs.items():
    current = digest(ROOT / name)
    if current != expected:
        review = reviewed_drift.get(name, {})
        require(review.get("tested_input_sha256") == expected and
                review.get("current_sha256") == current, f"Unreviewed input change: {name}")

for name in ("release-current.log", "sanitize-current.log"):
    require("100% tests passed, 0 tests failed out of 15" in (HERE / name).read_text(),
            f"Missing complete 15-suite result: {name}")
server = (HERE / "server-current.log").read_text()
require(re.search(r"^# pass 32$", server, re.M) and
        re.search(r"^# fail 0$", server, re.M), "Server test result mismatch")
require("RESULT passed=4 failed=1" in
        (HERE / "traffic-before-current-stall-gate.log").read_text(),
        "Missing expected pre-fix traffic failure")

engine_build = (HERE / "unreal-current-build.log").read_text()
require("Result: Succeeded" in engine_build, "Unreal build did not succeed")
require("Result: Succeeded" in (HERE / "unreal-current-wrapper-recheck.log").read_text(),
        "Current Mac wrapper recheck did not succeed")
engine_run = (HERE / "unreal-current-automation.log").read_text()
require("Unreal automation passed." in engine_run, "Unreal automation did not pass")
engine = read_json("unreal-current-report.json")
require(engine["succeeded"] + engine["succeededWithWarnings"] == 37 and
        engine["failed"] == 0 and engine["notRun"] == 0 and engine["inProcess"] == 0,
        "Unreal report is incomplete")

synthetic = read_json("synthetic-current.json")
require(synthetic["success"] and len(synthetic["workloads"]) == 3,
        "Synthetic workload did not pass")
for result, expected in zip(synthetic["workloads"], (160, 200, 400)):
    require(result["valid"] and result["units"]["arrived"] == expected and
            result["state_hash"] == result["repeat_state_hash"] and not result["errors"],
            f"Synthetic arrival/repeat failure: {result['name']}")

paid = []
for players, name in ((2, "two-player-current.json"), (4, "four-player-current.json")):
    result = read_json(name)
    require(result["success"] and result["prepared"] and result["deterministic_repeat"] and
            not result["economy_bypass"] and not result["debug_spawn_used"] and
            result["players"] == players and not result["errors"], f"Paid workload failed: {name}")
    for kind in ("state_hash", "trajectory_hash", "recording_hash"):
        require(result[kind] == result[f"repeat_{kind}"], f"Repeat mismatch: {name}/{kind}")
    for team in result["teams"]:
        require(team["march"]["arrived"] == team["march"]["expected"] == 80 and
                team["march"]["goal_changed"] == 0, f"Paid march mismatch: {name}")
    require(any(phase["name"] == "combat" and phase["ticks"] == 2400
                for phase in result["phases"]), f"Combat skipped: {name}")
    paid.append({"players": players, "army_arrivals": players * 80,
                 "state_hash": result["state_hash"], "trajectory_hash": result["trajectory_hash"],
                 "recording_hash": result["recording_hash"],
                 "phase_step_ms": {phase["name"]: phase["step_ms"] for phase in result["phases"]}})

record = {
    "schema": "cinderline.production_exit_verification.v1",
    "status": "bounded pass locally verified; P0 and SC2 gameplay capability goal remain partial",
    "git_head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
    "dirty_tree_source_manifest": "current-source-sha256.json",
    "reviewed_concurrent_packaging_changes": reviewed_drift,
    "checks": {"portable_suites": 15, "sanitizer_suites": 15, "server_tests": 32,
               "unreal_checks": 37, "unreal_succeeded_with_warnings": engine["succeededWithWarnings"],
               "navigation_multi_source_cases": 8, "production_exit_cases": 10,
               "production_traffic_cases": 5, "pre_fix_traffic_cases_passed": 4,
               "pre_fix_traffic_cases_failed": 1},
    "synthetic_arrivals": [160, 200, 400],
    "synthetic_state_hashes": [w["state_hash"] for w in synthetic["workloads"]],
    "paid_workloads": paid,
    "compatibility": {"save": 10, "protocol": 7},
    "new_ios_package": False, "installed_on_device": False,
    "physical_gameplay_tested": False, "gpu_profiled": False, "public_deployment": False,
    "remaining": ["peak simulation latency attribution and reduction", "physical input and complete match",
                  "sustained CPU/GPU/frame/memory/battery/thermal acceptance", "P1-P7 gameplay milestones"],
    "files_sha256": {p.name: digest(p) for p in sorted(HERE.iterdir())
                     if p.is_file() and p.name != "verification.json"},
}
(HERE / "verification.json").write_text(json.dumps(record, indent=2) + "\n")
print("Verified gameplay inputs and reviewed packaging drift, 15 Release + 15 sanitizer suites,")
print("32 server tests, 37 Unreal checks, current Mac wrapper build,")
print("160/200/400 synthetic arrivals and 160/320 paid army arrivals with deterministic repeats.")
