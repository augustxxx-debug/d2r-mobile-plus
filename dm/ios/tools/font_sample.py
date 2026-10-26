#!/usr/bin/env python3
"""在 Mac 上出可选字体的样张：各加粗档并排，再模拟「渲染分辨率」上限（按输出尺寸栅格化后整帧双线性放大）看文字变软的程度。
只读字体文件与游戏原字体，产物写到 --out（缺省 DM_ROOT/run/font-weight），不进仓库；需要 PIL 与 fontTools。
Render sample sheets of the optional font on a Mac: the weight levels side by side, then a simulation of the render-resolution cap
(text rasterised at the output size and bilinearly upscaled as a whole frame) to see how soft it gets. Reads only font files and the
original game font; writes to --out (default DM_ROOT/run/font-weight), never into the repository; needs PIL and fontTools.

用法 | Usage:
  python3 font_sample.py [--font F] [--original blizzardglobal.ttf|<RomFS>] [--weights 0 1 2 3 4 6] [--sizes 20 28] [--scales 1 1.43 1.66] [--out DIR]
"""
import argparse
import os
from pathlib import Path
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import font_override

LINES = ['图形设置　渲染分辨率　自动／省电／均衡／原生　任务日志',
         '寻找凯恩的书页：前往石荒野，找到被诅咒的村庄。',
         'Graphics  Render resolution  Auto / Eco / Balanced / Native',
         'Quest log: find the Book of Cain in the Stony Field. 0123456789']
INK, PAPER = (222, 196, 140), (18, 14, 12)   # 游戏常见的金字黑底 | the game's usual gold text on a dark panel


def compose_all(original, requested, weights, out):
    paths = {}
    for weight in weights:
        target = out / ('theme-w%d.ttf' % weight)
        if not target.is_file():
            font_override.compose(original, requested, target, weight)
        paths[weight] = target
    return paths


def render_line(font, text, scale):
    """按输出尺寸栅格化，再整帧双线性放大 scale 倍（scale 1 = 原生）。 | Rasterise at the output size, then upscale bilinearly by scale (1 = native)."""
    from PIL import Image, ImageDraw
    box = font.getbbox(text)
    w, h = box[2] + 8, font.size * 2
    im = Image.new('RGB', (w, h), PAPER)
    ImageDraw.Draw(im).text((4, font.size // 2 - box[1] // 2), text, font=font, fill=INK)
    if scale != 1:
        im = im.resize((round(w * scale), round(h * scale)), Image.BILINEAR)
    return im


def coverage(font, text):
    """笔画黑度：文字像素的平均覆盖率与满覆盖像素占比，用于档位间的量化对比。 | Ink measure: mean alpha and fully covered share of the text pixels, for comparing levels."""
    from PIL import Image, ImageDraw
    box = font.getbbox(text)
    im = Image.new('L', (box[2] + 8, font.size * 2), 0)
    ImageDraw.Draw(im).text((4, font.size // 2 - box[1] // 2), text, font=font, fill=255)
    pixels = [p for p in im.tobytes() if p]
    if not pixels:
        return 0.0, 0.0
    return sum(pixels) / 255.0 / len(pixels), sum(1 for p in pixels if p >= 250) / len(pixels)


def sheet(rows, title, out):
    """rows: [(label, [images])]，每行一个标签加若干张图竖排。 | rows: [(label, [images])], one label and its images stacked per row."""
    from PIL import Image, ImageDraw, ImageFont
    label_font = ImageFont.load_default()
    pad, label_w = 12, 150
    width = max(max(im.width for im in images) for _, images in rows) + label_w + pad * 2
    height = pad + 20 + sum(sum(im.height for im in images) + pad for _, images in rows)
    page = Image.new('RGB', (width, height), PAPER)
    draw = ImageDraw.Draw(page)
    draw.text((pad, pad), title, font=label_font, fill=(160, 160, 160))
    y = pad + 20
    for label, images in rows:
        draw.text((pad, y + 4), label, font=label_font, fill=(160, 160, 160))
        for im in images:
            page.paste(im, (label_w, y))
            y += im.height
        y += pad
    page.save(out)
    return out


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--font', default=None, help='requested font; default DM_UI_FONT')
    parser.add_argument('--original', default=None, help='original game font file or a RomFS directory; default DM_ROMFS_SRC')
    parser.add_argument('--weights', type=int, nargs='+', default=[0, 1, 2, 3, 4, 6])
    parser.add_argument('--sizes', type=int, nargs='+', default=[20, 28])
    parser.add_argument('--scales', type=float, nargs='+', default=[1.0, 1.43, 1.66],
                        help='whole-frame upscale factors to simulate: 1.43 ≈ Duo inner screen Auto/Balanced, 1.66 ≈ Eco')
    parser.add_argument('--out', default=None)
    args = parser.parse_args(argv)
    from PIL import ImageFont
    requested = Path(args.font) if args.font else font_override.source_path()
    if requested is None:
        parser.error('no requested font: pass --font or set DM_UI_FONT')
    original = Path(args.original or os.environ.get('DM_ROMFS_SRC', ''))
    if original.is_dir():
        import romfs_art
        data = romfs_art.read(str(original), font_override.GAME_FONT)
    elif original.is_file():
        data = original.read_bytes()
    else:
        parser.error('no original game font: pass --original or set DM_ROMFS_SRC')
    out = Path(args.out or os.path.join(os.environ.get('DM_ROOT', '.'), 'run', 'font-weight'))
    out.mkdir(parents=True, exist_ok=True)
    weights = sorted(set(args.weights))
    if any(not 0 <= w <= font_override.WEIGHT_MAX for w in weights):
        parser.error('weights must be 0..%d' % font_override.WEIGHT_MAX)
    paths = compose_all(data, requested, weights, out)
    original_font = out / 'original.ttf'
    original_font.write_bytes(data)
    report = []
    for size in args.sizes:
        fonts = [('original %dpx' % size, ImageFont.truetype(str(original_font), size))]
        fonts += [('weight %d %dpx' % (w, size), ImageFont.truetype(str(paths[w]), size)) for w in weights]
        rows = [(label, [render_line(font, line, 1.0) for line in LINES]) for label, font in fonts]
        sheet(rows, 'weights at native output size %dpx' % size, out / ('weights-%dpx.png' % size))
        for label, font in fonts:
            mean, solid = coverage(font, LINES[0])
            report.append('%-22s mean ink %.3f  solid share %.3f' % (label, mean, solid))
        for scale in args.scales:
            if scale == 1:
                continue
            small = max(6, round(size / scale))
            rows = []
            for label, font in fonts:
                capped = ImageFont.truetype(font.path, small)
                rows.append((label + ' native', [render_line(font, LINES[0], 1.0), render_line(font, LINES[2], 1.0)]))
                rows.append((label + ' cap x%.2f' % scale, [render_line(capped, LINES[0], scale), render_line(capped, LINES[2], scale)]))
            sheet(rows, 'render-resolution cap: text at %dpx (%dpx rasterised, upscaled x%.2f)' % (size, small, scale),
                  out / ('rescap-%dpx-x%.2f.png' % (size, scale)))
    (out / 'report.txt').write_text('\n'.join(report) + '\n')
    print('\n'.join(report))
    print(out)


if __name__ == '__main__':
    main()
