#!/usr/bin/env python3
import json
import os
from pathlib import Path
import stat
import struct
import subprocess
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]


class Packaging(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='era package тест ')
        self.directory = Path(self.temp.name)
        self.binary = self.directory / 'test-arm.app'
        header = bytearray(64)
        header[:7] = b'\x7fELF\x01\x01\x01'
        struct.pack_into('<HHI', header, 16, 2, 40, 1)
        struct.pack_into('<I', header, 36, 0x05000200)
        self.binary.write_bytes(header)

    def tearDown(self):
        self.temp.cleanup()

    def package(self, ok=True):
        result = subprocess.run(['python3', str(ROOT / 'scripts/package.py'), '--binary', str(self.binary), '--output', str(self.directory / 'out')], capture_output=True, text=True)
        if ok:
            self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
            return next((self.directory / 'out').glob('*.zip'))
        return result

    def test_archive_contains_only_installable_files_with_executable_launchers(self):
        path = self.package()
        with zipfile.ZipFile(path) as archive:
            names = set(archive.namelist())
            self.assertIn('applications/era-sgf.app', names)
            self.assertIn('applications/era-sgf-install.app', names)
            self.assertIn('applications/era-sgf-uninstall.app', names)
            self.assertIn('Books/Era-SGF/first-steps.sgf', names)
            self.assertEqual(archive.read('applications/era-sgf.app'), self.binary.read_bytes())
            self.assertTrue(archive.getinfo('applications/era-sgf.app').external_attr >> 16 & stat.S_IXUSR)
            self.assertTrue(all(not any(part in name for part in ('.sdk/', '.git/', 'state/', '.env')) for name in names))
        self.assertTrue(path.with_suffix('.zip.sha256').exists())

    def test_launcher_resolves_its_own_path_with_spaces_and_cyrillic(self):
        path = self.package()
        target = self.directory / 'установка с пробелами'
        with zipfile.ZipFile(path) as archive:
            archive.extractall(target)
        app = target / 'applications/era-sgf.app'
        app.write_text('#!/bin/sh\nprintf "%s\\n" "$@" > "$ERA_SGF_ARG_FILE"\n')
        app.chmod(0o755)
        output = self.directory / 'arguments'
        for launcher, expected in [('era-sgf-install.app', '--install'), ('era-sgf-uninstall.app', '--uninstall')]:
            env = dict(os.environ, ERA_SGF_ARG_FILE=str(output))
            result = subprocess.run(['/bin/sh', str(target / 'applications' / launcher)], env=env)
            self.assertEqual(result.returncode, 0)
            self.assertEqual(output.read_text(), expected + '\n')

    def test_wrong_elf_architecture_or_hard_float_is_refused(self):
        data = bytearray(self.binary.read_bytes())
        struct.pack_into('<H', data, 18, 62)
        self.binary.write_bytes(data)
        self.assertNotEqual(self.package(ok=False).returncode, 0)
        struct.pack_into('<H', data, 18, 40)
        struct.pack_into('<I', data, 36, 0x05000400)
        self.binary.write_bytes(data)
        self.assertNotEqual(self.package(ok=False).returncode, 0)

    def test_same_inputs_produce_the_same_archive(self):
        first = self.package().read_bytes()
        self.assertEqual(self.package().read_bytes(), first)


if __name__ == '__main__':
    unittest.main(verbosity=2)
