#!/usr/bin/env python3
"""Join simulation.update signposts to thread-specific Time Profiler samples."""

from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET


def local_name(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


class TraceXml:
    def __init__(self, path: Path) -> None:
        self.path = path
        self.root = ET.parse(path).getroot()
        self.ids = {
            element.attrib["id"]: element
            for element in self.root.iter()
            if "id" in element.attrib
        }

    def resolve(self, element: ET.Element | None) -> ET.Element | None:
        seen: set[str] = set()
        while element is not None and "ref" in element.attrib:
            reference = element.attrib["ref"]
            if reference in seen:
                raise ValueError(f"cyclic XML reference {reference!r} in {self.path}")
            seen.add(reference)
            try:
                element = self.ids[reference]
            except KeyError as error:
                raise ValueError(
                    f"unresolved XML reference {reference!r} in {self.path}"
                ) from error
        return element

    def children(self, element: ET.Element, name: str) -> list[ET.Element]:
        return [
            resolved
            for child in element
            if local_name(child.tag) == name
            and (resolved := self.resolve(child)) is not None
        ]

    def child(self, element: ET.Element, name: str) -> ET.Element | None:
        matches = self.children(element, name)
        return matches[0] if matches else None

    def text(self, element: ET.Element | None) -> str:
        resolved = self.resolve(element)
        return (resolved.text or "").strip() if resolved is not None else ""

    def integer(self, element: ET.Element | None) -> int:
        value = self.text(element)
        if not value:
            raise ValueError(f"missing integer value in {self.path}")
        return int(value)

    def display(self, element: ET.Element | None) -> str:
        resolved = self.resolve(element)
        if resolved is None:
            return ""
        return resolved.attrib.get("fmt", self.text(resolved))

    def rows(self):
        return (element for element in self.root.iter() if local_name(element.tag) == "row")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def process_identity(document: TraceXml, row: ET.Element) -> dict[str, object]:
    process = document.child(row, "process")
    pid = document.integer(document.child(process, "pid")) if process is not None else None
    return {"pid": pid, "name": document.display(process)}


def thread_identity(document: TraceXml, row: ET.Element) -> dict[str, object]:
    thread = document.child(row, "thread")
    tid = document.integer(document.child(thread, "tid")) if thread is not None else None
    return {"tid": tid, "name": document.display(thread)}


def value(document: TraceXml, row: ET.Element, name: str) -> str:
    element = document.child(row, name)
    return document.display(element) or document.text(element)


def select_intervals(
    document: TraceXml, ticks: list[int], signpost_name: str
) -> list[dict[str, object]]:
    requested = set(ticks)
    found: dict[int, list[dict[str, object]]] = {tick: [] for tick in ticks}
    tick_pattern = re.compile(r"\btick=([0-9,]+)\b")
    phase_pattern = re.compile(r"\bphase=([^\s]+)")

    for row in document.rows():
        if value(document, row, "signpost-name") != signpost_name:
            continue
        metadata_elements = document.children(row, "os-log-metadata")
        metadata = document.display(metadata_elements[0]) if metadata_elements else ""
        match = tick_pattern.search(metadata)
        if match is None:
            continue
        tick = int(match.group(1).replace(",", ""))
        if tick not in requested:
            continue
        start_ns = document.integer(document.child(row, "start-time"))
        duration_ns = document.integer(document.child(row, "duration"))
        phase_match = phase_pattern.search(metadata)
        found[tick].append(
            {
                "tick": tick,
                "phase": phase_match.group(1) if phase_match else None,
                "start_ns": start_ns,
                "end_ns_exclusive": start_ns + duration_ns,
                "duration_ns": duration_ns,
                "duration_ms": duration_ns / 1_000_000.0,
                "signpost_name": signpost_name,
                "category": value(document, row, "category"),
                "subsystem": value(document, row, "subsystem"),
                "signpost_identifier": value(document, row, "os-signpost-identifier"),
                "metadata": metadata,
                "process": process_identity(document, row),
                "thread": thread_identity(document, row),
            }
        )

    intervals: list[dict[str, object]] = []
    for tick in ticks:
        matches = found[tick]
        if len(matches) != 1:
            raise ValueError(
                f"expected exactly one {signpost_name!r} interval for tick {tick}, "
                f"found {len(matches)}"
            )
        intervals.append(matches[0])
    return intervals


def frame_chain(document: TraceXml, row: ET.Element) -> tuple[str, ...]:
    backtrace = document.child(row, "tagged-backtrace")
    if backtrace is None:
        return ()
    frames: list[str] = []
    for child in backtrace:
        if local_name(child.tag) != "frame":
            continue
        frame = document.resolve(child)
        if frame is None:
            continue
        name = frame.attrib.get("name") or frame.attrib.get("fmt")
        if not name:
            name = f"[unknown frame {frame.attrib.get('addr', '?')}]"
        frames.append(name)
    return tuple(frames)


def ranked(counter: Counter[str], total: int) -> list[dict[str, object]]:
    return [
        {
            "frame": frame,
            "sample_count": count,
            "sample_percent": 100.0 * count / total if total else 0.0,
        }
        for frame, count in sorted(counter.items(), key=lambda item: (-item[1], item[0]))
    ]


def join_samples(document: TraceXml, intervals: list[dict[str, object]]) -> None:
    samples: list[dict[str, object]] = []
    for row in document.rows():
        sample_time = document.integer(document.child(row, "sample-time"))
        samples.append(
            {
                "time_ns": sample_time,
                "weight_ns": document.integer(document.child(row, "weight")),
                "process": process_identity(document, row),
                "thread": thread_identity(document, row),
                "chain": frame_chain(document, row),
            }
        )

    for interval in intervals:
        start_ns = int(interval["start_ns"])
        end_ns = int(interval["end_ns_exclusive"])
        in_range = [sample for sample in samples if start_ns <= sample["time_ns"] < end_ns]
        joined = [
            sample
            for sample in in_range
            if sample["process"]["pid"] == interval["process"]["pid"]
            and sample["thread"]["tid"] == interval["thread"]["tid"]
        ]
        with_backtraces = [sample for sample in joined if sample["chain"]]
        inclusive: Counter[str] = Counter()
        leaf: Counter[str] = Counter()
        chains: Counter[tuple[str, ...]] = Counter()
        for sample in with_backtraces:
            chain = sample["chain"]
            inclusive.update(set(chain))
            leaf[chain[0]] += 1
            chains[chain] += 1

        dominant_chain, dominant_count = (chains.most_common(1)[0] if chains else ((), 0))
        count = len(with_backtraces)
        interval["sample_join"] = {
            "range_predicate": "start_ns <= sample_time_ns < end_ns_exclusive",
            "identity_predicate": "sample PID and TID equal interval PID and TID",
            "samples_in_time_range_all_threads": len(in_range),
            "samples_excluded_by_process_or_thread": len(in_range) - len(joined),
            "joined_sample_rows": len(joined),
            "joined_samples_with_backtraces": count,
            "joined_samples_without_backtraces": len(joined) - count,
            "sampled_weight_ns": sum(int(sample["weight_ns"]) for sample in joined),
            "sampled_weight_ms": sum(int(sample["weight_ns"]) for sample in joined)
            / 1_000_000.0,
            "first_sample_time_ns": min((int(sample["time_ns"]) for sample in joined), default=None),
            "last_sample_time_ns": max((int(sample["time_ns"]) for sample in joined), default=None),
        }
        interval["inclusive_sample_counts"] = ranked(inclusive, count)
        interval["leaf_sample_counts"] = ranked(leaf, count)
        interval["dominant_full_chain"] = {
            "sample_count": dominant_count,
            "sample_percent": 100.0 * dominant_count / count if count else 0.0,
            "frames_leaf_to_root": list(dominant_chain),
        }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("signposts", type=Path, help="xctrace signpost interval XML")
    parser.add_argument("time_profile", type=Path, help="xctrace Time Profiler XML")
    parser.add_argument("--ticks", nargs="+", type=int, default=[6416, 8827])
    parser.add_argument("--signpost-name", default="simulation.update")
    parser.add_argument("--output", type=Path, help="write JSON here instead of stdout")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    signposts = TraceXml(args.signposts)
    time_profile = TraceXml(args.time_profile)
    intervals = select_intervals(signposts, args.ticks, args.signpost_name)
    join_samples(time_profile, intervals)
    report = {
        "schema_version": 1,
        "inputs": {
            "signposts": {"path": str(args.signposts), "sha256": sha256(args.signposts)},
            "time_profile": {
                "path": str(args.time_profile),
                "sha256": sha256(args.time_profile),
            },
        },
        "selection": {
            "signpost_name": args.signpost_name,
            "ticks": args.ticks,
            "interval_range": "half-open [start_ns, end_ns_exclusive)",
            "sample_identity": "exact PID and TID match",
        },
        "sampling_note": (
            "Counts are Time Profiler sample evidence (1 ms weights in this capture), "
            "not exact CPU duration, wall-clock utilization, or per-frame coverage."
        ),
        "count_semantics": {
            "inclusive": "samples whose backtrace contains the frame at least once",
            "leaf": "samples whose first (leaf) frame is the frame",
            "full_chain": "exact ordered frame sequence from leaf to root",
        },
        "intervals": intervals,
    }
    encoded = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded, encoding="utf-8")
    else:
        print(encoded, end="")


if __name__ == "__main__":
    main()
