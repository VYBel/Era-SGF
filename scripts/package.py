#!/usr/bin/env python3
"""Create an explicit, deterministic PocketBook install archive."""
import argparse
import hashlib
from pathlib import Path
import stat
import struct
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=ROOT / 'dist')
    args = parser.parse_args()
    binary = args.binary.read_bytes()
    if len(binary) < 52 or binary[:7] != b'\x7fELF\x01\x01\x01' or struct.unpack_from('<H', binary, 18)[0] != 40:
        parser.error('Expected a little-endian ELF32 ARM executable, not a host binary')
    if struct.unpack_from('<I', binary, 36)[0] & 0x400:
        parser.error('B288 requires the ARM soft-float calling convention; hard-float binary refused')
    version = (ROOT / 'VERSION').read_text().strip()
    readme = f'''Era-SGF {version} — experimental B288 build / экспериментальная сборка

Target: PocketBook OS / Linux, ARMv7 soft-float, SDK 6.8 (firmware 6.8+).
First target: PocketBook Era 700. Physical-device compatibility is not yet confirmed.
Целевая модель: Era 700. На физической книге эта сборка пока не проверена.

Installation / Установка:
1. Connect the reader over USB. Extract this archive into the reader's storage root,
   merging applications/ and Books/. Do not copy the containing ZIP directory itself.
   Подключи книгу по USB и распакуй архив в корень доступной памяти, объединив папки.
2. Safely eject the reader. Open era-sgf from Applications first.
   Безопасно отключи USB и сначала запусти era-sgf из меню приложений.
3. To register .sgf files, run era-sgf-install or choose the association item in Era-SGF's menu.
   Для ассоциации .sgf запусти era-sgf-install либо выбери соответствующий пункт меню Era-SGF.
   Existing extensions.cfg is backed up before changes. Other associations are preserved.
4. Close and reopen the PocketBook library. Test Books/Era-SGF/first-steps.sgf.
   Закрой и открой библиотеку, затем проверь учебный SGF из папки Books/Era-SGF/.
   The stock library's handling of a newly registered format must be checked on your firmware.
   If SGF is not visible, use Era-SGF's own Open SGF menu and report the model/firmware.

Keys / Управление:
Page-turn keys: previous/next node. Menu or OK opens the menu.
Choose variations with the Branches button/menu. Tap the comment heading to expand it.
In the full comment view, page-turn keys change comment pages; Back returns to the board.
Кнопки перелистывания меняют ход. Варианты и возврат к развилке доступны в меню.
Нажатие на заголовок комментария разворачивает его; «Назад» возвращает к доске.

Encoding / Кодировки:
SGF CA is respected. Menu → Кодировка provides Auto, UTF-8, Windows-1251, KOI8-R, CP866,
ISO-8859-5 and ISO-8859-1. A manual choice overrides wrong CA metadata and is remembered.
Если русский текст нечитаем, выбери кодировку вручную. Исходный SGF не изменяется.

Removal / Удаление:
Run era-sgf-uninstall before deleting the three Era-SGF .app files from applications/.
The uninstaller removes only Era-SGF's file handler, retaining other SGF handlers.
Private resume state remains in system/config/era-sgf/ and can be removed separately.
Backups beside system/config/extensions.cfg are kept for manual rollback; restoring a whole
backup would replace any later association edits, so prefer the uninstaller.

Source / Исходный код: https://github.com/VYBel/Era-SGF
Original project license: MIT. See THIRD_PARTY and LICENSES for compiler runtime notices.
'''
    members = {
        'applications/era-sgf.app': (binary, 0o755),
        'applications/era-sgf-install.app': (b'#!/bin/sh\nexec "$(dirname "$0")/era-sgf.app" --install\n', 0o755),
        'applications/era-sgf-uninstall.app': (b'#!/bin/sh\nexec "$(dirname "$0")/era-sgf.app" --uninstall\n', 0o755),
        'Books/Era-SGF/first-steps.sgf': ((ROOT / 'examples/first-steps.sgf').read_bytes(), 0o644),
        'Books/Era-SGF/windows-1251.sgf': ((ROOT / 'tests/fixtures/windows-1251-declared.sgf').read_bytes(), 0o644),
        'README-Era-SGF.txt': (readme.encode('utf-8'), 0o644),
        'LICENSE-Era-SGF.txt': ((ROOT / 'LICENSE').read_bytes(), 0o644),
        'THIRD_PARTY-Era-SGF.md': ((ROOT / 'THIRD_PARTY.md').read_bytes(), 0o644),
    }
    for license in sorted((ROOT / 'LICENSES').glob('*.txt')):
        members['Era-SGF-LICENSES/' + license.name] = (license.read_bytes(), 0o644)
    args.output.mkdir(parents=True, exist_ok=True)
    destination = args.output / f'Era-SGF-{version}-experimental-b288.zip'
    with zipfile.ZipFile(destination, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, (data, mode) in sorted(members.items()):
            info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            info.create_system = 3
            info.external_attr = (stat.S_IFREG | mode) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, data, compresslevel=9)
    digest = hashlib.sha256(destination.read_bytes()).hexdigest()
    destination.with_suffix('.zip.sha256').write_text(f'{digest}  {destination.name}\n')
    print(destination)


if __name__ == '__main__':
    main()
