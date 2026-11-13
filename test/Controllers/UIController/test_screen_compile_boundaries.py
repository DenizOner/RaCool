"""Compile production screens separately so amalgamated fixtures cannot hide includes."""
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[3]


class ScreenCompileBoundaryTests(unittest.TestCase):
    def test_every_screen_has_its_own_required_declarations(self):
        compiler=shutil.which("gcc")
        self.assertIsNotNone(compiler,"Native C compiler is required")
        with tempfile.TemporaryDirectory() as temporary:
            generated=Path(temporary)
            result=subprocess.run([sys.executable,str(ROOT/"tools/generate_translations.py"),
                str(ROOT/"resources/translations"),str(generated)],capture_output=True,text=True,timeout=30)
            self.assertEqual(0,result.returncode,result.stderr)
            result=subprocess.run([sys.executable,str(ROOT/"tools/product_version.py"),"generate",
                "--manifest",str(ROOT/"resources/product-version.json"),"--root",str(ROOT),
                "--output",str(generated)],capture_output=True,text=True,timeout=30)
            self.assertEqual(0,result.returncode,result.stderr)
            paths=["include","src","test","test/Infrastructure/mocks",
                "test/Infrastructure/mocks/freertos","test/Infrastructure/mocks/esp_common",
                "test/Infrastructure/mocks/esp_rom","test/Infrastructure/support"]
            flags=["-fsyntax-only","-std=gnu11","-Werror=implicit-function-declaration",
                "-DRACOOL_TEST_NATIVE","-D_GNU_SOURCE","-include",
                str(ROOT/"test/Infrastructure/support/test_mocks.h"),"-I",str(generated)]
            for path in paths:flags += ["-I",str(ROOT/path)]
            screens=sorted((ROOT/"src/Controllers/UIController/Screens").glob("*.c"))
            self.assertGreater(len(screens),0,"Zero screen translation units is not acceptance")
            for screen in screens:
                with self.subTest(screen=screen.name):
                    result=subprocess.run([compiler,*flags,str(screen)],capture_output=True,text=True,timeout=30)
                    self.assertEqual(0,result.returncode,result.stderr)
                    self.assertNotIn("warning:",result.stderr.lower())

    def test_missing_localization_include_is_rejected(self):
        compiler=shutil.which("gcc")
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as temporary:
            generated=Path(temporary)
            result=subprocess.run([sys.executable,str(ROOT/"tools/generate_translations.py"),
                str(ROOT/"resources/translations"),str(generated)],capture_output=True,text=True,timeout=30)
            self.assertEqual(0,result.returncode,result.stderr)
            result=subprocess.run([sys.executable,str(ROOT/"tools/product_version.py"),"generate",
                "--manifest",str(ROOT/"resources/product-version.json"),"--root",str(ROOT),
                "--output",str(generated)],capture_output=True,text=True,timeout=30)
            self.assertEqual(0,result.returncode,result.stderr)
            screen=generated/"missing_include.c"
            source=(ROOT/"src/Controllers/UIController/Screens/ScreenBoot.c").read_text(encoding="utf-8")
            source=source.replace('#include "Core/Localization/Localization.h"','')
            screen.write_text(source,encoding="utf-8")
            flags=["-fsyntax-only","-std=gnu11","-Werror=implicit-function-declaration",
                "-DRACOOL_TEST_NATIVE","-D_GNU_SOURCE","-include",
                str(ROOT/"test/Infrastructure/support/test_mocks.h"),"-I",str(generated)]
            for path in ["include","src","test","test/Infrastructure/mocks",
                "test/Infrastructure/mocks/freertos","test/Infrastructure/mocks/esp_common",
                "test/Infrastructure/mocks/esp_rom","test/Infrastructure/support"]:
                flags += ["-I",str(ROOT/path)]
            result=subprocess.run([compiler,*flags,str(screen)],capture_output=True,text=True,timeout=30)
            self.assertNotEqual(0,result.returncode,"The missing direct dependency must fail compilation")
            self.assertRegex(result.stderr,r"LocalizationCatalog|Localization_Snapshot")


if __name__=="__main__":
    unittest.main()
