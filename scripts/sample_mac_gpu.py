#!/usr/bin/env python3
"""Capture short whole-GPU utilization samples from macOS AGX ioreg data.

This intentionally samples only the aggregate counters exposed by macOS.  It
does not attempt to identify processes, applications, devices, or power use.
"""

from __future__ import annotations

import argparse
import json
import math
import plistlib
import statistics
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


COUNTERS = {
    "device_utilization_percent": "Device Utilization %",
    "renderer_utilization_percent": "Renderer Utilization %",
    "tiler_utilization_percent": "Tiler Utilization %",
}
IOREG_COMMAND = ["ioreg", "-r", "-c", "AGXAccelerator", "-a"]
IOREG_TIMEOUT_SECONDS = 5.0


def utc_now() -> str:
    """Return an RFC 3339 UTC timestamp with millisecond precision."""
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")


def find_performance_statistics(value: Any) -> dict[str, Any] | None:
    """Find the first PerformanceStatistics dictionary in an ioreg plist."""
    if isinstance(value, dict):
        stats = value.get("PerformanceStatistics")
        if isinstance(stats, dict):
            return stats
        for child in value.values():
            found = find_performance_statistics(child)
            if found is not None:
                return found
    elif isinstance(value, list):
        for child in value:
            found = find_performance_statistics(child)
            if found is not None:
                return found
    return None


def as_number(value: Any) -> float | int | None:
    """Keep valid finite numeric counters; missing or malformed values stay null."""
    if isinstance(value, bool):
        return None
    if isinstance(value, (int, float)) and math.isfinite(value):
        return value
    return None


def read_gpu_counters() -> tuple[dict[str, float | int | None], str | None]:
    """Read aggregate AGX counters, returning a safe diagnostic on failure."""
    unavailable = {name: None for name in COUNTERS}
    try:
        result = subprocess.run(
            IOREG_COMMAND,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            check=False,
            timeout=IOREG_TIMEOUT_SECONDS,
        )
    except FileNotFoundError:
        return unavailable, "ioreg is unavailable on this system"
    except subprocess.TimeoutExpired:
        return unavailable, "ioreg timed out"
    except OSError:
        return unavailable, "ioreg could not be executed"

    if result.returncode != 0:
        return unavailable, "ioreg exited with a non-zero status"
    try:
        registry = plistlib.loads(result.stdout)
    except (plistlib.InvalidFileException, ValueError, TypeError):
        return unavailable, "ioreg returned unreadable plist data"

    stats = find_performance_statistics(registry)
    if stats is None:
        return unavailable, "AGX PerformanceStatistics were unavailable"
    return {name: as_number(stats.get(source_key)) for name, source_key in COUNTERS.items()}, None


def summarize(samples: list[dict[str, Any]]) -> dict[str, dict[str, float | int | None]]:
    """Compute statistics from available values only; absent counters remain null."""
    output: dict[str, dict[str, float | int | None]] = {}
    for name in COUNTERS:
        values = [sample["values"][name] for sample in samples if sample["values"][name] is not None]
        if values:
            output[name] = {
                "count": len(values),
                "min": min(values),
                "median": statistics.median(values),
                "max": max(values),
                "mean": statistics.fmean(values),
            }
        else:
            output[name] = {"count": 0, "min": None, "median": None, "max": None, "mean": None}
    return output


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seconds", type=float, default=10, help="Capture duration, from 1 to 60 seconds (default: 10).")
    parser.add_argument("--interval", type=float, default=1, help="Seconds between readings, at least 0.2 (default: 1).")
    parser.add_argument("--label", help="Optional label included in the JSON result.")
    parser.add_argument("--output", help="Optional JSON output file; otherwise writes JSON to stdout.")
    args = parser.parse_args()
    if not 1 <= args.seconds <= 60:
        parser.error("--seconds must be between 1 and 60")
    if not math.isfinite(args.interval) or args.interval < 0.2:
        parser.error("--interval must be a finite value of at least 0.2")
    return args


def main() -> int:
    args = parse_args()
    started_at_utc = utc_now()
    started = time.monotonic()
    deadline = started + args.seconds
    samples: list[dict[str, Any]] = []

    while True:
        values, error = read_gpu_counters()
        sample: dict[str, Any] = {"timestamp_utc": utc_now(), "values": values}
        if error is not None:
            sample["error"] = error
        samples.append(sample)

        remaining = deadline - time.monotonic()
        if remaining <= 0:
            break
        time.sleep(min(args.interval, remaining))

    elapsed = time.monotonic() - started
    report = {
        "schema_version": 1,
        "label": args.label,
        "started_at_utc": started_at_utc,
        "finished_at_utc": utc_now(),
        "requested_seconds": args.seconds,
        "interval_seconds": args.interval,
        "elapsed_seconds": elapsed,
        "warning": (
            "These are whole-GPU utilization counters from macOS AGX ioreg. "
            "They are not process-specific and do not measure power consumption."
        ),
        "samples": samples,
        "summary": summarize(samples),
    }
    encoded = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output:
        try:
            Path(args.output).write_text(encoded, encoding="utf-8")
        except OSError as error:
            print(f"Could not write --output: {error}", file=sys.stderr)
            return 1
    else:
        sys.stdout.write(encoded)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
