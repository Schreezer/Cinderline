#!/usr/bin/env python3
"""Prepare an isolated UE 5.8 clone for Cinderline's iOS scene lifecycle.

The stock Epic installation is never modified. The clone keeps its installed-build
marker so every engine module remains precompiled except ApplicationCore and Launch.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
from datetime import datetime, timezone


PROJECT_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_SOURCE = Path("/Users/Shared/Epic Games/UE_5.8")
# UAT classifies paths below ProjectRoot as game content before checking the
# engine root. A nested engine would silently stage Engine/Config at game paths.
DEFAULT_DESTINATION = PROJECT_ROOT.parent / "CinderlineEngineIOS27"
MANIFEST_NAME = ".cinderline-ios27-engine.json"
PATCH_MARKER = "// Cinderline iOS scene lifecycle: rebuild both entry modules together."

MODULE_RULES = {
    "ApplicationCore": Path("Engine/Source/Runtime/ApplicationCore/ApplicationCore.Build.cs"),
    "Launch": Path("Engine/Source/Runtime/Launch/Launch.Build.cs"),
}

SUPPORT_FILES = (
    Path("Engine/Build/Build.version"),
    Path("Engine/Build/InstalledBuild.txt"),
    Path("Engine/Source/Programs/UnrealBuildTool/Platform/IOS/UEBuildIOS.cs"),
    Path("Engine/Source/Runtime/ApplicationCore/Private/IOS/IOSSceneDelegate.cpp"),
    Path("Engine/Source/Runtime/ApplicationCore/Private/IOS/IOSAppDelegate.cpp"),
    Path("Engine/Source/Runtime/Launch/Private/IOS/LaunchIOS.cpp"),
    Path("Engine/Intermediate/Build/IOS/UnrealGame/Development/Core/Core.precompiled"),
)


class PreparationError(RuntimeError):
    pass


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as exc:
        raise PreparationError(f"Cannot read {path}: {exc}") from exc


def expected_patched_text(original: str, module: str) -> str:
    signature = f"\tpublic {module}(ReadOnlyTargetRules Target) : base(Target)\n\t{{\n"
    if original.count(signature) != 1:
        raise PreparationError(f"Expected one constructor anchor for {module}")
    insertion = (
        signature
        + f"\t\t{PATCH_MARKER}\n"
        + "\t\tif (Target.Platform == UnrealTargetPlatform.IOS && Target.Name == \"Cinderline\")\n"
        + "\t\t{\n"
        + "\t\t\tbUsePrecompiled = false;\n"
        + "\t\t}\n\n"
    )
    return original.replace(signature, insertion, 1)


def validate_engine(root: Path, label: str) -> dict[str, object]:
    build_version_path = root / "Engine/Build/Build.version"
    try:
        version = json.loads(read_text(build_version_path))
    except json.JSONDecodeError as exc:
        raise PreparationError(f"Invalid {label} Build.version: {exc}") from exc
    if (version.get("MajorVersion"), version.get("MinorVersion")) != (5, 8):
        raise PreparationError(f"{label} must be Unreal Engine 5.8, got {version}")

    for relative in (*SUPPORT_FILES, *MODULE_RULES.values()):
        path = root / relative
        if not path.is_file():
            raise PreparationError(f"Missing required {label} file: {path}")

    source_checks = {
        "Engine/Source/Programs/UnrealBuildTool/Platform/IOS/UEBuildIOS.cs": (
            "bUseSceneBasedLifecycle",
            "UE_IOS_SCENE_LIFECYCLE=",
        ),
        "Engine/Source/Runtime/ApplicationCore/Private/IOS/IOSSceneDelegate.cpp": (
            "@implementation IOSSceneDelegate",
            "#if UE_IOS_SCENE_LIFECYCLE",
        ),
        "Engine/Source/Runtime/ApplicationCore/Private/IOS/IOSAppDelegate.cpp": (
            "#if !UE_IOS_SCENE_LIFECYCLE",
        ),
        "Engine/Source/Runtime/Launch/Private/IOS/LaunchIOS.cpp": (
            "#if UE_IOS_SCENE_LIFECYCLE",
        ),
    }
    for relative, needles in source_checks.items():
        contents = read_text(root / relative)
        for needle in needles:
            if needle not in contents:
                raise PreparationError(f"{label} lacks required scene support in {relative}: {needle}")
    return version


def pristine_and_patched_rules(source: Path) -> dict[Path, tuple[str, str]]:
    result: dict[Path, tuple[str, str]] = {}
    for module, relative in MODULE_RULES.items():
        original = read_text(source / relative)
        if PATCH_MARKER in original:
            raise PreparationError(f"Stock engine was already modified: {source / relative}")
        result[relative] = (original, expected_patched_text(original, module))
    return result


def verify_clone_identity(
    source: Path,
    destination: Path,
    rules: dict[Path, tuple[str, str]],
) -> None:
    for relative in SUPPORT_FILES:
        source_path = source / relative
        destination_path = destination / relative
        if not destination_path.is_file() or sha256(destination_path) != sha256(source_path):
            raise PreparationError(f"Existing destination is not the expected engine clone: {destination_path}")
    for relative, (original, patched) in rules.items():
        contents = read_text(destination / relative)
        if contents not in (original, patched):
            raise PreparationError(f"Unexpected edits in clone module rule: {destination / relative}")


def validate_microsoft_descriptors(directory: Path) -> dict[str, str]:
    descriptors = sorted(directory.rglob("*.uplugin"))
    if not descriptors:
        raise PreparationError(f"No Microsoft plugin descriptors found under {directory}")
    hashes: dict[str, str] = {}
    for descriptor in descriptors:
        try:
            data = json.loads(read_text(descriptor))
        except json.JSONDecodeError as exc:
            raise PreparationError(f"Invalid plugin descriptor {descriptor}: {exc}") from exc
        if data.get("SupportedTargetPlatforms") != ["Win64"]:
            raise PreparationError(f"Refusing to exclude non-Win64 plugin: {descriptor}")
        modules = data.get("Modules")
        if not isinstance(modules, list) or not modules:
            raise PreparationError(f"Plugin has no module allow-list to validate: {descriptor}")
        for module in modules:
            if not isinstance(module, dict) or module.get("PlatformAllowList") != ["Win64"]:
                raise PreparationError(f"Refusing to exclude plugin with a non-Win64 module: {descriptor}")
        hashes[str(descriptor.relative_to(directory))] = sha256(descriptor)
    return hashes


def microsoft_quarantine_path(destination: Path) -> tuple[Path, Path]:
    active = destination / "Engine/Plugins/Online/Microsoft"
    excluded = destination / "CinderlineExcludedPlugins/Microsoft"
    return active, excluded


def verify_or_move_microsoft_plugins(source: Path, destination: Path, check_only: bool) -> dict[str, str]:
    active, excluded = microsoft_quarantine_path(destination)
    source_plugins = source / "Engine/Plugins/Online/Microsoft"
    source_hashes = validate_microsoft_descriptors(source_plugins)
    if active.exists() and excluded.exists():
        raise PreparationError("Both active and excluded Microsoft plugin trees exist; refusing to choose")
    if excluded.is_dir():
        hashes = validate_microsoft_descriptors(excluded)
        if hashes != source_hashes:
            raise PreparationError("Excluded Microsoft plugin descriptors do not match the stock engine")
        return hashes
    if not active.is_dir():
        raise PreparationError("Microsoft plugin tree is missing from both active and excluded locations")
    hashes = validate_microsoft_descriptors(active)
    if hashes != source_hashes:
        raise PreparationError("Active Microsoft plugin descriptors do not match the stock engine")
    if check_only:
        raise PreparationError(f"Win64-only Microsoft plugins are still active at {active}")
    excluded.parent.mkdir(parents=True, exist_ok=True)
    os.replace(active, excluded)
    return hashes


def make_owner_writable(path: Path) -> None:
    path.chmod(stat.S_IMODE(path.stat().st_mode) | stat.S_IWUSR)


def verify_writable_build_inputs(destination: Path) -> None:
    for relative in MODULE_RULES.values():
        path = destination / relative
        if not stat.S_IMODE(path.stat().st_mode) & stat.S_IWUSR:
            raise PreparationError(f"Module rule is not owner-writable: {path}")
    build_rules = destination / "Engine/Intermediate/Build/BuildRules"
    if not build_rules.is_dir() or not stat.S_IMODE(build_rules.stat().st_mode) & stat.S_IWUSR:
        raise PreparationError(f"BuildRules directory is not writable: {build_rules}")
    for path in build_rules.iterdir():
        if path.is_file() and not stat.S_IMODE(path.stat().st_mode) & stat.S_IWUSR:
            raise PreparationError(f"Generated rules file is not writable: {path}")


def set_writable_build_inputs(destination: Path) -> None:
    for relative in MODULE_RULES.values():
        make_owner_writable(destination / relative)
    build_rules = destination / "Engine/Intermediate/Build/BuildRules"
    make_owner_writable(build_rules)
    for path in build_rules.iterdir():
        if path.is_file():
            make_owner_writable(path)


def write_manifest(
    source: Path,
    destination: Path,
    version: dict[str, object],
    rules: dict[Path, tuple[str, str]],
    plugin_hashes: dict[str, str],
) -> None:
    source_hashes = {str(relative): sha256(source / relative) for relative in SUPPORT_FILES}
    source_hashes.update({str(relative): hashlib.sha256(original.encode()).hexdigest() for relative, (original, _) in rules.items()})
    clone_hashes = {str(relative): sha256(destination / relative) for relative in SUPPORT_FILES}
    clone_hashes.update({str(relative): sha256(destination / relative) for relative in rules})
    manifest = {
        "schemaVersion": 1,
        "preparedAtUtc": datetime.now(timezone.utc).isoformat(),
        "sourceEngine": str(source),
        "destinationEngine": str(destination),
        "buildVersion": version,
        "scope": "Cinderline IOS only; ApplicationCore and Launch rebuilt, other engine modules precompiled",
        "sourceSha256": source_hashes,
        "cloneSha256": clone_hashes,
        "excludedMicrosoftPluginDescriptorSha256": plugin_hashes,
    }
    path = destination / MANIFEST_NAME
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    os.replace(temporary, path)


def verify_manifest(
    source: Path,
    destination: Path,
    version: dict[str, object],
    rules: dict[Path, tuple[str, str]],
    plugin_hashes: dict[str, str],
) -> None:
    path = destination / MANIFEST_NAME
    try:
        manifest = json.loads(read_text(path))
    except json.JSONDecodeError as exc:
        raise PreparationError(f"Invalid preparation manifest {path}: {exc}") from exc
    if manifest.get("schemaVersion") != 1 or manifest.get("buildVersion") != version:
        raise PreparationError(f"Preparation manifest version mismatch: {path}")
    if manifest.get("sourceEngine") != str(source) or manifest.get("destinationEngine") != str(destination):
        raise PreparationError(f"Preparation manifest path mismatch: {path}")
    expected_source = {str(relative): sha256(source / relative) for relative in SUPPORT_FILES}
    expected_source.update({str(relative): hashlib.sha256(original.encode()).hexdigest() for relative, (original, _) in rules.items()})
    expected_clone = {str(relative): sha256(destination / relative) for relative in SUPPORT_FILES}
    expected_clone.update({str(relative): sha256(destination / relative) for relative in rules})
    if manifest.get("sourceSha256") != expected_source:
        raise PreparationError(f"Stock source hashes changed since preparation: {path}")
    if manifest.get("cloneSha256") != expected_clone:
        raise PreparationError(f"Prepared clone hashes changed: {path}")
    if manifest.get("excludedMicrosoftPluginDescriptorSha256") != plugin_hashes:
        raise PreparationError(f"Excluded plugin hashes changed: {path}")


def clone_engine(source: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    if source.stat().st_dev != destination.parent.stat().st_dev:
        raise PreparationError("APFS clone source and destination must be on the same filesystem")
    temporary = destination.with_name(f"{destination.name}.partial-{os.getpid()}")
    if temporary.exists():
        raise PreparationError(f"Temporary destination already exists: {temporary}")
    try:
        subprocess.run(["/bin/cp", "-c", "-R", str(source), str(temporary)], check=True)
        os.replace(temporary, destination)
    except Exception:
        if temporary.exists():
            shutil.rmtree(temporary)
        raise


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, default=DEFAULT_SOURCE, help="stock Unreal Engine 5.8 root")
    parser.add_argument("--destination", type=Path, default=DEFAULT_DESTINATION)
    parser.add_argument("--check", action="store_true", help="verify an existing prepared clone without changing it")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    source_input = Path(os.path.abspath(os.path.expanduser(str(args.engine))))
    destination_input = Path(os.path.abspath(os.path.expanduser(str(args.destination))))
    if source_input.is_symlink() or destination_input.is_symlink():
        raise PreparationError("Source and destination engine roots must not be symbolic links")
    source = source_input.resolve()
    destination = destination_input.resolve()
    if source == destination or source in destination.parents or destination in source.parents:
        raise PreparationError("Source and destination must be separate, non-nested engine roots")
    if destination == PROJECT_ROOT or PROJECT_ROOT in destination.parents:
        raise PreparationError("The prepared engine must be outside the project so UAT stages engine content correctly")

    version = validate_engine(source, "source")
    rules = pristine_and_patched_rules(source)

    if args.check:
        if not destination.is_dir():
            raise PreparationError(f"Prepared clone does not exist: {destination}")
        validate_engine(destination, "clone")
        verify_clone_identity(source, destination, rules)
        for relative, (_, patched) in rules.items():
            if read_text(destination / relative) != patched:
                raise PreparationError(f"Expected patch is absent or incomplete: {destination / relative}")
        plugin_hashes = verify_or_move_microsoft_plugins(source, destination, check_only=True)
        verify_writable_build_inputs(destination)
        verify_manifest(source, destination, version, rules, plugin_hashes)
        print(f"Cinderline iOS 27 engine clone verified: {destination}")
        return 0

    if destination.exists() and not destination.is_dir():
        raise PreparationError(f"Destination exists and is not a directory: {destination}")
    if not destination.exists():
        clone_engine(source, destination)

    validate_engine(destination, "clone")
    verify_clone_identity(source, destination, rules)
    set_writable_build_inputs(destination)
    for relative, (original, patched) in rules.items():
        path = destination / relative
        if read_text(path) == original:
            path.write_text(patched, encoding="utf-8")
        elif read_text(path) != patched:
            raise PreparationError(f"Unexpected edits in clone module rule: {path}")
    plugin_hashes = verify_or_move_microsoft_plugins(source, destination, check_only=False)
    write_manifest(source, destination, version, rules, plugin_hashes)
    verify_writable_build_inputs(destination)
    print(f"Cinderline iOS 27 engine clone prepared: {destination}")
    print("Build with -SkipRulesCompile -ForceRulesCompile so the installed-engine rules DLL is regenerated.")
    print("Package with -target=Cinderline using this engine outside the project directory.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (PreparationError, OSError, subprocess.CalledProcessError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(2)
