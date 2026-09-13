"""Report fixtures and source-registration checks; no Unreal process is needed."""

from collections import Counter
from contextlib import redirect_stderr, redirect_stdout
from copy import deepcopy
import importlib.util
import io
import json
from pathlib import Path
import re
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "scripts" / "lib" / "unreal_suites.json"
SPEC = importlib.util.spec_from_file_location(
    "validate_unreal_report", ROOT / "scripts" / "validate-unreal-report.py"
)
validator = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(validator)


def successful_report(paths):
    return {
        "succeeded": len(paths),
        "succeededWithWarnings": 0,
        "failed": 0,
        "notRun": 0,
        "inProcess": 0,
        "tests": [
            {
                "fullTestPath": path, "state": "Success", "errors": 0,
                "warnings": 0, "entries": [],
            }
            for path in paths
        ],
    }


def event(message, kind="Info", context=""):
    return {"event": {"type": kind, "message": message, "context": context}}


def idevice_pair(engine_root="/Users/Shared/Epic Games/UE_5.8"):
    tool = engine_root + "/Engine/Extras/ThirdPartyNotUE/libimobiledevice/Mac/idevice_id"
    return [
        event(
            f'LogHAL: FMacPlatformProcess::CreateProc: posix_spawn("{tool}") '
            "failed (86, Bad CPU type in executable)", "Warning", "log",
        ),
        event(f"LogHAL: Failed to launch Tool. ({tool})", "Warning", "log"),
    ]


def report_with_warnings(paths, entries):
    report = successful_report(paths)
    report["tests"][0]["entries"] = entries
    report["tests"][0]["warnings"] = sum(
        entry["event"]["type"] == "Warning" for entry in entries
    )
    if report["tests"][0]["warnings"]:
        report["succeeded"] -= 1
        report["succeededWithWarnings"] = 1
    return report


class UnrealReportTests(unittest.TestCase):
    def setUp(self):
        self.manifest = validator.load_manifest(MANIFEST)
        _, self.paths = validator.select_suites(self.manifest, ["integration"])

    def assertRejected(self, report, pattern, **kwargs):
        with self.assertRaisesRegex(validator.ValidationError, pattern):
            validator.validate_report(report, self.paths, **kwargs)

    def test_all_manifest_suites_have_valid_complete_fixtures(self):
        for name in self.manifest["suites"]:
            with self.subTest(suite=name):
                _, paths = validator.select_suites(self.manifest, [name])
                result = validator.validate_report(successful_report(paths), paths)
                self.assertEqual(result, {
                    "tests": len(paths), "warning_tests": 0, "allowed_warnings": 0,
                })

    def test_combined_suites_deduplicate_expected_paths_and_filters(self):
        test_filter, paths = validator.select_suites(
            self.manifest, ["integration", "tutorials", "integration"]
        )
        self.assertEqual(test_filter, "Cinderline.Integration+Cinderline.Tutorial")
        self.assertEqual(set(paths), set(self.paths) | set(
            self.manifest["suites"]["tutorials"]["expected_paths"]
        ))
        validator.validate_report(successful_report(paths), paths)

    def test_wrong_aggregate_counts_fail(self):
        for field in ("succeeded", "succeededWithWarnings", "failed", "notRun", "inProcess"):
            with self.subTest(field=field):
                report = successful_report(self.paths)
                report[field] += 1
                self.assertRejected(report, field)

    def test_noninteger_and_negative_aggregate_counts_fail(self):
        for field in ("succeeded", "succeededWithWarnings", "failed", "notRun", "inProcess"):
            for value in (None, True, False, "0", 0.0, [], {}, -1):
                with self.subTest(field=field, value=value):
                    report = successful_report(self.paths)
                    report[field] = value
                    self.assertRejected(report, field + " must be a nonnegative integer")

    def test_missing_test_fails_even_when_counts_are_adjusted(self):
        report = successful_report(self.paths)
        report["tests"].pop()
        report["succeeded"] -= 1
        self.assertRejected(report, "missing test paths")

    def test_duplicate_test_fails_even_when_length_and_counts_match(self):
        report = successful_report(self.paths)
        report["tests"][-1] = deepcopy(report["tests"][0])
        self.assertRejected(report, "duplicate test path")

    def test_unexpected_test_fails_even_when_length_and_counts_match(self):
        report = successful_report(self.paths)
        report["tests"][-1]["fullTestPath"] = "Cinderline.Integration.Unrequested"
        self.assertRejected(report, "unexpected test path")

    def test_unsuccessful_states_fail_with_successful_aggregate_counts(self):
        for state in ("Fail", "NotRun", "InProcess", "Skipped", "success", None, 1, []):
            with self.subTest(state=state):
                report = successful_report(self.paths)
                report["tests"][0]["state"] = state
                self.assertRejected(report, "ended in state")

    def test_per_test_counts_require_nonnegative_integers(self):
        for field in ("errors", "warnings"):
            for value in (None, True, False, "0", 0.0, [], {}, -1):
                with self.subTest(field=field, value=value):
                    report = successful_report(self.paths)
                    report["tests"][0][field] = value
                    self.assertRejected(report, field + " must be a nonnegative integer")

    def test_error_counter_fails(self):
        report = successful_report(self.paths)
        report["tests"][0]["errors"] = 1
        self.assertRejected(report, "has 1 errors")

    def test_error_event_fails_even_with_zero_counter(self):
        report = successful_report(self.paths)
        report["tests"][0]["entries"] = [event("Assertion failed", "Error")]
        self.assertRejected(report, "contains an error event")

    def test_malformed_report_and_test_shapes_fail(self):
        for value in (None, [], "report", 1):
            with self.subTest(root=value):
                self.assertRejected(value, "report root")
        for value in (None, {}, "tests", 1):
            with self.subTest(tests=value):
                report = successful_report(self.paths)
                report["tests"] = value
                self.assertRejected(report, "tests must be an array")
        report = successful_report(self.paths)
        report["tests"][0] = []
        self.assertRejected(report, "test entry must be an object")
        report = successful_report(self.paths)
        report["tests"][0]["fullTestPath"] = []
        self.assertRejected(report, "unexpected test path")

    def test_malformed_entry_shapes_and_types_fail(self):
        cases = (
            (None, "entries must be an array"),
            ({}, "entries must be an array"),
            ([None], "malformed event entry"),
            ([{}], "malformed event entry"),
            ([{"event": []}], "malformed event entry"),
            ([{"event": {"message": "x"}}], "invalid event type"),
            ([{"event": {"type": [], "message": "x"}}], "invalid event type"),
            ([{"event": {"type": "Warning", "message": 5}}], "non-string event message"),
            ([event("x", "warning")], "invalid event type"),
            ([event("x", context=None)], "non-string event context"),
        )
        for entries, pattern in cases:
            with self.subTest(entries=entries):
                report = successful_report(self.paths)
                report["tests"][0]["entries"] = entries
                self.assertRejected(report, pattern)

    def test_info_events_are_accepted(self):
        report = successful_report(self.paths)
        report["tests"][0]["entries"] = [event("Assertions complete")]
        validator.validate_report(report, self.paths)

    def test_known_idevice_warning_requires_explicit_opt_in(self):
        report = report_with_warnings(self.paths, idevice_pair())
        self.assertRejected(report, "warnings are rejected by default")
        result = validator.validate_report(report, self.paths, allow_idevice_id_warning=True)
        self.assertEqual(result["allowed_warnings"], 2)
        self.assertEqual(result["warning_tests"], 1)

    def test_known_warning_accepts_both_observed_engine_locations(self):
        roots = (
            "/Users/Shared/Epic Games/UE_5.8",
            "/Users/chirag13/Documents/ChatGPT/CinderlineEngineIOS27",
        )
        for root in roots:
            with self.subTest(root=root):
                validator.validate_report(
                    report_with_warnings(self.paths, idevice_pair(root)), self.paths,
                    allow_idevice_id_warning=True,
                )

    def test_repeated_complete_warning_pairs_count_every_event(self):
        report = report_with_warnings(self.paths, idevice_pair() + idevice_pair())
        result = validator.validate_report(report, self.paths, allow_idevice_id_warning=True)
        self.assertEqual(result["allowed_warnings"], 4)
        self.assertEqual(result["warning_tests"], 1)

    def test_warning_totals_must_match_entries(self):
        for count in (0, 1, 3):
            with self.subTest(count=count):
                report = report_with_warnings(self.paths, idevice_pair())
                report["tests"][0]["warnings"] = count
                self.assertRejected(report, "warning events", allow_idevice_id_warning=True)
        report = successful_report(self.paths)
        report["tests"][0]["warnings"] = 2
        self.assertRejected(report, "warning events", allow_idevice_id_warning=True)

    def test_warning_aggregate_counts_are_derived_from_tests(self):
        for field, value in (("succeeded", len(self.paths)), ("succeededWithWarnings", 0)):
            with self.subTest(field=field):
                report = report_with_warnings(self.paths, idevice_pair())
                report[field] = value
                self.assertRejected(report, field, allow_idevice_id_warning=True)

    def test_other_warning_is_rejected_even_with_opt_in(self):
        report = report_with_warnings(self.paths, [event("Gameplay warning", "Warning")])
        self.assertRejected(report, "incomplete idevice_id", allow_idevice_id_warning=True)
        report = report_with_warnings(self.paths, idevice_pair() + [
            event("Gameplay warning", "Warning"), event("Another warning", "Warning"),
        ])
        self.assertRejected(report, "unapproved warning", allow_idevice_id_warning=True)

    def test_known_warning_text_and_path_must_match_exactly(self):
        replacements = (
            (0, "86, Bad CPU type in executable", "13, Permission denied"),
            (0, "idevice_id", "ideviceinfo"),
            (1, "UE_5.8", "OtherEngine"),
            (1, "Failed to launch Tool.", "Failed to launch Tool. ignored"),
            (0, "LogHAL:", "Unrelated: LogHAL:"),
            (0, ") failed", ") failed unexpectedly"),
        )
        for index, before, after in replacements:
            with self.subTest(replacement=(index, before, after)):
                entries = idevice_pair()
                entries[index]["event"]["message"] = entries[index]["event"]["message"].replace(before, after)
                report = report_with_warnings(self.paths, entries)
                self.assertRejected(report, "unapproved", allow_idevice_id_warning=True)

    def test_incomplete_reordered_and_nonlog_pairs_fail(self):
        for entries in (idevice_pair()[:1], idevice_pair()[1:], list(reversed(idevice_pair()))):
            with self.subTest(entries=entries):
                self.assertRejected(
                    report_with_warnings(self.paths, entries), "idevice_id|unapproved",
                    allow_idevice_id_warning=True,
                )
        entries = idevice_pair()
        entries[0]["event"]["context"] = "assertion"
        self.assertRejected(
            report_with_warnings(self.paths, entries), "unapproved", allow_idevice_id_warning=True
        )
        for root in ("/tmp/../EngineCopy", "/tmp//EngineCopy", "/tmp/./EngineCopy"):
            with self.subTest(root=root):
                self.assertRejected(
                    report_with_warnings(self.paths, idevice_pair(root)), "unapproved",
                    allow_idevice_id_warning=True,
                )

    def test_json_reader_accepts_bom_but_rejects_duplicate_keys_and_non_json_numbers(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "report.json"
            path.write_text(json.dumps(successful_report(self.paths)), encoding="utf-8-sig")
            validator.validate_report(validator.read_json(path), self.paths)
            for content in ('{"succeeded": 1, "succeeded": 2}', '{"x": NaN}', '{"x": Infinity}', '{'):
                with self.subTest(content=content):
                    path.write_text(content, encoding="utf-8")
                    with self.assertRaises(validator.ValidationError):
                        validator.read_json(path)

    def test_manifest_schema_rejects_malformed_and_duplicate_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "manifest.json"
            mutations = (
                ("schema_version", True),
                ("suites", []),
                ("local_exclusions", []),
            )
            for key, value in mutations:
                with self.subTest(key=key):
                    manifest = deepcopy(self.manifest)
                    manifest[key] = value
                    path.write_text(json.dumps(manifest), encoding="utf-8")
                    with self.assertRaises(validator.ValidationError):
                        validator.load_manifest(path)
            for key, value in (
                ("expected_paths", self.paths + self.paths[:1]),
                ("expected_paths", []),
                ("expected_paths", ["Cinderline.Other.Unselected"]),
                ("filter", "Cinderline.Integration; Quit"),
                ("filter", "Cinderline.Integration+Cinderline.Integration"),
            ):
                with self.subTest(key=key, value=value):
                    manifest = deepcopy(self.manifest)
                    manifest["suites"]["integration"][key] = value
                    path.write_text(json.dumps(manifest), encoding="utf-8")
                    with self.assertRaises(validator.ValidationError):
                        validator.load_manifest(path)

    def test_cli_prints_only_the_filter(self):
        output = io.StringIO()
        with redirect_stdout(output):
            code = validator.main([
                "--manifest", str(MANIFEST), "--suite", "integration",
                "--suite", "tutorials", "--print-filter",
            ])
        self.assertEqual(code, 0)
        self.assertEqual(output.getvalue(), "Cinderline.Integration+Cinderline.Tutorial\n")

    def test_cli_validates_positional_report_and_reports_rejection(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "index.json"
            path.write_text(json.dumps(successful_report(self.paths)), encoding="utf-8")
            with redirect_stdout(io.StringIO()):
                self.assertEqual(validator.main([
                    "--manifest", str(MANIFEST), "--suite", "integration", str(path),
                ]), 0)
            path.write_text("{}", encoding="utf-8")
            errors = io.StringIO()
            with redirect_stderr(errors):
                self.assertEqual(validator.main(["--suite", "integration", str(path)]), 1)
            self.assertIn(str(path), errors.getvalue())
            self.assertIn("verification failed", errors.getvalue())

    def test_unknown_suite_fails_before_report_validation(self):
        with self.assertRaisesRegex(validator.ValidationError, "unknown suite"):
            validator.select_suites(self.manifest, ["misspelled"])


class UnrealSuiteRegistryTests(unittest.TestCase):
    def test_manifest_matches_source_registrations_and_filter_coverage(self):
        manifest = validator.load_manifest(MANIFEST)
        # Search all Source .cpp files: LAN discovery registers tests beside its
        # implementation, outside Private/Tests. Do not derive expected counts
        # from a second hand-maintained list that can drift with the manifest.
        macro = re.compile(r"\bIMPLEMENT_\w*AUTOMATION_TEST\s*\(")
        pretty_name = re.compile(r'"(Cinderline(?:\.[A-Za-z][A-Za-z0-9_]*)+)"')
        registered = []
        for source in sorted((ROOT / "Source").rglob("*.cpp")):
            content = source.read_text(encoding="utf-8")
            matches = list(macro.finditer(content))
            for index, match in enumerate(matches):
                # The display path is in the macro arguments, before RunTest.
                end = matches[index + 1].start() if index + 1 < len(matches) else len(content)
                declaration = content[match.end():end].split("bool ", 1)[0]
                names = pretty_name.findall(declaration)
                self.assertEqual(len(names), 1, f"Cannot resolve registration in {source}:{content[:match.start()].count(chr(10)) + 1}")
                registered.extend(names)
        self.assertTrue(registered, "No Unreal test registrations were found")
        duplicates = {path: count for path, count in Counter(registered).items() if count != 1}
        self.assertFalse(duplicates, f"Duplicate source registrations: {duplicates}")
        registry = set(registered)
        for name, suite in manifest["suites"].items():
            with self.subTest(suite=name):
                filters = suite["filter"].split("+")
                selected = {
                    path for path in registry
                    if any(path == part or path.startswith(part + ".") for part in filters)
                }
                self.assertEqual(
                    set(suite["expected_paths"]), selected,
                    f"Suite {name!r} drifted from its source registrations or filter",
                )
        local = set(manifest["suites"]["local-all"]["expected_paths"])
        excluded = set(manifest["local_exclusions"])
        self.assertFalse(local & excluded, "A local test is also explicitly excluded")
        self.assertEqual(local | excluded, registry, "Classify every registered test as local or explicitly excluded")
        self.assertTrue(
            set(manifest["suites"]["multiplayer"]["expected_paths"]) <= excluded,
            "Transport tests must remain excluded from local-all",
        )


if __name__ == "__main__":
    unittest.main()
