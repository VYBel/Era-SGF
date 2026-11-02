#!/usr/bin/env python3
"""Rasterize the original Book SVG into two high-contrast PocketBook BMP icons.

Pillow is only a host dependency for regeneration. Installing and testing an
existing release uses Python's standard library; the reader needs neither.
"""
import argparse
from pathlib import Path
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]


def render_book(source):
    from PIL import Image, ImageDraw
    tree = ET.parse(source).getroot()
    if tree.attrib.get('viewBox') != '0 0 1000 1000':
        raise ValueError('Expected the original 1000x1000 code-native Book SVG')
    image = Image.new('L', (1000, 1000), 255)
    pen = ImageDraw.Draw(image)
    for element in tree:
        kind, attrs = element.tag.rsplit('}', 1)[-1], element.attrib
        if kind == 'line':
            if attrs.get('stroke') != 'rgb(0,0,0)':
                raise ValueError('Expected a black Book outline')
            pen.line(tuple(round(float(attrs[key])) for key in ('x1', 'y1', 'x2', 'y2')), fill=0)
        elif kind == 'circle':
            x, y, radius = (float(attrs[key]) for key in ('cx', 'cy', 'r'))
            color = {'rgb(255,255,255)': 255, 'rgb(0,0,0)': 0}[attrs['fill']]
            pen.ellipse((round(x-radius), round(y-radius), round(x+radius), round(y+radius)), fill=color)
        elif kind == 'rect' and attrs.get('fill') == 'white':
            pass  # The SVG background is already represented by the white canvas.
        elif kind != 'title':
            raise ValueError('Unsupported SVG primitive: ' + kind)
    return image


def generate(source, output, width, height):
    from PIL import Image, ImageChops, ImageDraw, ImageOps
    book = render_book(source)
    bounds = ImageChops.invert(book).getbbox()
    if bounds is None:
        raise ValueError('Book SVG is empty')
    book = book.crop(bounds)
    output.mkdir(parents=True, exist_ok=True)
    for focused, name in ((False, 'era-sgf.bmp'), (True, 'era-sgf-focused.bmp')):
        canvas = Image.new('L', (width, height), 255)
        mark = book.copy()
        padding = round(width * (0.14 if focused else 0.08))
        mark.thumbnail((width - 2 * padding, height - 2 * padding), Image.Resampling.LANCZOS)
        # Quantize without dithering: E Ink launcher lines stay black or white.
        mark = mark.point(lambda value: 0 if value < 160 else 255)
        if focused:
            pen = ImageDraw.Draw(canvas)
            inset = round(width * 0.03)
            pen.rounded_rectangle((inset, inset, width-inset-1, height-inset-1),
                                  radius=round(width * 0.15), fill=0)
            mark = ImageOps.invert(mark)
        canvas.paste(mark, ((width - mark.width) // 2, (height - mark.height) // 2))
        target = output / name
        # L, rather than 1, deliberately produces an uncompressed 8-bit BMP.
        canvas.save(target, format='BMP')
        print(target)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT / 'assets/logos/logo-05.svg')
    parser.add_argument('--output', type=Path, default=ROOT / 'assets/icons')
    args = parser.parse_args()
    generate(args.source, args.output, 106, 112)


if __name__ == '__main__':
    main()
