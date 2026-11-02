#!/usr/bin/env python3
"""Plan or apply an Era-SGF ZIP to a mounted PocketBook (host Python only)."""
import argparse
import copy
from dataclasses import dataclass
from datetime import datetime, timezone
import errno
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import stat
import struct
import sys
import uuid
import zipfile

PROJECT_ROOT = Path(__file__).resolve().parents[1]
APP = 'applications/era-sgf.app'
ICON = 'applications/icons/era-sgf.bmp'
FOCUSED = 'applications/icons/era-sgf-focused.bmp'
CONFIG = 'system/config/desktop/view.json'
APP_ID = 'U_EraSGF'
NATIVE_APP = '/mnt/ext1/' + APP
SAMPLES = {'Books/Era-SGF/first-steps.sgf', 'Books/Era-SGF/windows-1251.sgf'}
PAYLOAD = {APP, ICON, FOCUSED, *SAMPLES, 'README-Era-SGF.txt',
           'LICENSE-Era-SGF.txt', 'THIRD_PARTY-Era-SGF.md'}
DIRECTORIES = {'applications', 'applications/icons', 'Books', 'Books/Era-SGF', 'Era-SGF-LICENSES'}
MAX_FILE = 64 * 1024 * 1024
MAX_TOTAL = 128 * 1024 * 1024
DIR_FLAGS = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW


class Refused(ValueError):
    """Input cannot be installed safely without changing the requested scope."""


def digest(data):
    return hashlib.sha256(data).hexdigest()


def absolute(path):
    # Do not resolve symlinks: every component is checked by open_directory.
    return Path(os.path.abspath(path))


def sync_directory(fd):
    try:
        os.fsync(fd)
    except OSError as error:
        # Some removable filesystems do not implement directory fsync. File
        # fsync and read-back verification remain mandatory on every write.
        if error.errno not in {errno.EINVAL, errno.ENOTSUP, errno.EOPNOTSUPP}:
            raise


def entry(parent_fd, name):
    # Reject aliases even on case-sensitive test hosts: FAT is case-insensitive.
    aliases = [n for n in os.listdir(parent_fd) if n.casefold() == name.casefold() and n != name]
    if aliases:
        raise Refused('case collision at ' + name)
    try:
        result = os.stat(name, dir_fd=parent_fd, follow_symlinks=False)
    except FileNotFoundError:
        return None
    if stat.S_ISLNK(result.st_mode):
        raise Refused('symlink destination or parent: ' + name)
    return result


def descend(start_fd, parts, create=False, missing_ok=False):
    """Walk anchored directory descriptors without following any symlink."""
    current = os.dup(start_fd)
    try:
        for part in parts:
            info = entry(current, part)
            if info is None:
                if create:
                    os.mkdir(part, 0o755, dir_fd=current)
                    sync_directory(current)
                elif missing_ok:
                    os.close(current)
                    return None
                else:
                    raise Refused('required directory is missing: ' + part)
            elif not stat.S_ISDIR(info.st_mode):
                raise Refused('directory destination is not a directory: ' + part)
            next_fd = os.open(part, DIR_FLAGS, dir_fd=current)
            os.close(current)
            current = next_fd
        return current
    except BaseException:
        os.close(current)
        raise


def open_directory(path, create=False, missing_ok=False):
    path = absolute(path)
    start = os.open('/', DIR_FLAGS)
    try:
        return descend(start, path.parts[1:], create=create, missing_ok=missing_ok)
    finally:
        os.close(start)


def read_at(parent_fd, name):
    info = entry(parent_fd, name)
    if info is None:
        return None
    if not stat.S_ISREG(info.st_mode):
        raise Refused('file destination is not a regular file: ' + name)
    fd = os.open(name, os.O_RDONLY | os.O_NOFOLLOW, dir_fd=parent_fd)
    with os.fdopen(fd, 'rb') as source:
        if not stat.S_ISREG(os.fstat(source.fileno()).st_mode):
            raise Refused('file changed type while reading: ' + name)
        data = source.read(MAX_TOTAL + 1)
    if len(data) > MAX_TOTAL:
        raise Refused('file is too large to snapshot safely: ' + name)
    return data


class Tree:
    def __init__(self, root):
        self.fd = open_directory(root)

    def __enter__(self):
        return self

    def __exit__(self, *unused):
        os.close(self.fd)

    def read(self, name):
        parts = PurePosixPath(name).parts
        parent = descend(self.fd, parts[:-1], missing_ok=True)
        if parent is None:
            return None
        try:
            return read_at(parent, parts[-1])
        finally:
            os.close(parent)

    def stage(self, name, data):
        parts = PurePosixPath(name).parts
        parent = descend(self.fd, parts[:-1], create=True)
        try:
            return Staged(parent, parts[-1], data, 0o755 if name == APP else 0o644)
        except BaseException:
            os.close(parent)
            raise

    def write(self, name, data):
        staged = self.stage(name, data)
        try:
            staged.publish()
        finally:
            staged.close()


class Staged:
    """A flushed temporary file in the same directory as its final destination."""
    def __init__(self, parent, name, data, mode):
        self.parent, self.name, self.data, self.mode = parent, name, data, mode
        self.temporary = '.era-sgf-install-' + uuid.uuid4().hex + '.tmp'
        self.published = False
        entry(parent, name)
        fd = os.open(self.temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW,
                     mode, dir_fd=parent)
        try:
            with os.fdopen(fd, 'wb') as destination:
                destination.write(data)
                destination.flush()
                os.fsync(destination.fileno())
            if read_at(parent, self.temporary) != data:
                raise OSError('temporary file failed read-back verification: ' + name)
        except BaseException:
            os.unlink(self.temporary, dir_fd=parent)
            raise

    def publish(self):
        entry(self.parent, self.name)
        os.replace(self.temporary, self.name, src_dir_fd=self.parent, dst_dir_fd=self.parent)
        self.temporary = None
        self.published = True
        sync_directory(self.parent)
        if read_at(self.parent, self.name) != self.data:
            raise OSError('installed file failed read-back verification: ' + self.name)

    def close(self):
        try:
            if self.temporary is not None:
                os.unlink(self.temporary, dir_fd=self.parent)
                self.temporary = None
        finally:
            os.close(self.parent)


def archive_path(name, directory=False):
    plain = name[:-1] if directory and name.endswith('/') else name
    parts = plain.split('/')
    if (not plain or '\\' in plain or '\0' in plain or name.startswith('/')
            or any(p in ('', '.', '..') or ':' in p or p.endswith((' ', '.')) for p in parts)):
        raise Refused('unsafe archive path: ' + repr(name))
    return plain


def read_archive(path):
    path = absolute(path)
    with Tree(path.parent) as parent:
        raw = parent.read(path.name)
    if raw is None:
        raise Refused('archive does not exist')
    contents, seen, total = {}, set(), 0
    with zipfile.ZipFile(io.BytesIO(raw)) as archive:
        for info in archive.infolist():
            # orig_filename preserves a NUL which ZipInfo.filename would truncate.
            name = archive_path(info.orig_filename, info.is_dir())
            folded = name.casefold()
            if folded in seen:
                raise Refused('duplicate or case-colliding archive member: ' + name)
            seen.add(folded)
            file_type = stat.S_IFMT(info.external_attr >> 16)
            if file_type not in (0, stat.S_IFDIR if info.is_dir() else stat.S_IFREG):
                raise Refused('archive member is not a regular file/directory: ' + name)
            if info.is_dir():
                if name not in DIRECTORIES:
                    raise Refused('unexpected archive directory: ' + name)
                continue
            license_file = (name.startswith('Era-SGF-LICENSES/') and len(name.split('/')) == 2
                            and name.endswith('.txt'))
            if name not in PAYLOAD and not license_file:
                raise Refused('unexpected archive payload: ' + name)
            total += info.file_size
            if info.file_size > MAX_FILE or total > MAX_TOTAL:
                raise Refused('archive exceeds the installation size limit')
            # Read every member, checking ZIP CRC, before any backup or reader write.
            contents[name] = archive.read(info)
    if not {APP, ICON, FOCUSED}.issubset(contents):
        raise Refused('archive must contain the application and both desktop icons')
    binary = contents[APP]
    if (len(binary) < 52 or binary[:7] != b'\x7fELF\x01\x01\x01'
            or struct.unpack_from('<H', binary, 18)[0] != 40
            or struct.unpack_from('<I', binary, 36)[0] & 0x400):
        raise Refused('application must be a little-endian ELF32 ARM soft-float binary')
    for name in (ICON, FOCUSED):
        data = contents[name]
        if len(data) < 1078 or data[:2] != b'BM':
            raise Refused('invalid desktop BMP: ' + name)
        width, height, planes, depth, compression = struct.unpack_from('<iiHHI', data, 18)
        offset = struct.unpack_from('<I', data, 10)[0]
        if ((width, height, planes, depth, compression) != (106, 112, 1, 8, 0)
                or offset < 1078 or len(data) != offset + 108 * 112):
            raise Refused('desktop icon must be a 106x112 uncompressed 8-bit BMP: ' + name)
    return contents, digest(raw)


def unique_object(pairs):
    result = {}
    for name, value in pairs:
        if name in result:
            raise Refused('duplicate JSON key: ' + name)
        result[name] = value
    return result


def invalid_constant(value):
    raise Refused('non-standard JSON number: ' + value)


def merge_desktop(data):
    config = json.loads(data.decode('utf-8'), object_pairs_hook=unique_object, parse_constant=invalid_constant)
    if (not isinstance(config, dict)
            or not ((type(config.get('version')) is int and config['version'] == 7)
                    or config.get('version') == '7')
            or not isinstance(config.get('applications'), dict)
            or not isinstance(config.get('view'), dict)
            or not isinstance(config['view'].get('groups'), list)):
        raise Refused('expected the reader desktop version 7 applications/view/groups structure')
    merged = copy.deepcopy(config)
    applications, groups = merged['applications'], merged['view']['groups']
    games = None
    for group in groups:
        if (not isinstance(group, dict) or not isinstance(group.get('title'), str)
                or not isinstance(group.get('apps'), list)
                or not all(isinstance(app, str) for app in group['apps'])):
            raise Refused('unsupported desktop group structure')
        if group['title'] == '@Games':
            if games is not None:
                raise Refused('multiple @Games groups are ambiguous')
            games = group
    if games is None:
        raise Refused('the reader has no @Games group; inspect its launcher configuration first')
    own = applications.get(APP_ID, {})
    if not isinstance(own, dict) or own.get('path', NATIVE_APP) not in (APP, NATIVE_APP):
        raise Refused(APP_ID + ' is already owned by a different application')
    own['path'], own['title'] = NATIVE_APP, 'Era-SGF'
    for key, path in (('icon', ICON), ('focused_icon', FOCUSED)):
        icon = own.get(key)
        if not isinstance(icon, dict):
            icon = {}
        icon['path'] = path
        own[key] = icon
    applications[APP_ID] = own
    for group in groups:
        # Retain the first existing Games position and every unrelated app/order.
        apps, retained = [], False
        for app in group['apps']:
            if app != APP_ID:
                apps.append(app)
            elif group is games and not retained:
                apps.append(app)
                retained = True
        if group is games and not retained:
            apps.append(APP_ID)
        group['apps'] = apps
    if merged == config:
        return data
    return (json.dumps(merged, ensure_ascii=False, indent=4, allow_nan=False) + '\n').encode('utf-8')


@dataclass
class Action:
    path: str
    before: object
    after: bytes
    action: str

    def record(self):
        return {'path': self.path, 'action': self.action,
                'before_sha256': None if self.before is None else digest(self.before),
                'after_sha256': digest(self.before if self.action == 'preserve' else self.after),
                'backup_path': 'files/' + self.path if self.action == 'replace' else None}

    @property
    def changes(self):
        return self.action in ('create', 'replace')


@dataclass
class Plan:
    root: Path
    backup_base: Path
    root_identity: tuple
    archive_sha256: str
    actions: list

    def report(self, apply=False, backup=None):
        return {'mode': 'apply' if apply else 'dry-run', 'root': str(self.root),
                'archive_sha256': self.archive_sha256, 'backup': None if backup is None else str(backup),
                'changes': [a.record() for a in self.actions if a.changes],
                'preserved': [a.path for a in self.actions if a.action == 'preserve']}


def make_plan(root, archive, backup_base):
    root, backup_base = absolute(root), absolute(backup_base)
    if root == Path('/') or backup_base == root or root in backup_base.parents:
        raise Refused('backups must be outside the reader root; the root must not be /')
    backup_fd = open_directory(backup_base, missing_ok=True)
    if backup_fd is not None:
        os.close(backup_fd)
    payload, archive_hash = read_archive(archive)
    actions = []
    with Tree(root) as tree:
        identity = os.fstat(tree.fd)
        applications = descend(tree.fd, ['applications'])
        os.close(applications)
        before_config = tree.read(CONFIG)
        if before_config is None:
            raise Refused('wrong reader root: ' + CONFIG + ' is missing')
        after_config = merge_desktop(before_config)
        # Validate all target parents and file types, including samples retained
        # in place. A late collision must not partially update the application.
        for name, after in sorted(payload.items()):
            before = tree.read(name)
            kind = ('preserve' if name in SAMPLES and before is not None else
                    'unchanged' if before == after else 'create' if before is None else 'replace')
            actions.append(Action(name, before, after, kind))
        actions.append(Action(CONFIG, before_config, after_config,
                              'unchanged' if before_config == after_config else 'replace'))
    return Plan(root, backup_base, (identity.st_dev, identity.st_ino), archive_hash, actions)


def check_snapshot(tree, actions):
    for action in actions:
        if tree.read(action.path) != action.before:
            raise Refused('destination changed since planning: ' + action.path)


def encode_manifest(manifest):
    return (json.dumps(manifest, ensure_ascii=False, indent=2) + '\n').encode('utf-8')


def apply_plan(plan):
    changes = [a for a in plan.actions if a.changes]
    if not changes:
        return None
    with Tree(plan.root) as reader:
        identity = os.fstat(reader.fd)
        if (identity.st_dev, identity.st_ino) != plan.root_identity:
            raise Refused('reader root changed since planning')
        check_snapshot(reader, plan.actions)
        # Back up and verify every overwritten original before touching reader
        # files. The unique session never overwrites an earlier backup.
        base_fd = open_directory(plan.backup_base, create=True)
        session = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ-') + uuid.uuid4().hex[:12]
        try:
            os.mkdir(session, 0o755, dir_fd=base_fd)
            sync_directory(base_fd)
        finally:
            os.close(base_fd)
        backup = plan.backup_base / session
        manifest = {'schema': 1, 'status': 'prepared', 'root': str(plan.root),
                    'archive_sha256': plan.archive_sha256,
                    'files': [a.record() for a in plan.actions]}
        with Tree(backup) as copies:
            for action in changes:
                if action.before is not None:
                    copies.write('files/' + action.path, action.before)
            copies.write('manifest.json', encode_manifest(manifest))
            staged = []
            try:
                for action in changes:
                    staged.append((action, reader.stage(action.path, action.after)))
                check_snapshot(reader, plan.actions)
                # Desktop registration is last, after the app and its icons.
                for action, destination in staged:
                    if read_at(destination.parent, destination.name) != action.before:
                        raise Refused('destination changed before replace: ' + action.path)
                    destination.publish()
                manifest['status'] = 'complete'
                copies.write('manifest.json', encode_manifest(manifest))
            except BaseException as failure:
                rollback_errors = []
                for action, destination in reversed(staged):
                    if not destination.published:
                        continue
                    try:
                        if read_at(destination.parent, destination.name) != action.after:
                            raise OSError('installed file changed concurrently')
                        if action.before is None:
                            os.unlink(destination.name, dir_fd=destination.parent)
                            sync_directory(destination.parent)
                        else:
                            restored = Staged(os.dup(destination.parent), destination.name, action.before, destination.mode)
                            try:
                                restored.publish()
                            finally:
                                restored.close()
                    except BaseException as error:
                        rollback_errors.append(action.path + ': ' + str(error))
                manifest['status'] = 'rollback-incomplete' if rollback_errors else 'rolled-back'
                manifest['error'] = str(failure)
                manifest['rollback_errors'] = rollback_errors
                copies.write('manifest.json', encode_manifest(manifest))
                raise OSError('installation failed; ' + manifest['status'] + '; backups: '
                              + str(backup) + '; ' + str(failure)) from failure
            finally:
                for unused, destination in staged:
                    destination.close()
    return backup


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True, help='mounted reader storage root')
    parser.add_argument('--archive', type=Path, required=True, help='Era-SGF B288 ZIP built by scripts/package.py')
    parser.add_argument('--backup-dir', type=Path, default=PROJECT_ROOT / 'local/reader-backups',
                        help='host backup directory, outside reader (default: local/reader-backups)')
    parser.add_argument('--apply', action='store_true', help='write the validated plan; otherwise dry-run')
    args = parser.parse_args()
    try:
        plan = make_plan(args.root, args.archive, args.backup_dir)
    except (OSError, ValueError, zipfile.BadZipFile, RuntimeError) as error:
        print('Installation refused: ' + str(error), file=sys.stderr)
        return 1
    try:
        backup = apply_plan(plan) if args.apply else None
    except (OSError, ValueError) as error:
        print('Installation failed: ' + str(error), file=sys.stderr)
        return 1
    print(json.dumps(plan.report(args.apply, backup), ensure_ascii=False, indent=2))
    return 0


if __name__ == '__main__':
    sys.exit(main())
