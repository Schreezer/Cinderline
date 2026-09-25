#!/usr/bin/env python3
"""Validate local P1.2 receipts; physical and release acceptance stays open."""
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def require(condition, message):
    if not condition:
        raise SystemExit(message)


def read(name):
    return json.loads((HERE / name).read_text(encoding="utf-8-sig"))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def engine_report(name, count):
    log = (HERE / name).read_text()
    match = re.search(r"Report: (.+/index\.json)", log)
    require(match and f"Verified {count} expected Unreal tests" in log, f"Missing successful {name}")
    path = Path(match.group(1))
    report = json.loads(path.read_text(encoding="utf-8-sig"))
    require(report["failed"] == report["notRun"] == report["inProcess"] == 0,
            f"Failed or unfinished {name}")
    require(report["succeeded"] + report["succeededWithWarnings"] == count,
            f"Unexpected test count in {name}")
    return {"path": str(path.relative_to(ROOT)), "sha256": sha(path),
            "succeeded": report["succeeded"], "succeeded_with_warnings": report["succeededWithWarnings"]}


for name in ["release.log", "sanitize.log"]:
    log = (HERE / name).read_text()
    require("100% tests passed, 0 tests failed out of 20" in log, f"Incomplete {name}")
sanitize_cache = (ROOT / "Saved/P1SustainedSanitize/CMakeCache.txt").read_text()
require("CINDERLINE_SANITIZE:BOOL=ON" in sanitize_cache and
        "CMAKE_BUILD_TYPE:STRING=RelWithDebInfo" in sanitize_cache, "Wrong sanitizer configuration")
server = (HERE / "server.log").read_text()
require("# pass 35" in server and "# fail 0" in server and "# skipped 0" in server,
        "Incomplete current-worker server tests")
require("Result: Succeeded" in (HERE / "unreal-build.log").read_text(), "Missing current Mac build")
integration = engine_report("unreal-tests.log", 39)
transport = engine_report("multiplayer.log", 2)

sustained = read("sustained-baseline.json")
require(sustained["success"] and sustained["patrol_required_legs"] == 4 and
        sustained["escort_stable_ticks"] == 20 and sustained["maximum_ticks"] == 9600,
        "Sustained workload rules or completion failed")
require([(w["mobile_units"],w["mode"]) for w in sustained["workloads"]] ==
        [(n,m) for n in [16,160,200] for m in ["patrol","escort"]], "Missing sustained workload")
for workload in sustained["workloads"]:
    require(workload["completed"] == workload["recipients"] and
            workload["accepted_points_unchanged"] and workload["deterministic_repeat"],
            "Sustained arrivals or repeat failed")
    if workload["mode"] == "escort":
        require(workload["followers_moved_during_leader_route"] == workload["recipients"],
                "Escort did not follow during the leader route")
queues = read("queue-baseline.json")
require(queues["success"] and [w["unit_count"] for w in queues["workloads"]] == [16, 160, 200],
        "Queue workload failed")
for workload in queues["workloads"]:
    require(all(workload[key] for key in ["valid", "deterministic_state", "deterministic_recording",
            "seventeenth_group_append_rejected", "rejection_state_hash_unchanged",
            "rejection_recording_count_unchanged"]), "Queue acceptance or repeat failed")
    require(all(stage["hold_preserved"] and stage["accepted_points_immutable"] and
                0 < stage["bytes"] <= queues["snapshot_frame_byte_limit"]
                for stage in workload["snapshots"]), "Invalid queue snapshot stage")
navigation = read("navigation-baseline.json")
require(navigation["success"] and [w["units"]["arrived"] for w in navigation["workloads"]] == [160, 200, 400],
        "Navigation arrivals failed")
require(all(w["valid"] and w["deterministic_state"] and not w["units"]["unexplained_order_loss"]
            for w in navigation["workloads"]), "Navigation validity or repeat failed")
paid = []
for name, players, expected in [("paid-two-player.json", 2, 160), ("paid-four-player.json", 4, 320)]:
    workload = read(name)
    require(workload["success"] and workload["prepared"] and workload["deterministic_repeat"] and
            workload["players"] == players and not workload["debug_spawn_used"] and not workload["economy_bypass"],
            f"Paid workload or repeat failed: {name}")
    require(sum(team["march"]["arrived"] for team in workload["teams"]) == expected,
            f"Missing paid army arrivals: {name}")
    paid.append({"players": players, "army_arrivals": expected, "deterministic_repeat": True,
                 "state_hash": workload["state_hash"], "trajectory_hash": workload["trajectory_hash"],
                 "recording_hash": workload["recording_hash"],
                 "phase_step_ms": {phase["name"]: phase["step_ms"] for phase in workload["phases"]}})
preview = read("preview-verification.json")
require(preview["complete"] and preview["protected_unchanged"] and len(preview["cases"]) == 8,
        "Incomplete preview capture or changed player state")
for case in preview["cases"]:
    require(sha(ROOT / case["screenshot"]) == case["sha256"], "Screenshot changed after capture")
require(preview.get("visual_review") == "passed", "Human/model visual inspection not recorded")
frozen = read("frozen-core-sha256.json")
archive = ROOT / frozen["source_archive"]
require(sha(ROOT / frozen["module_archive"]) == frozen["module_sha256"] == preview["module_sha256"],
        "Archived preview module changed")
for filename, expected in frozen["files"].items():
    require(sha(ROOT / filename) == expected, "Frozen P1.2 core input changed")
    relative = Path(filename).relative_to("Saved/P1SustainedVerified")
    if relative.parts[0] == "Include":
        require(sha(archive / "Source/Cinderline/Public" / relative.relative_to("Include")) == expected,
                "Archived source header differs from tested core header")
corrected = frozen["workload_correction"]
require(sha(ROOT / corrected["path"]) == corrected["sha256"], "Staged workload binary changed")
require(sha(archive / "Tools/MatchBaseline.cpp") == corrected["source_sha256"],
        "Staged workload source changed")
source_manifest = read("source-sha256.json")
for filename, expected in source_manifest.items():
    require(sha(archive / filename) == expected, f"Archived P1.2 source changed: {filename}")
binary_paths = [ROOT / frozen["module_archive"],
                ROOT / "Saved/P1SustainedVerified/libCinderlineSimulation.a",
                ROOT / "Saved/P1SustainedVerified/CinderlineMatchWorker",
                ROOT / corrected["path"]]
require(all(p.is_file() for p in binary_paths), "Missing tested binary")
receipt = {
    "schema": "cinderline.sustained_order_verification.v1",
    "verified_at_utc": datetime.now(timezone.utc).isoformat(),
    "status": "locally verified P1.2 implementation; physical acceptance and remaining P1 features open",
    "git_head_context": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
    "dirty_checkout": True, "source_archive": frozen["source_archive"], "source_manifest_sha256": sha(HERE / "source-sha256.json"),
    "source_file_count": len(source_manifest), "binary_sha256": {str(p.relative_to(ROOT)): sha(p) for p in binary_paths},
    "compatibility": {"save": 12, "protocol": 9, "legacy_saves_supported": "1-11"},
    "checks": {"release_suites": 20, "sanitizer_suites": 20,
               "server_tests": 35, "unreal_tests": 39, "real_loopback_transport_tests": 2},
    "integration_report": integration, "transport_report": transport,
    "synthetic_navigation_arrivals": [160, 200, 400], "synthetic_queue_units": [16, 160, 200],
    "sustained_workloads": sustained["workloads"], "paid_workloads": paid, "render_captures": preview["cases"], "protected_player_state_unchanged": True,
    "not_accepted": ["native mouse/touch full playtest", "current iPhone package",
                     "device CPU/GPU/thermal budget", "physical LAN/Internet match", "public deployment",
                     "Android package", "P1 formation facing/spacing", "SC2 capability parity"],
}
(HERE / "verification.json").write_text(json.dumps(receipt, indent=2) + "\n")
print("Verified 20 Release + 20 sanitizer suites, 35 server tests, 39 Unreal tests,")
print("2 real transport tests, complete sustained/queue/navigation/paid workloads and eight reviewed captures.")
