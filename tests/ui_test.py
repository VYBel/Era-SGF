#!/usr/bin/env python3
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

UI = str(Path(sys.argv.pop(1)).resolve())
INSPECT = str(Path(sys.argv.pop(1)).resolve())


class DeviceAdapter(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='era ui тест ')
        self.root = Path(self.temp.name)
        (self.root / 'system/config').mkdir(parents=True)
        (self.root / 'applications').mkdir()
        (self.root / 'applications/era-sgf.app').touch()
        self.record = self.root / 'партия с ветками.sgf'
        self.record.write_text('(;SZ[9]CA[UTF-8]C[Корень];B[aa](;W[bb]C[Первый вариант])(;W[cc]C[Второй вариант]))')

    def tearDown(self):
        self.temp.cleanup()

    def run_ui(self, events, *args):
        env = dict(os.environ, ERA_SGF_ROOT=str(self.root), ERA_SGF_TEST_EVENTS=events)
        result = subprocess.run([UI, *map(str, args)], env=env, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout

    def test_hardware_keys_and_variation_menu_save_selected_sibling(self):
        trace = self.run_ui('next;menu;select:103;select:1001;close', self.record)
        self.assertIn('Второй вариант', trace)
        result = subprocess.run([INSPECT, str(self.record), '--state-dir', str(self.root / 'system/config/era-sgf')], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)['node'], 3)

    def test_bad_ca_can_be_fixed_in_the_device_encoding_menu(self):
        self.record.write_bytes('(;SZ[9]CA[UTF-8]C[Привет, ёж!])'.encode('cp1251'))
        before = self.record.read_bytes()
        trace = self.run_ui('menu;select:107;select:202;close', self.record)
        self.assertIn('Привет, ёж!', trace)
        self.assertEqual(self.record.read_bytes(), before)

    def test_node_name_only_encoding_error_keeps_a_touch_menu_and_can_be_fixed(self):
        self.record.write_bytes('(;SZ[9]CA[UTF-8]N[Привет, ёж!])'.encode('cp1251'))
        trace = self.run_ui('tap:1100,1600;select:107;select:202;close', self.record)
        self.assertIn('FRAME\n', trace)
        self.assertIn('Привет, ёж!', trace)

    def test_encoding_error_beyond_detection_sample_keeps_touch_recovery(self):
        prefix = '(;SZ[9]CA[UTF-8]C[' + 'x' * 66000 + '];N['
        self.record.write_bytes(prefix.encode() + 'Привет, ёж!'.encode('cp1251') + b'])')
        self.run_ui('next;close', self.record)
        # Resume on the bad node: no previous frame is available to mask the failure.
        trace = self.run_ui('tap:1100,1600;select:107;select:202;close', self.record)
        self.assertIn('Привет, ёж!', trace)

    def test_automatic_detection_includes_node_names(self):
        self.record.write_bytes('(;SZ[9]N[Привет, ёж! Разбор партии и варианты продолжения.])'.encode('cp1251'))
        trace = self.run_ui('close', self.record)
        self.assertIn('Привет, ёж!', trace)

    def test_reopening_current_record_preserves_fresh_position(self):
        trace = self.run_ui('next;next;menu;select:100;select:1002;close', self.record)
        result = subprocess.run([INSPECT, str(self.record), '--state-dir', str(self.root / 'system/config/era-sgf')], capture_output=True, text=True)
        self.assertEqual(json.loads(result.stdout)['node'], 2)

    def test_open_from_applications_without_a_launch_argument(self):
        # applications/ and system/ are the first two directory rows.
        trace = self.run_ui('menu;select:100;select:1002;close')
        self.assertIn('Корень', trace)

    def test_explicit_install_mode_registers_sgf_and_preserves_other_entries(self):
        config = self.root / 'system/config/extensions.cfg'
        original = 'fb2:@FB2_file:1:fbreader.app:ICON_FB2\n'
        config.write_text(original)
        self.run_ui('close', '--install')
        self.assertTrue(config.read_text().startswith(original))
        self.assertIn('era-sgf.app', config.read_text())

    def test_orientation_and_full_comment_do_not_change_selected_node(self):
        trace = self.run_ui('next;rotate;menu;select:109;next;back;close', self.record)
        self.assertIn('Ход 1', trace)
        result = subprocess.run([INSPECT, str(self.record), '--state-dir', str(self.root / 'system/config/era-sgf')], capture_output=True, text=True)
        self.assertEqual(json.loads(result.stdout)['node'], 1)


if __name__ == '__main__':
    unittest.main(verbosity=2)
