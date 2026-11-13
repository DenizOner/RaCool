"""Regression checks for CMake-to-SCons positional linker flag preservation."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))


import json
import runpy
import tempfile
import types
import unittest
from unittest.mock import patch


class Literal(str):
    pass


class FakeEnvironment(dict):
    def subst(self, value):
        return self['BUILD_DIR'] if value == '$BUILD_DIR' else value

    def Flatten(self, values):
        return [item for value in values for item in (self.Flatten(value) if isinstance(value, list) else [value])]

    def subst_list(self, expression, **kwargs):
        return [self.Flatten(self["LIBS"])]

    def Depends(self, target, source):
        self.setdefault("dependencies", []).append((target, source))

    def Replace(self, **values):
        self.update(values)


class LinkScopeTests(unittest.TestCase):
    def run_model(self, fragments, libraries):
        with tempfile.TemporaryDirectory() as directory:
            reply = Path(directory) / '.cmake/api/v1/reply'
            reply.mkdir(parents=True)
            (reply / 'target-RaCool.elf-test.json').write_text(json.dumps({
                'link': {'commandFragments': [{'fragment': value} for value in fragments]}
            }), encoding='utf-8')
            env = FakeEnvironment(BUILD_DIR=directory, LIBS=libraries, _LIBFLAGS="original",
                                  LINKFLAGS=['-Wl,--no-whole-archive', '-g', '-Wl,--whole-archive'])
            substitute = types.ModuleType('SCons.Subst')
            substitute.Literal = Literal
            with patch.dict(sys.modules, {'SCons': types.ModuleType('SCons'), 'SCons.Subst': substitute}):
                runpy.run_path(str(Path(__file__).resolve().parents[2] / 'tools/platformio_link.py'),
                               init_globals={'env': env, 'Import': lambda name: None})
            env["rendered"] = env["_LIBFLAGS"](None, None, env, False)
            self.assertEqual(libraries, env["LIBS"])
            return env

    def test_linker_scripts_track_sdk_configuration_changes(self):
        env = self.run_model([], ["c"])
        directory = Path(env["BUILD_DIR"])
        expected = {directory / "esp-idf/esp_system/ld/sections.ld.in",
                    directory / "memory.ld", directory / "sections.ld"}
        self.assertEqual(expected, {Path(target) for target, source in env["dependencies"]})
        self.assertTrue(all(Path(source) == directory / "config/sdkconfig.h"
                            for target, source in env["dependencies"]))

    def test_only_requested_archive_is_forced(self):
        env = self.run_model(['-Wl,--whole-archive', r'esp-idf\zigbee\libzigbee.a',
                              '-Wl,--no-whole-archive', 'libc.a'],
                             [['libapp.a'], [r'build\zigbee\libzigbee.a'], 'c', 'nosys'])
        self.assertEqual(['-g'], env['LINKFLAGS'])
        self.assertEqual(['libapp.a', '-Wl,--whole-archive', r'build\zigbee\libzigbee.a',
                          '-Wl,--no-whole-archive', 'c', 'nosys'], env['rendered'])
        self.assertIsInstance(env['rendered'][1], Literal)
        self.assertIsInstance(env['rendered'][3], Literal)

    def test_late_dependency_is_preserved(self):
        env = self.run_model(['-Wl,--whole-archive', 'libzigbee.a', '-Wl,--no-whole-archive'],
                             ['libzigbee.a'])
        env['LIBS'].append('liblvgl.a')
        self.assertEqual('liblvgl.a', env['_LIBFLAGS'](None, None, env, False)[-1])

    def test_missing_archive_fails_instead_of_dropping_initializers(self):
        with self.assertRaisesRegex(RuntimeError, 'missing'):
            self.run_model(['-Wl,--whole-archive', 'libmissing.a', '-Wl,--no-whole-archive'], ['c'])

    def test_unbalanced_model_fails(self):
        with self.assertRaisesRegex(RuntimeError, 'Unbalanced'):
            self.run_model(['-Wl,--whole-archive', 'libzigbee.a'], ['libzigbee.a'])

    def test_no_forced_archives_preserves_library_order(self):
        env = self.run_model(['libapp.a', 'libc.a'], [['libapp.a'], 'c', 'nosys'])
        self.assertEqual(['libapp.a', 'c', 'nosys'], env['rendered'])


if __name__ == '__main__':
    unittest.main()
