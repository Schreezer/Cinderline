"""Exercise the full Unreal Linux packaging path with local argument-only fixtures."""

import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest


SCRIPTS = Path(__file__).resolve().parents[1]


@unittest.skipUnless(Path("/bin/bash").is_file(), "Requires Bash")
class UnrealLinuxPackageTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="cinder-linux-package-")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name).resolve()
        self.project = self.directory / "Game project with spaces"
        scripts = self.project / "scripts"
        (scripts / "lib").mkdir(parents=True)
        (scripts / "tests").mkdir()
        (self.project / "docs").mkdir()
        (self.project / "Content" / "Maps").mkdir(parents=True)
        shutil.copy2(SCRIPTS / "unreal.sh", scripts / "unreal.sh")
        shutil.copy2(SCRIPTS / "archive-unreal-linux.sh", scripts / "archive-unreal-linux.sh")
        shutil.copy2(SCRIPTS / "lib" / "unreal-engine.sh", scripts / "lib" / "unreal-engine.sh")
        shutil.copy2(
            SCRIPTS.parent / "docs" / "UBUNTU_UNREAL_PLAYTEST.txt",
            self.project / "docs" / "UBUNTU_UNREAL_PLAYTEST.txt",
        )
        (self.project / "Cinderline.uproject").write_text("{}\n", encoding="utf-8")
        (self.project / "Content" / "Maps" / "Frontier.umap").write_bytes(b"fixture")

        self.capture = self.directory / "uat-arguments.bin"
        self.engine = self.directory / "UE 5.8 Linux"
        (self.engine / "Engine" / "Build" / "BatchFiles" / "Linux").mkdir(parents=True)
        uat = self.engine / "Engine" / "Build" / "BatchFiles" / "RunUAT.sh"
        uat.write_text(
            "#!/bin/bash\n"
            "set -euo pipefail\n"
            "printf '%s\\0' \"$@\" > \"$CINDERLINE_UAT_CAPTURE\"\n"
            "archive=''\n"
            "for argument in \"$@\"; do\n"
            "  case \"$argument\" in -archivedirectory=*) archive=${argument#*=} ;; esac\n"
            "done\n"
            "test -n \"$archive\"\n"
            "mkdir -p \"$archive/Linux/Cinderline/Content/Paks\"\n"
            "printf '#!/bin/sh\\nexit 0\\n' > \"$archive/Linux/Cinderline.sh\"\n"
            "chmod +x \"$archive/Linux/Cinderline.sh\"\n"
            "printf cooked > \"$archive/Linux/Cinderline/Content/Paks/Cinderline-Linux.pak\"\n",
            encoding="utf-8",
        )
        uat.chmod(0o755)

        fake_bin = self.directory / "fake-bin"
        fake_bin.mkdir()
        uname = fake_bin / "uname"
        uname.write_text(
            "#!/bin/sh\n"
            "if [ \"${1:-}\" = -m ]; then printf '%s\\n' x86_64; else printf '%s\\n' Linux; fi\n",
            encoding="utf-8",
        )
        uname.chmod(0o755)
        self.environment = os.environ.copy()
        for key in ("BASH_ENV", "ENV", "SHELLOPTS", "BASHOPTS"):
            self.environment.pop(key, None)
        self.environment.update({
            "UE_ROOT": str(self.engine),
            "PATH": f"{fake_bin}:{self.environment['PATH']}",
            "CINDERLINE_UAT_CAPTURE": str(self.capture),
            "CINDERLINE_LINUX_BUILD_JOBS": "3",
        })

    def run_package(self, *arguments):
        return subprocess.run(
            ["/bin/bash", str(self.project / "scripts" / "unreal.sh"), "package-linux", *arguments],
            cwd=self.project,
            env=self.environment,
            text=True,
            capture_output=True,
            timeout=30,
        )

    def captured_arguments(self):
        raw = self.capture.read_bytes()
        self.assertTrue(raw.endswith(b"\0"))
        return [item.decode("utf-8") for item in raw[:-1].split(b"\0")]

    def test_packages_shipping_game_and_archives_cooked_content(self):
        completed = self.run_package("-AdditionalCookerOptions=-CookProcessCount=1")
        self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
        arguments = self.captured_arguments()
        self.assertEqual(arguments[0], "BuildCookRun")
        for expected in (
            "-platform=Linux", "-clientconfig=Shipping", "-build", "-cook", "-stage",
            "-pak", "-iostore", "-package", "-archive", "-target=Cinderline",
            "-ubtargs=-MaxParallelActions=3",
        ):
            self.assertEqual(arguments.count(expected), 1, arguments)
        archive_arg = next(item for item in arguments if item.startswith("-archivedirectory="))
        self.assertEqual(archive_arg, f"-archivedirectory={self.project / 'Saved/Packages/Linux'}")

        artifact = self.project / "artifacts" / "Cinderline-Ubuntu-x86_64-Unreal.tar.gz"
        self.assertTrue(artifact.is_file())
        self.assertTrue(Path(f"{artifact}.sha256").is_file())
        with tarfile.open(artifact, "r:gz") as package:
            names = {name.removeprefix("./") for name in package.getnames()}
        self.assertIn("README-UBUNTU.txt", names)
        self.assertIn("Linux/Cinderline.sh", names)
        self.assertIn("Linux/Cinderline/Content/Paks/Cinderline-Linux.pak", names)

    def test_explicit_ubt_args_are_not_duplicated(self):
        completed = self.run_package("-ubtargs=-MaxParallelActions=1 -Define=two words")
        self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
        arguments = self.captured_arguments()
        ubt_args = [item for item in arguments if item.lower().startswith("-ubtargs=")]
        self.assertEqual(ubt_args, ["-ubtargs=-MaxParallelActions=1 -Define=two words"])

    def test_archiver_refuses_package_without_cooked_content(self):
        package = self.directory / "incomplete package"
        package.mkdir()
        launcher = package / "Cinderline.sh"
        launcher.write_text("#!/bin/sh\n", encoding="utf-8")
        launcher.chmod(0o755)
        completed = subprocess.run(
            [
                "/bin/bash", str(self.project / "scripts" / "archive-unreal-linux.sh"),
                str(package), str(self.directory / "bad.tar.gz"),
            ],
            cwd=self.project,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(completed.returncode, 2)
        self.assertIn("No cooked Unreal content container", completed.stderr)


if __name__ == "__main__":
    unittest.main()
