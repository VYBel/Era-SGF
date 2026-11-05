# Changelog

## 1.2.0 — 2026-11-05

- Библиотека проверена на 117 245 SGF: убран предел 20 000 файлов, добавлены индекс путей и обновление только новых/изменённых файлов.
- Потоковая запись каталога и небольшие атомарные пакеты изменений вместо перезаписи всей базы при каждой отметке просмотра, открытии нового файла, перемещении или удалении.
- Повторное использование поиска при листании; случайная партия не запускает повторное сканирование в той же сессии.
- «Перепроверить все файлы» для полного чтения, включая изменения с сохранёнными timestamps.
- Миграция старой базы со статусами просмотра; поддержка исторических годов до 1800.
- Инструмент измерений и [результаты на полном архиве](docs/library-performance.md). Формат v2 не поддерживается версией 1.1.1: для отката нужен прежний каталог состояния.

## 1.1.1 — 2026-11-05

- Собственные полноэкранные меню вместо InkView OpenMenu: единый заголовок
  со стрелкой назад слева, названием по центру и крестиком справа.
- Крупные строки и адаптивные страницы во всех списках, аппаратный выбор и OK.
- Общий заголовок библиотеки; инструменты перенесены к счётчику партий.
- Меню сохраняется после сна и поворота; видимая позиция длинного списка сохраняется.
- Крестик всегда возвращает к доске, в том числе из меню поверх комментария.


## 1.1.0 — 2026-11-05

- Full-screen library with direct All/New/Viewed tabs, search, year selection,
  readable two-line rows, adaptive pagination and physical page-button support.
- Random unread game from the complete refreshed library; separate messages for
  empty, fully viewed and unreadable collections. Opening alone does not mark viewed.
- Shorter main menu, file actions under Current game, back arrows and distinct close behavior. Settings return to their parent menu.
- Includes the locally tested 1.0.2 fixes below.

## 1.0.2 — Local build, 2026-11-05

- Disable automatic keylock and power-off only while autoplay runs in the foreground;
  restore the normal idle policy on Stop, menus, branches, game end, errors and exit.
- Pause autoplay while hidden/locked and re-arm its timer on SHOW/FOREGROUND.
  Resume after a full selected pause, without advancing moves while asleep or
  requiring two button presses when the firmware drops a timer.
- Enlarge hoshi in proportion to grid spacing and line thickness so they remain
  visible on the high-resolution Era display.

## 1.0.1 — 2026-11-02

First public release, incorporating the features developed through 0.6.0.
The public version is 1.0.1; no separate 0.6.0 release is published.

- Standalone PocketBook InkView application with SGF file association.
- Variations, fullscreen comments, game information and main-line result.
- UTF-8 and legacy Cyrillic decoding with automatic and manual selection.
- Touch and physical navigation, ±10 moves, move-number entry and resume state.
- Catalogue with year/search and new/viewed filters; confirmed move/delete actions.
- Autoplay with configurable pause and mandatory stop at branches.
- High-contrast stones, last-move mark and configurable full screen refresh.
- PNG screenshots, Book launcher icon, seven startup marks and About screen.
- Compiled B288 release archive, backed-up Linux/POSIX desktop installer,
  Russian/English installation documentation and dependency notices.

Primary device: PocketBook Era 700, PocketBook OS/Linux. The B288 build uses SDK 6.8
and targets compatible firmware 6.8+. Other models require separate compatibility
checks. The UI is Russian; online downloads, SGF editing and engine analysis are
not included. See docs/validation.md for the scope of testing.
