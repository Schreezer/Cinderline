#!/usr/bin/env python3
"""Atomically archive Cinderline's freshly assembled, signed iOS app."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import os
from pathlib import Path
import shutil
import subprocess
import sys


class ArchiveError(RuntimeError):
    pass


def verify_signature(app: Path, label: str) -> None:
    if not app.is_dir() or app.is_symlink():
        raise ArchiveError(f"{label} is not a regular app bundle: {app}")
    result = subprocess.run(
        ["/usr/bin/codesign", "--verify", "--deep", "--strict", str(app)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        raise ArchiveError(f"{label} failed strict code-signature verification: {app}")


def unique_quarantine_path(root: Path, destination: Path, kind: str) -> Path:
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    candidate = root / f"{kind}-{stamp}-{os.getpid()}-{destination.name}"
    if candidate.exists():
        raise ArchiveError(f"Quarantine path already exists: {candidate}")
    return candidate


def archive(source: Path, destination: Path, quarantine_root: Path) -> Path | None:
    source_input = Path(os.path.abspath(os.path.expanduser(str(source))))
    destination_input = Path(os.path.abspath(os.path.expanduser(str(destination))))
    quarantine_input = Path(os.path.abspath(os.path.expanduser(str(quarantine_root))))
    for label, path in (
        ("Source app", source_input),
        ("Archive destination", destination_input),
        ("Quarantine root", quarantine_input),
    ):
        if path.is_symlink():
            raise ArchiveError(f"{label} must not be a symbolic link: {path}")
    source = source_input.resolve()
    destination = destination_input.resolve()
    quarantine_root = quarantine_input.resolve()
    if source == destination or source in destination.parents or destination in source.parents:
        raise ArchiveError("Source and archive destination must be separate, non-nested paths")
    if quarantine_root in (source, destination) or source in quarantine_root.parents or destination in quarantine_root.parents:
        raise ArchiveError("Quarantine root must not be the source, destination, or a child of either app bundle")

    verify_signature(source, "Fresh Binaries/IOS app")
    destination.parent.mkdir(parents=True, exist_ok=True)
    quarantine_root.mkdir(parents=True, exist_ok=True)
    temporary = destination.parent / f".{destination.name}.fresh-{os.getpid()}"
    if temporary.exists():
        raise ArchiveError(f"Temporary archive path already exists: {temporary}")

    previous: Path | None = None
    try:
        subprocess.run(["/bin/cp", "-c", "-R", str(source), str(temporary)], check=True)
        verify_signature(temporary, "Temporary archive copy")
        if destination.exists():
            if not destination.is_dir() or destination.is_symlink():
                raise ArchiveError(f"Archive destination is not a regular app bundle: {destination}")
            previous = unique_quarantine_path(quarantine_root, destination, "previous")
            os.replace(destination, previous)
        os.replace(temporary, destination)
        verify_signature(destination, "Final archived app")
        return previous
    except Exception:
        if temporary.exists():
            shutil.rmtree(temporary)
        if previous is not None and not destination.exists() and previous.exists():
            os.replace(previous, destination)
        elif previous is not None and destination.exists() and previous.exists():
            failed = unique_quarantine_path(quarantine_root, destination, "failed")
            os.replace(destination, failed)
            os.replace(previous, destination)
        raise


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--destination", required=True, type=Path)
    parser.add_argument("--quarantine-root", required=True, type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    previous = archive(args.source, args.destination, args.quarantine_root)
    destination = Path(os.path.abspath(os.path.expanduser(str(args.destination))))
    print(f"Verified physical-iOS archive: {destination}")
    if previous is not None:
        print(f"Previous archive quarantined: {previous}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ArchiveError, OSError, subprocess.CalledProcessError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(2)
