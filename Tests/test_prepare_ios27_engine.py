#!/usr/bin/env python3

import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest


SCRIPT_PATH = Path(__file__).resolve().parents[1] / "scripts/prepare-ios27-engine.py"
SPEC = importlib.util.spec_from_file_location("prepare_ios27_engine", SCRIPT_PATH)
assert SPEC is not None and SPEC.loader is not None
PREPARER = importlib.util.module_from_spec(SPEC)
sys.dont_write_bytecode = True
SPEC.loader.exec_module(PREPARER)


class PrepareIOS27EngineTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        if not PREPARER.DEFAULT_SOURCE.is_dir():
            raise unittest.SkipTest("UE 5.8 stock engine is not installed")

    def test_module_diagnostics_uses_public_callback_without_raw_snapshot(self) -> None:
        path = PREPARER.DEFAULT_SOURCE / PREPARER.MODULE_DIAGNOSTICS_PATH
        original = PREPARER.read_text(path)
        patched = PREPARER.expected_module_diagnostics_text(original)

        self.assertIn("#include <mach-o/dyld.h>", patched)
        self.assertIn("thread_local bool GCollectDyldImages = false;", patched)
        self.assertIn("GModulesInitialized.compare_exchange_strong", patched)
        self.assertIn("_dyld_register_func_for_add_image(&Modules_OnDyldImageAdded);", patched)
        self.assertIn("GCollectDyldImages = false;", patched)
        self.assertIn("dladdr(Header, &ImageInfo)", patched)
        self.assertNotIn("#include <mach-o/dyld_images.h>", patched)
        self.assertNotIn("task_info(", patched)
        self.assertNotIn("infos->infoArray", patched)
        self.assertEqual(patched.count("void Modules_Initialize()"), 2)

    def test_module_diagnostics_patch_rejects_source_drift(self) -> None:
        path = PREPARER.DEFAULT_SOURCE / PREPARER.MODULE_DIAGNOSTICS_PATH
        patched = PREPARER.expected_module_diagnostics_text(PREPARER.read_text(path))
        with self.assertRaises(PREPARER.PreparationError):
            PREPARER.expected_module_diagnostics_text(patched)

    def test_core_rule_rebuild_and_allocator_reuse_are_limited_to_native_development(self) -> None:
        path = PREPARER.DEFAULT_SOURCE / PREPARER.MODULE_RULES["Core"]
        patched = PREPARER.expected_patched_text(PREPARER.read_text(path), "Core")

        self.assertIn(PREPARER.CORE_PATCH_MARKER, patched)
        self.assertEqual(patched.count("bUsePrecompiled = false;"), 1)
        self.assertIn(
            "Target.Platform == UnrealTargetPlatform.IOS",
            patched,
        )
        self.assertIn("Target.Architecture == UnrealArch.Arm64", patched)
        self.assertIn("Target.Configuration == UnrealTargetConfiguration.Development", patched)
        self.assertIn('Target.Name == "Cinderline"', patched)
        self.assertIn('PrivateDefinitions.Add("CINDERLINE_USE_PRECOMPILED_MIMALLOC=1")', patched)
        self.assertIn('PublicAdditionalLibraries.Add(Path.Combine(EngineDirectory, "CinderlinePrecompiled"', patched)
        self.assertNotIn(
            Path("Engine/Intermediate/Build/IOS/UnrealGame/Development/Core/Core.precompiled"),
            PREPARER.SUPPORT_FILES,
        )

    def test_mimalloc_wrapper_omits_source_only_for_guarded_core_build(self) -> None:
        path = PREPARER.DEFAULT_SOURCE / PREPARER.MIMALLOC_SOURCE_PATH
        original = PREPARER.read_text(path)
        patched = PREPARER.expected_mimalloc_text(original)

        self.assertIn(
            "#if PLATFORM_BUILDS_MIMALLOC && !defined(CINDERLINE_USE_PRECOMPILED_MIMALLOC)",
            patched,
        )
        self.assertIn("#include <static.c>", patched)
        with self.assertRaises(PREPARER.PreparationError):
            PREPARER.expected_mimalloc_text(patched)

    def test_schema2_core_rule_is_exact_migration_input(self) -> None:
        path = PREPARER.DEFAULT_SOURCE / PREPARER.MODULE_RULES["Core"]
        original = PREPARER.read_text(path)
        previous = PREPARER.expected_previous_core_rule_text(original)
        current = PREPARER.expected_patched_text(original, "Core")

        self.assertIn(PREPARER.CORE_PATCH_MARKER, previous)
        self.assertNotIn("CINDERLINE_USE_PRECOMPILED_MIMALLOC", previous)
        self.assertNotEqual(previous, current)

    def test_stock_mimalloc_object_matches_inspected_native_ios_object(self) -> None:
        digest = PREPARER.validate_stock_mimalloc_object(PREPARER.DEFAULT_SOURCE)
        self.assertEqual(digest, PREPARER.MIMALLOC_STOCK_OBJECT_SHA256)

    def test_mimalloc_object_copy_is_exact_and_check_rejects_tampering(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            destination = Path(temporary)
            PREPARER.verify_or_copy_mimalloc_object(
                PREPARER.DEFAULT_SOURCE, destination, check_only=False
            )
            copied = destination / PREPARER.MIMALLOC_CLONE_OBJECT_PATH
            self.assertEqual(PREPARER.sha256(copied), PREPARER.MIMALLOC_STOCK_OBJECT_SHA256)
            PREPARER.verify_or_copy_mimalloc_object(
                PREPARER.DEFAULT_SOURCE, destination, check_only=True
            )
            copied.write_bytes(b"tampered")
            with self.assertRaises(PREPARER.PreparationError):
                PREPARER.verify_or_copy_mimalloc_object(
                    PREPARER.DEFAULT_SOURCE, destination, check_only=True
                )

    def test_existing_lifecycle_rule_patch_stays_stable(self) -> None:
        for module in ("ApplicationCore", "Launch"):
            with self.subTest(module=module):
                path = PREPARER.DEFAULT_SOURCE / PREPARER.MODULE_RULES[module]
                patched = PREPARER.expected_patched_text(PREPARER.read_text(path), module)
                self.assertIn(PREPARER.PATCH_MARKER, patched)
                self.assertNotIn(PREPARER.CORE_PATCH_MARKER, patched)


if __name__ == "__main__":
    unittest.main()
