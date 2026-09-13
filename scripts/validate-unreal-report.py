#!/usr/bin/env python3
"""Print Unreal suite filters or verify their complete automation reports.

The manifest is the source of truth for filters and exact expected test paths.
Validation checks UE 5.8's lower-initial-case result fields and event entries;
editor exit status alone does not establish that the selected tests passed.
"""

import argparse
import json
from pathlib import Path, PurePosixPath
import re
import sys


DEFAULT_MANIFEST = Path(__file__).resolve().parent / "lib" / "unreal_suites.json"
TEST_PATH = re.compile(r"Cinderline(?:\.[A-Za-z][A-Za-z0-9_]*)+")
IDEVICE_SUFFIX = "/Engine/Extras/ThirdPartyNotUE/libimobiledevice/Mac/idevice_id"
IDEVICE_SPAWN = re.compile(
    r'LogHAL: FMacPlatformProcess::CreateProc: posix_spawn\("(?P<tool>/[^"\r\n]+)"\) '
    r"failed \(86, Bad CPU type in executable\)"
)


class ValidationError(ValueError):
    """The manifest or report cannot establish a complete successful run."""


def _unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValidationError(f"duplicate JSON key {key!r}")
        result[key] = value
    return result


def _invalid_constant(value):
    raise ValidationError(f"non-JSON number {value!r}")


def read_json(path):
    """Read UTF-8 or BOM-prefixed Unreal JSON without lossy duplicate keys."""
    try:
        with Path(path).open(encoding="utf-8-sig") as source:
            return json.load(
                source, object_pairs_hook=_unique_object, parse_constant=_invalid_constant
            )
    except (OSError, UnicodeError, ValueError) as error:
        raise ValidationError(f"cannot read {path}: {error}") from error


def _integer(value, label):
    # bool is an int subclass in Python but is never an automation count.
    if type(value) is not int or value < 0:
        raise ValidationError(f"{label} must be a nonnegative integer, got {value!r}")
    return value


def _test_path(value, label):
    if not isinstance(value, str) or TEST_PATH.fullmatch(value) is None:
        raise ValidationError(f"{label} is not a Cinderline test path: {value!r}")
    return value


def load_manifest(path):
    manifest = read_json(path)
    if not isinstance(manifest, dict):
        raise ValidationError("manifest root must be an object")
    if type(manifest.get("schema_version")) is not int or manifest["schema_version"] != 1:
        raise ValidationError("manifest schema_version must be 1")
    suites = manifest.get("suites")
    if not isinstance(suites, dict) or not suites:
        raise ValidationError("manifest suites must be a nonempty object")
    for name, suite in suites.items():
        if not isinstance(name, str) or not re.fullmatch(r"[a-z][a-z0-9-]*", name):
            raise ValidationError(f"invalid suite key {name!r}")
        if not isinstance(suite, dict):
            raise ValidationError(f"suite {name!r} must be an object")
        test_filter = suite.get("filter")
        if not isinstance(test_filter, str) or not test_filter:
            raise ValidationError(f"suite {name!r} filter must be a nonempty string")
        parts = test_filter.split("+")
        for part in parts:
            _test_path(part, f"suite {name!r} filter")
        if len(parts) != len(set(parts)):
            raise ValidationError(f"suite {name!r} repeats a filter")
        paths = suite.get("expected_paths")
        if not isinstance(paths, list) or not paths:
            raise ValidationError(f"suite {name!r} expected_paths must be a nonempty array")
        seen = set()
        for test in paths:
            _test_path(test, f"suite {name!r} expected path")
            if test in seen:
                raise ValidationError(f"suite {name!r} repeats expected path {test!r}")
            if not any(test == part or test.startswith(part + ".") for part in parts):
                raise ValidationError(f"suite {name!r} filter does not select {test!r}")
            seen.add(test)
    exclusions = manifest.get("local_exclusions", {})
    if not isinstance(exclusions, dict):
        raise ValidationError("manifest local_exclusions must be an object")
    for path, reason in exclusions.items():
        _test_path(path, "local exclusion")
        if not isinstance(reason, str) or not reason.strip():
            raise ValidationError(f"local exclusion {path!r} requires a reason")
    return manifest


def select_suites(manifest, names):
    """Return the combined filter and unique expected paths for selected suites."""
    if not names:
        raise ValidationError("at least one suite is required")
    filters = []
    paths = set()
    for name in names:
        suite = manifest["suites"].get(name)
        if suite is None:
            choices = ", ".join(manifest["suites"])
            raise ValidationError(f"unknown suite {name!r}; choose {choices}")
        for part in suite["filter"].split("+"):
            if part not in filters:
                filters.append(part)
        paths.update(suite["expected_paths"])
    return "+".join(filters), sorted(paths)


def _validate_idevice_warnings(events, path):
    """Only accept complete, matching pairs from the known macOS host failure.

    Recorded examples are in Saved/TutorialControls/automation/index.json and
    Saved/OnlineLAN/runs/20260913T144007Z-64399/UnrealReport-loopback/index.json.
    Engine locations vary, but the executable suffix and both messages do not.
    Repeated complete pairs are allowed because device discovery can repeat.
    """
    if len(events) % 2:
        raise ValidationError(f"{path} has an incomplete idevice_id warning pair")
    for offset in range(0, len(events), 2):
        first, second = events[offset:offset + 2]
        match = IDEVICE_SPAWN.fullmatch(first["message"])
        if match is None:
            raise ValidationError(f"{path} has an unapproved warning: {first['message']!r}")
        tool = match.group("tool")
        if (
            not tool.endswith(IDEVICE_SUFFIX)
            or any(ord(character) < 32 for character in tool)
            or ".." in tool.split("/")
            or str(PurePosixPath(tool)) != tool
            or first.get("context") != "log"
            or second.get("context") != "log"
            or second["message"] != f"LogHAL: Failed to launch Tool. ({tool})"
        ):
            raise ValidationError(f"{path} has an unapproved idevice_id warning pair")


def validate_report(report, expected_paths, *, allow_idevice_id_warning=False):
    """Return verified counts, or raise ValidationError on any incomplete proof."""
    if not isinstance(report, dict):
        raise ValidationError("report root must be an object")
    counts = {
        field: _integer(report.get(field), f"report {field}")
        for field in ("succeeded", "succeededWithWarnings", "failed", "notRun", "inProcess")
    }
    for field in ("failed", "notRun", "inProcess"):
        if counts[field] != 0:
            raise ValidationError(f"report {field}={counts[field]}, expected 0")

    expected = set(expected_paths)
    if not expected or len(expected) != len(expected_paths):
        raise ValidationError("expected test paths must be nonempty and unique")
    tests = report.get("tests")
    if not isinstance(tests, list):
        raise ValidationError("report tests must be an array")
    seen = set()
    for test in tests:
        if not isinstance(test, dict):
            raise ValidationError("test entry must be an object")
        path = test.get("fullTestPath")
        if not isinstance(path, str) or path not in expected:
            raise ValidationError(f"unexpected test path {path!r}")
        if path in seen:
            raise ValidationError(f"duplicate test path {path!r}")
        seen.add(path)
    missing = expected - seen
    if missing:
        raise ValidationError(f"missing test paths: {', '.join(sorted(missing))}")

    warning_tests = 0
    warning_total = 0
    for test in tests:
        path = test["fullTestPath"]
        if test.get("state") != "Success":
            raise ValidationError(f"{path} ended in state {test.get('state')!r}, expected 'Success'")
        errors = _integer(test.get("errors"), f"{path} errors")
        warnings = _integer(test.get("warnings"), f"{path} warnings")
        if errors:
            raise ValidationError(f"{path} has {errors} errors")
        entries = test.get("entries")
        if not isinstance(entries, list):
            raise ValidationError(f"{path} entries must be an array")
        warning_events = []
        for entry in entries:
            if not isinstance(entry, dict) or not isinstance(entry.get("event"), dict):
                raise ValidationError(f"{path} has a malformed event entry")
            event = entry["event"]
            kind = event.get("type")
            if not isinstance(kind, str) or kind not in ("Info", "Warning", "Error"):
                raise ValidationError(f"{path} has an invalid event type {kind!r}")
            if not isinstance(event.get("message"), str):
                raise ValidationError(f"{path} has a non-string event message")
            if "context" in event and not isinstance(event["context"], str):
                raise ValidationError(f"{path} has a non-string event context")
            if kind == "Error":
                raise ValidationError(f"{path} contains an error event: {event['message']!r}")
            if kind == "Warning":
                warning_events.append(event)
        if warnings != len(warning_events):
            raise ValidationError(
                f"{path} warnings={warnings}, but contains {len(warning_events)} warning events"
            )
        if warnings:
            if not allow_idevice_id_warning:
                raise ValidationError(f"{path} has {warnings} warnings; warnings are rejected by default")
            _validate_idevice_warnings(warning_events, path)
            warning_tests += 1
            warning_total += warnings

    required = {
        "succeeded": len(expected) - warning_tests,
        "succeededWithWarnings": warning_tests,
    }
    for field, value in required.items():
        if counts[field] != value:
            raise ValidationError(f"report {field}={counts[field]}, expected {value} from test entries")
    return {"tests": len(expected), "warning_tests": warning_tests, "allowed_warnings": warning_total}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", nargs="?", type=Path, help="Unreal's exported index.json")
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST, help="suite manifest JSON")
    parser.add_argument("--suite", action="append", required=True, help="suite key; may be repeated")
    parser.add_argument("--print-filter", action="store_true", help="print the selected Unreal filter only")
    parser.add_argument(
        "--allow-idevice-id-warning", action="store_true",
        help="allow only paired macOS idevice_id Bad CPU type host warnings; all other warnings fail",
    )
    args = parser.parse_args(argv)
    if args.print_filter and args.report is not None:
        parser.error("--print-filter cannot be combined with a report path")
    if not args.print_filter and args.report is None:
        parser.error("provide a report path or --print-filter")
    try:
        manifest = load_manifest(args.manifest)
        test_filter, paths = select_suites(manifest, args.suite)
        if args.print_filter:
            print(test_filter)
            return 0
        result = validate_report(
            read_json(args.report), paths,
            allow_idevice_id_warning=args.allow_idevice_id_warning,
        )
    except ValidationError as error:
        location = f" Report: {args.report}" if args.report is not None else ""
        print(f"Unreal report verification failed: {error}.{location}", file=sys.stderr)
        return 1
    detail = "no errors, warnings, or unfinished tests"
    if result["allowed_warnings"]:
        detail = (
            f"no errors or unfinished tests; explicitly allowed {result['allowed_warnings']} "
            f"idevice_id host warnings across {result['warning_tests']} tests"
        )
    print(f"Verified {result['tests']} expected Unreal tests: {detail}.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
