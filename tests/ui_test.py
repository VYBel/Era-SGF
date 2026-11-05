#!/usr/bin/env python3
import json
import os
from pathlib import Path
import subprocess
import struct
import zlib
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
from unittest.mock import patch

UI = str(Path(sys.argv.pop(1)).resolve())
INSPECT = str(Path(sys.argv.pop(1)).resolve())
PREVIEW = str(Path(INSPECT).with_name('era-sgf-preview'))


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

    def run_ui(self, events, *args, wait_startup=True):
        env = dict(os.environ, ERA_SGF_ROOT=str(self.root), ERA_SGF_TEST_EVENTS=('wait:1500;' if wait_startup else '') + events,
                   ERA_SGF_TEST_FRAME=str(self.root / 'frame.pgm'))
        result = subprocess.run([UI, *map(str, args)], env=env, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout

    def test_custom_menu_header_back_close_and_wake(self):
        trace = self.run_ui('menu;tap-text:Настройки;tap-text:Пауза автолистания;'
                            'hide;show;rotate;back;close', self.record)
        self.assertIn('Настройки', self.last_frame(trace))
        self.assertNotIn('MENU\n', trace)
        trace = self.run_ui('menu;tap-text:Настройки;tap:1200,60;close', self.record)
        self.assertIn('Ход', self.last_frame(trace))
        self.assertEqual(self.saved_node(), 0)
        self.assertIn('TIMERS_ACTIVE 0', trace)

    def test_long_file_menu_paging_keyboard_and_touch_open_correct_record(self):
        for i in range(25):
            (self.root / f'game-{i:02}.sgf').write_text(f'(;SZ[19]C[Record {i:02}])')
        trace = self.run_ui('menu;tap-text:Открыть SGF;tap:1150,1600;hide;show;'
                            'tap-text:game-15.sgf;close')
        self.assertIn('Record 15', self.last_frame(trace))
        trace = self.run_ui('menu;tap-text:Текущая партия;tap-text:Файл и отметка;tap-text:Открыть SGF;'
                            + ';'.join(['next'] * 18) + ';rotate;hide;show;ok;close')
        self.assertIn('Record 15', self.last_frame(trace))
        self.assertIn('TIMERS_ACTIVE 0', trace)

    def test_touch_paged_menu_keeps_visible_anchor_on_rotation(self):
        for i in range(40):
            (self.root / f'game-{i:02}.sgf').write_text(f'(;SZ[19]C[Record {i:02}])')
        trace = self.run_ui('menu;tap-text:Открыть SGF;tap:1150,1600;tap:1150,1600;'
                            'tap:1150,1600;rotate;hide;show;close')
        self.assertIn('game-31.sgf', self.last_frame(trace))

    def test_header_close_leaves_full_comment_and_static_menu_has_no_timer(self):
        trace = self.run_ui('menu;tap-text:Текущая партия;tap-text:Комментарий на весь экран;'
                            'menu;tap-text:Настройки;tap:1200,60;close', self.record)
        self.assertNotIn('Доска', self.last_frame(trace))
        self.assertIn('Ход', self.last_frame(trace))
        baseline = self.run_ui('menu;tap-text:Настройки;close', self.record)
        waiting = self.run_ui('menu;tap-text:Настройки;wait:60000;close', self.record)
        self.assertEqual(baseline.count('UPDATE_FULL'), waiting.count('UPDATE_FULL'))
        self.assertIn('TIMERS_ACTIVE 0', waiting)

    def test_white_stone_has_white_interior_and_black_border_in_native_framebuffer(self):
        self.record.write_text('(;SZ[9]CA[UTF-8]PB[Black player]PW[White player]AB[bb]AW[cc])')
        self.run_ui('close', self.record)
        vector = subprocess.run([PREVIEW, str(self.record)], capture_output=True, text=True, check=True)
        circles = ET.fromstring(vector.stdout).findall('{http://www.w3.org/2000/svg}circle')
        white = next(c for c in circles if c.attrib.get('fill') == 'rgb(255,255,255)' and int(c.attrib['r']) > 10)
        black = max((c for c in circles if c.attrib.get('fill') == 'rgb(0,0,0)'), key=lambda c: int(c.attrib['r']))
        header, size, maximum, data = (self.root / 'frame.pgm').read_bytes().split(b'\n', 3)
        self.assertEqual((header, maximum), (b'P5', b'255'))
        width, height = map(int, size.split())
        self.assertEqual((width, height), (1264, 1680))
        for circle, interior in [(black, 0), (white, 255)]:
            x, y, radius = (int(circle.attrib[k]) for k in ['cx', 'cy', 'r'])
            self.assertEqual(data[y * width + x], interior)
            self.assertEqual(data[(y + 2) * width + x + 2], interior)
            self.assertEqual(data[y * width + x - radius], 0)

    def test_players_have_explicit_colours_and_empty_comments_stay_empty(self):
        self.record.write_text('(;SZ[9]CA[UTF-8]PB[Black player]BR[9d]PW[White player]WR[8d];B[aa])')
        trace = self.run_ui('close', self.record)
        self.assertIn('B: Black player (9d) - W: White player (8d)', trace)
        self.assertNotIn('нет комментария', trace)
        self.assertNotIn('Комментарий 1/1', trace)
        self.assertNotIn('UTF-8', trace)
        self.assertNotIn('Ветки 1', trace)

    def test_submenus_return_one_level_with_arrow_and_close_to_board(self):
        for back in ('tap:70,60',):
            trace = self.run_ui(f'menu;tap-text:Настройки;tap-text:Пауза автолистания;{back}', self.record)
            self.assertIn('Настройки', self.last_frame(trace))
            trace = self.run_ui(f'menu;tap-text:Текущая партия;tap-text:Навигация;{back};{back}', self.record)
            final = self.last_frame(trace)
            self.assertIn('Текущая партия', final)
            self.assertNotIn('900:', final)
        trace = self.run_ui('menu;tap-text:Настройки;tap-text:Пауза автолистания;tap:1200,60;close', self.record)
        self.assertIn('Ход', self.last_frame(trace))

    def test_refresh_interval_counts_changes_and_manual_button_resets_it(self):
        self.navigation_fixture()
        # Startup full updates are independent of the navigation interval.
        baseline = self.run_ui('close', self.record).count('UPDATE_FULL')
        trace = self.run_ui(';'.join(['next'] * 9) + ';close', self.record)
        self.assertEqual(trace.count('UPDATE_FULL'), baseline)
        trace = self.run_ui(';'.join(['next'] * 10) + ';close', self.record)
        self.assertEqual(trace.count('UPDATE_FULL'), baseline + 1)
        trace = self.run_ui('tap:1220,40;close', self.record)
        self.assertEqual(trace.count('UPDATE_FULL'), baseline + 1)
        changes = ';'.join('next' if i % 2 == 0 else 'previous' for i in range(9))
        trace = self.run_ui(changes + ';tap:1220,40;' + changes + ';close', self.record)
        self.assertEqual(trace.count('UPDATE_FULL'), baseline + 1)

    def test_refresh_setting_is_persisted_and_manual_disables_periodic_cleaning(self):
        self.navigation_fixture()
        for option, interval in ((500, 5), (501, 10), (502, 20), (503, 50), (504, 0)):
            trace = self.run_ui(f"menu;tap-text:Настройки;tap-text:Обновление экрана;tap-text:{('Через ' + str(interval)) if interval else 'Только вручную'};close", self.record)
            prefs = (self.root / 'system/config/era-sgf/preferences').read_text()
            self.assertIn(f'refresh={interval}\n', prefs)
        baseline = self.run_ui('close', self.record).count('UPDATE_FULL')
        trace = self.run_ui(';'.join(['next'] * 20) + ';close', self.record)
        self.assertEqual(trace.count('UPDATE_FULL'), baseline)
        self.assertIn('TIMERS_ACTIVE 0', trace)

    def test_configured_refresh_threshold_and_menu_return(self):
        self.navigation_fixture()
        for interval in (5, 10, 20, 50):
            prefs = self.root / 'system/config/era-sgf/preferences'
            prefs.parent.mkdir(parents=True, exist_ok=True)
            prefs.write_text(f'pause=3\nlogo=5\nrefresh={interval}\n')
            baseline = self.run_ui('close', self.record).count('UPDATE_FULL')
            changes = ['next' if i % 2 == 0 else 'previous' for i in range(interval)]
            trace = self.run_ui(';'.join(changes) + ';close', self.record)
            self.assertEqual(trace.count('UPDATE_FULL'), baseline + 1)
        trace = self.run_ui('menu;tap-text:Текущая партия;tap-text:Навигация;tap-text:На 10 ходов назад;close', self.record)
        self.assertEqual(trace.count('UPDATE_FULL'), baseline + 4)  # main, game, navigation, board

    def test_library_search_year_and_viewed_filters_open_the_expected_game(self):
        (self.root / 'sgf/1969').mkdir(parents=True)
        first = self.root / 'sgf/1969/first.sgf'
        first.write_text('(;SZ[9]CA[UTF-8]PB[Go Seigen]PW[Cho Chikun]DT[1969-11-14];B[aa]C[Target game])')
        (self.root / 'sgf/2001').mkdir()
        (self.root / 'sgf/2001/second.sgf').write_text('(;SZ[9]PB[Other]PW[Players]DT[2001-01-01])')
        trace = self.run_ui('menu;tap-text:Библиотека партий;tap-text:Поиск;text:seigen;tap-text:Go Seigen;next;close')
        self.assertIn('Target game', trace)
        trace = self.run_ui('menu;tap-text:Библиотека партий;tap-text:Просмотренные;close')
        final_menu = trace.split('FRAME\n')[-2].split('END_')[0]
        self.assertIn('Seigen', final_menu)
        self.assertNotIn('Other', final_menu)
        trace = self.run_ui('menu;tap-text:Библиотека партий;tap-text:Год;tap-text:1969;close')
        self.assertIn('1969', trace.split('FRAME\n')[-2].split('END_')[0])
        self.assertNotIn('2001-01-01', trace.split('FRAME\n')[-2].split('END_')[0])

    def test_delete_requires_confirmation_and_moves_preserve_resume_and_bytes(self):
        (self.root / 'sgf/1969').mkdir(parents=True)
        game = self.root / 'sgf/1969/game.sgf'
        game.write_text('(;SZ[9]CA[UTF-8]DT[1969];B[aa];W[bb])')
        before = game.read_bytes()
        self.run_ui('next;menu;tap-text:Текущая партия;tap-text:Файл и отметка;tap-text:Удалить файл с книги;confirm:2;close', game)
        self.assertEqual(game.read_bytes(), before)
        self.run_ui('menu;tap-text:Текущая партия;tap-text:Файл и отметка;tap-text:Перенести в viewed;confirm:1;close', game)
        moved = self.root / 'sgf/viewed/1969/game.sgf'
        self.assertFalse(game.exists())
        self.assertEqual(moved.read_bytes(), before)
        result = subprocess.run([INSPECT, str(moved), '--state-dir', str(self.root / 'system/config/era-sgf')], capture_output=True, text=True, check=True)
        self.assertEqual(json.loads(result.stdout)['node'], 1)
        self.run_ui('menu;tap-text:Текущая партия;tap-text:Файл и отметка;tap-text:Удалить файл с книги;confirm:1;close', moved)
        self.assertFalse(moved.exists())

    def saved_node(self, record=None):
        result = subprocess.run([INSPECT, str(record or self.record), '--state-dir', str(self.root / 'system/config/era-sgf')], capture_output=True, text=True, check=True)
        return json.loads(result.stdout)['node']

    def test_autoplay_defaults_to_three_seconds_and_stops_on_arriving_at_a_branch(self):
        self.run_ui('menu;tap-text:Текущая партия;tap-text:Начать автолистание;wait:2999;hide;close', self.record)
        self.assertEqual(self.saved_node(), 0)
        trace = self.run_ui('menu;tap-text:Текущая партия;tap-text:Начать автолистание;wait:3000;wait:12000;close', self.record)
        self.assertEqual(self.saved_node(), 1)
        self.assertIn('TIMERS_ACTIVE 0', trace)
        self.run_ui('tap-text:Авто;wait:6000;close', self.record)
        self.assertEqual(self.saved_node(), 1, 'starting autoplay must not choose a branch')

    def test_autoplay_pause_is_selectable_remembered_and_stops_at_the_end(self):
        self.record.write_text('(;SZ[9];B[aa];W[bb])')
        self.run_ui('menu;tap-text:Настройки;tap-text:Пауза автолистания;tap-text:2 с;menu;tap-text:Текущая партия;tap-text:Начать автолистание;wait:1999;hide;close', self.record)
        self.assertEqual(self.saved_node(), 0)
        trace = self.run_ui('menu;tap-text:Текущая партия;tap-text:Начать автолистание;wait:2000;wait:2000;wait:20000;close', self.record)
        self.assertEqual(self.saved_node(), 2)
        self.assertIn('TIMERS_ACTIVE 0', trace)
        self.assertEqual(trace.splitlines().count('FRAME'), 7)  # splash, board, two menus, start, two moves

    def test_autoplay_stops_on_manual_navigation_menu_hide_background_rotation_and_errors(self):
        self.record.write_text('(;SZ[9];B[aa];W[bb];B[cc];W[dd])')
        for interruption in ['next', 'menu', 'hide', 'background', 'rotate']:
            with self.subTest(interruption=interruption):
                self.run_ui('menu;tap-text:Текущая партия;tap-text:Навигация;tap-text:В начало;menu;tap-text:Текущая партия;tap-text:Начать автолистание;' + interruption + ';wait:30000;close', self.record)
                self.assertEqual(self.saved_node(), 1 if interruption == 'next' else 0)
        self.record.write_text('(;SZ[9];B[zz])')
        trace = self.run_ui('menu;tap-text:Текущая партия;tap-text:Начать автолистание;wait:6000;close', self.record)
        self.assertEqual(self.saved_node(), 0)
        self.assertIn('MESSAGE Era-SGF', trace)
        self.assertIn('TIMERS_ACTIVE 0', trace)

    def test_static_view_has_no_periodic_refresh_or_active_timer(self):
        trace = self.run_ui('previous;wait:60000;previous;wait:60000;close', self.record)
        self.assertEqual(trace.splitlines().count('FRAME'), 2)  # splash, then one static board
        self.assertIn('TIMERS_ACTIVE 0', trace)

    def test_autoplay_disables_system_idle_actions_only_while_running(self):
        self.record.write_text('(;SZ[19];B[aa];W[bb];B[cc])')
        trace = self.run_ui('policy;menu;tap-text:Текущая партия;tap-text:Начать автолистание;wait:6000;policy;menu;policy;close', self.record)
        policies = [line for line in trace.splitlines() if line.startswith('POWER_POLICY ')]
        self.assertEqual(policies, ['POWER_POLICY 1 1 TIMERS 0', 'POWER_POLICY 0 0 TIMERS 1', 'POWER_POLICY 1 1 TIMERS 0'])
        self.assertIn('POWER_POLICY_EXIT 1 1', trace)
        self.assertEqual(self.saved_node(), 2)

    def test_autoplay_restores_idle_policy_at_branch_end_error_and_exit(self):
        for record in ('(;SZ[19];B[aa](;W[bb])(;W[cc]))', '(;SZ[19];B[aa])', '(;SZ[19];B[zz])'):
            with self.subTest(record=record):
                self.record.write_text(record)
                trace = self.run_ui('menu;tap-text:Текущая партия;tap-text:Начать автолистание;wait:3000;policy;close', self.record)
                self.assertIn('POWER_POLICY 1 1 TIMERS 0', trace)
                self.assertIn('POWER_POLICY_EXIT 1 1', trace)
        self.record.write_text('(;SZ[19];B[aa];W[bb])')
        trace = self.run_ui('menu;tap-text:Текущая партия;tap-text:Начать автолистание;policy;close', self.record)
        self.assertIn('POWER_POLICY 0 0 TIMERS 1', trace)
        self.assertIn('POWER_POLICY_EXIT 1 1', trace)

    def test_autoplay_resumes_after_hide_or_background_without_skipping_moves(self):
        self.record.write_text('(;SZ[19];B[aa];W[bb];B[cc];W[dd])')
        for leave in ('hide', 'background', 'hide;background'):
            for wake in ('show', 'foreground', 'foreground;show', 'show;foreground'):
                with self.subTest(leave=leave, wake=wake):
                    trace = self.run_ui('menu;tap-text:Текущая партия;tap-text:Навигация;tap-text:В начало;menu;tap-text:Текущая партия;tap-text:Начать автолистание;wait:3000;'
                                        + leave + ';policy;wait:60000;drop-timers;' + wake + ';policy;wait:2999;hide;close', self.record)
                    self.assertIn('POWER_POLICY 1 1 TIMERS 0', trace)
                    self.assertIn('POWER_POLICY 0 0 TIMERS 1', trace)
                    self.assertEqual(self.saved_node(), 1, 'sleep must not advance or catch up moves')
                    trace = self.run_ui('menu;tap-text:Текущая партия;tap-text:Начать автолистание;' + leave + ';wait:60000;' + wake + ';wait:3000;close', self.record)
                    self.assertEqual(self.saved_node(), 2, 'resume must advance once without a button press')

    def test_show_recovers_lost_autoplay_timer_and_stop_needs_one_tap(self):
        self.record.write_text('(;SZ[19];B[aa];W[bb];B[cc];W[dd])')
        trace = self.run_ui('menu;tap-text:Текущая партия;tap-text:Начать автолистание;wait:3000;drop-timers;show;foreground;wait:3000;'
                            'tap:875,1600;wait:30000;policy;close', self.record)
        self.assertEqual(self.saved_node(), 2)
        self.assertIn('POWER_POLICY 1 1 TIMERS 0', trace)

    def test_autoplay_does_not_advance_if_locked_before_hide(self):
        self.record.write_text('(;SZ[19];B[aa];W[bb];B[cc])')
        trace = self.run_ui('menu;tap-text:Текущая партия;tap-text:Начать автолистание;lock;wait:30000;policy;show;wait:3000;close', self.record)
        self.assertEqual(self.saved_node(), 1)
        self.assertIn('POWER_POLICY 1 1 TIMERS 0', trace)

    def test_stopped_autoplay_stays_stopped_after_wake(self):
        self.record.write_text('(;SZ[19];B[aa];W[bb];B[cc])')
        trace = self.run_ui('menu;tap-text:Текущая партия;tap-text:Начать автолистание;tap:875,1600;hide;background;show;foreground;wait:30000;policy;close', self.record)
        self.assertEqual(self.saved_node(), 0)
        self.assertIn('POWER_POLICY 1 1 TIMERS 0', trace)

    def test_hoshi_are_visible_beyond_grid_lines_in_native_framebuffer(self):
        for size in (9, 13, 19):
            with self.subTest(size=size):
                self.record.write_text(f'(;SZ[{size}])')
                self.run_ui('close', self.record)
                vector = subprocess.run([PREVIEW, str(self.record)], capture_output=True, text=True, check=True)
                tree = ET.fromstring(vector.stdout)
                points = [c for c in tree.findall('{http://www.w3.org/2000/svg}circle') if c.attrib.get('fill') == 'rgb(0,0,0)']
                self.assertEqual(len(points), 9)
                _, dims, _, data = (self.root / 'frame.pgm').read_bytes().split(b'\n', 3)
                width, _ = map(int, dims.split())
                for point in points:
                    x, y, r = (int(point.attrib[k]) for k in ('cx', 'cy', 'r'))
                    self.assertGreaterEqual(r, 6, 'star points must stand out from the 3px Era grid')
                    self.assertEqual(data[(y + 3) * width + x + 3], 0)

    def test_autoplay_resume_keeps_selected_pause_through_repeated_sleep(self):
        self.record.write_text('(;SZ[19];B[aa];W[bb];B[cc])')
        trace = self.run_ui('menu;tap-text:Настройки;tap-text:Пауза автолистания;tap-text:60 с;menu;tap-text:Текущая партия;tap-text:Начать автолистание;'
                            'hide;wait:120000;show;wait:59999;hide;wait:120000;foreground;wait:60000;close', self.record)
        self.assertEqual(self.saved_node(), 1)
        self.assertIn('POWER_POLICY_EXIT 1 1', trace)

    def test_screenshots_save_real_board_pixels_unique_names_and_keep_the_position(self):
        self.record.write_text('(;SZ[9]CA[UTF-8]PB[Black]PW[White]AB[bb]AW[cc]C[Position note])')
        trace = self.run_ui('menu;tap-text:Снимок позиции;menu;tap-text:Текущая партия;tap-text:Комментарий на весь экран;menu;tap-text:Снимок позиции;close', self.record)
        images = sorted((self.root / 'screenshots/Era-SGF').glob('*.png'))
        self.assertEqual(len(images), 2)
        self.assertNotEqual(images[0].name, images[1].name)
        self.assertEqual(self.saved_node(), 0)
        vector = subprocess.run([PREVIEW, str(self.record)], capture_output=True, text=True, check=True)
        white = next(c for c in ET.fromstring(vector.stdout).findall('{http://www.w3.org/2000/svg}circle')
                     if c.attrib.get('fill') == 'rgb(255,255,255)' and int(c.attrib['r']) > 10)
        x, y, radius = (int(white.attrib[k]) for k in ['cx', 'cy', 'r'])
        for path in images:
            data = path.read_bytes()
            self.assertEqual(data[:8], b'\x89PNG\r\n\x1a\n')
            chunks, at = {}, 8
            while at < len(data):
                length = struct.unpack_from('>I', data, at)[0]
                kind = data[at + 4:at + 8]; payload = data[at + 8:at + 8 + length]
                self.assertEqual(zlib.crc32(kind + payload), struct.unpack_from('>I', data, at + 8 + length)[0])
                chunks[kind] = chunks.get(kind, b'') + payload
                at += length + 12
            self.assertIn(b'IEND', chunks)
            width, height, depth, colour, compression, filtering, interlace = struct.unpack('>IIBBBBB', chunks[b'IHDR'])
            self.assertEqual((width, height, depth, colour, compression, filtering, interlace), (1264, 1680, 8, 0, 0, 0, 0))
            pixels = zlib.decompress(chunks[b'IDAT'])
            self.assertEqual(len(pixels), height * (width + 1))
            self.assertTrue(all(pixels[row * (width + 1)] == 0 for row in range(height)))
            self.assertEqual(pixels[y * (width + 1) + 1 + x], 255)
            self.assertEqual(pixels[y * (width + 1) + 1 + x - radius], 0)
            self.assertLess(len(data), width * height // 4)
        self.assertIn('Снимок сохранён', trace)

    def test_launch_shows_version_then_dismisses_once_without_idle_timers(self):
        trace = self.run_ui('wait:1500;wait:60000;close', self.record, wait_startup=False)
        self.assertIn('Версия 1.1.1', trace)
        self.assertEqual(trace.splitlines().count('FRAME'), 2)
        self.assertIn('TIMERS_ACTIVE 0', trace)
        trace = self.run_ui('hide;wait:60000;close', self.record, wait_startup=False)
        self.assertEqual(trace.splitlines().count('FRAME'), 1)
        self.assertIn('TIMERS_ACTIVE 0', trace)

    def test_all_seven_logo_choices_draw_distinct_marks_and_the_choice_survives_restart(self):
        images = set()
        for choice in range(7):
            trace = self.run_ui(f'menu;tap-text:Настройки;tap-text:Логотип при запуске;tap-text:{choice + 1}. ;close', self.record)
            images.add((self.root / 'frame.pgm').read_bytes())
            self.assertIn('TIMERS_ACTIVE 0', trace)
        self.assertEqual(len(images), 7)
        last = (self.root / 'frame.pgm').read_bytes()
        self.run_ui('close', self.record, wait_startup=False)
        self.assertEqual((self.root / 'frame.pgm').read_bytes(), last)
        self.assertIn('logo=7', (self.root / 'system/config/era-sgf/preferences').read_text())

    def test_stale_viewer_cannot_move_or_delete_a_replacement_after_catalogue_refresh(self):
        replacement = '(;SZ[9]PB[Replacement];B[hh])'
        for action in [122, 123]:
            with self.subTest(action=action):
                self.record.write_text('(;SZ[9]PB[Original];B[aa])')
                with patch.dict(os.environ, ERA_SGF_TEST_REPLACE_FILE=str(self.record), ERA_SGF_TEST_REPLACE_BYTES=replacement):
                    action_label = 'Перенести в viewed' if action == 122 else 'Удалить файл с книги'
                    trace = self.run_ui(f'replace;menu;tap-text:Библиотека партий;menu;tap-text:Обновить список;back;menu;tap-text:Текущая партия;tap-text:Файл и отметка;tap-text:{action_label};confirm:1;close', self.record)
                self.assertTrue(self.record.exists())
                self.assertEqual(self.record.read_text(), replacement)
                self.assertIn('Файл изменён', trace)

    def test_fat_without_chmod_still_saves_position_and_exits(self):
        trace = self.run_ui('hide;show;next;fat-chmod-failure;menu;tap-text:Выход', self.record)
        self.assertIn('APP_CLOSED', trace)
        self.assertEqual(self.saved_node(), 1)
        self.assertNotIn('Cannot preserve file permissions', trace)

    def test_exit_closes_once_after_a_real_save_error_and_keeps_the_previous_state(self):
        trace = self.run_ui('hide;show;next;state-write-failure;menu;tap-text:Выход', self.record)
        self.assertEqual(trace.count('APP_CLOSED'), 1)
        self.assertEqual(self.saved_node(), 0)
        self.assertEqual(trace.count('MESSAGE Era-SGF'), 1)
        log = self.root / 'system/config/era-sgf/diagnostics.log'
        self.assertIn('Write failed', log.read_text())

    def test_back_exits_after_a_posix_mode_error_instead_of_silently_ignoring_it(self):
        trace = self.run_ui('hide;show;next;posix-chmod-failure;back', self.record)
        self.assertEqual(trace.count('APP_CLOSED'), 1)
        self.assertEqual(self.saved_node(), 0)
        self.assertIn('Cannot preserve file permissions', trace)

    def test_era_grid_and_white_stone_outline_have_multiple_black_pixels(self):
        self.record.write_text('(;SZ[19]AB[bb]AW[cc])')
        self.run_ui('close', self.record)
        vector = subprocess.run([PREVIEW, str(self.record)], capture_output=True, text=True, check=True)
        tree = ET.fromstring(vector.stdout)
        white = next(c for c in tree.findall('{http://www.w3.org/2000/svg}circle')
                     if c.attrib.get('fill') == 'rgb(255,255,255)' and int(c.attrib['r']) > 10)
        x, y, radius = (int(white.attrib[k]) for k in ['cx', 'cy', 'r'])
        _, size, _, data = (self.root / 'frame.pgm').read_bytes().split(b'\n', 3)
        width, _ = map(int, size.split())
        self.assertEqual(data[y * width + x + radius - 2], 0, 'white outline must stay visible inside its edge')
        self.assertEqual(data[y * width + x], 255)
        horizontal = next(line for line in tree.findall('{http://www.w3.org/2000/svg}line')
                          if line.attrib['y1'] == line.attrib['y2'] and int(line.attrib['y1']) > 150 and int(line.attrib['x2']) - int(line.attrib['x1']) > 500)
        gx, gy = int(horizontal.attrib['x1']) + 17, int(horizontal.attrib['y1'])
        self.assertEqual(data[(gy - 1) * width + gx], 0)
        self.assertEqual(data[(gy + 1) * width + gx], 0)

    def navigation_fixture(self):
        moves = []
        for i in range(25):
            moves.append(';' + ('B' if i % 2 == 0 else 'W') + '[' + chr(97 + i % 19) + chr(97 + i // 19) + ']')
            if i in [4, 9]:
                moves.append(';C[Comment-only node]')
        self.record.write_text('(;SZ[19]' + ''.join(moves) + ')')

    def tap_label(self, label):
        vector = subprocess.run([PREVIEW, str(self.record)], capture_output=True, text=True, check=True)
        texts = ET.fromstring(vector.stdout).findall('{http://www.w3.org/2000/svg}text')
        if label == 'number':
            item = next(t for t in texts if (t.text or '').startswith('Ход '))
        elif label in ('|<', '<10', '10>', '>|'):
            number = next(t for t in texts if (t.text or '').startswith('Ход '))
            baseline = int(number.attrib['y'])
            boxes = [r for r in ET.fromstring(vector.stdout).findall('{http://www.w3.org/2000/svg}rect')
                     if 'x' in r.attrib and int(r.attrib['y']) < baseline < int(r.attrib['y']) + int(r.attrib['height'])]
            boxes.sort(key=lambda r: int(r.attrib['x']))
            r = boxes[('|<', '<10', '10>', '>|').index(label)]
            return f"tap:{int(r.attrib['x']) + int(r.attrib['width'])//2},{int(r.attrib['y']) + int(r.attrib['height'])//2}"
        else:
            item = next(t for t in texts if t.text == label)
        return f"tap:{item.attrib['x']},{item.attrib['y']}"

    def saved_move(self):
        result = subprocess.run([INSPECT, str(self.record), '--state-dir', str(self.root / 'system/config/era-sgf')], capture_output=True, text=True, check=True)
        return json.loads(result.stdout)['move']

    def test_main_menu_is_compact_and_settings_are_in_their_submenu(self):
        trace = self.run_ui('menu;close', self.record)
        entries = [line for line in self.last_frame(trace).splitlines() if line and not line.startswith('Era-SGF')]
        self.assertLessEqual(len(entries), 6)
        self.assertNotIn('Кодировка', '\n'.join(entries))
        self.assertIn('Настройки', '\n'.join(entries))
        trace = self.run_ui('menu;tap-text:Настройки;close', self.record)
        settings = self.last_frame(trace)
        self.assertIn('Кодировка', settings)
        self.assertIn('Логотип', settings)

    def test_random_opens_only_unviewed_game_without_marking_it_viewed(self):
        (self.root / 'sgf').mkdir()
        new = self.root / 'sgf/new.sgf'
        new.write_text('(;SZ[19]PB[Random target]PW[Opponent];B[aa])')
        trace = self.run_ui('menu;tap-text:Текущая партия;tap-text:Файл и отметка;tap-text:Отметить просмотренной;menu;tap-text:Случайная новая партия;close', self.record)
        self.assertIn('B: Random target - W: Opponent', trace)
        trace = self.run_ui('menu;tap-text:Библиотека партий;tap-text:Новые;close')
        self.assertIn('Random target', self.last_frame(trace))
        self.assertIn('Найдено: 1 из 2', self.last_frame(trace))

    def test_random_distinguishes_empty_all_viewed_and_unreadable(self):
        trace = self.run_ui('menu;tap-text:Случайная новая партия;close')
        self.assertIn('В библиотеке пока нет партий', trace)
        trace = self.run_ui('next;next;menu;tap-text:Случайная новая партия;close', self.record)
        self.assertIn('Все партии уже просмотрены', trace)
        self.assertEqual(self.saved_node(), 2)
        (self.root / 'sgf').mkdir()
        (self.root / 'sgf/broken.sgf').write_text('(;SZ[19]AB[zz])')
        trace = self.run_ui('menu;tap-text:Случайная новая партия;close', self.record)
        self.assertIn('не удалось прочитать', trace)
        self.assertNotIn('Все партии уже просмотрены', trace)

    @staticmethod
    def last_frame(trace):
        return trace.split('FRAME\n')[-2].split('END_')[0]

    def test_library_is_fullscreen_with_direct_filters_and_random_ignores_them(self):
        (self.root / 'sgf').mkdir()
        (self.root / 'sgf/new.sgf').write_text('(;SZ[19]PB[Only new]PW[Other]DT[2000];B[aa])')
        trace = self.run_ui('menu;tap-text:Текущая партия;tap-text:Файл и отметка;tap-text:Отметить просмотренной;menu;tap-text:Библиотека партий;tap-text:Просмотренные;'
                            'tap-text:Поиск;text:нет такой партии;tap-text:Случайная новая;close', self.record)
        self.assertIn('B: Only new - W: Other', self.last_frame(trace))
        self.assertIn('По этим условиям партий нет', trace)

    def test_library_paging_back_close_rotation_and_wake_preserve_context(self):
        (self.root / 'sgf').mkdir()
        for i in range(20):
            (self.root / f'sgf/{i:02}.sgf').write_text(f'(;SZ[19]PB[Player {i:02}]PW[Opponent]DT[2000])')
        trace = self.run_ui('menu;tap-text:Библиотека партий;next;tap-text:Год;tap:70,60;hide;show;close')
        frame = self.last_frame(trace)
        self.assertIn('2 / 3', frame)
        self.assertIn('Player 08', frame)
        self.assertNotIn('Player 00', frame)
        trace = self.run_ui('menu;tap-text:Библиотека партий;rotate;next;close')
        self.assertIn('Библиотека', self.last_frame(trace))
        trace = self.run_ui('menu;tap-text:Библиотека партий;tap:70,60;close')
        self.assertIn('Случайная новая партия', self.last_frame(trace))
        trace = self.run_ui('menu;tap-text:Библиотека партий;tap:1200,60;close', self.record)
        self.assertNotIn('Библиотека', self.last_frame(trace))
        self.assertIn('Ход', self.last_frame(trace))
        self.assertIn('TIMERS_ACTIVE 0', trace)

    def test_library_tools_reset_filters_and_settings_stay_at_the_parent(self):
        trace = self.run_ui('menu;tap-text:Библиотека партий;tap-text:Поиск;text:missing;menu;tap-text:Сбросить поиск;close', self.record)
        self.assertIn('Найдено: 1 из 1', self.last_frame(trace))
        for submenu, choice in (('Пауза автолистания','2 с'),('Обновление экрана','Через 20'),('Кодировка','Авто (CA')):
            trace = self.run_ui(f'menu;tap-text:Настройки;tap-text:{submenu};tap-text:{choice};close', self.record)
            self.assertIn('Настройки', self.last_frame(trace))
            self.assertNotIn('←', self.last_frame(trace))  # geometric header icon, never a list item

    def test_navigation_row_jumps_ten_moves_seeks_a_number_and_reaches_both_ends(self):
        self.navigation_fixture()
        self.run_ui(self.tap_label('10>') + ';close', self.record)
        self.assertEqual(self.saved_move(), 10)
        self.run_ui(self.tap_label('number') + ';text:17;close', self.record)
        self.assertEqual(self.saved_move(), 17)
        self.run_ui(self.tap_label('>|') + ';' + self.tap_label('<10') + ';close', self.record)
        self.assertEqual(self.saved_move(), 15)
        self.run_ui(self.tap_label('|<') + ';close', self.record)
        self.assertEqual(self.saved_move(), 0)

    def test_numeric_jump_validates_input_and_cancels_autoplay(self):
        self.navigation_fixture()
        trace = self.run_ui(self.tap_label('Авто') + ';' + self.tap_label('number') + ';text:17;wait:12000;close', self.record)
        self.assertEqual(self.saved_move(), 17)
        self.assertIn('TIMERS_ACTIVE 0', trace)
        for invalid in ['-1', '26', 'abc', '']:
            with self.subTest(invalid=invalid):
                trace = self.run_ui(self.tap_label('number') + ';text:' + invalid + ';close', self.record)
                self.assertEqual(self.saved_move(), 17)
                self.assertIn('MESSAGE Era-SGF', trace)
        trace = self.run_ui(self.tap_label('number') + ';cancel-text;close', self.record)
        self.assertEqual(self.saved_move(), 17)
        self.assertNotIn('MESSAGE Era-SGF', trace)

    def test_hardware_keys_and_variation_menu_save_selected_sibling(self):
        trace = self.run_ui('next;menu;tap-text:Текущая партия;tap-text:Ветки;tap-text:2. W[cc];close', self.record)
        self.assertIn('Второй вариант', trace)
        result = subprocess.run([INSPECT, str(self.record), '--state-dir', str(self.root / 'system/config/era-sgf')], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)['node'], 3)

    def test_bad_ca_can_be_fixed_in_the_device_encoding_menu(self):
        self.record.write_bytes('(;SZ[9]CA[UTF-8]C[Привет, ёж!])'.encode('cp1251'))
        before = self.record.read_bytes()
        trace = self.run_ui('menu;tap-text:Настройки;tap-text:Кодировка;tap-text:Windows-1251;tap:1200,60;close', self.record)
        self.assertIn('Привет, ёж!', trace)
        self.assertEqual(self.record.read_bytes(), before)

    def test_node_name_only_encoding_error_keeps_a_touch_menu_and_can_be_fixed(self):
        self.record.write_bytes('(;SZ[9]CA[UTF-8]N[Привет, ёж!])'.encode('cp1251'))
        trace = self.run_ui('tap:1100,1600;tap-text:Настройки;tap-text:Кодировка;tap-text:Windows-1251;tap:1200,60;close', self.record)
        self.assertIn('FRAME\n', trace)
        self.assertIn('Привет, ёж!', trace)

    def test_encoding_error_beyond_detection_sample_keeps_touch_recovery(self):
        prefix = '(;SZ[9]CA[UTF-8]C[' + 'x' * 66000 + '];N['
        self.record.write_bytes(prefix.encode() + 'Привет, ёж!'.encode('cp1251') + b'])')
        self.run_ui('next;close', self.record)
        # Resume on the bad node: no previous frame is available to mask the failure.
        trace = self.run_ui('tap:1100,1600;tap-text:Настройки;tap-text:Кодировка;tap-text:Windows-1251;tap:1200,60;close', self.record)
        self.assertIn('Привет, ёж!', trace)

    def test_automatic_detection_includes_node_names(self):
        self.record.write_bytes('(;SZ[9]N[Привет, ёж! Разбор партии и варианты продолжения.])'.encode('cp1251'))
        trace = self.run_ui('close', self.record)
        self.assertIn('Привет, ёж!', trace)

    def test_reopening_current_record_preserves_fresh_position(self):
        trace = self.run_ui('next;next;menu;tap-text:Текущая партия;tap-text:Файл и отметка;tap-text:Открыть SGF;tap-text:партия с ветками.sgf;close', self.record)
        result = subprocess.run([INSPECT, str(self.record), '--state-dir', str(self.root / 'system/config/era-sgf')], capture_output=True, text=True)
        self.assertEqual(json.loads(result.stdout)['node'], 2)

    def test_open_from_applications_without_a_launch_argument(self):
        # applications/ and system/ are the first two directory rows.
        trace = self.run_ui('menu;tap-text:Открыть SGF;tap-text:партия с ветками.sgf;close')
        self.assertIn('Корень', trace)

    def test_explicit_install_mode_registers_sgf_and_preserves_other_entries(self):
        config = self.root / 'system/config/extensions.cfg'
        original = 'fb2:@FB2_file:1:fbreader.app:ICON_FB2\n'
        config.write_text(original)
        self.run_ui('close', '--install')
        self.assertTrue(config.read_text().startswith(original))
        self.assertIn('era-sgf.app', config.read_text())

    def test_result_appears_only_at_end_of_main_line_and_disappears_on_back(self):
        self.record.write_text('(;SZ[9]RE[W+2.5];B[aa](;W[bb])(;W[cc]))')
        trace = self.run_ui('close', self.record)
        self.assertNotIn('Результат:', trace)
        trace = self.run_ui('menu;tap-text:Текущая партия;tap-text:Навигация;tap-text:В конец;close', self.record)
        self.assertIn('Результат: Победа белых: +2.5', trace)
        trace = self.run_ui('previous;close', self.record)
        self.assertNotIn('Результат:', trace.split('\nFRAME\n')[-1].split('\nEND_FRAME')[0])
        trace = self.run_ui('menu;tap-text:Текущая партия;tap-text:Ветки;tap-text:2. W[cc];close', self.record)
        self.assertNotIn('Результат:', trace)
        self.assertEqual(self.saved_move(), 2)

    def test_info_button_opens_game_information_and_back_preserves_move(self):
        self.record.write_text('(;SZ[9]PB[Black]PW[White]EV[Test tournament];B[aa])')
        trace = self.run_ui('next;tap:40,40;back;close', self.record)
        self.assertIn('EV: Test tournament', trace)
        self.assertEqual(self.saved_move(), 1)

    def test_about_screen_has_version_author_license_and_returns_to_settings(self):
        trace = self.run_ui('menu;tap-text:Настройки;tap-text:О программе;back;close', self.record)
        self.assertIn('Лицензия исходников: MIT', trace)
        self.assertIn('VYBel', trace)
        self.assertIn('github.com/VYBel/Era-SGF', trace)
        self.assertIn('Настройки', self.last_frame(trace))

    def test_tapping_comment_opens_full_screen_without_changing_move(self):
        self.record.write_text('(;SZ[9]C[Tap this comment];B[aa])')
        trace = self.run_ui(self.tap_label('Tap this comment') + ';back;close', self.record)
        self.assertIn('Доска', trace)
        self.assertEqual(self.saved_move(), 0)

    def test_orientation_and_full_comment_do_not_change_selected_node(self):
        trace = self.run_ui('next;rotate;menu;tap-text:Текущая партия;tap-text:Комментарий на весь экран;next;back;close', self.record)
        self.assertIn('Ход 1', trace)
        result = subprocess.run([INSPECT, str(self.record), '--state-dir', str(self.root / 'system/config/era-sgf')], capture_output=True, text=True)
        self.assertEqual(json.loads(result.stdout)['node'], 1)


if __name__ == '__main__':
    unittest.main(verbosity=2)
