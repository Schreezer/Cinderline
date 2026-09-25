#!/usr/bin/env python3
"""Validate local P1.1 receipts; physical and release acceptance stays open."""
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
    require("100% tests passed, 0 tests failed out of 19" in log, f"Incomplete {name}")
for name in ["release-save10.log", "sanitize-save10.log"]:
    require("100% tests passed, 0 tests failed out of 1" in (HERE / name).read_text(),
            f"Incomplete final save10 tactical-suite check: {name}")
sanitize_cache = (ROOT / "Saved/P1TacticalSanitize/CMakeCache.txt").read_text()
require("CINDERLINE_SANITIZE:BOOL=ON" in sanitize_cache and
        "CMAKE_BUILD_TYPE:STRING=RelWithDebInfo" in sanitize_cache, "Wrong sanitizer configuration")
server = (HERE / "server.log").read_text()
require("# pass 34" in server and "# fail 0" in server and "# skipped 0" in server,
        "Incomplete current-worker server tests")
require("Result: Succeeded" in (HERE / "unreal-build.log").read_text(), "Missing current Mac build")
integration = engine_report("unreal-tests.log", 38)
transport = engine_report("multiplayer.log", 2)

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
require(preview["complete"] and preview["protected_unchanged"] and len(preview["cases"]) == 4,
        "Incomplete preview capture or changed player state")
for case in preview["cases"]:
    require(sha(ROOT / case["screenshot"]) == case["sha256"], "Screenshot changed after capture")
require(preview.get("visual_review") == "passed", "Human/model visual inspection not recorded")
require(sha(ROOT / preview["module"]) == preview["module_sha256"], "Preview used a different game module")
for filename, expected in read("frozen-core-sha256.json")["files"].items():
    require(sha(ROOT / filename) == expected, "Frozen P1 core input changed")
    relative = Path(filename).relative_to("Saved/P1TacticalVerified")
    live = (ROOT / "Source/Cinderline/Public" / relative.relative_to("Include")
            if relative.parts[0] == "Include" else ROOT / "Saved/P1TacticalBuild" / relative)
    require(sha(live) == expected, f"Current core input differs from tested frozen input: {live}")

source_files = set()
for directory in [ROOT / "Source", ROOT / "Plugins/CinderMetalFX/Source", ROOT / "Tests", ROOT / "Tools"]:
    source_files.update(p for p in directory.rglob("*") if p.is_file() and
                        p.suffix in [".cpp", ".h", ".cs", ".mm", ".cinder"])
for directory in [ROOT / "Server"]:
    source_files.update(p for p in directory.rglob("*") if p.is_file() and "node_modules" not in p.parts
                        and p.suffix in [".cpp", ".h", ".js"])
for name in ["CMakeLists.txt", "Cinderline.uproject", "Config/DefaultEngine.ini", "Config/DefaultGame.ini",
             "Config/DefaultDeviceProfiles.ini", "scripts/lib/unreal_suites.json"]:
    path = ROOT / name
    if path.is_file():
        source_files.add(path)
source_manifest = {str(p.relative_to(ROOT)): sha(p) for p in sorted(source_files)}
(HERE / "source-sha256.json").write_text(json.dumps(source_manifest, indent=2) + "\n")
binary_paths = [ROOT / "Binaries/Mac/libUnrealEditor-Cinderline.dylib",
                ROOT / "Saved/P1TacticalBuild/libCinderlineSimulation.a",
                ROOT / "Saved/P1TacticalBuild/CinderlineMatchWorker"]
require(all(p.is_file() for p in binary_paths), "Missing tested binary")
receipt = {
    "schema": "cinderline.tactical_order_verification.v1",
    "verified_at_utc": datetime.now(timezone.utc).isoformat(),
    "status": "locally verified P1.1 implementation; physical acceptance and remaining P1 features open",
    "git_head_context": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
    "dirty_checkout": True, "source_manifest_sha256": sha(HERE / "source-sha256.json"),
    "source_file_count": len(source_manifest), "binary_sha256": {str(p.relative_to(ROOT)): sha(p) for p in binary_paths},
    "compatibility": {"save": 11, "protocol": 8, "legacy_saves_supported": "1-10"},
    "checks": {"release_suites": 19, "sanitizer_suites": 19, "final_tactical_save10_reruns": 2,
               "server_tests": 34, "unreal_tests": 38, "real_loopback_transport_tests": 2},
    "integration_report": integration, "transport_report": transport,
    "synthetic_navigation_arrivals": [160, 200, 400], "synthetic_queue_units": [16, 160, 200],
    "paid_workloads": paid, "render_captures": preview["cases"], "protected_player_state_unchanged": True,
    "not_accepted": ["native mouse/touch full playtest", "current iPhone package",
                     "device CPU/GPU/thermal budget", "physical LAN/Internet match", "public deployment",
                     "Android package", "P1 patrol/escort and formation facing", "SC2 capability parity"],
}
(HERE / "verification.json").write_text(json.dumps(receipt, indent=2) + "\n")
print("Verified 19 Release + 19 sanitizer suites, final save10 checks, 34 server tests, 38 Unreal tests,")
print("2 real transport tests, complete synthetic/paid workloads and four visually reviewed Mac captures.")
