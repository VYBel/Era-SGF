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
    readme = f'''Era-SGF {version} — B288 release / выпуск для PocketBook

Target: PocketBook OS / Linux, ARMv7 soft-float, SDK 6.8 (firmware 6.8+).
Primary target: PocketBook Era 700. Other models/firmware need compatibility checks.
Целевая модель: Era 700. Совместимость с другими моделями проверяется отдельно.

Installation / Установка:
1. Connect the reader over USB. Extract this archive into the reader's storage root,
   merging applications/ and Books/. Do not copy the containing ZIP directory itself.
   Подключи книгу по USB и распакуй архив в корень доступной памяти, объединив папки.
2. Safely eject the reader. Open era-sgf from Applications first.
   Безопасно отключи USB и сначала запусти era-sgf из меню приложений.
   To add the Book icon beside Chess in Games, use the Linux/POSIX host installer attached to the release:
   python3 install-mounted-reader.py --root <reader> --archive <this.zip> --backup-dir <host-backups> --apply
   Для иконки «Книга» в разделе «Игры» используй установщик на компьютере из вложений релиза (Python 3.9+).
   It backs up and merges desktop/view.json; extracting the ZIP alone does not register it.
3. To register .sgf files, choose Settings → Associations in Era-SGF's menu.
   Для ассоциации .sgf открой «Настройки → Ассоциации SGF».
   Existing extensions.cfg is backed up before changes. Other associations are preserved.
4. Close and reopen the PocketBook library. Test Books/Era-SGF/first-steps.sgf.
   Закрой и открой библиотеку, затем проверь учебный SGF из папки Books/Era-SGF/.
   The stock library's handling of a newly registered format must be checked on your firmware.
   If SGF is not visible, use Era-SGF's own Open SGF menu and report the model/firmware.

Library / Библиотека:
Put your games under sgf/ (subfolders, including years, are supported). Choose Menu → Библиотека
партий for persistent all/new/viewed filters, year selection and search. Refresh the library
after copying new records. Reaching a leaf during navigation marks a file viewed; opening it
alone does not. Menu actions can mark it new/viewed, move it to sgf/viewed/<year>/, or delete
the selected file after confirmation. A move preserves the saved position and never replaces
an existing file. Source SGFs are never rewritten to change comments or encoding.
Положи партии в sgf/, при желании — в подпапки по годам. В «Библиотеке партий» доступны
все/новые/просмотренные, год и поиск. После копирования файлов нажми «⋯ → Обновить список партий».
Перенос и удаление текущего файла доступны в меню и требуют подтверждения.

Keys / Управление:
Page-turn keys: previous/next node. Menu or OK opens the menu.
Choose variations with the Branches button/menu. Tap a comment to expand it.
In the full comment view, page-turn keys change comment pages; Back returns to the board.
Кнопки перелистывания меняют ход. Варианты и возврат к развилке доступны в меню.
Нажатие на комментарий разворачивает его; «Назад» возвращает к доске.
Below the board: beginning, -10 moves, clickable move number, +10 moves and end.
Под доской: начало, −10 ходов, номер хода (нажми для ввода), +10 и конец.
Main menu: Library, Game, File, Screenshot, Settings and Exit.
Основное меню: «Библиотека», «Случайная новая партия», «Текущая партия», «Снимок позиции», «Настройки», «Выход».
Файловые операции доступны в «Текущая партия → Файл и отметка просмотра».
В библиотеке фильтры и поиск находятся над списком; ⋯ — обновление, открытие файла и сброс фильтров.
← возвращает на уровень выше, закрытие меню возвращает к доске.
Случайная новая выбирается среди непросмотренных файлов всей библиотеки.
Screen refresh / Очистка экрана:
Settings → Screen refresh: every 5/10/20/50 changes or manual; default 10.
The circular-arrow button at the top right forces a full refresh and resets the count.
Menu/keyboard return and rotation still refresh fully. Ordinary navigation uses partial
updates between cleanings; idle does not schedule any refresh timer.
«Настройки → Обновление экрана»: 5/10/20/50 изменений либо вручную, по умолчанию 10.
Круговая стрелка справа вверху очищает экран. Возврат из меню сохраняет полную очистку.
В ручном режиме при появлении следов старой позиции нажми кнопку очистки.

Autoplay / Автолистание:
Auto starts with a 3-second pause; the menu provides 1/2/3/5/10/15/30/60 seconds.
It stops at a branch or the end, on manual navigation, menus, hide/background and rotation.
No playback timer runs while stopped. No background library scanning or network use.
Автолистание: кнопка «Авто», выбор паузы в меню. На развилке программа останавливается.

Screenshots and logos / Снимки и логотипы:
Menu → Снимок позиции saves a PNG under screenshots/Era-SGF/ with player colours and board.
Menu → Settings → Логотип при запуске selects one of seven original monochrome marks.
Снимки сохраняются в screenshots/Era-SGF/. Все семь логотипов можно переключать в меню.

Game information and result / Информация и результат:
The top-left i button opens game details. The result from RE appears at the main-line end.
Settings → О программе shows version, author and license notices.
Кнопка i слева вверху — сведения о партии. Результат из RE появляется в конце основной
линии. «Настройки → О программе» — версия, автор и лицензии.

Encoding / Кодировки:
SGF CA is respected. Menu → Settings → Кодировка provides Auto, UTF-8, Windows-1251, KOI8-R, CP866,
ISO-8859-5 and ISO-8859-1. A manual choice overrides wrong CA metadata and is remembered.
Если русский текст нечитаем, выбери кодировку вручную. Исходный SGF не изменяется.

Removal / Удаление:
Use Menu → Settings → Associations → Удалить ассоциацию before deleting applications/era-sgf.app.
The menu action removes only Era-SGF's file handler, retaining other SGF handlers.
Only one .app is shipped. If upgrading from 0.1, delete the obsolete era-sgf-install.app
and era-sgf-uninstall.app launchers from applications/; the same actions remain in the menu.
Private resume state remains in system/config/era-sgf/ and can be removed separately.
Backups beside system/config/extensions.cfg are kept for manual rollback; restoring a whole
backup would replace any later association edits, so prefer the menu action.

Source / Исходный код: https://github.com/VYBel/Era-SGF
Original project license: MIT. See THIRD_PARTY and LICENSES for compiler runtime notices.
'''
    members = {
        'applications/era-sgf.app': (binary, 0o755),
        'applications/icons/era-sgf.bmp': ((ROOT / 'assets/icons/era-sgf.bmp').read_bytes(), 0o644),
        'applications/icons/era-sgf-focused.bmp': ((ROOT / 'assets/icons/era-sgf-focused.bmp').read_bytes(), 0o644),
        'Books/Era-SGF/first-steps.sgf': ((ROOT / 'examples/first-steps.sgf').read_bytes(), 0o644),
        'Books/Era-SGF/windows-1251.sgf': ((ROOT / 'tests/fixtures/windows-1251-declared.sgf').read_bytes(), 0o644),
        'README-Era-SGF.txt': (readme.encode('utf-8'), 0o644),
        'LICENSE-Era-SGF.txt': ((ROOT / 'LICENSE').read_bytes(), 0o644),
        'THIRD_PARTY-Era-SGF.md': ((ROOT / 'THIRD_PARTY.md').read_bytes(), 0o644),
    }
    for license in sorted((ROOT / 'LICENSES').glob('*.txt')):
        members['Era-SGF-LICENSES/' + license.name] = (license.read_bytes(), 0o644)
    args.output.mkdir(parents=True, exist_ok=True)
    destination = args.output / f'Era-SGF-{version}-b288.zip'
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
