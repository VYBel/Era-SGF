#!/usr/bin/env python3
"""Render original code-native logo choices for the owner's review.

The SVGs come from the same C++ drawing operations as the reader.
Pillow is an optional desktop dependency for the PNG contact sheet only.
"""
import argparse
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
NAMES = ['Два камня', 'Лист', 'Равновесие', 'Печать го', 'Книга', 'Крыло', 'Путь хода']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=ROOT / 'build/era-sgf-logos')
    args = parser.parse_args()
    from PIL import Image, ImageDraw, ImageFont
    target = ROOT / 'assets/logos'
    target.mkdir(parents=True, exist_ok=True)
    previews = ROOT / 'docs/logos'
    previews.mkdir(parents=True, exist_ok=True)
    sheet = Image.new('L', (1400, 900), 255)
    draw = ImageDraw.Draw(sheet)
    font_path = '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
    title = ImageFont.truetype(font_path, 35)
    label = ImageFont.truetype(font_path, 24)
    note = ImageFont.truetype(font_path, 21)
    draw.text((700, 36), 'Семь знаков для Era-SGF', fill=0, font=title, anchor='mt')
    draw.text((700, 90), 'Основной знак — № 5 «Книга»', fill=0, font=note, anchor='mt')
    for index, name in enumerate(NAMES, 1):
        vector = subprocess.check_output([str(args.binary), str(index)], text=True)
        (target / f'logo-{index:02d}.svg').write_text(vector)
        mark = Image.new('L', (1000, 1000), 255)
        pen = ImageDraw.Draw(mark)
        tree = ET.fromstring(vector)
        for element in tree:
            kind = element.tag.rsplit('}', 1)[-1]
            attrs = element.attrib
            if kind == 'line':
                pen.line(tuple(round(float(attrs[a])) for a in ['x1', 'y1', 'x2', 'y2']), fill=0, width=1)
            elif kind == 'circle':
                x, y, radius = (float(attrs[a]) for a in ['cx', 'cy', 'r'])
                fill = 255 if attrs.get('fill') == 'rgb(255,255,255)' else 0
                pen.ellipse((round(x-radius), round(y-radius), round(x+radius), round(y+radius)), fill=fill)
        row, column = divmod(index - 1, 4)
        x = 35 + column * 340 + (170 if row else 0)
        y = 150 + row * 345
        sheet.paste(mark.resize((280, 280), Image.Resampling.LANCZOS), (x + 15, y))
        draw.text((x + 155, y + 288), f'{index}. {name}', fill=0, font=label, anchor='mt')
    draw.text((700, 858), 'На книге: Меню → Логотип при запуске → 1–7', fill=0, font=note, anchor='mt')
    sheet.save(previews / 'contact-sheet.png')
    print(previews / 'contact-sheet.png')


if __name__ == '__main__':
    main()
