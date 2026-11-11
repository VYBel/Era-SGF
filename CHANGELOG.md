# Changelog

## 1.2.4 — 2026-11-11

Публичный выпуск включает исправления локальных сборок 1.2.1–1.2.3:
быстрое открытие SGF и последней позиции, загрузку полного каталога только по
запросу, отдельный индекс случайных партий, надёжное сохранение отложенных отметок
и возврат в энергосберегающее ожидание без автоплея. После обновления со старого
релиза один раз создай индекс командой «Библиотека → ⋯ → Обновить список партий».
Подробности установки и отката — в [инструкции](docs/install.md).

- Случайный выбор проверяет не больше 32 файлов и прекращает следующие попытки
  после 250 мс перебора. Отдельное системное чтение этим временем не прерывается.
- Если доступная партия не найдена, приложение предлагает обновить список и
  возвращает управление без фоновых повторов. Каталог и отметки не меняются.

## 1.2.3 — локальная сборка, 2026-11-11

- Случайная новая партия выбирается из компактного двоичного индекса без загрузки
  полного каталога и заставки. Удалённые или недоступные файлы пропускаются.
- Индекс создаётся при обновлении библиотеки. Последние отметки просмотра,
  перемещения, удаления и открытые отдельно SGF учитываются через журнал изменений.
- Повреждённый или устаревший индекс требует явного обновления библиотеки;
  скрытого пересканирования архива нет.

## 1.2.2 — локальная сборка, 2026-11-11

- Отдельный SGF открывается сразу по ассоциации; повторный запуск восстанавливает
  последнюю позицию без чтения большого каталога. При обычном запуске остаётся
  короткая заставка с выбранным логотипом, при открытии файла заставки нет.
- Каталог загружается только для библиотеки, случайного выбора и перемещения/
  удаления файла. Обход архива запускается только явной командой обновления.
- Открытые файлы и отметки до загрузки каталога сохраняются атомарно в
  `library.pending/`; при загрузке объединяются без сброса старых отметок.
  Малое число изменений записывается в журнал вместо перезаписи всей базы.
- Отмена загрузки возвращает к доске. Повреждённый каталог не мешает читать SGF.
- Завершение экранной клавиатуры и подтверждений возвращает процесс в режим
  ожидания. В статическом просмотре нет таймеров или фоновой индексации.
- Перед возвратом к 1.2.1 нужно открыть библиотеку в 1.2.2 и дождаться объединения
  записей: старые версии не понимают `library.pending/`. Всю папку состояния
  следует сохранять при обновлениях и откате.

## 1.2.1 — локальная сборка, 2026-11-11

- Каталог загружается после первого кадра, порциями до 256 записей с возвратом
  в цикл событий примерно каждые 8 мс обработки, вместо чтения всей базы до InkViewMain.
- Процент загрузки, выход крестиком/«Назад», пауза при скрытии и продолжение после сна.
- Готовая библиотека используется без автоматического обхода файлов при первом
  открытии или выборе случайной партии. После добавления/удаления SGF нужно выбрать
  «Библиотека → инструменты → Обновить список партий».
- Ошибка чтения каталога останавливает запуск без замены базы данными одной партии.
  В журнале запуска сохраняются длительность загрузки, число записей и пик RSS.
- Формат каталога v2 и отметки просмотра не меняются. Проверка на процессоре
  книги ещё необходима; локальные проверки не подтверждают исчезновение зависания на Era.

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
