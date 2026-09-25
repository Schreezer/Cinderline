#!/usr/bin/env python3
"""Check the terrain-visibility pass against the preserved endpoint-only baseline."""
from pathlib import Path
import hashlib
import json
import re

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
BEFORE = ROOT / "artifacts/latency"


def require(condition, message):
    if not condition:
        raise SystemExit(message)


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def stable(value):
    """Exclude measured wall-clock fields, retaining all gameplay observations."""
    if isinstance(value, dict):
        return {k: stable(v) for k, v in value.items()
                if not k.endswith("_ms") and k != "wall_ms_by_type"}
    if isinstance(value, list):
        return [stable(v) for v in value]
    return value


for manifest in (HERE / "gameplay-inputs-sha256.json", HERE / "test-inputs-sha256.json",
                 HERE / "trace-inputs-sha256.json",
                 HERE / "verified-core-sha256.json", BEFORE / "verified-core-sha256.json"):
    for name, expected in read(manifest).items():
        require(digest(ROOT / name) == expected, f"Frozen input changed: {name}")

for name in ("release-after.log", "sanitize-after.log"):
    require("100% tests passed, 0 tests failed out of 17" in (HERE / name).read_text(),
            f"Missing complete 17-suite result: {name}")
server = (HERE / "server-after.log").read_text()
require(re.search(r"^# pass 32$", server, re.M) and re.search(r"^# fail 0$", server, re.M),
        "Server result incomplete")

old_predicates = (HERE / "predicates-before.log").read_text()
new_predicates = (HERE / "predicates-after.log").read_text()
require("RESULT passed=7 failed=0" in old_predicates and old_predicates == new_predicates,
        "Predicate oracle or exact route corpus differs")
route_fingerprint = re.search(r"^ROUTE_CORPUS .+$", new_predicates, re.M).group(0)
require("RESULT passed=6 failed=0" in (HERE / "cache-tests-after.log").read_text(),
        "Cache regression suite incomplete")

synthetic = read(HERE / "synthetic-after.json")
require(synthetic["success"] and stable(synthetic) == stable(read(BEFORE / "synthetic-after.json")),
        "Synthetic workload semantics differ")
require([w["units"]["arrived"] for w in synthetic["workloads"]] == [160, 200, 400],
        "Synthetic arrivals incomplete")

paid = []
for players, stem in ((2, "two-player"), (4, "four-player")):
    old = read(BEFORE / f"{stem}-after.json")
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

trace = read(HERE / "sample-analysis-after.json")
require([i["tick"] for i in trace["intervals"]] == [6416, 8827], "Wrong diagnostic trace intervals")
require(all(i["duration_ms"] > 0 and i["sample_join"]["joined_samples_with_backtraces"] > 0
            for i in trace["intervals"]), "Trace intervals lack matching CPU samples")
prefix = read(HERE / "probe-trace-after.json")
require(prefix["diagnostic_only"] and not prefix["success"] and not prefix["prepared"],
        "Diagnostic prefix incorrectly claims full acceptance")
for kind in ("state_hash", "trajectory_hash", "recording_hash"):
    require(prefix[kind] == read(HERE / "envelope-prefix.json")[kind],
            f"Diagnostic prefix mismatch: {kind}")

engine = read(HERE / "unreal-verification.json")
for name, expected in engine["inputs_sha256"].items():
    require(digest(ROOT / name) == expected, f"Engine-tested input changed: {name}")
for name, expected in engine.get("binaries_sha256", {}).items():
    require(digest(ROOT / name) == expected, f"Engine-tested binary changed: {name}")
if engine["build_passed"]:
    require("Result: Succeeded" in (HERE / "unreal-build.log").read_text(), "Missing Unreal build success")
if engine["automation_passed"]:
    require("Unreal automation passed." in (HERE / "unreal-automation.log").read_text(),
            "Missing validated Unreal automation result")
    report = ROOT / engine["report"]
    require(digest(report) == engine["report_sha256"], "Unreal report changed")
    data = read(report)
    require(data["succeeded"] == engine["checks"] and data["failed"] == 0,
            "Unreal result counts differ")

record = {
    "schema": "cinderline.navigation_visibility_verification.v1",
    "status": "bounded pass verified; P0 remains open",
    "checks": {"release_suites": 17, "sanitizer_suites": 17, "server_tests": 32,
               "predicate_cases": 7, "cache_cases": 6, "route_corpus": route_fingerprint,
               "current_unreal_build": engine["build_passed"],
               "current_unreal_automation": engine["automation_passed"],
               "unreal_checks": engine["checks"]},
    "synthetic_arrivals": [160, 200, 400],
    "paid_workloads": paid,
    "trace_attribution": "sample-analysis-after.json",
    "memory_bound": {"terrain_result_payload_bytes": 4194304, "page_limit": 1024,
                     "target_limit": 16384, "metadata_in_payload_count": False,
                     "existing_full_goal_cache_is_separate": True},
    "measurement_limits": ["desktop simulation only", "baseline from previous verified pass",
                           "wall time depends on host scheduling", "diagnostic traces include capture overhead"],
    "new_ios_package": False, "device_install": False, "gpu_profiled": False,
    "thermal_acceptance": False, "public_deployment": False,
    "compatibility": {"save": 10, "protocol": 7},
    "remaining": ["remaining long-step cost", "complete physical match and sustained CPU/GPU/thermal checks",
                  "P1-P7 roadmap"],
    "files_sha256": {p.name: digest(p) for p in sorted(HERE.iterdir())
                     if p.is_file() and p.name != "verification.json"},
}
(HERE / "verification.json").write_text(json.dumps(record, indent=2) + "\n")
print("Verified 17 Release + 17 sanitizer suites, 32 server tests, predicate/cache regressions,")
print("and all non-timing fields for synthetic and complete paid workloads.")
print(f"Current Unreal build={engine['build_passed']}; automation={engine['automation_passed']} ({engine['checks']} checks).")
print("P0, physical play, rendering and thermal acceptance remain open.")
