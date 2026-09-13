#!/usr/bin/env python3

from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import time


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "artifacts/metalfx/mac"
OUTPUT.mkdir(parents=True, exist_ok=True)
ENGINE_ROOT = Path(
    os.environ.get("UE_ROOT", ROOT.parent / "CinderlineEngineIOS27")
).expanduser().resolve()
if ENGINE_ROOT.name == "Engine":
    ENGINE_ROOT = ENGINE_ROOT.parent
EDITOR = (
    ENGINE_ROOT
    / "Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"
)
STOCK_EDITOR = Path(
    "/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/"
    "UnrealEditor.app/Contents/MacOS/UnrealEditor"
)
PROJECT_PLUGIN_BINARY = ROOT / "Plugins/CinderMetalFX/Binaries/Mac/libUnrealEditor-CinderMetalFX.dylib"
UNUSED_SHADER_THREADS = max(1, (os.cpu_count() or 2) - 1)
ALL_RENDER_CASES = (
    ("on-1080", "on", 1920, 1080, False),
    ("off-1080", "off", 1920, 1080, False),
    ("on-phone", "on", 956, 440, True),
)
parser = argparse.ArgumentParser(description="Verify Cinderline MetalFX rendering on Mac")
parser.add_argument(
    "--case",
    choices=("on-1080", "off-1080", "on-phone", "menu-1080", "stock-fallback"),
    help="run one explicit case while debugging; the default runs the four prepared-engine cases",
)
arguments = parser.parse_args()
CASES = tuple(case for case in ALL_RENDER_CASES if arguments.case in (None, case[0]))
PROTECTED = tuple(
    ROOT / "Saved" / relative
    for relative in (
        "Matches/skirmish.cinder",
        "Config/Training.ini",
        "Config/Skirmish.ini",
    )
)
FAILURE_PATTERN = re.compile(
    r"Fatal error:|Assertion failed:|Handled ensure condition failed|Ensure condition failed:|"
    r"CINDERLINE_METALFX_PREVIEW (?:aborted|refused|usage=)|"
    r"CINDERLINE_METALFX_MENU_PREVIEW (?:aborted|refused|failed=)|"
    r"CINDERLINE_COMMAND_PREVIEW (?:failed|refused)="
)
RESULT_PATTERN = re.compile(
    r"CINDERLINE_METALFX_PREVIEW result "
    r"mode=(on|off) enabled=(-?\d+) screen_percentage=(-?\d+) taa_upsampling=(-?\d+) "
    r"fixture=(\S+) warmup_frames=(\d+) sample_frames=(\d+) gpu_samples=(\d+) "
    r"gpu_mean_ms=([\d.]+) gpu_median_ms=([\d.]+) gpu_p95_ms=([\d.]+) "
    r"gpu_disjoint=(\d+) viewport_px=(\d+)x(\d+) render_target_px=(\d+)x(\d+) "
    r"world_rendering_disabled=(\d+) menu_probe=(\d+) status_emitted=(\d+) "
    r"elapsed_seconds=([\d.]+) screenshot=([^\r\n]+)"
)
STATUS_PATTERN = re.compile(
    r"CINDER_METALFX_STATUS requested=(\d+) metal_rhi=(\d+) os_supported=(\d+) "
    r"device_supported=(\d+) bridge_available=(\d+) interface_active=(\d+) "
    r"requested_percent=([\d.]+) effective_fraction=([\d.]+) "
    r"registered_frames=(\d+) encoded_frames=(\d+) scaler_creations=(\d+) "
    r"validation_probes=(\d+) fallbacks=(\d+) "
    r"last_input=(\d+)x(\d+) last_output=(\d+)x(\d+) fallback_reason=(\S+)"
)
MENU_RESULT_PATTERN = re.compile(
    r"CINDERLINE_METALFX_MENU_PREVIEW result passed=(\d+) menu=(\d+) "
    r"observation_seconds=([\d.]+) encoded_start=(\d+) encoded_finish=(\d+) "
    r"encoded_delta=(\d+) viewport_px=(\d+)x(\d+) render_target_px=(\d+)x(\d+) "
    r"world_rendering_disabled=(\d+) status_emitted=(\d+) elapsed_seconds=([\d.]+) "
    r"screenshot=([^\r\n]+)"
)


def file_sha(path: Path):
    if not path.exists():
        return None
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def protected_hashes():
    return {
        str(path.relative_to(ROOT / "Saved")): file_sha(path)
        for path in PROTECTED
    }


def verify_protected_state(before):
    after = protected_hashes()
    assert before == after, "MetalFX preview changed a player save or preference"
    (OUTPUT / "protected-player-state.json").write_text(
        json.dumps({"unchanged": True, "before": before, "after": after}, indent=2)
        + "\n"
    )


def editor_pids():
    result = subprocess.run(
        ["pgrep", "-x", "UnrealEditor"], capture_output=True, text=True, check=False
    )
    return {int(value) for value in result.stdout.split() if value.isdigit()}


def png_dimensions(path: Path):
    result = subprocess.run(
        ["sips", "-g", "pixelWidth", "-g", "pixelHeight", str(path)],
        capture_output=True,
        text=True,
        check=True,
    )
    width = re.search(r"pixelWidth: (\d+)", result.stdout)
    height = re.search(r"pixelHeight: (\d+)", result.stdout)
    assert width and height, (path, result.stdout)
    return int(width.group(1)), int(height.group(1))


def complete_png_size(path: Path, started_at: float):
    if not path.exists() or path.stat().st_mtime <= started_at:
        return None
    data = path.read_bytes()
    png_iend = b"\x00\x00\x00\x00IEND\xaeB\x60\x82"
    return len(data) if len(data) >= 24 and data.endswith(png_iend) else None


def parse_native_status(match):
    values = match.groups()
    return {
        "requested": int(values[0]),
        "metalRHI": int(values[1]),
        "osSupported": int(values[2]),
        "deviceSupported": int(values[3]),
        "bridgeAvailable": int(values[4]),
        "interfaceActive": int(values[5]),
        "requestedPercent": float(values[6]),
        "effectiveFraction": float(values[7]),
        "registeredFrames": int(values[8]),
        "encodedFrames": int(values[9]),
        "scalerCreations": int(values[10]),
        "validationProbes": int(values[11]),
        "fallbacks": int(values[12]),
        "lastInput": [int(values[13]), int(values[14])],
        "lastOutput": [int(values[15]), int(values[16])],
        "fallbackReason": values[17],
    }


def last_quality_cvar(log: str, name: str):
    values = re.findall(
        rf"CINDERLINE_QUALITY_CVAR name={re.escape(name)} value=([^\s]+)", log
    )
    assert values, f"Missing quality CVar: {name}"
    return values[-1]


def verify_parser_fixture():
    status_line = (
        "CINDER_METALFX_STATUS requested=1 metal_rhi=1 os_supported=1 "
        "device_supported=1 bridge_available=1 interface_active=1 requested_percent=80.00 "
        "effective_fraction=0.8000 registered_frames=245 encoded_frames=242 "
        "scaler_creations=1 validation_probes=1 fallbacks=0 last_input=1536x864 "
        "last_output=1920x1080 fallback_reason=none"
    )
    match = STATUS_PATTERN.fullmatch(status_line)
    assert match, "Native MetalFX status fixture no longer matches the parser"
    parsed = parse_native_status(match)
    assert parsed == {
        "requested": 1,
        "metalRHI": 1,
        "osSupported": 1,
        "deviceSupported": 1,
        "bridgeAvailable": 1,
        "interfaceActive": 1,
        "requestedPercent": 80.0,
        "effectiveFraction": 0.8,
        "registeredFrames": 245,
        "encodedFrames": 242,
        "scalerCreations": 1,
        "validationProbes": 1,
        "fallbacks": 0,
        "lastInput": [1536, 864],
        "lastOutput": [1920, 1080],
        "fallbackReason": "none",
    }
    fallback_line = (
        "CINDER_METALFX_STATUS requested=1 metal_rhi=1 os_supported=1 "
        "device_supported=1 bridge_available=0 interface_active=0 requested_percent=80.00 "
        "effective_fraction=1.0000 registered_frames=0 encoded_frames=0 "
        "scaler_creations=0 validation_probes=0 fallbacks=0 last_input=0x0 "
        "last_output=0x0 fallback_reason=engine_bridge_unavailable"
    )
    fallback_match = STATUS_PATTERN.fullmatch(fallback_line)
    assert fallback_match, "Stock fallback status fixture no longer matches the parser"
    fallback = parse_native_status(fallback_match)
    assert fallback["bridgeAvailable"] == 0
    assert fallback["interfaceActive"] == 0
    assert fallback["encodedFrames"] == 0
    assert fallback["validationProbes"] == 0
    assert fallback["fallbacks"] == 0
    assert fallback["fallbackReason"] == "engine_bridge_unavailable"
    assert last_quality_cvar(
        "CINDERLINE_QUALITY_CVAR name=r.ScreenPercentage value=100 set_by=SystemSettingsIni",
        "r.ScreenPercentage",
    ) == "100"


verify_parser_fixture()
if arguments.case == "stock-fallback":
    assert STOCK_EDITOR.is_file(), STOCK_EDITOR
    assert PROJECT_PLUGIN_BINARY.is_file(), PROJECT_PLUGIN_BINARY
else:
    assert EDITOR.is_file(), EDITOR
assert not editor_pids(), "An UnrealEditor process is already running; leave it untouched"
protected_before = protected_hashes()

for label, mode, requested_width, requested_height, compact_hud in CASES:
    assert not editor_pids(), f"{label}: an UnrealEditor process is already running; leave it untouched"
    runtime_log = OUTPUT / f"{label}-runtime.txt"
    stdout_log = OUTPUT / f"{label}-stdout.txt"
    artifact_shot = OUTPUT / f"{label}.png"
    verification_path = OUTPUT / f"{label}-verification.json"
    engine_shot = ROOT / "Saved/MetalFX" / f"{mode}-{requested_width}x{requested_height}.png"
    for stale in (runtime_log, stdout_log, artifact_shot, verification_path, engine_shot):
        stale.unlink(missing_ok=True)

    command = [
        str(EDITOR),
        str(ROOT / "Cinderline.uproject"),
        "-game",
        "-windowed",
        "-ForceRes",
        f"-ResX={requested_width}",
        f"-ResY={requested_height}",
        "-nosound",
        "-nosplash",
        "-unattended",
        f"-ini:Engine:[DevOptions.Shaders]:NumUnusedShaderCompilingThreads={UNUSED_SHADER_THREADS}",
        f"-ini:Engine:[DevOptions.Shaders]:NumUnusedShaderCompilingThreadsDuringGame={UNUSED_SHADER_THREADS}",
        f"-ExecCmds=t.MaxFPS 30,cinder.metalfxpreview {mode} 120",
        "-AssetRegistryCacheRootFolder=" + str(ROOT / "Saved/MetalFX/PreviewRegistry"),
        "-abslog=" + str(runtime_log),
    ]
    if compact_hud:
        command.append("-mobilehud")
    started_at = time.time()
    marker = None
    current_log = ""
    last_complete_size = None
    stable_complete_polls = 0
    with stdout_log.open("w") as stdout:
        process = subprocess.Popen(
            command,
            cwd=ROOT,
            stdout=stdout,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )
        owned_pid = process.pid
        print(f"{label}: PID {owned_pid}", flush=True)
        try:
            while process.poll() is None and time.time() - started_at < 180:
                current_log = (
                    runtime_log.read_text(errors="replace")
                    if runtime_log.exists() and runtime_log.stat().st_mtime > started_at
                    else ""
                )
                failure = FAILURE_PATTERN.search(current_log)
                if failure:
                    raise AssertionError(f"{label}: runtime failure: {failure.group(0)}")
                marker = RESULT_PATTERN.search(current_log)
                marker_shot = Path(marker.group(21).strip()) if marker else None
                complete_size = complete_png_size(marker_shot, started_at) if marker_shot else None
                if complete_size is not None and complete_size == last_complete_size:
                    stable_complete_polls += 1
                else:
                    stable_complete_polls = 1 if complete_size is not None else 0
                last_complete_size = complete_size
                if marker and stable_complete_polls >= 2:
                    break
                time.sleep(0.5)
            assert marker, f"{label}: no completion marker; see {runtime_log}"
        finally:
            # This direct Popen PID is the only process this verifier terminates.
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()

    fields = marker.groups()
    observed_mode = fields[0]
    enabled, screen_percentage, taa_upsampling = map(int, fields[1:4])
    fixture = fields[4]
    warmup_frames, sample_frames, gpu_samples = map(int, fields[5:8])
    gpu_mean, gpu_median, gpu_p95 = map(float, fields[8:11])
    gpu_disjoint = int(fields[11])
    viewport = tuple(map(int, fields[12:14]))
    render_target = tuple(map(int, fields[14:16]))
    world_rendering_disabled, menu_probe, status_emitted = map(int, fields[16:19])
    elapsed_seconds = float(fields[19])
    screenshot = Path(fields[20].strip())

    assert observed_mode == mode, (label, observed_mode)
    assert enabled == (1 if mode == "on" else 0), (label, enabled)
    assert screen_percentage == 80 and taa_upsampling == 0, (
        label,
        screen_percentage,
        taa_upsampling,
    )
    assert fixture == "army" and warmup_frames == 30 and sample_frames == 120, fields
    assert gpu_samples > 0 and min(gpu_mean, gpu_median, gpu_p95) > 0, fields
    assert viewport == (requested_width, requested_height), (label, viewport)
    assert min(render_target) > 0, (label, render_target)
    assert world_rendering_disabled == 0 and menu_probe == 0 and status_emitted == 1, fields
    assert screenshot.resolve() == engine_shot.resolve(), (screenshot, engine_shot)
    assert screenshot.exists() and screenshot.stat().st_mtime > started_at, screenshot
    assert f"CINDERLINE_COMMAND_PREVIEW state=army " in current_log
    assert "CINDERLINE_QUALITY map=" in current_log
    assert "CINDERLINE_METALFX_STATUS phase=start command_processed=1" in current_log
    assert "CINDERLINE_METALFX_STATUS phase=finish command_processed=1" in current_log
    native_statuses = [parse_native_status(match) for match in STATUS_PATTERN.finditer(current_log)]
    assert len(native_statuses) >= 3, (label, native_statuses)
    native_start, native_finish = native_statuses[0], native_statuses[-1]
    assert native_finish["metalRHI"] == 1
    assert native_finish["osSupported"] == 1
    assert native_finish["bridgeAvailable"] == 1
    assert native_finish["requestedPercent"] == 80.0
    registered_delta = native_finish["registeredFrames"] - native_start["registeredFrames"]
    encoded_delta = native_finish["encodedFrames"] - native_start["encodedFrames"]
    validation_probe_delta = native_finish["validationProbes"] - native_start["validationProbes"]
    fallback_delta = native_finish["fallbacks"] - native_start["fallbacks"]
    assert validation_probe_delta >= 0, (label, native_start, native_finish)
    assert fallback_delta == 0, (label, native_start, native_finish)
    if mode == "on":
        assert native_finish["deviceSupported"] == 1
        assert native_finish["requested"] == 1 and native_finish["interfaceActive"] == 1
        assert registered_delta >= sample_frames and encoded_delta >= sample_frames, (
            label,
            native_start,
            native_finish,
        )
        assert native_finish["scalerCreations"] >= 1
        assert native_finish["validationProbes"] >= 1
        assert 0.79 <= native_finish["effectiveFraction"] <= 0.81
        assert native_finish["lastOutput"] == list(viewport)
        assert native_finish["fallbackReason"] == "none"
    else:
        assert native_finish["requested"] == 0 and native_finish["interfaceActive"] == 0
        assert registered_delta == 0 and encoded_delta == 0, (
            label,
            native_start,
            native_finish,
        )
        assert validation_probe_delta == 0, (label, native_start, native_finish)
        assert native_finish["fallbackReason"] == "disabled"
    assert not FAILURE_PATTERN.search(current_log)
    assert png_dimensions(screenshot) == viewport, (screenshot, png_dimensions(screenshot), viewport)
    shutil.copy2(screenshot, artifact_shot)

    result = {
        "case": label,
        "mode": mode,
        "requestedViewport": [requested_width, requested_height],
        "viewport": list(viewport),
        "renderTarget": list(render_target),
        "compactHUD": compact_hud,
        "warmupFrames": warmup_frames,
        "sampleFrames": sample_frames,
        "gpuTiming": {
            "source": "FRHIGPUFrameTimeHistory completed frames",
            "samples": gpu_samples,
            "meanMs": gpu_mean,
            "medianMs": gpu_median,
            "p95Ms": gpu_p95,
            "disjointEvents": gpu_disjoint,
        },
        "elapsedSeconds": elapsed_seconds,
        "screenPercentage": screen_percentage,
        "taaUpsampling": taa_upsampling,
        "statusCommandProcessed": True,
        "nativeStatus": {
            "start": native_start,
            "finish": native_finish,
            "registeredFrameDelta": registered_delta,
            "encodedFrameDelta": encoded_delta,
            "validationProbeDelta": validation_probe_delta,
            "fallbackDelta": fallback_delta,
        },
        "capture": artifact_shot.name,
        "captureSha256": file_sha(artifact_shot),
        "ownedPid": owned_pid,
        "scope": (
            "Mac Unreal renderer with deterministic army fixture; no physical device, "
            "native iOS-on-Mac, player save, preference, or AEON interaction"
        ),
    }
    verification_path.write_text(json.dumps(result, indent=2) + "\n")
    print(
        f"{label}: {gpu_samples} completed GPU timings and {viewport[0]}x{viewport[1]} capture pass",
        flush=True,
    )

if arguments.case == "stock-fallback":
    label = "stock-fallback"
    mode = "on"
    requested_width, requested_height = 1920, 1080
    assert not editor_pids(), f"{label}: an UnrealEditor process is already running; leave it untouched"
    runtime_log = OUTPUT / f"{label}-runtime.txt"
    stdout_log = OUTPUT / f"{label}-stdout.txt"
    artifact_shot = OUTPUT / f"{label}.png"
    verification_path = OUTPUT / f"{label}-verification.json"
    engine_shot = ROOT / "Saved/MetalFX" / f"{mode}-{requested_width}x{requested_height}.png"
    for stale in (runtime_log, stdout_log, artifact_shot, verification_path, engine_shot):
        stale.unlink(missing_ok=True)

    command = [
        str(STOCK_EDITOR),
        str(ROOT / "Cinderline.uproject"),
        "-game",
        "-windowed",
        "-ForceRes",
        f"-ResX={requested_width}",
        f"-ResY={requested_height}",
        "-nosound",
        "-nosplash",
        "-unattended",
        f"-ini:Engine:[DevOptions.Shaders]:NumUnusedShaderCompilingThreads={UNUSED_SHADER_THREADS}",
        f"-ini:Engine:[DevOptions.Shaders]:NumUnusedShaderCompilingThreadsDuringGame={UNUSED_SHADER_THREADS}",
        "-ExecCmds=t.MaxFPS 30,cinder.metalfxpreview on 120",
        "-AssetRegistryCacheRootFolder=" + str(ROOT / "Saved/MetalFX/StockFallbackRegistry"),
        "-abslog=" + str(runtime_log),
    ]
    started_at = time.time()
    marker = None
    current_log = ""
    last_complete_size = None
    stable_complete_polls = 0
    with stdout_log.open("w") as stdout:
        process = subprocess.Popen(
            command,
            cwd=ROOT,
            # The project plugin embeds its build engine as an LC_RPATH. Prefer
            # stock dylibs explicitly so this case really tests a missing bridge.
            env=dict(os.environ, DYLD_LIBRARY_PATH=str(STOCK_EDITOR.parents[3])),
            stdout=stdout,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )
        owned_pid = process.pid
        print(f"{label}: PID {owned_pid}", flush=True)
        try:
            while process.poll() is None and time.time() - started_at < 180:
                current_log = (
                    runtime_log.read_text(errors="replace")
                    if runtime_log.exists() and runtime_log.stat().st_mtime > started_at
                    else ""
                )
                failure = FAILURE_PATTERN.search(current_log)
                if failure:
                    raise AssertionError(f"{label}: runtime failure: {failure.group(0)}")
                marker = RESULT_PATTERN.search(current_log)
                marker_shot = Path(marker.group(21).strip()) if marker else None
                complete_size = complete_png_size(marker_shot, started_at) if marker_shot else None
                if complete_size is not None and complete_size == last_complete_size:
                    stable_complete_polls += 1
                else:
                    stable_complete_polls = 1 if complete_size is not None else 0
                last_complete_size = complete_size
                if marker and stable_complete_polls >= 2:
                    break
                time.sleep(0.5)
            assert marker, f"{label}: no completion marker; see {runtime_log}"
        finally:
            # This stock-editor Popen PID is the only process this verifier terminates.
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()

    fields = marker.groups()
    observed_mode = fields[0]
    enabled, screen_percentage, taa_upsampling = map(int, fields[1:4])
    fixture = fields[4]
    warmup_frames, sample_frames, gpu_samples = map(int, fields[5:8])
    gpu_mean, gpu_median, gpu_p95 = map(float, fields[8:11])
    gpu_disjoint = int(fields[11])
    viewport = tuple(map(int, fields[12:14]))
    render_target = tuple(map(int, fields[14:16]))
    world_rendering_disabled, menu_probe, status_emitted = map(int, fields[16:19])
    elapsed_seconds = float(fields[19])
    screenshot = Path(fields[20].strip())
    assert observed_mode == "on" and enabled == 1 and screen_percentage == 80, fields
    assert taa_upsampling == 0 and fixture == "army", fields
    assert warmup_frames == 30 and sample_frames == 120, fields
    assert gpu_samples > 0 and min(gpu_mean, gpu_median, gpu_p95) > 0, fields
    assert viewport == (requested_width, requested_height) and min(render_target) > 0, fields
    assert world_rendering_disabled == 0 and menu_probe == 0 and status_emitted == 1, fields
    assert screenshot.resolve() == engine_shot.resolve(), (screenshot, engine_shot)
    assert screenshot.exists() and screenshot.stat().st_mtime > started_at
    assert png_dimensions(screenshot) == viewport
    assert f"CINDERLINE_COMMAND_PREVIEW state=army " in current_log
    assert "CINDERLINE_QUALITY map=" in current_log
    assert float(last_quality_cvar(current_log, "r.ScreenPercentage")) == 100.0
    assert "CINDERLINE_METALFX_STATUS phase=start command_processed=1" in current_log
    assert "CINDERLINE_METALFX_STATUS phase=finish command_processed=1" in current_log
    native_statuses = [parse_native_status(match) for match in STATUS_PATTERN.finditer(current_log)]
    assert len(native_statuses) >= 3, native_statuses
    native_start, native_finish = native_statuses[0], native_statuses[-1]
    assert native_finish["requested"] == 1
    assert native_finish["metalRHI"] == 1 and native_finish["osSupported"] == 1
    assert native_finish["bridgeAvailable"] == 0 and native_finish["interfaceActive"] == 0
    assert native_finish["requestedPercent"] == 80.0
    assert native_finish["effectiveFraction"] == 1.0
    assert native_finish["registeredFrames"] == 0
    assert native_finish["encodedFrames"] == 0
    assert native_finish["scalerCreations"] == 0
    assert native_finish["validationProbes"] == 0
    assert native_finish["fallbacks"] == 0
    assert native_finish["fallbackReason"] == "engine_bridge_unavailable"
    assert not FAILURE_PATTERN.search(current_log)
    shutil.copy2(screenshot, artifact_shot)
    verification_path.write_text(
        json.dumps(
            {
                "case": label,
                "editor": str(STOCK_EDITOR),
                "engineLibraryOverride": str(STOCK_EDITOR.parents[3]),
                "pluginBinary": str(PROJECT_PLUGIN_BINARY),
                "pluginBinarySha256": file_sha(PROJECT_PLUGIN_BINARY),
                "mode": mode,
                "viewport": list(viewport),
                "renderTarget": list(render_target),
                "nativeScreenPercentage": 100.0,
                "warmupFrames": warmup_frames,
                "sampleFrames": sample_frames,
                "gpuTiming": {
                    "source": "FRHIGPUFrameTimeHistory completed frames",
                    "samples": gpu_samples,
                    "meanMs": gpu_mean,
                    "medianMs": gpu_median,
                    "p95Ms": gpu_p95,
                    "disjointEvents": gpu_disjoint,
                },
                "nativeStatus": {"start": native_start, "finish": native_finish},
                "expectedFallback": "engine_bridge_unavailable",
                "elapsedSeconds": elapsed_seconds,
                "capture": artifact_shot.name,
                "captureSha256": file_sha(artifact_shot),
                "ownedPid": owned_pid,
                "scope": (
                    "Stock UE 5.8 editor loading the project-built MetalFX plugin; validates "
                    "native-resolution fallback only, with no physical device or AEON interaction"
                ),
            },
            indent=2,
        )
        + "\n"
    )
    print(f"{label}: stock editor retained native 100% fallback with zero native work", flush=True)

if arguments.case is not None and arguments.case != "menu-1080":
    verify_protected_state(protected_before)
    raise SystemExit(0)

menu_label = "menu-1080"
menu_width, menu_height = 1920, 1080
assert not editor_pids(), "menu-1080: an UnrealEditor process is already running; leave it untouched"
menu_runtime = OUTPUT / f"{menu_label}-runtime.txt"
menu_stdout = OUTPUT / f"{menu_label}-stdout.txt"
menu_artifact_shot = OUTPUT / f"{menu_label}.png"
menu_verification = OUTPUT / f"{menu_label}-verification.json"
menu_engine_shot = ROOT / "Saved/MetalFX" / f"menu-{menu_width}x{menu_height}.png"
for stale in (
    menu_runtime,
    menu_stdout,
    menu_artifact_shot,
    menu_verification,
    menu_engine_shot,
):
    stale.unlink(missing_ok=True)

menu_command = [
    str(EDITOR),
    str(ROOT / "Cinderline.uproject"),
    "-game",
    "-windowed",
    "-ForceRes",
    f"-ResX={menu_width}",
    f"-ResY={menu_height}",
    "-nosound",
    "-nosplash",
    f"-ini:Engine:[DevOptions.Shaders]:NumUnusedShaderCompilingThreads={UNUSED_SHADER_THREADS}",
    f"-ini:Engine:[DevOptions.Shaders]:NumUnusedShaderCompilingThreadsDuringGame={UNUSED_SHADER_THREADS}",
    "-ExecCmds=t.MaxFPS 30,cinder.metalfxmenupreview",
    "-AssetRegistryCacheRootFolder=" + str(ROOT / "Saved/MetalFX/MenuPreviewRegistry"),
    "-abslog=" + str(menu_runtime),
]
menu_started_at = time.time()
menu_marker = None
menu_log = ""
last_complete_size = None
stable_complete_polls = 0
with menu_stdout.open("w") as stdout:
    menu_process = subprocess.Popen(
        menu_command,
        cwd=ROOT,
        stdout=stdout,
        stderr=subprocess.STDOUT,
        start_new_session=True,
    )
    menu_pid = menu_process.pid
    print(f"{menu_label}: PID {menu_pid}", flush=True)
    try:
        while menu_process.poll() is None and time.time() - menu_started_at < 180:
            menu_log = (
                menu_runtime.read_text(errors="replace")
                if menu_runtime.exists() and menu_runtime.stat().st_mtime > menu_started_at
                else ""
            )
            failure = FAILURE_PATTERN.search(menu_log)
            if failure:
                raise AssertionError(f"{menu_label}: runtime failure: {failure.group(0)}")
            menu_marker = MENU_RESULT_PATTERN.search(menu_log)
            marker_shot = Path(menu_marker.group(14).strip()) if menu_marker else None
            complete_size = complete_png_size(marker_shot, menu_started_at) if marker_shot else None
            if complete_size is not None and complete_size == last_complete_size:
                stable_complete_polls += 1
            else:
                stable_complete_polls = 1 if complete_size is not None else 0
            last_complete_size = complete_size
            if menu_marker and stable_complete_polls >= 2:
                break
            time.sleep(0.5)
        assert menu_marker, f"{menu_label}: no completion marker; see {menu_runtime}"
    finally:
        # This direct Popen PID is the only process this verifier terminates.
        if menu_process.poll() is None:
            menu_process.terminate()
            try:
                menu_process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                menu_process.kill()
                menu_process.wait()

menu_fields = menu_marker.groups()
passed, menu = map(int, menu_fields[0:2])
observation_seconds = float(menu_fields[2])
encoded_start, encoded_finish, encoded_delta = map(int, menu_fields[3:6])
menu_viewport = tuple(map(int, menu_fields[6:8]))
menu_render_target = tuple(map(int, menu_fields[8:10]))
world_disabled, menu_status_emitted = map(int, menu_fields[10:12])
menu_elapsed = float(menu_fields[12])
menu_screenshot = Path(menu_fields[13].strip())
assert passed == 1 and menu == 1 and observation_seconds == 5.0, menu_fields
assert encoded_finish == encoded_start and encoded_delta == 0, menu_fields
assert menu_viewport == (menu_width, menu_height), menu_fields
assert min(menu_render_target) > 0 and world_disabled == 1 and menu_status_emitted == 1, menu_fields
assert menu_elapsed >= 6.0, menu_fields
assert menu_screenshot.resolve() == menu_engine_shot.resolve(), (menu_screenshot, menu_engine_shot)
assert menu_screenshot.exists() and menu_screenshot.stat().st_mtime > menu_started_at
assert png_dimensions(menu_screenshot) == menu_viewport
assert "CINDERLINE_QUALITY map=" in menu_log
assert "CINDERLINE_METALFX_STATUS phase=menu_start command_processed=1" in menu_log
assert "CINDERLINE_METALFX_STATUS phase=menu_finish command_processed=1" in menu_log
menu_statuses = [parse_native_status(match) for match in STATUS_PATTERN.finditer(menu_log)]
assert len(menu_statuses) >= 3, menu_statuses
assert menu_statuses[-1]["encodedFrames"] == menu_statuses[0]["encodedFrames"], menu_statuses
assert not FAILURE_PATTERN.search(menu_log)
shutil.copy2(menu_screenshot, menu_artifact_shot)
menu_verification.write_text(
    json.dumps(
        {
            "case": menu_label,
            "menu": True,
            "observationSeconds": observation_seconds,
            "viewport": list(menu_viewport),
            "renderTarget": list(menu_render_target),
            "worldRenderingDisabled": True,
            "encodedFrames": {
                "start": encoded_start,
                "finish": encoded_finish,
                "delta": encoded_delta,
            },
            "nativeStatus": {"start": menu_statuses[0], "finish": menu_statuses[-1]},
            "elapsedSeconds": menu_elapsed,
            "statusCommandProcessed": True,
            "capture": menu_artifact_shot.name,
            "captureSha256": file_sha(menu_artifact_shot),
            "ownedPid": menu_pid,
            "scope": (
                "Normal Mac menu with read-only rendering observation; no input, match, "
                "physical device, native iOS-on-Mac, player save, preference, or AEON interaction"
            ),
        },
        indent=2,
    )
    + "\n"
)
print(f"{menu_label}: world rendering disabled and encoded-frame delta stayed zero", flush=True)

verify_protected_state(protected_before)
