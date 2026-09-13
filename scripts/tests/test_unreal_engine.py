"""Engine resolver checks use temporary fake installations, never Unreal tools."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


HELPER = Path(__file__).resolve().parents[1] / "lib" / "unreal-engine.sh"
PROBE = r'''
set -euo pipefail
source "$1"
cinder_test_platform="$2"
uname() { printf '%s\n' "$cinder_test_platform"; }
cinder_find_engine "$3"
printf 'CINDER_ENGINE\t%s\t%s\t%s\t%s\t%s\t%s\n' "$engine_root" "$platform" "$editor" "$command_editor" "$build_script" "$using_prepared_metalfx_engine"
'''


class UnrealEngineResolverTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="cinder-engine-tests-")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name).resolve()
        self.project = self.directory / "Game project"
        (self.project / "scripts").mkdir(parents=True)
        # The real checker is replaced by a local argument-only fixture. It
        # fails if selection ever asks to prepare instead of checking.
        (self.project / "scripts" / "prepare-metalfx-engine.py").write_text(
            "import json, sys\n"
            "assert len(sys.argv) == 6 and sys.argv[-1] == '--check'\n"
            "print('CINDER_CHECK\\t' + json.dumps(sys.argv[1:]))\n",
            encoding="utf-8",
        )

    def engine(self, name="Engine installation"):
        root = self.directory / name
        for platform in ("Mac", "Linux"):
            (root / "Engine" / "Build" / "BatchFiles" / platform).mkdir(parents=True)
        return root

    def executable(self, path):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("#!/bin/sh\nexit 99 # Resolver must not execute this tool.\n", encoding="utf-8")
        path.chmod(0o755)
        return path

    def prepared(self):
        root = self.engine("CinderlineEngineIOS27")
        source = self.engine("Stock source")
        (root / ".cinderline-metalfx-engine.json").write_text(json.dumps({"sourceEngine": str(source)}), encoding="utf-8")
        return root, source

    def resolve(self, root=None, platform="Darwin", success=True):
        environment = os.environ.copy()
        environment.pop("UE_ROOT", None)
        if root is not None:
            environment["UE_ROOT"] = str(root)
        environment["CINDERLINE_TEST_PYTHON"] = sys.executable
        completed = subprocess.run(
            ["bash", "-c", PROBE, "engine-resolver-test", str(HELPER), platform, str(self.project)],
            env=environment, text=True, capture_output=True, timeout=10,
        )
        if not success:
            self.assertNotEqual(completed.returncode, 0, completed.stdout)
            self.assertNotIn("CINDER_ENGINE\t", completed.stdout)
            return completed
        self.assertEqual(completed.returncode, 0, completed.stderr)
        rows = [line.split("\t")[1:] for line in completed.stdout.splitlines() if line.startswith("CINDER_ENGINE\t")]
        self.assertEqual(len(rows), 1, completed.stdout)
        self.assertEqual(len(rows[0]), 6)
        return rows[0], completed

    def test_explicit_root_and_engine_subdirectory_with_spaces(self):
        root = self.engine()
        for candidate in (root, root / "Engine", str(root) + "/"):
            with self.subTest(candidate=candidate):
                values, completed = self.resolve(candidate)
                self.assertEqual(values[0], str(root.resolve()))
                self.assertEqual(values[1], "Mac")
                self.assertEqual(values[-1], "false")
                self.assertNotIn("CINDER_CHECK", completed.stdout)

    def test_explicit_root_beats_prepared_sibling(self):
        self.prepared()
        root = self.engine("Explicit engine")
        values, completed = self.resolve(root)
        self.assertEqual(values[0], str(root))
        self.assertNotIn("CINDER_CHECK", completed.stdout)

    def test_default_prepared_sibling_is_verified_read_only(self):
        root, source = self.prepared()
        values, completed = self.resolve()
        self.assertEqual(values[0], str(root))
        self.assertEqual(values[-1], "true")
        checks = [json.loads(line.split("\t", 1)[1]) for line in completed.stdout.splitlines() if line.startswith("CINDER_CHECK\t")]
        self.assertEqual(checks, [["--engine", str(source), "--destination", str(root), "--check"]])

    def test_explicit_prepared_engine_also_checks_marker(self):
        root, _ = self.prepared()
        values, completed = self.resolve(root / "Engine")
        self.assertEqual(values[-1], "true")
        self.assertIn("CINDER_CHECK\t", completed.stdout)

    def test_failed_prepared_check_does_not_fall_back_to_stock(self):
        self.prepared()
        (self.project / "scripts" / "prepare-metalfx-engine.py").write_text("raise SystemExit(7)\n", encoding="utf-8")
        completed = self.resolve(success=False)
        self.assertIn("failed verification", completed.stderr)

    def test_invalid_prepared_manifest_rejected_before_checker(self):
        root, _ = self.prepared()
        marker = root / ".cinderline-metalfx-engine.json"
        for contents in ("not-json", "[]", '{"sourceEngine": ""}', '{"sourceEngine": 3}'):
            with self.subTest(contents=contents):
                marker.write_text(contents, encoding="utf-8")
                completed = self.resolve(success=False)
                self.assertNotIn("CINDER_CHECK", completed.stdout)

    def test_missing_engine_and_unsupported_platform_fail(self):
        self.resolve(self.directory / "missing", success=False)
        result = self.resolve(self.engine(), platform="Windows_NT", success=False)
        self.assertIn("macOS and Linux", result.stderr)

    def test_symlink_is_canonicalized(self):
        root = self.engine()
        alias = self.directory / "Selected link"
        alias.symlink_to(root, target_is_directory=True)
        values, _ = self.resolve(alias / "Engine")
        self.assertEqual(values[0], str(root.resolve()))

    def test_mac_command_binary_fallbacks(self):
        root = self.engine()
        binaries = root / "Engine" / "Binaries" / "Mac"
        gui = self.executable(binaries / "UnrealEditor.app" / "Contents" / "MacOS" / "UnrealEditor")
        values, _ = self.resolve(root)
        self.assertEqual(values[3], str(gui))
        bare = self.executable(binaries / "UnrealEditor-Cmd")
        values, _ = self.resolve(root)
        self.assertEqual(values[3], str(bare))
        bundle = self.executable(binaries / "UnrealEditor-Cmd.app" / "Contents" / "MacOS" / "UnrealEditor-Cmd")
        values, _ = self.resolve(root)
        self.assertEqual(values[3], str(bundle))

    def test_linux_command_binary_fallbacks(self):
        root = self.engine()
        binaries = root / "Engine" / "Binaries" / "Linux"
        gui = self.executable(binaries / "UnrealEditor")
        values, _ = self.resolve(root, platform="Linux")
        self.assertEqual(values[1], "Linux")
        self.assertEqual(values[3], str(gui))
        command = self.executable(binaries / "UnrealEditor-Cmd")
        values, _ = self.resolve(root, platform="Linux")
        self.assertEqual(values[3], str(command))

    def test_tool_validation_checks_executable_without_launching_it(self):
        tool = self.directory / "Unreal tool"
        probe = 'set -euo pipefail; source "$1"; cinder_require_engine_tool "$2"'
        for state in ("missing", "not-executable", "executable"):
            with self.subTest(state=state):
                if state != "missing":
                    tool.write_text("#!/bin/sh\nexit 99\n", encoding="utf-8")
                    tool.chmod(0o755 if state == "executable" else 0o644)
                completed = subprocess.run(["bash", "-c", probe, "engine-tool-test", str(HELPER), str(tool)],
                    text=True, capture_output=True, timeout=10)
                self.assertEqual(completed.returncode, 0 if state == "executable" else 2, completed.stderr)


if __name__ == "__main__":
    unittest.main()
