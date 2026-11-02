#!/usr/bin/env python3
"""Exercise the host installer against synthetic readers; no device is needed."""
import copy
import errno
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import stat
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import warnings
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'scripts/install-mounted-reader.py'
APP = 'applications/era-sgf.app'
ICON = 'applications/icons/era-sgf.bmp'
FOCUSED = 'applications/icons/era-sgf-focused.bmp'
CONFIG = 'system/config/desktop/view.json'


def arm_binary():
    data = bytearray(64)
    data[:7] = b'\x7fELF\x01\x01\x01'
    struct.pack_into('<HHI', data, 16, 2, 40, 1)
    struct.pack_into('<I', data, 36, 0x05000200)
    return bytes(data) + b'new reader application'


def bmp():
    # Real, uncompressed 8-bit BMP. This fixture needs no Pillow installation.
    width, height = 106, 112
    pixels = (bytes([255]) * width + bytes(2)) * height
    palette = b''.join(bytes((n, n, n, 0)) for n in range(256))
    offset = 14 + 40 + len(palette)
    return (struct.pack('<2sIHHI', b'BM', offset + len(pixels), 0, 0, offset)
            + struct.pack('<IiiHHIIiiII', 40, width, height, 1, 8, 0, len(pixels), 2835, 2835, 256, 0)
            + palette + pixels)


def desktop():
    return {
        'version': 7,
        'vendor_setting': {'retain': True},
        'applications': {
            '_comment': ['User IDs MUST start U_; icon uses an object path.'],
            'U_Custom': {'path': 'applications/custom.app', 'title': 'Custom', 'params': ['x']},
        },
        'view': {
            '_comment': ['Never discard these comments.'],
            'orientation': 1,
            'groups': [
                {'title': '@General', 'sort': 'title', 'apps': ['PB_Library', 'U_Custom'], 'extra': 7},
                {'title': '@Services', 'sort': 'title', 'apps': ['PB_Settings']},
                {'title': '@Games', 'sort': 'title', 'apps': ['PB_Chess', 'PB_Painter', 'PB_Klondike',
                    'PB_Scribble', 'PB_Sudoku', 'PB_Snake']},
            ],
        },
    }


def snapshot(directory):
    result = {}
    for path in sorted(directory.rglob('*')):
        name = path.relative_to(directory).as_posix()
        if path.is_symlink():
            result[name] = ('link', os.readlink(path))
        elif path.is_dir():
            result[name] = ('dir',)
        else:
            result[name] = ('file', path.read_bytes())
    return result


def load_installer():
    name = 'era_sgf_mounted_installer_test'
    spec = importlib.util.spec_from_file_location(name, SCRIPT)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


class Installer(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='era installer тест ')
        self.directory = Path(self.temp.name)
        self.reader = self.directory / 'reader'
        self.backups = self.directory / 'backups'
        self.archive = self.directory / 'release.zip'
        (self.reader / 'applications/icons').mkdir(parents=True)
        (self.reader / 'system/config/desktop').mkdir(parents=True)
        self.config = desktop()
        self.write_config()
        for name, data in {
            APP: b'old working application',
            ICON: b'old icon',
            'applications/custom.app': b'unrelated app',
            'system/config/extensions.cfg': b'sgf:@SGF_file:1:era-sgf.app:ICON_SGF\n',
            'system/config/era-sgf/resume.state': b'keep resume',
            'system/config/era-sgf/catalogue': b'keep catalogue',
            'system/config/era-sgf/preferences': b'keep preferences',
            'sgf/2026/my-game.sgf': b'(;C[user game])',
            'Books/Era-SGF/first-steps.sgf': b'(;C[my edited sample])',
        }.items():
            path = self.reader / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        self.payload = {
            APP: arm_binary(), ICON: bmp(), FOCUSED: bmp(),
            'Books/Era-SGF/first-steps.sgf': b'(;C[packaged example])',
            'Books/Era-SGF/windows-1251.sgf': b'(;CA[windows-1251])',
            'README-Era-SGF.txt': b'release notes',
            'LICENSE-Era-SGF.txt': b'MIT',
            'THIRD_PARTY-Era-SGF.md': b'compiler notices',
            'Era-SGF-LICENSES/GCC-Runtime-Library-Exception.txt': b'runtime notice',
        }
        self.write_archive()

    def tearDown(self):
        self.temp.cleanup()

    def write_config(self):
        (self.reader / CONFIG).write_text(json.dumps(self.config, indent=2) + '\n', encoding='utf-8')

    def write_archive(self, extra=()):
        with warnings.catch_warnings():
            warnings.simplefilter('ignore', UserWarning)
            with zipfile.ZipFile(self.archive, 'w') as archive:
                for name, data in list(self.payload.items()) + list(extra):
                    archive.writestr(name, data)

    def install(self, apply=False, root=None, backups=None):
        self.assertTrue(SCRIPT.is_file(), 'mounted-reader installer is not implemented')
        # -S also proves applying a packaged release needs no site packages.
        command = [sys.executable, '-S', str(SCRIPT), '--root', str(root or self.reader),
                   '--archive', str(self.archive), '--backup-dir', str(backups or self.backups)]
        if apply:
            command.append('--apply')
        return subprocess.run(command, capture_output=True, text=True)

    def success(self, result):
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        return json.loads(result.stdout)

    def refused_without_changes(self, root=None, backups=None):
        before = snapshot(self.directory)
        result = self.install(apply=True, root=root, backups=backups)
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn('refused', result.stderr.lower())
        self.assertEqual(snapshot(self.directory), before, 'refusal changed a reader or backup file')

    def test_dry_run_reports_changes_without_creating_files_or_backups(self):
        before = snapshot(self.directory)
        report = self.success(self.install())
        self.assertEqual(report['mode'], 'dry-run')
        self.assertEqual(report['archive_sha256'], hashlib.sha256(self.archive.read_bytes()).hexdigest())
        self.assertIn(CONFIG, [item['path'] for item in report['changes']])
        self.assertEqual(snapshot(self.directory), before)

    def test_actual_firmware_string_version_is_preserved_and_merge_is_idempotent(self):
        installer = load_installer()
        for version in ('7', 7):
            with self.subTest(version=version):
                config = desktop()
                config['version'] = version
                merged = installer.merge_desktop(json.dumps(config).encode())
                self.assertEqual(json.loads(merged)['version'], version)
                self.assertEqual(installer.merge_desktop(merged), merged)
        for version in ('6', '7.0', 7.0, True):
            with self.subTest(rejected=version):
                config = desktop()
                config['version'] = version
                with self.assertRaises(installer.Refused):
                    installer.merge_desktop(json.dumps(config).encode())

    def test_registers_in_games_and_preserves_other_config_and_reader_data(self):
        before = snapshot(self.reader)
        self.success(self.install(apply=True))
        installed = json.loads((self.reader / CONFIG).read_text())
        own_app = installed['applications'].pop('U_EraSGF')
        self.assertEqual(own_app, {
            'path': '/mnt/ext1/applications/era-sgf.app', 'title': 'Era-SGF',
            'icon': {'path': ICON}, 'focused_icon': {'path': FOCUSED},
        })
        self.assertEqual(installed['view']['groups'][2]['apps'].pop(), 'U_EraSGF')
        self.assertEqual(installed, self.config)
        self.assertEqual((self.reader / APP).read_bytes(), self.payload[APP])
        self.assertEqual((self.reader / ICON).read_bytes(), self.payload[ICON])
        self.assertEqual((self.reader / FOCUSED).read_bytes(), self.payload[FOCUSED])
        for name, value in before.items():
            if name not in (APP, ICON, CONFIG):
                self.assertEqual(snapshot(self.reader)[name], value, name)
        self.assertEqual((self.reader / 'Books/Era-SGF/windows-1251.sgf').read_bytes(), self.payload['Books/Era-SGF/windows-1251.sgf'])
        self.assertEqual(sorted(p.relative_to(self.reader).as_posix() for p in self.reader.rglob('*.app')),
                         ['applications/custom.app', APP])

    def test_existing_own_fields_survive_and_only_own_group_duplicates_are_removed(self):
        self.config['applications']['U_EraSGF'] = {
            'path': 'applications/era-sgf.app', 'title': 'Old title', 'params': ['keep'],
            'vendor_extra': {'keep': 3}, 'icon': {'path': 'old.bmp', 'scale': 2},
        }
        self.config['view']['groups'][0]['apps'] += ['U_EraSGF', 'U_EraSGF']
        self.config['view']['groups'][2]['apps'] = ['U_EraSGF', 'PB_Chess', 'U_EraSGF', 'PB_Snake']
        self.write_config()
        self.success(self.install(apply=True))
        installed = json.loads((self.reader / CONFIG).read_text())
        self.assertEqual(installed['applications']['U_EraSGF']['params'], ['keep'])
        self.assertEqual(installed['applications']['U_EraSGF']['vendor_extra'], {'keep': 3})
        self.assertEqual(installed['applications']['U_EraSGF']['icon'], {'path': ICON, 'scale': 2})
        self.assertEqual(installed['view']['groups'][0]['apps'], ['PB_Library', 'U_Custom'])
        self.assertEqual(installed['view']['groups'][2]['apps'], ['U_EraSGF', 'PB_Chess', 'PB_Snake'])

    def test_second_install_is_byte_identical_and_does_not_create_another_backup(self):
        self.success(self.install(apply=True))
        before = snapshot(self.directory)
        report = self.success(self.install(apply=True))
        self.assertEqual(report['changes'], [])
        self.assertIsNone(report['backup'])
        self.assertEqual(snapshot(self.directory), before)

    def test_every_replaced_file_has_an_exact_verified_backup_and_hash_manifest(self):
        before = snapshot(self.reader)
        report = self.success(self.install(apply=True))
        backup = Path(report['backup'])
        self.assertTrue(backup.is_relative_to(self.backups))
        manifest = json.loads((backup / 'manifest.json').read_text())
        self.assertEqual(manifest['status'], 'complete')
        self.assertEqual(manifest['archive_sha256'], hashlib.sha256(self.archive.read_bytes()).hexdigest())
        replacements = {item['path']: item for item in manifest['files'] if item['action'] == 'replace'}
        self.assertEqual(set(replacements), {APP, ICON, CONFIG})
        for name, item in replacements.items():
            original = before[name][1]
            self.assertEqual((backup / item['backup_path']).read_bytes(), original)
            self.assertEqual(item['before_sha256'], hashlib.sha256(original).hexdigest())
            self.assertEqual(item['after_sha256'], hashlib.sha256((self.reader / name).read_bytes()).hexdigest())

    def test_wrong_storage_root_is_refused_without_creating_a_reader_layout(self):
        wrong = self.directory / 'wrong-root'
        wrong.mkdir()
        self.refused_without_changes(root=wrong)

    def test_unsupported_or_ambiguous_desktop_config_is_refused_before_payload_write(self):
        for mutation in ('version', 'groups', 'duplicate-games'):
            with self.subTest(mutation=mutation):
                self.config = desktop()
                if mutation == 'version':
                    self.config['version'] = 6
                elif mutation == 'groups':
                    self.config['view']['groups'] = {'@Games': []}
                else:
                    self.config['view']['groups'].append(copy.deepcopy(self.config['view']['groups'][2]))
                self.write_config()
                self.refused_without_changes()

    def test_id_owned_by_a_different_application_is_refused(self):
        self.config['applications']['U_EraSGF'] = {'path': 'applications/something-else.app', 'title': 'Keep me'}
        self.write_config()
        self.refused_without_changes()

    def test_symlinked_parent_or_payload_file_is_refused_before_any_write(self):
        outside = self.directory / 'outside'
        outside.mkdir()
        (outside / 'era-sgf-focused.bmp').write_bytes(b'outside')
        for name, target in ((FOCUSED, outside / 'era-sgf-focused.bmp'), ('applications/icons', outside)):
            with self.subTest(name=name):
                path = self.reader / name
                if path.is_dir():
                    original = path.with_name('icons.saved')
                    path.rename(original)
                else:
                    original = None
                path.symlink_to(target, target_is_directory=target.is_dir())
                self.refused_without_changes()
                path.unlink()
                if original:
                    original.rename(path)

    def test_symlinked_root_and_backup_destinations_are_refused(self):
        alias = self.directory / 'reader-alias'
        alias.symlink_to(self.reader, target_is_directory=True)
        self.refused_without_changes(root=alias)
        outside = self.directory / 'outside-backups'
        outside.mkdir()
        self.backups.symlink_to(outside, target_is_directory=True)
        self.refused_without_changes()

    def test_archive_traversal_absolute_and_case_collision_are_refused(self):
        for name in ('../lost.txt', '/tmp/lost.txt', 'applications/../lost.txt',
                     'applications\\lost.txt', 'applications/ERA-SGF.app', APP):
            with self.subTest(name=name):
                self.write_archive([(name, b'bad')])
                self.refused_without_changes()

    def test_archive_symlinks_and_extra_app_or_config_payloads_are_refused(self):
        link = zipfile.ZipInfo('README-Era-SGF.txt')
        link.create_system = 3
        link.external_attr = (stat.S_IFLNK | 0o777) << 16
        for name, data in ((link, b'/tmp/target'), ('applications/installer.app', b'bad'),
                           ('system/config/extensions.cfg', b'bad'), ('sgf/my-game.sgf', b'bad')):
            with self.subTest(name=str(name)):
                self.write_archive([(name, data)])
                self.refused_without_changes()

    def test_missing_icon_or_wrong_binary_or_icon_format_is_refused(self):
        original = self.payload.copy()
        for name, value in ((ICON, None), (APP, b'host binary'), (FOCUSED, b'BMbad')):
            with self.subTest(name=name):
                self.payload = original.copy()
                if value is None:
                    del self.payload[name]
                else:
                    self.payload[name] = value
                self.write_archive()
                self.refused_without_changes()

    def test_late_destination_directory_collision_is_refused_before_app_is_replaced(self):
        (self.reader / 'README-Era-SGF.txt').mkdir()
        self.refused_without_changes()

    def test_case_alias_in_destination_is_refused_without_creating_a_second_file(self):
        (self.reader / APP).rename(self.reader / 'applications/ERA-SGF.app')
        self.refused_without_changes()

    def test_backups_inside_reader_are_refused(self):
        self.refused_without_changes(backups=self.reader / 'backup')

    def test_changed_destination_after_planning_is_refused_before_backup_or_write(self):
        installer = load_installer()
        plan = installer.make_plan(self.reader, self.archive, self.backups)
        (self.reader / APP).write_bytes(b'newer independent edit')
        before = snapshot(self.directory)
        with self.assertRaises(installer.Refused):
            installer.apply_plan(plan)
        self.assertEqual(snapshot(self.directory), before)

    def test_replace_failure_restores_all_original_files_from_verified_backup(self):
        installer = load_installer()
        plan = installer.make_plan(self.reader, self.archive, self.backups)
        before = {n: v for n, v in snapshot(self.reader).items() if v[0] != 'dir'}
        desktop_identity = (self.reader / CONFIG).parent.stat()
        replace = installer.os.replace
        injected = False

        def fail_config_once(source, destination, **kwargs):
            nonlocal injected
            parent = os.fstat(kwargs['dst_dir_fd'])
            if (not injected and destination == 'view.json'
                    and (parent.st_dev, parent.st_ino) == (desktop_identity.st_dev, desktop_identity.st_ino)):
                injected = True
                raise OSError(errno.EIO, 'injected configuration replace failure')
            return replace(source, destination, **kwargs)

        with mock.patch.object(installer.os, 'replace', side_effect=fail_config_once):
            with self.assertRaisesRegex(OSError, 'injected configuration replace failure'):
                installer.apply_plan(plan)
        after = {n: v for n, v in snapshot(self.reader).items() if v[0] != 'dir'}
        self.assertEqual(after, before)
        self.assertFalse(list(self.reader.rglob('.era-sgf-install-*')))
        manifest_path = next(self.backups.glob('*/manifest.json'))
        manifest = json.loads(manifest_path.read_text())
        self.assertEqual(manifest['status'], 'rolled-back')
        self.assertEqual(manifest['rollback_errors'], [])
        self.assertEqual((manifest_path.parent / 'files' / APP).read_bytes(), b'old working application')

    def test_file_flush_failure_does_not_replace_an_existing_reader_file(self):
        installer = load_installer()
        plan = installer.make_plan(self.reader, self.archive, self.backups)
        before = {n: v for n, v in snapshot(self.reader).items() if v[0] != 'dir'}
        fsync = installer.os.fsync
        injected = False

        def fail_reader_flush_once(fd):
            nonlocal injected
            if (not injected and stat.S_ISREG(os.fstat(fd).st_mode)
                    and os.readlink('/proc/self/fd/' + str(fd)).startswith(str(self.reader) + '/')):
                injected = True
                raise OSError(errno.EIO, 'injected reader flush failure')
            return fsync(fd)

        with mock.patch.object(installer.os, 'fsync', side_effect=fail_reader_flush_once):
            with self.assertRaisesRegex(OSError, 'injected reader flush failure'):
                installer.apply_plan(plan)
        self.assertEqual({n: v for n, v in snapshot(self.reader).items() if v[0] != 'dir'}, before)
        self.assertFalse(list(self.reader.rglob('.era-sgf-install-*')))

    def test_fat_mode_change_rejection_does_not_block_installation(self):
        installer = load_installer()
        plan = installer.make_plan(self.reader, self.archive, self.backups)
        denied = PermissionError(errno.EPERM, 'FAT does not support Unix mode changes')
        with mock.patch.object(installer.os, 'fchmod', side_effect=denied):
            with mock.patch.object(installer.os, 'chmod', side_effect=denied):
                installer.apply_plan(plan)
        self.assertEqual((self.reader / APP).read_bytes(), self.payload[APP])
        self.assertEqual(json.loads((self.reader / CONFIG).read_text())['applications']['U_EraSGF']['path'],
                         '/mnt/ext1/applications/era-sgf.app')


class DesktopIcons(unittest.TestCase):
    def test_committed_icons_are_distinct_crisp_8bit_uncompressed_bitmaps(self):
        images = []
        for name in ('era-sgf.bmp', 'era-sgf-focused.bmp'):
            path = ROOT / 'assets/icons' / name
            self.assertTrue(path.is_file(), 'Book launcher icon is not generated: ' + name)
            data = path.read_bytes()
            self.assertEqual(data[:2], b'BM')
            self.assertEqual(struct.unpack_from('<iiHHI', data, 18), (106, 112, 1, 8, 0))
            offset = struct.unpack_from('<I', data, 10)[0]
            self.assertEqual(len(data), offset + 108 * 112)
            pixels = data[offset:]
            self.assertEqual(set(pixels), {0, 255})
            self.assertGreater(pixels.count(0), 1500)
            self.assertGreater(pixels.count(255), 1500)
            images.append(data)
        self.assertNotEqual(*images)


if __name__ == '__main__':
    unittest.main(verbosity=2)
