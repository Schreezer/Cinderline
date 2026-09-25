#!/usr/bin/env python3
"""Verify the bounded local P1 feature pass without closing paid/device gates."""
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
    require(match and f"Verified {count} expected Unreal tests" in log,
            f"Missing successful {name}")
    path = Path(match.group(1))
    report = json.loads(path.read_text(encoding="utf-8-sig"))
    require(report["failed"] == report["notRun"] == report["inProcess"] == 0,
            f"Failed or unfinished {name}")
    require(report["succeeded"] + report["succeededWithWarnings"] == count,
            f"Unexpected test count in {name}")
    return {"path": str(path.relative_to(ROOT)), "sha256": sha(path),
            "succeeded": report["succeeded"],
            "succeeded_with_warnings": report["succeededWithWarnings"]}


for name in ["release.log", "sanitize.log"]:
    require("100% tests passed, 0 tests failed out of 21" in (HERE / name).read_text(),
            f"Incomplete {name}")
cache = (ROOT / "Saved/P1FormationSanitize/CMakeCache.txt").read_text()
require("CINDERLINE_SANITIZE:BOOL=ON" in cache and
        "CMAKE_BUILD_TYPE:STRING=RelWithDebInfo" in cache,
        "Wrong sanitizer configuration")
server = (HERE / "server.log").read_text()
require("# pass 35" in server and "# fail 0" in server and "# skipped 0" in server,
        "Incomplete current-worker server tests")
require("Result: Succeeded" in (HERE / "unreal-build.log").read_text(),
        "Missing current Mac build")
integration = engine_report("unreal-tests.log", 40)
transport = engine_report("multiplayer.log", 2)

formation = read("formation-baseline.json")
require(formation["success"] and formation["maximum_ticks_per_leg"] == 9600 and
        formation["stable_arrival_ticks"] == 20 and
        [w["units"] for w in formation["workloads"]] == [16, 160, 200],
        "Formation workload completion or rules failed")
for workload in formation["workloads"]:
    require(workload["deterministic_repeat"] and workload["accepted_points_unchanged"],
            "Formation repeat or immutable destinations failed")
    require([leg["spacing"] for leg in workload["legs"]] == [0, 1, 2] and
            all(leg["arrived"] == workload["units"] for leg in workload["legs"]),
            "Missing complete formation leg")
    require(workload["snapshots"]["protocol"] == 10 and
            workload["snapshots"]["message_bytes"]["max"] <= 1048576,
            "Formation snapshot compatibility or size failed")
sustained = read("sustained-baseline.json")
require(sustained["success"] and sustained["patrol_required_legs"] == 4 and
        sustained["escort_stable_ticks"] == 20 and sustained["maximum_ticks"] == 9600,
        "Sustained workload rules or completion failed")
require([(w["mobile_units"], w["mode"]) for w in sustained["workloads"]] ==
        [(n, mode) for n in [16, 160, 200] for mode in ["patrol", "escort"]],
        "Missing sustained workload")
for workload in sustained["workloads"]:
    require(workload["completed"] == workload["recipients"] and
            workload["accepted_points_unchanged"] and workload["deterministic_repeat"],
            "Sustained arrivals or repeat failed")
    if workload["mode"] == "escort":
        require(workload["followers_moved_during_leader_route"] == workload["recipients"],
                "Escort did not follow during leader movement")
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
require(navigation["success"] and
        [w["units"]["arrived"] for w in navigation["workloads"]] == [160, 200, 400],
        "Navigation arrivals failed")
require(all(w["valid"] and w["deterministic_state"] and
            not w["units"]["unexplained_order_loss"] for w in navigation["workloads"]),
        "Navigation validity or repeat failed")

preview = read("preview-verification.json")
expected_cases = [f"{device}-formation-{state}" for device in ["desktop", "mobile"]
                  for state in ["tight", "standard", "wide", "facing", "pending", "accepted"]]
require(preview["complete"] and preview["protected_unchanged"] and
        [case["case"] for case in preview["cases"]] == expected_cases,
        "Incomplete preview capture or changed player state")
require(preview.get("visual_review") == "passed", "Visual inspection not recorded")
for case in preview["cases"]:
    require(sha(ROOT / case["screenshot"]) == case["sha256"], "Screenshot changed after capture")
require(sha(ROOT / preview["module"]) == preview["module_sha256"],
        "Preview module changed after capture")
manifest = read("source-sha256.json")
for name, expected in manifest.items():
    require(sha(ROOT / name) == expected, f"Source changed after verification began: {name}")
binaries = ["Binaries/Mac/libUnrealEditor-Cinderline.dylib",
            "Saved/P1FormationBuild/libCinderlineSimulation.a",
            "Saved/P1FormationBuild/CinderlineMatchWorker",
            "Saved/P1FormationBuild/CinderlineFormationOrderBaseline",
            "Saved/P1FormationBuild/CinderlineSustainedOrderBaseline",
            "Saved/P1FormationBuild/CinderlineTacticalOrderBaseline",
            "Saved/P1FormationBuild/CinderlineGameplayBaseline"]
evidence = ["release.log", "sanitize.log", "server.log", "unreal-build.log",
            "unreal-tests.log", "multiplayer.log", "formation-baseline.json",
            "sustained-baseline.json", "queue-baseline.json", "navigation-baseline.json",
            "preview-verification.json"]
receipt = {
    "schema": "cinderline.formation_order_verification.v1",
    "verified_at_utc": datetime.now(timezone.utc).isoformat(),
    "status": "P1 local feature implementation verified; paid workload and physical acceptance remain open",
    "execution_boundary": "stop after P1; P2 and later not started",
    "git_head_context": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
    "dirty_checkout": True, "source_manifest_sha256": sha(HERE / "source-sha256.json"),
    "source_file_count": len(manifest),
    "binary_sha256": {name: sha(ROOT / name) for name in binaries},
    "evidence_sha256": {name: sha(HERE / name) for name in evidence},
    "compatibility": {"save": 13, "protocol": 10, "legacy_saves_supported": "1-12"},
    "checks": {"release_suites": 21, "sanitizer_suites": 21, "server_tests": 35,
               "unreal_tests": 40, "real_loopback_transport_tests": 2},
    "integration_report": integration, "transport_report": transport,
    "formation_workloads": formation["workloads"],
    "synthetic_navigation_arrivals": [160, 200, 400],
    "synthetic_queue_units": [16, 160, 200],
    "sustained_workloads": sustained["workloads"],
    "render_captures": preview["cases"], "protected_player_state_unchanged": True,
    "not_accepted": ["P0 remaining latency spikes", "paid four-player noncombat isolation",
                     "native mouse and iPhone touch full match", "matching iPhone package",
                     "device CPU/GPU/thermal budget", "physical LAN/Internet match",
                     "public deployment", "Android package", "SC2 capability parity"],
    "paid_four_player_evidence": "artifacts/patrol-escort/staging-attempt.json",
    "paid_four_player_passed": False,
}
(HERE / "verification.json").write_text(json.dumps(receipt, indent=2) + "\n")
print("Verified 21 Release + 21 sanitizer suites, 35 server tests, 40 Unreal tests,")
print("2 real transport tests, synthetic formation/queue/patrol/escort/navigation repeats and 12 reviewed captures.")
print("P0 paid/latency/device gates remain open. Stop before P2.")
