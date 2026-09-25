"""Exercise unreal.sh's build path under /bin/bash using argument-only fixtures.

On macOS this uses the system Bash 3.2, including its nounset array behavior.
Every engine path points inside a temporary directory; no Unreal tool runs.
"""

import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile
import unittest


SCRIPTS = Path(__file__).resolve().parents[1]


def expected_default_parallel():
    """Mirror of the shell rule in scripts/unreal.sh: physical cores, capped at 10.

    The default used to be the constant 2, chosen for a 32 GB machine under cook
    memory pressure. Editor builds are not that case, so the default now follows
    the host and CINDERLINE_UE_JOBS restores a ceiling when memory is tight. The
    expectation is derived rather than hardcoded so this fixture keeps testing
    the rule instead of one machine's answer to it.
    """
    count = 0
    if platform.system() == "Darwin":
        probe = subprocess.run(["sysctl", "-n", "hw.physicalcpu"], text=True, capture_output=True)
        stdout = probe.stdout.strip()
        count = int(stdout) if stdout.isdigit() else 0
    elif platform.system() == "Linux":
        count = os.sysconf("SC_NPROCESSORS_ONLN")
    return min(count, 10) if count > 0 else 2


@unittest.skipUnless(Path("/bin/bash").is_file(), "Requires the system /bin/bash")
class UnrealBuildArgumentTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="cinder-build-args-")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name).resolve()
        self.project = self.directory / "Game project with spaces"
        scripts = self.project / "scripts"
        (scripts / "lib").mkdir(parents=True)
        shutil.copy2(SCRIPTS / "unreal.sh", scripts / "unreal.sh")
        shutil.copy2(SCRIPTS / "lib" / "unreal-engine.sh", scripts / "lib" / "unreal-engine.sh")
        (self.project / "Cinderline.uproject").write_text("{}\n", encoding="utf-8")
        self.build_capture = self.directory / "build arguments.bin"
        self.preparer_capture = self.directory / "preparer arguments.json"
        # The copied resolver can only verify this marker. Its checker fixture
        # rejects any invocation that could ask the real preparer to build.
        (scripts / "prepare-metalfx-engine.py").write_text(
            "import json, os, pathlib, sys\n"
            "assert len(sys.argv) == 6\n"
            "assert sys.argv[1] == '--engine' and sys.argv[3] == '--destination'\n"
            "assert sys.argv[5] == '--check'\n"
            "pathlib.Path(os.environ['CINDERLINE_PREPARER_CAPTURE']).write_text(\n"
            "    json.dumps(sys.argv[1:]), encoding='utf-8')\n",
            encoding="utf-8",
        )

    def make_engine(self, name):
        root = self.directory / name
        for target_platform in ("Mac", "Linux"):
            build = root / "Engine" / "Build" / "BatchFiles" / target_platform / "Build.sh"
            build.parent.mkdir(parents=True, exist_ok=True)
            # NUL separators preserve each argument exactly, including spaces.
            build.write_text(
                "#!/bin/bash\n"
                "set -eu\n"
                "printf '%s\\0' \"$@\" > \"$CINDERLINE_BUILD_CAPTURE\"\n"
                "printf '%s\\n' CINDER_FAKE_BUILD_CALLED\n",
                encoding="utf-8",
            )
            build.chmod(0o755)
        return root

    def capture_build(self, *, prepared, user_args, expected_parallel):
        source = self.make_engine("Stock engine with spaces")
        root = source
        if prepared:
            root = self.make_engine("Prepared engine with spaces")
            (root / ".cinderline-metalfx-engine.json").write_text(
                json.dumps({"sourceEngine": str(source)}), encoding="utf-8"
            )
        for capture in (self.build_capture, self.preparer_capture):
            if capture.exists():
                capture.unlink()
        environment = os.environ.copy()
        # Shell startup hooks are unrelated to this fixture and could launch
        # commands before the copied runner. Preserve the user's other values.
        for key in ("BASH_ENV", "ENV", "SHELLOPTS", "BASHOPTS", "CINDERLINE_BUILD_JOBS",
                    "CINDERLINE_UE_JOBS"):
            environment.pop(key, None)
        environment.update({
            "UE_ROOT": str(root),
            "CINDERLINE_TEST_PYTHON": sys.executable,
            "CINDERLINE_BUILD_CAPTURE": str(self.build_capture),
            "CINDERLINE_PREPARER_CAPTURE": str(self.preparer_capture),
        })
        completed = subprocess.run(
            ["/bin/bash", str(self.project / "scripts" / "unreal.sh"), "build", *user_args],
            cwd=self.project, env=environment, text=True, capture_output=True, timeout=10,
        )
        self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
        self.assertEqual(completed.stdout.splitlines().count("CINDER_FAKE_BUILD_CALLED"), 1)
        self.assertTrue(self.build_capture.is_file(), completed.stdout + completed.stderr)
        raw = self.build_capture.read_bytes()
        self.assertTrue(raw.endswith(b"\0"))
        args = [part.decode("utf-8") for part in raw[:-1].split(b"\0")]
        expected_platform = {"Darwin": "Mac", "Linux": "Linux"}[platform.system()]
        self.assertEqual(args[:5], [
            "CinderlineEditor", expected_platform, "Development",
            str(self.project / "Cinderline.uproject"), "-WaitMutex",
        ])
        parallel = [arg for arg in args if arg.startswith("-MaxParallelActions=")]
        self.assertEqual(parallel, [f"-MaxParallelActions={expected_parallel}"])
        for rule in ("-ForceRulesCompile", "-SkipRulesCompile"):
            self.assertEqual(args.count(rule), 1 if prepared else 0, args)
        remaining = [
            arg for arg in args[5:]
            if not arg.startswith("-MaxParallelActions=")
            and arg not in ("-ForceRulesCompile", "-SkipRulesCompile")
        ]
        self.assertEqual(remaining, [
            arg for arg in user_args if not arg.startswith("-MaxParallelActions=")
        ])
        if prepared:
            self.assertEqual(json.loads(self.preparer_capture.read_text(encoding="utf-8")), [
                "--engine", str(source), "--destination", str(root), "--check",
            ])
        else:
            self.assertFalse(self.preparer_capture.exists(), "Stock engine must not run the preparer")
        return args

    def test_default_parallel_from_physical_cores_with_zero_user_arguments(self):
        # This is the original Bash 3.2 failure path: empty optional arrays and
        # empty "$@" must still produce a valid build invocation under nounset.
        for prepared in (False, True):
            with self.subTest(prepared=prepared):
                self.capture_build(prepared=prepared, user_args=[],
                                   expected_parallel=expected_default_parallel())

    def test_explicit_parallel_one_as_only_user_argument(self):
        for prepared in (False, True):
            with self.subTest(prepared=prepared):
                self.capture_build(
                    prepared=prepared, user_args=["-MaxParallelActions=1"], expected_parallel=1,
                )

    def test_user_arguments_with_spaces_survive_default_and_explicit_parallel(self):
        spaced = ["-LogFile=Build logs/Editor build.log", "-Define=CINDERLINE_LABEL=two words"]
        for prepared in (False, True):
            for override in (False, True):
                with self.subTest(prepared=prepared, override=override):
                    args = spaced[:1] + (["-MaxParallelActions=1"] if override else []) + spaced[1:]
                    self.capture_build(
                        prepared=prepared, user_args=args, expected_parallel=1 if override else expected_default_parallel(),
                    )

    def test_last_parallel_override_is_emitted_exactly_once(self):
        for prepared in (False, True):
            with self.subTest(prepared=prepared):
                self.capture_build(
                    prepared=prepared,
                    user_args=[
                        "-MaxParallelActions=5", "-LogFile=Build logs/Editor build.log",
                        "-MaxParallelActions=3", "-Define=CINDERLINE_LABEL=two words",
                        "-MaxParallelActions=1",
                    ],
                    expected_parallel=1,
                )


if __name__ == "__main__":
    unittest.main()
