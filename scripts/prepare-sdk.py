#!/usr/bin/env python3
"""Extract the pinned SDK locally and repair only known in-SDK linker symlinks."""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess

SHA256 = 'b924fbeba90e9258854e910c2c29a3105e6fb9c6e0e6de4cb13a015be3fc23e2'


def inside(path, root):
    return os.path.commonpath([str(path.resolve()), str(root.resolve())]) == str(root.resolve())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk-root', type=Path, required=True)
    parser.add_argument('--archive', type=Path)
    args = parser.parse_args()
    sdk = args.sdk_root.resolve()
    compiler = sdk / 'usr/bin/arm-obreey-linux-gnueabi-g++'
    if not compiler.is_file() or compiler.stat().st_size == 0:
        if not args.archive or not args.archive.is_file():
            parser.error('SDK is absent; run bash scripts/setup-sdk.sh first')
        checksum = hashlib.sha256()
        with args.archive.open('rb') as source:
            for chunk in iter(lambda: source.read(1024 * 1024), b''):
                checksum.update(chunk)
        if checksum.hexdigest() != SHA256:
            parser.error('SDK archive SHA-256 mismatch; extraction refused')
        result = subprocess.run(['7z', 'x', '-y', '-bso0', '-bsp0', '-o' + str(sdk.parent), str(args.archive.resolve())], capture_output=True, text=True)
        errors = [line for line in (result.stdout + result.stderr).splitlines() if line.startswith('ERROR:')]
        if result.returncode and (not errors or any(not line.startswith('ERROR: Dangerous link ') for line in errors)):
            raise SystemExit(result.stderr + result.stdout)
        if not compiler.is_file() or compiler.stat().st_size == 0:
            parser.error('Extraction did not produce the expected B288 compiler')
    # New 7-Zip versions leave empty placeholders for some safe ../ symlinks.
    # Recreate only explicit linker inputs, after resolving both paths inside this SDK.
    sysroot = sdk / 'usr/arm-obreey-linux-gnueabi/sysroot'
    targets = {'libm.so': 'libm.so.6', 'libdl.so': 'libdl.so.2', 'librt.so': 'librt.so.1', 'libutil.so': 'libutil.so.1', 'libresolv.so': 'libresolv.so.2', 'libnsl.so': 'libnsl.so.1', 'libcrypt.so': 'libcrypt.so.1', 'libanl.so': 'libanl.so.1'}
    for name, target in targets.items():
        destination = sysroot / 'usr/lib' / name
        real_target = sysroot / 'lib' / target
        if destination.is_symlink() or (destination.is_file() and destination.stat().st_size):
            continue
        if not real_target.is_file():
            continue
        if not inside(destination.parent, sdk) or not inside(real_target, sdk):
            parser.error('Link repair would leave the SDK directory')
        if destination.exists():
            destination.unlink()
        destination.symlink_to('../../lib/' + target)
    print(sdk)


if __name__ == '__main__':
    main()
