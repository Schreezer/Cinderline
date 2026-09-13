#!/usr/bin/env python3
"""Apply or verify Cinderline's bounded MetalRHI bridge in its isolated UE clone."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import stat
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_SOURCE = Path('/Users/Shared/Epic Games/UE_5.8')
DEFAULT_DESTINATION = ROOT.parent / 'CinderlineEngineIOS27'
MODULE = Path('Engine/Source/Runtime/Apple/MetalRHI')
PATCHES = {
    MODULE / 'Public/MetalRHIContext.h': 'MetalRHIContext.h.patch',
    MODULE / 'Private/MetalRHIContext.cpp': 'MetalRHIContext.cpp.patch',
}
MANIFEST = '.cinderline-metalfx-engine.json'


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def expected_files(source: Path) -> dict[Path, tuple[bytes, bytes]]:
    expected = {}
    patch_dir = ROOT / 'Plugins/CinderMetalFX/EnginePatch'
    with tempfile.TemporaryDirectory(prefix='cinder-metalfx-patch-') as directory:
        scratch = Path(directory)
        for relative, name in PATCHES.items():
            original = (source / relative).read_bytes()
            patch = (patch_dir / name).read_bytes()
            if not patch.strip():
                raise RuntimeError(f'Empty bridge patch: {name}')
            # patch receives an explicit temporary input file. It cannot select
            # another engine path from a diff header.
            temporary = scratch / relative.name
            temporary.write_bytes(original)
            subprocess.run(['/usr/bin/patch', '--batch', '--forward', str(temporary)],
                           input=patch, check=True, capture_output=True)
            expected[relative] = original, temporary.read_bytes()
    relative = MODULE / 'MetalRHI.Build.cs'
    original = (source / relative).read_bytes()
    anchor = '\tpublic MetalRHI(ReadOnlyTargetRules Target) : base(Target)\n\t{\n'
    text = original.decode()
    if text.count(anchor) != 1:
        raise RuntimeError('Stock MetalRHI rule constructor changed')
    insertion = anchor + '''\t\t// Cinderline MetalFX: rebuild the native command-buffer bridge in this clone.
\t\tif ((Target.Platform == UnrealTargetPlatform.Mac || Target.Platform == UnrealTargetPlatform.IOS)
\t\t\t&& (Target.Name == "Cinderline" || Target.Name == "CinderlineEditor"))
\t\t{
\t\t\tbUsePrecompiled = false;
\t\t}

'''
    expected[relative] = original, text.replace(anchor, insertion, 1).encode()
    return expected


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine', type=Path, default=DEFAULT_SOURCE,
                        help='stock Unreal Engine 5.8 root')
    parser.add_argument('--destination', type=Path, default=DEFAULT_DESTINATION)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    source_input = Path(args.engine).expanduser().absolute()
    destination_input = Path(args.destination).expanduser().absolute()
    if source_input.is_symlink() or destination_input.is_symlink():
        raise RuntimeError('Engine roots must be real directories, not symbolic links')
    source = source_input.resolve()
    destination = destination_input.resolve()
    if source == destination or source in destination.parents or destination in source.parents:
        raise RuntimeError('Engine roots must be distinct real directories')
    subprocess.run([
        'python3', str(ROOT / 'scripts/prepare-ios27-engine.py'),
        '--engine', str(source), '--destination', str(destination), '--check'
    ], check=True)
    version = json.loads((source / 'Engine/Build/Build.version').read_text())
    if (version.get('MajorVersion'), version.get('MinorVersion'), version.get('PatchVersion'), version.get('Changelist')) != (5, 8, 2, 56702186):
        raise RuntimeError('This MetalRHI bridge requires the audited UE 5.8.2 CL 56702186')
    expected = expected_files(source)
    manifest = {
        'schemaVersion': 1, 'sourceEngine': str(source), 'destinationEngine': str(destination),
        'buildVersion': version,
        'files': {str(path): {'sourceSha256': digest(original), 'patchedSha256': digest(patched)}
                  for path, (original, patched) in expected.items()},
    }
    manifest_path = destination / MANIFEST
    previous = json.loads(manifest_path.read_text()) if manifest_path.exists() else {}
    for relative, (original, patched) in expected.items():
        target = destination / relative
        if target.is_symlink():
            raise RuntimeError(f'Refusing symlink engine input: {relative}')
        current = target.read_bytes()
        prior_file = previous.get('files', {}).get(str(relative), {})
        known_previous = (previous.get('sourceEngine') == str(source)
                          and previous.get('destinationEngine') == str(destination)
                          and prior_file.get('sourceSha256') == digest(original)
                          and prior_file.get('patchedSha256') == digest(current))
        if current not in (original, patched) and not (known_previous and not args.check):
            raise RuntimeError(f'Unexpected engine edits: {relative}')
        if args.check and current != patched:
            raise RuntimeError(f'Unapplied engine patch: {relative}')
    if args.check:
        if json.loads(manifest_path.read_text()) != manifest:
            raise RuntimeError('MetalFX preparation manifest differs from current inputs')
    else:
        backup = ROOT / 'Saved/MetalFX/engine-originals'
        for relative, (original, patched) in expected.items():
            backup_path = backup / relative
            backup_path.parent.mkdir(parents=True, exist_ok=True)
            if backup_path.exists() and backup_path.read_bytes() != original:
                raise RuntimeError(f'Original backup differs: {relative}')
            backup_path.write_bytes(original)
            target = destination / relative
            target.chmod(stat.S_IMODE(target.stat().st_mode) | stat.S_IWUSR)
            if target.read_bytes() != patched:
                target.write_bytes(patched)
        manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')
    # The original engine must still match every pristine input after preparation.
    for relative, (original, _) in expected.items():
        if (source / relative).read_bytes() != original:
            raise RuntimeError(f'Stock engine changed during preparation: {relative}')
    print(f'Cinderline MetalFX engine {"verified" if args.check else "prepared"}: {destination}')


if __name__ == '__main__':
    main()
