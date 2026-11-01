#!/usr/bin/env python3
"""Generate only synthetic, redistributable SGF examples for codec tests."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
COMMENT = 'Привет, ёж! Чёрные окружили белую группу. Разбор партии и варианты продолжения.'
CODECS = {'Windows-1251': 'cp1251', 'KOI8-R': 'koi8_r', 'CP866': 'cp866', 'ISO-8859-5': 'iso8859_5', 'UTF-8': 'utf-8'}

for name, codec in CODECS.items():
    for declared in (True, False):
        ca = f'CA[{name}]' if declared else ''
        sgf = f'(;FF[4]GM[1]SZ[9]{ca}PB[Чёрные]PW[Белые]C[{COMMENT}];B[cc](;W[dd]C[Первый вариант])(;W[ee]C[Другой вариант]))\n'
        path = ROOT / 'tests' / 'fixtures' / f'{name.lower()}-{"declared" if declared else "auto"}.sgf'
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(sgf.encode(codec))
