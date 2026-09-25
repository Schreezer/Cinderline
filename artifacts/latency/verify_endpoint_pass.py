#!/usr/bin/env python3
"""Validate portable endpoint-optimization receipts; never infer engine/device proof."""
from pathlib import Path
import hashlib
import json
import re

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
BEFORE = ROOT / "artifacts/production-exit"


def require(condition, message):
    if not condition:
        raise SystemExit(message)


def read(path):
    return json.loads(path.read_text())


def stable(value):
    """Only measured wall-clock distributions/totals are excluded."""
    if isinstance(value, dict):
        return {k: stable(v) for k, v in value.items()
                if not k.endswith("_ms") and k != "wall_ms_by_type"}
    if isinstance(value, list):
        return [stable(v) for v in value]
    return value


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


for name, expected in read(HERE / "endpoint-inputs-sha256.json").items():
    require(digest(ROOT / name) == expected, f"Frozen gameplay input changed: {name}")
for name, expected in read(HERE / "test-inputs-sha256.json").items():
    require(digest(ROOT / name) == expected, f"Frozen test input changed: {name}")
for name, expected in read(HERE / "verified-core-sha256.json").items():
    require(digest(ROOT / name) == expected, f"Preserved core changed: {name}")
for name in ("release-after.log", "sanitize-after.log"):
    require("100% tests passed, 0 tests failed out of 16" in (HERE / name).read_text(),
            f"Missing complete 16-suite result: {name}")
server = (HERE / "server-after.log").read_text()
require(re.search(r"^# pass 32$", server, re.M) and re.search(r"^# fail 0$", server, re.M),
        "Server result incomplete")

old_routes = (HERE / "predicates-before.log").read_text()
new_routes = (HERE / "predicates-after.log").read_text()
require("RESULT passed=4 failed=0" in old_routes and old_routes == new_routes,
        "Predicate tests or exact route corpus differ")
route_fingerprint = re.search(r"^ROUTE_CORPUS .+$", new_routes, re.M).group(0)

synthetic = read(HERE / "synthetic-after.json")
require(synthetic["success"] and stable(synthetic) == stable(read(BEFORE / "synthetic-current.json")),
        "Synthetic workload semantics differ from verified baseline")
require([w["units"]["arrived"] for w in synthetic["workloads"]] == [160, 200, 400],
        "Synthetic arrivals incomplete")

paid = []
for players, stem in ((2, "two-player"), (4, "four-player")):
    old = read(BEFORE / f"{stem}-current.json")
    new = read(HERE / f"{stem}-after.json")
    require(new["success"] and new["prepared"] and new["deterministic_repeat"] and not new["errors"],
            f"Incomplete paid workload: {stem}")
    require(stable(old) == stable(new), f"Non-timing workload fields changed: {stem}")
    for kind in ("state_hash", "trajectory_hash", "recording_hash"):
        require(new[kind] == new[f"repeat_{kind}"], f"Repeat mismatch: {stem}/{kind}")
    require(all(t["march"]["arrived"] == t["march"]["expected"] == 80 and
                t["march"]["goal_changed"] == 0 for t in new["teams"]),
            f"Army arrivals incomplete: {stem}")
    paid.append({"players": players, "army_arrivals": players * 80,
                 "all_non_timing_fields_match": True,
                 **{k: new[k] for k in ("state_hash", "trajectory_hash", "recording_hash")},
                 "before_phase_step_ms": {p["name"]: p["step_ms"] for p in old["phases"]},
                 "after_phase_step_ms": {p["name"]: p["step_ms"] for p in new["phases"]}})

record = {
    "schema": "cinderline.navigation_endpoint_verification.v1",
    "status": "portable verified; current Unreal gates pending Android initialization; P0 remains open",
    "checks": {"release_suites": 16, "sanitizer_suites": 16, "server_tests": 32,
               "route_corpus": route_fingerprint, "current_unreal_build": False,
               "current_unreal_automation": False},
    "synthetic_arrivals": [160, 200, 400],
    "paid_workloads": paid,
    "trace_attribution": "sample-analysis-prefix-before.json",
    "measurement_limits": ["desktop simulation only", "before complete workloads from prior verified pass",
                           "wall time depends on OS scheduling and Launcher initialization",
                           "Instruments prefixes are diagnostic and include capture overhead"],
    "new_ios_package": False, "device_install": False, "gpu_profiled": False,
    "thermal_acceptance": False, "public_deployment": False,
    "compatibility": {"save": 10, "protocol": 7},
    "remaining": ["remaining long-step collision cost", "current Unreal build and automation",
                  "complete phone match and sustained CPU/GPU/thermal checks", "P1-P7 roadmap"],
    "files_sha256": {p.name: digest(p) for p in sorted(HERE.iterdir())
                     if p.is_file() and p.name != "verification.json"},
}
(HERE / "verification.json").write_text(json.dumps(record, indent=2) + "\n")
print("Verified 16 Release + 16 sanitizer suites, 32 server tests, exact route corpus,")
print("and all non-timing fields for synthetic and complete paid workloads.")
print("Current Unreal build, device, rendering and thermal gates remain open.")
