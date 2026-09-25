"""Exercise Android UAT arguments and APK validation without invoking Unreal."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import zipfile


SCRIPTS = Path(__file__).resolve().parents[1]


@unittest.skipUnless(Path("/bin/bash").is_file(), "Requires Bash")
class UnrealAndroidPackageTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="cinder-android-package-")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name).resolve()
        self.project = self.directory / "Game project with spaces"
        scripts = self.project / "scripts"
        (scripts / "lib").mkdir(parents=True)
        (self.project / "docs").mkdir()
        (self.project / "Content" / "Maps").mkdir(parents=True)
        shutil.copy2(SCRIPTS / "unreal.sh", scripts / "unreal.sh")
        shutil.copy2(SCRIPTS / "archive-unreal-android.sh", scripts / "archive-unreal-android.sh")
        shutil.copy2(SCRIPTS / "lib" / "unreal-engine.sh", scripts / "lib" / "unreal-engine.sh")
        shutil.copy2(
            SCRIPTS.parent / "docs" / "ANDROID_PLAYTEST.txt",
            self.project / "docs" / "ANDROID_PLAYTEST.txt",
        )
        (self.project / "Cinderline.uproject").write_text("{}\n", encoding="utf-8")
        (self.project / "Content" / "Maps" / "Frontier.umap").write_bytes(b"fixture")

        self.capture = self.directory / "uat-arguments.bin"
        self.engine = self.directory / "UE 5.8 Android"
        android_receipt = (
            self.engine / "Engine" / "Intermediate" / "Build" / "Android" /
            "UnrealGame" / "Development" / "Launch" / "Launch.precompiled"
        )
        android_receipt.parent.mkdir(parents=True)
        android_receipt.write_text("fixture\n", encoding="utf-8")
        uat = self.engine / "Engine" / "Build" / "BatchFiles" / "RunUAT.sh"
        uat.parent.mkdir(parents=True)
        uat.write_text(
            "#!/bin/bash\n"
            "set -euo pipefail\n"
            "printf '%s\\0' \"$@\" > \"$CINDERLINE_UAT_CAPTURE\"\n"
            "archive=''\n"
            "for argument in \"$@\"; do\n"
            "  case \"$argument\" in -archivedirectory=*) archive=${argument#*=} ;; esac\n"
            "done\n"
            "test -n \"$archive\"\n"
            "root=\"$archive/apk-root\"\n"
            "mkdir -p \"$root/lib/arm64-v8a\" \"$root/assets/Cinderline/Content/Paks\"\n"
            "printf manifest > \"$root/AndroidManifest.xml\"\n"
            "printf native > \"$root/lib/arm64-v8a/libUnreal.so\"\n"
            "printf cooked > \"$root/assets/Cinderline/Content/Paks/Cinderline.utoc\"\n"
            "(cd \"$root\" && /usr/bin/zip -qr \"$archive/Cinderline.apk\" .)\n",
            encoding="utf-8",
        )
        uat.chmod(0o755)

        self.environment = os.environ.copy()
        for key in ("BASH_ENV", "ENV", "SHELLOPTS", "BASHOPTS"):
            self.environment.pop(key, None)
        self.environment.update({
            "UE_ROOT": str(self.engine),
            "CINDERLINE_UAT_CAPTURE": str(self.capture),
        })

    def run_package(self, *arguments):
        return subprocess.run(
            ["/bin/bash", str(self.project / "scripts" / "unreal.sh"), "package-android", *arguments],
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

    def test_builds_multi_texture_arm64_apk_with_embedded_assets(self):
        completed = self.run_package("-ubtargs=-MaxParallelActions=1 -Define=two words")
        self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
        arguments = self.captured_arguments()
        for expected in (
            "BuildCookRun", "-platform=Android", "-cookflavor=Multi",
            "-clientconfig=Development", "-build", "-cook", "-stage", "-pak",
            "-iostore", "-compressed", "-package", "-archive", "-target=Cinderline",
            "-ubtargs=-MaxParallelActions=1 -Define=two words",
        ):
            self.assertEqual(arguments.count(expected), 1, arguments)

        artifact = self.project / "artifacts" / "Cinderline-Android-arm64.apk"
        self.assertTrue(artifact.is_file())
        self.assertTrue(Path(f"{artifact}.sha256").is_file())
        self.assertTrue((self.project / "artifacts" / "README-ANDROID.txt").is_file())
        with zipfile.ZipFile(artifact) as apk:
            names = set(apk.namelist())
        self.assertIn("AndroidManifest.xml", names)
        self.assertIn("lib/arm64-v8a/libUnreal.so", names)
        self.assertIn("assets/Cinderline/Content/Paks/Cinderline.utoc", names)

    def test_archiver_rejects_apk_without_cooked_game_data(self):
        package = self.directory / "incomplete package"
        package.mkdir()
        apk = package / "Cinderline.apk"
        with zipfile.ZipFile(apk, "w") as archive:
            archive.writestr("AndroidManifest.xml", "manifest")
            archive.writestr("lib/arm64-v8a/libUnreal.so", "native")
        completed = subprocess.run(
            [
                "/bin/bash", str(self.project / "scripts" / "archive-unreal-android.sh"),
                str(package), str(self.directory / "bad artifacts"),
            ],
            cwd=self.project,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(completed.returncode, 2)
        self.assertIn("no packaged Unreal game data", completed.stderr)


if __name__ == "__main__":
    unittest.main()
