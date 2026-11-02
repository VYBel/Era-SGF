#!/usr/bin/env python3
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

BINARY = str(Path(sys.argv.pop(1) if len(sys.argv) > 1 else 'build/era-sgf-inspect').resolve())


def command(*args, ok=True):
    result = subprocess.run([BINARY, *map(str, args)], capture_output=True, text=True)
    if ok and result.returncode:
        raise AssertionError(result.stderr)
    return json.loads(result.stdout) if ok else result


class FileOperations(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='era sgf тест ')
        self.root = Path(self.temp.name)
        self.config = self.root / 'extensions.cfg'

    def tearDown(self):
        self.temp.cleanup()

    def associate(self, action):
        return command('--associate', self.config, action)

    def test_new_association_is_idempotent_and_removable(self):
        self.assertTrue(self.associate('install')['changed'])
        self.assertIn('sgf:', self.config.read_text())
        self.assertFalse(self.associate('install')['changed'])
        self.assertTrue(self.associate('remove')['changed'])
        self.assertEqual(self.config.read_bytes(), b'')
        self.assertFalse(self.associate('remove')['changed'])

    def test_existing_handlers_and_crlf_survive_install_and_remove(self):
        original = b'# user settings\r\nfb2:@FB2_file:1:fbreader.app:ICON_FB2\r\nsgf:@SGF_file:1:other.app:ICON_TXT\r\n'
        self.config.write_bytes(original)
        result = self.associate('install')
        self.assertEqual(Path(result['backup']).read_bytes(), original)
        data = self.config.read_bytes()
        self.assertIn(b'fb2:@FB2_file:1:fbreader.app:ICON_FB2\r\n', data)
        self.assertIn(b'era-sgf.app,other.app', data)
        self.assertFalse(self.associate('install')['changed'])
        self.associate('remove')
        self.assertEqual(self.config.read_bytes(), original)

    def test_symlinked_configuration_is_refused_without_touching_target(self):
        target = self.root / 'real.cfg'
        target.write_text('fb2:keep:this:content:unchanged\n')
        self.config.symlink_to(target)
        result = command('--associate', self.config, 'install', ok=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue(self.config.is_symlink())
        self.assertEqual(target.read_text(), 'fb2:keep:this:content:unchanged\n')

    def test_viewing_and_manual_encoding_never_modify_source(self):
        path = self.root / 'русская партия с пробелом.sgf'
        original = '(;SZ[9]CA[UTF-8]C[Привет];B[aa])'.encode('cp1251')
        path.write_bytes(original)
        self.assertNotEqual(command(path, ok=False).returncode, 0)
        result = command(path, '--encoding', 'cp1251')
        self.assertEqual(result['comment'], 'Привет')
        self.assertEqual(path.read_bytes(), original)

    def test_resume_remembers_branch_and_codec_but_rejects_replaced_source(self):
        path = self.root / 'партия.sgf'
        state_dir = self.root / 'state'
        original = '(;SZ[9]CA[UTF-8](;B[aa]C[Ветка один])(;B[bb]C[Ветка два]))'.encode('cp1251')
        path.write_bytes(original)
        command(path, '--node', 2, '--encoding', 'cp1251', '--state-dir', state_dir, '--remember')
        result = command(path, '--state-dir', state_dir)
        self.assertEqual(result['node'], 2)
        self.assertEqual(result['comment'], 'Ветка два')
        self.assertEqual(result['board'][1][1], 'B')
        self.assertEqual(path.read_bytes(), original)
        path.write_text('(;SZ[13]C[Новая запись])')
        result = command(path, '--state-dir', state_dir)
        self.assertEqual(result['node'], 0)
        self.assertEqual(result['encoding'], 'UTF-8')
        self.assertEqual(result['size'], 13)

    def test_corrupt_saved_state_does_not_prevent_opening(self):
        path = self.root / 'game.sgf'
        state_dir = self.root / 'state'
        path.write_text('(;SZ[9];B[aa])')
        command(path, '--state-dir', state_dir, '--remember')
        for state in state_dir.glob('*.state'):
            state.write_text('invalid state\n')
        self.assertEqual(command(path, '--state-dir', state_dir)['node'], 0)

    def test_host_catalogue_command_indexes_reader_folders_and_preserves_game_bytes(self):
        games = self.root / 'sgf/1969'
        games.mkdir(parents=True)
        record = games / 'game.sgf'
        original = b'(;SZ[9]PB[Black]PW[White]DT[1969-11-14];B[aa])'
        record.write_bytes(original)
        result = command('--catalogue', self.root)
        self.assertEqual(result['files'], 1)
        self.assertEqual(result['years'], {'1969': 1})
        self.assertEqual(result['viewed'], 0)
        self.assertEqual(result['errors'], 0)
        self.assertTrue((self.root / 'system/config/era-sgf/library.db').is_file())
        self.assertEqual(record.read_bytes(), original)


if __name__ == '__main__':
    unittest.main(verbosity=2)
