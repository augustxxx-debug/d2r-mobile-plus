#!/usr/bin/env python3
"""加载页中央的字标图（自制）：「D2R」大字 + 「MOBILE PLUS」小字 + 饰线，金属金色渐变、浮雕、深色描边、暖色外发光、投影；RGBA PNG，透明底。
字体：Cinzel Decorative Black（SIL Open Font License 1.1，Natanael Gama；字体文件与许可证在 dm/assets/loadpage_fonts/（不进安装包））。OFL 允许把字形渲染成图任意使用。
不含任何原版游戏素材（不是暴雪的 logo，只是项目名的字标）。固定输出 1600×600，重跑同图。
用法：python3 dm/tools/gen_loadpage_logo.py [输出目录=dm/assets/loadpage]（要 Pillow + numpy，项目 venv 里有）
Wordmark image for the centre of the loading page (self-made): big "D2R" + small "MOBILE PLUS" + rules, metallic gold gradient, emboss, dark outline, warm outer glow, drop shadow; RGBA PNG.
Font: Cinzel Decorative Black (SIL Open Font License 1.1, Natanael Gama; font file and licence in dm/assets/loadpage_fonts/ (not shipped)). The OFL allows rendered glyphs to be used freely.
No original game assets (it is not Blizzard's logo, only the project name's wordmark). Fixed 1600×600 output, reproducible.
"""
import os, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter

W, H = 1600, 600

def text_mask(font, text, spacing=0):
    """返回文字的 L 遮罩（紧贴内容裁好，含 1 像素边） | the text's L mask, trimmed to content with 1 px padding"""
    big = Image.new('L', (W * 2, H), 0); d = ImageDraw.Draw(big)
    x = 0
    for ch in text:   # 手动排字距 | manual letter spacing
        d.text((x, 0), ch, font=font, fill=255); x += d.textlength(ch, font=font) + spacing
    box = big.getbbox(); return big.crop((box[0] - 1, box[1] - 1, box[2] + 1, box[3] + 1))

def metallic(mask, hue=(1.0, 0.78, 0.36)):
    """按遮罩做金属金色：竖向渐变 + 中段高光带 + 左上提亮 / 右下压暗的浮雕 | metallic gold by mask: vertical gradient + highlight band + top-left lighten / bottom-right darken emboss"""
    h, w = mask.size[1], mask.size[0]; m = np.asarray(mask, np.float32) / 255
    v = np.linspace(0, 1, h)[:, None] * np.ones((1, w), np.float32)
    base = np.where(v < 0.42, 0.95 - v * 0.5, 0.75 - (v - 0.42) * 0.95)             # 上亮下暗 | bright above, dark below
    band = np.exp(-((v - 0.47) / 0.045) ** 2) * 0.35                                  # 高光带 | highlight band
    lum = np.clip(base + band, 0.12, 1.0)
    # 浮雕：遮罩向左上 / 右下各偏移，差值当光照 | emboss: mask offset to the top-left / bottom-right, the difference acts as lighting
    up = np.zeros_like(m); up[2:, 2:] = m[:-2, :-2]; dn = np.zeros_like(m); dn[:-2, :-2] = m[2:, 2:]
    light = np.clip(m - up, 0, 1) * 0.45 - np.clip(m - dn, 0, 1) * 0.35
    lum = np.clip(lum + light, 0.05, 1.0)
    r = np.clip(lum * hue[0] * 1.05, 0, 1); g = np.clip(lum * hue[1] * (0.9 + 0.2 * v), 0, 1) * 0.95; b = np.clip(lum * hue[2] * (0.5 + 0.8 * v), 0, 1) * 0.55
    rgba = np.stack([r, g, b, m], -1)
    return Image.fromarray((rgba * 255 + 0.5).astype(np.uint8), 'RGBA')

def outlined(mask, grow):
    return mask.filter(ImageFilter.MaxFilter(grow * 2 + 1))

def compose(font_big, font_small):
    out = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    big = text_mask(font_big, 'D2R', spacing=14); small = text_mask(font_small, 'MOBILE PLUS', spacing=22)
    # 尺寸：大字宽约 56% | sizes: the big word about 56% wide
    bw = int(W * 0.56); big = big.resize((bw, int(big.size[1] * bw / big.size[0])), Image.LANCZOS)
    sw = int(W * 0.42); small = small.resize((sw, int(small.size[1] * sw / small.size[0])), Image.LANCZOS)
    layers = []
    y = 70
    for mask, grow in ((big, 7), (small, 4)):
        x = (W - mask.size[0]) // 2
        full = Image.new('L', (W, H), 0); full.paste(mask, (x, y))
        edge = outlined(full, grow)
        layers.append((full, edge)); y += mask.size[1] + 52
    # 饰线：小字两侧各一条渐隐的线 + 中点菱形 | rules: a fading line on each side of the small text + a diamond
    rule = Image.new('L', (W, H), 0); rd = ImageDraw.Draw(rule)
    sm_full, _ = layers[1]; sb = sm_full.getbbox(); cy = (sb[1] + sb[3]) // 2
    for sx0, sx1 in ((sb[0] - 70, sb[0] - 420), (sb[2] + 70, sb[2] + 420)):
        for i in range(0, 350, 2):
            t = i / 350; xx = int(sx0 + (sx1 - sx0) * t)
            rd.rectangle([xx, cy - 2, xx + 2, cy + 1], fill=int(255 * (1 - t) ** 1.4))
        dx = sx0 + (sx1 - sx0) * 0.03; rd.polygon([(dx, cy - 9), (dx + 9, cy), (dx, cy + 9), (dx - 9, cy)], fill=255)
    # 组装：投影 → 外发光 → 描边 → 金属字 | assemble: shadow → outer glow → outline → metallic text
    all_edge = Image.new('L', (W, H), 0)
    for full, edge in layers: all_edge = Image.fromarray(np.maximum(np.asarray(all_edge), np.asarray(edge)))
    all_edge = Image.fromarray(np.maximum(np.asarray(all_edge), np.asarray(outlined(rule, 2))))
    shadow = all_edge.filter(ImageFilter.GaussianBlur(10)); sh = Image.new('RGBA', (W, H), (0, 0, 0, 0)); sh.putalpha(shadow.point(lambda a: int(a * 0.85)))
    out = Image.alpha_composite(out, sh.transform(sh.size, Image.AFFINE, (1, 0, -4, 0, 1, -8)))
    glow = all_edge.filter(ImageFilter.GaussianBlur(22)); gl = Image.new('RGBA', (W, H), (255, 120, 30, 0)); gl.putalpha(glow.point(lambda a: int(a * 0.55)))
    out = Image.alpha_composite(out, gl)
    ol = Image.new('RGBA', (W, H), (38, 20, 6, 0)); ol.putalpha(all_edge); out = Image.alpha_composite(out, ol)
    for full, _ in layers: out = Image.alpha_composite(out, metallic(full))
    out = Image.alpha_composite(out, metallic(rule, hue=(0.95, 0.72, 0.32)))
    return out

def main():
    here = os.path.dirname(os.path.abspath(__file__))
    dest = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, '..', 'assets', 'loadpage')
    fdir = os.path.join(here, '..', 'assets', 'loadpage_fonts'); fpath = os.path.join(fdir, 'CinzelDecorative-Black.ttf')
    if not os.path.exists(fpath): raise SystemExit('缺字体 %s（SIL OFL，见同目录 OFL.txt）' % fpath)
    font_big = ImageFont.truetype(fpath, 300); font_small = ImageFont.truetype(fpath, 96)
    img = compose(font_big, font_small)
    p = os.path.join(dest, 'loadpage_logo.png'); img.save(p, 'PNG', optimize=True)
    print('loadpage_logo.png %dx%d %.0f KB' % (W, H, os.path.getsize(p) / 1024))
    if os.path.getsize(p) > 600 * 1024: raise SystemExit('loadpage_logo.png 超过 600 KB')

if __name__ == '__main__':
    main()
