#!/usr/bin/env python3
"""Prepare an isolated UE 5.8 clone for Cinderline's iOS 27 support.

The stock Epic installation is never modified. The clone keeps its installed-build
marker so every engine module remains precompiled except ApplicationCore, Core, and
Launch.
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
CORE_PATCH_MARKER = "// Cinderline iOS diagnostics: rebuild Core with public dyld callbacks."
MODULE_DIAGNOSTICS_PATH = Path("Engine/Source/Runtime/Core/Private/IOS/IOSModuleDiagnostics.cpp")
MIMALLOC_SOURCE_PATH = Path("Engine/Source/Runtime/Core/Private/Thirdparty/MiMalloc.c")
MIMALLOC_STOCK_OBJECT_PATH = Path(
    "Engine/Intermediate/Build/IOS/arm64/UnrealGame/Development/Core/MiMalloc.c.o"
)
MIMALLOC_CLONE_OBJECT_PATH = Path(
    "Engine/CinderlinePrecompiled/IOS/arm64/UnrealGame/Development/Core/MiMalloc.c.o"
)
MIMALLOC_STOCK_OBJECT_SHA256 = "7d32cd0001b5daf4bd16a02ca75746d5d4b3972dbd3c77592861a266bc5cd94c"

MODULE_RULES = {
    "ApplicationCore": Path("Engine/Source/Runtime/ApplicationCore/ApplicationCore.Build.cs"),
    "Core": Path("Engine/Source/Runtime/Core/Core.Build.cs"),
    "Launch": Path("Engine/Source/Runtime/Launch/Launch.Build.cs"),
}

SUPPORT_FILES = (
    Path("Engine/Build/Build.version"),
    Path("Engine/Build/InstalledBuild.txt"),
    Path("Engine/Source/Programs/UnrealBuildTool/Platform/IOS/UEBuildIOS.cs"),
    Path("Engine/Source/Runtime/ApplicationCore/Private/IOS/IOSSceneDelegate.cpp"),
    Path("Engine/Source/Runtime/ApplicationCore/Private/IOS/IOSAppDelegate.cpp"),
    Path("Engine/Source/Runtime/Launch/Private/IOS/LaunchIOS.cpp"),
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
    if module == "Core":
        insertion = (
            signature
            + f"\t\t{CORE_PATCH_MARKER}\n"
            + "\t\tif (Target.Platform == UnrealTargetPlatform.IOS\n"
            + "\t\t\t&& Target.Architecture == UnrealArch.Arm64\n"
            + "\t\t\t&& Target.Configuration == UnrealTargetConfiguration.Development\n"
            + "\t\t\t&& Target.Name == \"Cinderline\")\n"
            + "\t\t{\n"
            + "\t\t\tbUsePrecompiled = false;\n"
            + "\t\t\tPrivateDefinitions.Add(\"CINDERLINE_USE_PRECOMPILED_MIMALLOC=1\");\n"
            + "\t\t\tPublicAdditionalLibraries.Add(Path.Combine(EngineDirectory, \"CinderlinePrecompiled\", \"IOS\", \"arm64\", \"UnrealGame\", \"Development\", \"Core\", \"MiMalloc.c.o\"));\n"
            + "\t\t}\n\n"
        )
    else:
        insertion = (
            signature
            + f"\t\t{PATCH_MARKER}\n"
            + "\t\tif (Target.Platform == UnrealTargetPlatform.IOS && Target.Name == \"Cinderline\")\n"
            + "\t\t{\n"
            + "\t\t\tbUsePrecompiled = false;\n"
            + "\t\t}\n\n"
        )
    return original.replace(signature, insertion, 1)


def expected_previous_core_rule_text(original: str) -> str:
    """Return the exact schema-2 Core rule accepted for one-way migration."""
    signature = "\tpublic Core(ReadOnlyTargetRules Target) : base(Target)\n\t{\n"
    if original.count(signature) != 1:
        raise PreparationError("Expected one constructor anchor for Core")
    insertion = (
        signature
        + f"\t\t{CORE_PATCH_MARKER}\n"
        + "\t\tif (Target.Platform == UnrealTargetPlatform.IOS && Target.Name == \"Cinderline\")\n"
        + "\t\t{\n"
        + "\t\t\tbUsePrecompiled = false;\n"
        + "\t\t}\n\n"
    )
    return original.replace(signature, insertion, 1)


def expected_mimalloc_text(original: str) -> str:
    anchor = "#if PLATFORM_BUILDS_MIMALLOC"
    replacement = "#if PLATFORM_BUILDS_MIMALLOC && !defined(CINDERLINE_USE_PRECOMPILED_MIMALLOC)"
    if original.count(anchor) != 1 or replacement in original:
        raise PreparationError("UE 5.8 MiMalloc source no longer matches the expected patch anchor")
    return original.replace(anchor, replacement, 1)


def expected_module_diagnostics_text(original: str) -> str:
    header_anchor = '''#include "Apple/PreAppleSystemHeaders.h"
#include <mach/mach.h>
#include <mach-o/dyld_images.h>
#include <mach-o/loader.h>
#include "Apple/PostAppleSystemHeaders.h"'''
    patched_headers = '''#include "Apple/PreAppleSystemHeaders.h"
#include <atomic>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include "Apple/PostAppleSystemHeaders.h"'''
    function_start = original.find("void Modules_Initialize()\n{", original.find("#else // !UE_BUILD_SHIPPING"))
    function_end_anchor = "\n\n#undef TRACE_MODULES_DEBUG_LOG"
    function_end = original.find(function_end_anchor, function_start)
    if original.count(header_anchor) != 1 or function_start < 0 or function_end < 0:
        raise PreparationError("UE 5.8 IOSModuleDiagnostics source no longer matches the expected patch anchors")

    implementation = r'''namespace
{
	thread_local bool GCollectDyldImages = false;
	std::atomic<bool> GModulesInitialized{false};
#if UE_MEMORY_TRACE_ENABLED
	FString GExecutablePath;
	uint32 GProgramHeapId = 0;
#endif

	void Modules_OnDyldImageAdded(const struct mach_header* Header, intptr_t)
	{
		using namespace UE::Trace;

		if (!GCollectDyldImages || Header == nullptr || Header->magic != MH_MAGIC_64)
		{
			return;
		}

		Dl_info ImageInfo = {};
		if (dladdr(Header, &ImageInfo) == 0 || ImageInfo.dli_fname == nullptr)
		{
			return;
		}

		TRACE_MODULES_DEBUG_LOG(TEXT("Found module %s"), ANSI_TO_TCHAR(ImageInfo.dli_fname));

		const uint64 ModuleBase = uint64(Header);
		TRACE_MODULES_DEBUG_LOG(TEXT("    Base: 0x%llX"), ModuleBase);

		const struct mach_header_64* Header64 = reinterpret_cast<const struct mach_header_64*>(Header);

		// calc image size by adding header and size of segments
		uint64 ImageSize = sizeof(*Header64) + Header64->sizeofcmds;
		constexpr uint64 PageMask = (1 << 12) - 1;
		ImageSize = (ImageSize + PageMask) & ~PageMask; // 4K page aligned
		TRACE_MODULES_DEBUG_LOG(TEXT("    Header: %llu + %llu (%d cmds) -> %llu"), sizeof(*Header64), Header64->sizeofcmds, Header64->ncmds, ImageSize);

		// Send Mach-O's uuid as the BuildId. Psym generation seems to have an extra 0 but we will ignore it on the other end.
		constexpr uint32 BuildIdSize = 16;
		uint8 BuildId[BuildIdSize] = {0};

		const char* CmdPtr = reinterpret_cast<const char*>(Header64 + 1);
		for (int CommandIndex = 0; CommandIndex < Header64->ncmds; ++CommandIndex)
		{
			const struct load_command* LoadCommand = reinterpret_cast<const struct load_command*>(CmdPtr);
			if (LoadCommand->cmd == LC_SEGMENT_64)
			{
				const struct segment_command_64* Segment = reinterpret_cast<const struct segment_command_64*>(LoadCommand);
				TRACE_MODULES_DEBUG_LOG(TEXT("    LC_SEGMENT_64 %s (vmaddr=0x%llX, vmsize=%llu, filesize=%llu)"),
					ANSI_TO_TCHAR(Segment->segname), uint64(Segment->vmaddr), uint64(Segment->vmsize), uint64(Segment->filesize));

				if (uint64(Segment->vmaddr) != 0 && // skips __PAGEZERO segment (4 GiB; reserved virtual memory)
					FPlatformString::Strcmp(Segment->segname, "__LINKEDIT") != 0) // skips __LINKEDIT segment
				{
					ImageSize += uint64(Segment->vmsize);
				}
			}
			else if (LoadCommand->cmd == LC_UUID)
			{
				FMemory::Memcpy(BuildId, reinterpret_cast<const struct uuid_command*>(LoadCommand)->uuid, BuildIdSize);
			}
			CmdPtr += LoadCommand->cmdsize;
		}

		TRACE_MODULES_DEBUG_LOG(TEXT("    ImageSize: %llu (end=0x%llX)"), ImageSize, ModuleBase + ImageSize);

		FString ImageName(ImageInfo.dli_fname);

#if UE_MEMORY_TRACE_ENABLED
		bool bInsideExecutablePath = ImageName.StartsWith(GExecutablePath);
#endif // UE_MEMORY_TRACE_ENABLED

		// trim path to leave just image name
		ImageName = FPaths::GetCleanFilename(ImageName);

		const uint32 ModuleSize = uint32(ImageSize);

		UE_TRACE_LOG(Diagnostics, ModuleLoad, ModuleChannel, sizeof(TCHAR) * ImageName.Len() + BuildIdSize)
			<< ModuleLoad.Name(*ImageName, ImageName.Len())
			<< ModuleLoad.Base(ModuleBase)
			<< ModuleLoad.Size(ModuleSize)
			<< ModuleLoad.ImageId(BuildId, BuildIdSize);

#if UE_MEMORY_TRACE_ENABLED
		// Only count the main executable and any other libraries inside our bundle as "Program Size"
		if (bInsideExecutablePath)
		{
			UE_TRACE_METADATA_CLEAR_SCOPE();
			LLM(UE_MEMSCOPE(ELLMTag::ProgramSize));
			MemoryTrace_Alloc(ModuleBase, ImageSize, 1);
			MemoryTrace_MarkAllocAsHeap(ModuleBase, GProgramHeapId);
			MemoryTrace_Alloc(ModuleBase, ImageSize, 1);
		}
#endif // UE_MEMORY_TRACE_ENABLED
	}
}

void Modules_Initialize()
{
	using namespace UE::Trace;

	bool bExpected = false;
	if (!GModulesInitialized.compare_exchange_strong(bExpected, true, std::memory_order_acq_rel))
	{
		return;
	}

	constexpr uint32 SizeOfSymbolFormatString = 4;
	UE_TRACE_LOG(Diagnostics, ModuleInit, ModuleChannel, sizeof(ANSICHAR) * SizeOfSymbolFormatString)
		<< ModuleInit.SymbolFormat("psym", SizeOfSymbolFormatString)
		<< ModuleInit.ModuleBaseShift(uint8(0));

#if UE_MEMORY_TRACE_ENABLED
	GProgramHeapId = MemoryTrace_HeapSpec(EMemoryTraceRootHeap::SystemMemory, TEXT("Program"), EMemoryTraceHeapFlags::NeverFrees);
	GExecutablePath = FPaths::GetPath(FString([[NSBundle mainBundle]executablePath]));
#endif // UE_MEMORY_TRACE_ENABLED

	// Registration synchronously invokes the callback once for every loaded image.
	// Disable collection afterward so future dyld callbacks do no Unreal work on loader threads.
	GCollectDyldImages = true;
	_dyld_register_func_for_add_image(&Modules_OnDyldImageAdded);
	GCollectDyldImages = false;
}'''

    patched = original[:function_start] + implementation + original[function_end:]
    return patched.replace(header_anchor, patched_headers, 1)


def validate_engine(root: Path, label: str) -> dict[str, object]:
    build_version_path = root / "Engine/Build/Build.version"
    try:
        version = json.loads(read_text(build_version_path))
    except json.JSONDecodeError as exc:
        raise PreparationError(f"Invalid {label} Build.version: {exc}") from exc
    if (version.get("MajorVersion"), version.get("MinorVersion")) != (5, 8):
        raise PreparationError(f"{label} must be Unreal Engine 5.8, got {version}")

    for relative in (*SUPPORT_FILES, *MODULE_RULES.values(), MODULE_DIAGNOSTICS_PATH, MIMALLOC_SOURCE_PATH):
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
        marker = CORE_PATCH_MARKER if module == "Core" else PATCH_MARKER
        if marker in original:
            raise PreparationError(f"Stock engine was already modified: {source / relative}")
        result[relative] = (original, expected_patched_text(original, module))
    return result


def pristine_and_patched_sources(source: Path) -> dict[Path, tuple[str, str]]:
    diagnostics = read_text(source / MODULE_DIAGNOSTICS_PATH)
    mimalloc = read_text(source / MIMALLOC_SOURCE_PATH)
    return {
        MODULE_DIAGNOSTICS_PATH: (diagnostics, expected_module_diagnostics_text(diagnostics)),
        MIMALLOC_SOURCE_PATH: (mimalloc, expected_mimalloc_text(mimalloc)),
    }


def verify_clone_identity(
    source: Path,
    destination: Path,
    patched_files: dict[Path, tuple[str, str]],
) -> None:
    for relative in SUPPORT_FILES:
        source_path = source / relative
        destination_path = destination / relative
        if not destination_path.is_file() or sha256(destination_path) != sha256(source_path):
            raise PreparationError(f"Existing destination is not the expected engine clone: {destination_path}")
    for relative, (original, patched) in patched_files.items():
        contents = read_text(destination / relative)
        accepted = (original, patched)
        if relative == MODULE_RULES["Core"]:
            accepted += (expected_previous_core_rule_text(original),)
        if contents not in accepted:
            raise PreparationError(f"Unexpected edits in clone build input: {destination / relative}")


def validate_stock_mimalloc_object(source: Path) -> str:
    path = source / MIMALLOC_STOCK_OBJECT_PATH
    if not path.is_file():
        raise PreparationError(f"Missing stock native iOS MiMalloc object: {path}")
    digest = sha256(path)
    if digest != MIMALLOC_STOCK_OBJECT_SHA256:
        raise PreparationError(
            f"Stock native iOS MiMalloc object hash mismatch: {path} ({digest})"
        )
    return digest


def verify_or_copy_mimalloc_object(source: Path, destination: Path, check_only: bool) -> str:
    digest = validate_stock_mimalloc_object(source)
    source_path = source / MIMALLOC_STOCK_OBJECT_PATH
    destination_path = destination / MIMALLOC_CLONE_OBJECT_PATH
    if destination_path.exists():
        if not destination_path.is_file() or sha256(destination_path) != digest:
            raise PreparationError(f"Prepared MiMalloc object does not match stock: {destination_path}")
    elif check_only:
        raise PreparationError(f"Prepared MiMalloc object is missing: {destination_path}")
    else:
        destination_path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source_path, destination_path)
        if sha256(destination_path) != digest:
            raise PreparationError(f"MiMalloc object copy verification failed: {destination_path}")
    return digest


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
    for relative in (*MODULE_RULES.values(), MODULE_DIAGNOSTICS_PATH, MIMALLOC_SOURCE_PATH):
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
    for relative in (*MODULE_RULES.values(), MODULE_DIAGNOSTICS_PATH, MIMALLOC_SOURCE_PATH):
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
    patched_files: dict[Path, tuple[str, str]],
    plugin_hashes: dict[str, str],
    mimalloc_object_hash: str,
) -> None:
    source_hashes = {str(relative): sha256(source / relative) for relative in SUPPORT_FILES}
    source_hashes.update({str(relative): hashlib.sha256(original.encode()).hexdigest() for relative, (original, _) in patched_files.items()})
    source_hashes[str(MIMALLOC_STOCK_OBJECT_PATH)] = mimalloc_object_hash
    clone_hashes = {str(relative): sha256(destination / relative) for relative in SUPPORT_FILES}
    clone_hashes.update({str(relative): sha256(destination / relative) for relative in patched_files})
    clone_hashes[str(MIMALLOC_CLONE_OBJECT_PATH)] = mimalloc_object_hash
    manifest = {
        "schemaVersion": 3,
        "preparedAtUtc": datetime.now(timezone.utc).isoformat(),
        "sourceEngine": str(source),
        "destinationEngine": str(destination),
        "buildVersion": version,
        "scope": "Cinderline native IOS arm64 Development: Core rebuilt with stock MiMalloc object; ApplicationCore and Launch rebuilt for Cinderline IOS",
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
    patched_files: dict[Path, tuple[str, str]],
    plugin_hashes: dict[str, str],
    mimalloc_object_hash: str,
) -> None:
    path = destination / MANIFEST_NAME
    try:
        manifest = json.loads(read_text(path))
    except json.JSONDecodeError as exc:
        raise PreparationError(f"Invalid preparation manifest {path}: {exc}") from exc
    if manifest.get("schemaVersion") != 3 or manifest.get("buildVersion") != version:
        raise PreparationError(f"Preparation manifest version mismatch: {path}")
    if manifest.get("sourceEngine") != str(source) or manifest.get("destinationEngine") != str(destination):
        raise PreparationError(f"Preparation manifest path mismatch: {path}")
    expected_source = {str(relative): sha256(source / relative) for relative in SUPPORT_FILES}
    expected_source.update({str(relative): hashlib.sha256(original.encode()).hexdigest() for relative, (original, _) in patched_files.items()})
    expected_source[str(MIMALLOC_STOCK_OBJECT_PATH)] = mimalloc_object_hash
    expected_clone = {str(relative): sha256(destination / relative) for relative in SUPPORT_FILES}
    expected_clone.update({str(relative): sha256(destination / relative) for relative in patched_files})
    expected_clone[str(MIMALLOC_CLONE_OBJECT_PATH)] = mimalloc_object_hash
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
    mimalloc_object_hash = validate_stock_mimalloc_object(source)
    rules = pristine_and_patched_rules(source)
    sources = pristine_and_patched_sources(source)
    patched_files = rules | sources

    if args.check:
        if not destination.is_dir():
            raise PreparationError(f"Prepared clone does not exist: {destination}")
        validate_engine(destination, "clone")
        verify_clone_identity(source, destination, patched_files)
        for relative, (_, patched) in patched_files.items():
            if read_text(destination / relative) != patched:
                raise PreparationError(f"Expected patch is absent or incomplete: {destination / relative}")
        plugin_hashes = verify_or_move_microsoft_plugins(source, destination, check_only=True)
        verify_or_copy_mimalloc_object(source, destination, check_only=True)
        verify_writable_build_inputs(destination)
        verify_manifest(source, destination, version, patched_files, plugin_hashes, mimalloc_object_hash)
        print(f"Cinderline iOS 27 engine clone verified: {destination}")
        return 0

    if destination.exists() and not destination.is_dir():
        raise PreparationError(f"Destination exists and is not a directory: {destination}")
    if not destination.exists():
        clone_engine(source, destination)

    validate_engine(destination, "clone")
    verify_clone_identity(source, destination, patched_files)
    set_writable_build_inputs(destination)
    for relative, (original, patched) in patched_files.items():
        path = destination / relative
        current = read_text(path)
        previous_core = (
            relative == MODULE_RULES["Core"]
            and current == expected_previous_core_rule_text(original)
        )
        if current == original or previous_core:
            path.write_text(patched, encoding="utf-8")
        elif current != patched:
            raise PreparationError(f"Unexpected edits in clone build input: {path}")
    plugin_hashes = verify_or_move_microsoft_plugins(source, destination, check_only=False)
    verify_or_copy_mimalloc_object(source, destination, check_only=False)
    write_manifest(source, destination, version, patched_files, plugin_hashes, mimalloc_object_hash)
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
