#!/usr/bin/env python3
"""Capture scripted formation HUD fixtures serially; no physical-input claim."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "artifacts/formation-orders"
PROTECTED = [ROOT / "Saved" / name for name in
             ("Matches/skirmish.cinder", "Config/Training.ini", "Config/Skirmish.ini")]


def protected_hashes():
    return {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
            if p.exists() else None for p in PROTECTED}


def png_size(path):
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n" or len(data) < 24:
        raise RuntimeError(f"Invalid screenshot: {path}")
    return list(struct.unpack(">II", data[16:24]))


before = protected_hashes()
module = ROOT / "Binaries/Mac/libUnrealEditor-Cinderline.dylib"
module_sha256 = hashlib.sha256(module.read_bytes()).hexdigest()
existing = subprocess.run(["pgrep", "-f", r"[U]nrealEditor.*Cinderline\.uproject"],
                          capture_output=True, text=True)
if existing.returncode == 0:
    raise SystemExit("An existing Cinderline Unreal process is running; finish it before captures.")

results = []
try:
    for device, width, height in [("desktop", 1440, 900), ("mobile", 956, 440)]:
        for mode in ["formation-tight", "formation-standard", "formation-wide", "formation-facing", "formation-pending", "formation-accepted"]:
            name = f"{device}-{mode}"
            state = mode
            runtime_log = OUTPUT / f"{name}.log"
            source_shot = ROOT / "Saved/MobileHUD" / f"{state}.png"
            output_shot = OUTPUT / f"{name}.png"
            env = dict(os.environ, CINDERLINE_RES_X=str(width), CINDERLINE_RES_Y=str(height))
            unused = max(1, (os.cpu_count() or 2) - 1)
            command = [str(ROOT / "scripts/unreal.sh"), "play", "-unattended", "-nosound",
                       "-nosplash", "-ForceRes", f"-ExecCmds=t.MaxFPS 30,cinder.mobilepreview {state} shot",
                       f"-abslog={runtime_log}",
                       f"-ini:Engine:[DevOptions.Shaders]:NumUnusedShaderCompilingThreads={unused}",
                       f"-ini:Engine:[DevOptions.Shaders]:NumUnusedShaderCompilingThreadsDuringGame={unused}"]
            if device == "mobile":
                command.append("-mobilehud")
            started = time.time()
            print(f"Starting {name} at {width}x{height}", flush=True)
            with (OUTPUT / f"{name}.stdout.log").open("w") as stdout:
                process = subprocess.Popen(command, cwd=ROOT, env=env, stdout=stdout, stderr=subprocess.STDOUT)
                try:
                    stable_size = None
                    stable_polls = 0
                    while time.time() - started < 120:
                        log = runtime_log.read_text(errors="replace") if runtime_log.exists() else ""
                        if any(text in log for text in ("Fatal error:", "Assertion failed:",
                                "CINDERLINE_MOBILE_HUD_PREVIEW failed=", "CINDERLINE_MOBILE_HUD_PREVIEW refused=")):
                            raise RuntimeError(f"Preview failed; inspect {runtime_log}")
                        if (f"CINDERLINE_MOBILE_HUD_CAPTURE state={state}" in log
                                and source_shot.exists() and source_shot.stat().st_mtime > started):
                            size = source_shot.stat().st_size
                            stable_polls = stable_polls + 1 if size == stable_size and size > 1000 else 0
                            stable_size = size
                            if stable_polls >= 2:
                                if png_size(source_shot) != [width, height]:
                                    raise RuntimeError(f"Unexpected image size: {png_size(source_shot)}")
                                shutil.copy2(source_shot, output_shot)
                                markers = [line for line in log.splitlines()
                                           if "CINDERLINE_FORMATION_PREVIEW " in line]
                                if not markers:
                                    raise RuntimeError("Missing tactical fixture receipt")
                                results.append({"case": name, "viewport": [width, height],
                                                "screenshot": str(output_shot.relative_to(ROOT)),
                                                "sha256": hashlib.sha256(output_shot.read_bytes()).hexdigest(),
                                                "fixture_receipt": markers[-1]})
                                print(f"Captured {name}", flush=True)
                                break
                        if process.poll() is not None:
                            raise RuntimeError(f"Preview exited before capture ({process.returncode})")
                        time.sleep(0.5)
                    else:
                        raise RuntimeError(f"Preview timed out; inspect {runtime_log}")
                finally:
                    if process.poll() is None:
                        process.terminate()
                        try:
                            process.wait(timeout=15)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.wait(timeout=5)
            if protected_hashes() != before:
                raise RuntimeError("Preview changed protected player state")
            if hashlib.sha256(module.read_bytes()).hexdigest() != module_sha256:
                raise RuntimeError("Game module changed during preview capture")
finally:
    after = protected_hashes()
    (OUTPUT / "preview-verification.json").write_text(json.dumps({
        "schema": "cinderline.formation_preview.v1", "cases": results,
        "complete": len(results) == 12, "evidence": "scripted Mac rendering; no native gesture or iPhone acceptance",
        "module": str(module.relative_to(ROOT)), "module_sha256": module_sha256,
        "protected_unchanged": before == after, "protected_before": before, "protected_after": after,
    }, indent=2) + "\n")
