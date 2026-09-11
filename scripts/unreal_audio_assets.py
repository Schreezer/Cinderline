"""Deferred UE 5.8 SoundWave import. Importing this module performs no asset work.

Call import_audio(project_root=None, destination='/Game/Art/Audio') explicitly
inside Unreal Editor. It imports/saves assets but never starts playback.
"""

from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path
import wave

NAMES = (
    "UI_Click", "Order_Ack", "Order_Invalid", "Unit_Ready", "Building_Ready",
    "Weapon_Pulse", "Impact", "Explosion",
)


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError("Cinderline audio assets: " + message)


def _source_metadata(project_root: Path) -> list[dict]:
    directory = project_root / "RawAssets" / "Audio"
    manifest = json.loads((directory / "manifest.json").read_text())
    rows = manifest.get("assets", [])
    _require(len(rows) == len(NAMES) and {row.get("name") for row in rows} == set(NAMES),
             "manifest must contain all eight expected cues exactly once")
    by_name = {row["name"]: row for row in rows}
    verified = []
    for name in NAMES:
        filename = directory / (name + ".wav")
        source = filename.read_bytes()
        _require(hashlib.sha256(source).hexdigest() == by_name[name]["sha256"], name + " source hash mismatch")
        with wave.open(str(filename), "rb") as wav:
            channels, rate, width = wav.getnchannels(), wav.getframerate(), wav.getsampwidth()
            duration = wav.getnframes() / rate
            _require(wav.getcomptype() == "NONE", name + " must be uncompressed PCM")
        _require((channels, rate, width) == (1, 48000, 2), name + " must be mono 48 kHz / 16-bit WAV")
        _require(math.isfinite(duration) and 0 < duration < 1, name + " has an invalid duration")
        _require(abs(duration - by_name[name]["duration_seconds"]) <= 1 / rate, name + " manifest duration mismatch")
        verified.append({"name": name, "filename": str(filename), "channels": channels,
                         "sample_rate_hz": rate, "duration_seconds": duration,
                         "source_sha256": by_name[name]["sha256"]})
    return verified


def import_audio(project_root: str | Path | None = None, destination: str = "/Game/Art/Audio") -> dict:
    """Import and validate eight SoundWaves; return metadata without playing audio.

    UE 5.8 source exposes SoundWave.NumChannels, ImportedSampleRate, SampleRate,
    and SoundBase.Duration as editor-visible properties. get_editor_property
    reads these even when they are protected C++ members.
    """
    root = Path(project_root).resolve() if project_root is not None else Path(__file__).resolve().parents[1]
    _require(destination.startswith("/Game/") and ".." not in destination and "." not in destination,
             "destination must be a /Game package directory")
    destination = destination.rstrip("/")
    sources = _source_metadata(root)

    import unreal  # Deferred: regular Python can inspect/import this module safely.

    tasks = []
    for source in sources:
        factory = unreal.SoundFactory()
        factory.set_editor_property("auto_create_cue", False)
        factory.set_editor_property("include_looping_node", False)
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", source["filename"])
        task.set_editor_property("destination_path", destination)
        task.set_editor_property("destination_name", source["name"])
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("replace_existing_settings", True)
        task.set_editor_property("save", False)
        task.set_editor_property("factory", factory)
        tasks.append(task)

    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    validated = []
    for source, task in zip(sources, tasks):
        # UE 5.8 GetObjects() blocks if an importer chooses asynchronous work.
        objects = task.get_objects()
        object_path = destination + "/" + source["name"] + "." + source["name"]
        imported = next((obj for obj in objects if isinstance(obj, unreal.SoundWave) and obj.get_path_name() == object_path), None)
        _require(imported is not None, source["name"] + " did not import as the expected SoundWave")
        channels = int(imported.get_editor_property("num_channels"))
        imported_rate = int(imported.get_editor_property("imported_sample_rate"))
        playback_rate = int(imported.get_editor_property("sample_rate"))
        duration = float(imported.get_editor_property("duration"))
        _require(channels == 1, source["name"] + " imported with unexpected channel count")
        _require(imported_rate == 48000, source["name"] + " imported at an unexpected source sample rate")
        _require(playback_rate == 48000, source["name"] + " has an unexpected playback sample rate")
        _require(math.isfinite(duration) and abs(duration - source["duration_seconds"]) <= 0.001,
                 source["name"] + " imported duration differs from source")
        imported.set_editor_property("looping", False)
        _require(unreal.EditorAssetLibrary.save_loaded_asset(imported), "could not save " + object_path)
        validated.append({"name": source["name"], "asset": object_path, "channels": channels,
                          "imported_sample_rate_hz": imported_rate, "sample_rate_hz": playback_rate,
                          "duration_seconds": duration, "source_sha256": source["source_sha256"]})

    report = {"destination": destination, "assets": validated, "count": len(validated),
              "validation": "Imported asset metadata and source hashes; no playback or listening test"}
    unreal.log("Cinderline audio: imported and validated {} SoundWaves; no playback performed.".format(len(validated)))
    return report
