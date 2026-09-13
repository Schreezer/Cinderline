#!/usr/bin/env python3
"""Import Cinderline's rendered unit portraits inside Unreal Editor.

Usage:
  UnrealEditor-Cmd Cinderline.uproject /Engine/Maps/Entry \
    -ExecutePythonScript=<absolute path>/scripts/unreal_unit_portraits.py \
    -unattended -nop4 -nosound -NullRHI

Importing this module outside Unreal has no asset side effects. Call
``import_unit_portraits`` explicitly when composing it into another batch.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "RawAssets" / "UI" / "Units"
DESTINATION = "/Game/Art/UI/Units"
EXPECTED = ("Drudge", "Ember", "Needle", "Skim", "Anvil", "Cinderthrow", "Mend", "Veil", "Ward")
SOURCE_HASH_TAG = "CinderlinePortraitSourceSHA256"
EXPECTED_SIZE = (384, 384)


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError("Cinderline unit portraits: " + message)


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _png_size(path: Path) -> tuple[int, int]:
    header = path.read_bytes()[:24]
    _require(len(header) == 24 and header[:8] == b"\x89PNG\r\n\x1a\n" and header[12:16] == b"IHDR",
             path.name + " is not a valid PNG source")
    return int.from_bytes(header[16:20], "big"), int.from_bytes(header[20:24], "big")


def _validated_sources() -> list[dict]:
    manifest_path = SOURCE / "manifest.json"
    _require(manifest_path.is_file(), "source manifest is missing")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    rows = manifest.get("assets", [])
    _require(len(rows) == len(EXPECTED), "manifest must contain nine portrait records")
    by_name = {row.get("display_name"): row for row in rows}
    _require(len(by_name) == len(EXPECTED) and set(by_name) == set(EXPECTED),
             "manifest portrait names do not match the HUD contract")
    ordered = []
    for display_name in EXPECTED:
        row = by_name[display_name]
        texture = ROOT / row.get("texture", "")
        _require(texture.resolve().parent == SOURCE.resolve(), display_name + " source path escapes portrait directory")
        _require(texture.name == f"T_CinderPortrait_{display_name}.png", display_name + " has an unexpected filename")
        _require(texture.is_file() and _sha256(texture) == row.get("texture_sha256"),
                 display_name + " source hash mismatch")
        _require(row.get("size") == list(EXPECTED_SIZE), display_name + " manifest dimensions changed")
        _require(_png_size(texture) == EXPECTED_SIZE, display_name + " PNG dimensions changed")
        _require(row.get("unreal_asset") == f"{DESTINATION}/T_CinderPortrait_{display_name}",
                 display_name + " Unreal path changed")
        ordered.append({**row, "source": str(texture)})
    return ordered


def _string_map(values) -> dict[str, str]:
    return {str(key): str(value) for key, value in values.items()}


def _source_dimensions(unreal, asset_path: str) -> tuple[int, int] | None:
    dimensions = _string_map(unreal.EditorAssetLibrary.get_tag_values(asset_path)).get("Dimensions", "")
    parts = dimensions.lower().split("x", 1)
    if len(parts) != 2:
        return None
    try:
        return int(parts[0]), int(parts[1])
    except ValueError:
        return None


def _import_filename(texture) -> Path | None:
    import_data = texture.get_editor_property("asset_import_data")
    if import_data is None:
        return None
    filename = import_data.get_first_filename()
    return Path(filename).resolve() if filename else None


def _settings_match(unreal, texture) -> bool:
    return (
        texture.get_editor_property("srgb") is True
        and texture.get_editor_property("never_stream") is True
        and texture.get_editor_property("lod_group") == unreal.TextureGroup.TEXTUREGROUP_UI
        and texture.get_editor_property("address_x") == unreal.TextureAddress.TA_CLAMP
        and texture.get_editor_property("address_y") == unreal.TextureAddress.TA_CLAMP
    )


def _apply_settings(unreal, texture) -> None:
    texture.set_editor_property("srgb", True)
    texture.set_editor_property("never_stream", True)
    texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
    texture.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP)
    texture.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)


def _make_import_task(unreal, source: dict):
    factory = unreal.TextureFactory()
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", source["source"])
    task.set_editor_property("destination_path", DESTINATION)
    task.set_editor_property("destination_name", "T_CinderPortrait_" + source["display_name"])
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("replace_existing_settings", True)
    task.set_editor_property("save", False)
    task.set_editor_property("factory", factory)
    return task


def import_unit_portraits(project_root: str | Path | None = None) -> dict:
    global ROOT, SOURCE
    if project_root is not None:
        ROOT = Path(project_root).resolve()
        SOURCE = ROOT / "RawAssets" / "UI" / "Units"
    sources = _validated_sources()

    import unreal

    unreal.EditorAssetLibrary.make_directory(DESTINATION)
    pending = []
    results = {}
    for source in sources:
        name = "T_CinderPortrait_" + source["display_name"]
        object_path = f"{DESTINATION}/{name}.{name}"
        texture = unreal.EditorAssetLibrary.load_asset(object_path)
        if texture is None:
            pending.append((source, _make_import_task(unreal, source), "imported"))
            continue

        _require(isinstance(texture, unreal.Texture2D), object_path + " exists but is not a Texture2D")
        imported_from = _import_filename(texture)
        _require(imported_from == Path(source["source"]).resolve(),
                 object_path + " exists but was not imported from its owned portrait source")
        dimensions = _source_dimensions(unreal, object_path)
        metadata = _string_map(unreal.EditorAssetLibrary.get_metadata_tag_values(texture))
        recorded_hash = metadata.get(SOURCE_HASH_TAG)
        _require(recorded_hash is None or len(recorded_hash) == 64,
                 object_path + " has malformed portrait source metadata")

        if dimensions != EXPECTED_SIZE or (recorded_hash is not None and recorded_hash != source["texture_sha256"]):
            pending.append((source, _make_import_task(unreal, source), "reimported"))
            continue

        changed = False
        if not _settings_match(unreal, texture):
            _apply_settings(unreal, texture)
            changed = True
        if recorded_hash is None:
            unreal.EditorAssetLibrary.set_metadata_tag(texture, SOURCE_HASH_TAG, source["texture_sha256"])
            changed = True
        if changed:
            _require(unreal.EditorAssetLibrary.save_loaded_asset(texture), "could not save " + object_path)
        results[source["display_name"]] = {
            "texture": texture,
            "operation": "updated" if changed else "preserved",
        }

    if pending:
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([item[1] for item in pending])
    for source, task, operation in pending:
        name = "T_CinderPortrait_" + source["display_name"]
        object_path = f"{DESTINATION}/{name}.{name}"
        texture = next((obj for obj in task.get_objects()
                        if isinstance(obj, unreal.Texture2D) and obj.get_path_name() == object_path), None)
        _require(texture is not None, name + " did not import at the expected object path")
        _apply_settings(unreal, texture)
        unreal.EditorAssetLibrary.set_metadata_tag(texture, SOURCE_HASH_TAG, source["texture_sha256"])
        # UTexture::PreSave finishes outstanding texture compilation before the
        # package is serialized. This is the engine synchronization point; do
        # not poll BlueprintGetSize or sleep on the editor game thread.
        _require(unreal.EditorAssetLibrary.save_loaded_asset(texture), "could not save " + object_path)
        _require(_source_dimensions(unreal, object_path) == EXPECTED_SIZE,
                 name + " saved with unexpected source dimensions")
        results[source["display_name"]] = {"texture": texture, "operation": operation}

    imported = []
    for source in sources:
        name = "T_CinderPortrait_" + source["display_name"]
        object_path = f"{DESTINATION}/{name}.{name}"
        result = results[source["display_name"]]
        imported.append({
            "kind": source["kind"],
            "display_name": source["display_name"],
            "asset": object_path,
            "source_sha256": source["texture_sha256"],
            "size": list(EXPECTED_SIZE),
            "army_ribbon": source["army_ribbon"],
            "operation": result["operation"],
        })

    report = {"destination": DESTINATION, "count": len(imported), "assets": imported}
    report_path = ROOT / "Saved" / "UnitPortraits" / "import-summary.json"
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    unreal.log("CINDERLINE_UNIT_PORTRAITS_IMPORTED " + json.dumps(report, sort_keys=True))
    return report


if __name__ == "__main__":
    import_unit_portraits()
