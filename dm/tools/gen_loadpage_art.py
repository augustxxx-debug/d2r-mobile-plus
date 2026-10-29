#!/usr/bin/env python3
"""加载页背景图（自绘，程序化生成；不含任何原版素材）：深色石纹 + 哥特尖拱 + 中下方火光 + 飘散火星 + 暗角 + 细金线花纹。
两张：横 2048×1152（loadpage_bg_land.jpg）、竖 1152×2048（loadpage_bg_port.jpg），焦点（火光中心）在 (0.5, 0.58)，与 dm/host/dm_loadpage.c 的 LP_FOCUS 一致。
固定随机种子，重跑得到同一张图。输出 JPEG 质量 82，每张控制在 1 MB 以内。
用法：python3 dm/tools/gen_loadpage_art.py [输出目录=dm/assets/loadpage]（要 Pillow + numpy，项目 venv 里有）
Loading page backgrounds (self-drawn, procedurally generated; no original game assets): dark stone + gothic pointed arch + fire glow centre-low + embers + vignette + thin gold tracery.
Two images: landscape 2048×1152, portrait 1152×2048; the focus (glow centre) is at (0.5, 0.58), matching LP_FOCUS in dm/host/dm_loadpage.c. Fixed seed, reproducible. JPEG quality 82, under 1 MB each.
"""
import os, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

def value_noise(h, w, cells, rng):
    """插值的格点噪声（0..1） | interpolated lattice noise (0..1)"""
    gy, gx = cells, max(1, int(cells * w / h))
    g = rng.random((gy + 1, gx + 1)).astype(np.float32)
    ys = np.linspace(0, gy, h, endpoint=False); xs = np.linspace(0, gx, w, endpoint=False)
    y0 = np.floor(ys).astype(int); x0 = np.floor(xs).astype(int); fy = (ys - y0)[:, None]; fx = (xs - x0)[None, :]
    fy = fy * fy * (3 - 2 * fy); fx = fx * fx * (3 - 2 * fx)
    a = g[y0][:, x0]; b = g[y0][:, x0 + 1]; c = g[y0 + 1][:, x0]; d = g[y0 + 1][:, x0 + 1]
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy

def fbm(h, w, rng, octaves=6, base=4):
    out = np.zeros((h, w), np.float32); amp = 1.0; tot = 0.0
    for i in range(octaves):
        out += amp * value_noise(h, w, base * 2 ** i, rng); tot += amp; amp *= 0.5
    return out / tot

def render(w, h, seed):
    rng = np.random.default_rng(seed)
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32); u = xx / w; v = yy / h
    # 1. 石纹底：两层 fbm 混成冷灰褐，带细裂纹 | stone base: two fbm layers into a cold grey-brown with fine cracks
    n1 = fbm(h, w, rng, 6, 3); n2 = fbm(h, w, rng, 7, 9)
    crack = np.clip(1 - np.abs(n2 - 0.5) * 30, 0, 1) ** 2 * 0.07 * (0.4 + 0.6 * n1)   # 细而淡、断续的裂纹 | fine, faint, broken cracks
    stone = 0.035 + 0.13 * n1 ** 1.6 + 0.05 * n2 - crack
    stone = np.clip(stone, 0, 1)
    r = stone * 0.95 + 0.012; g = stone * 0.82 + 0.010; b = stone * 0.70 + 0.008
    # 2. 石块接缝：略错位的横缝与竖缝（砌墙感），暗一点 | masonry joints: slightly offset courses, darker
    course = 0.085 if w >= h else 0.055
    row = np.floor(v / course); jitter = (np.sin(row * 12.9898) * 43758.5453) % 1.0
    mortar_h = np.clip(1 - np.abs(((v / course) % 1.0) - 0.5) * 60, 0, 1)
    colw = course * (1.9 if w >= h else 1.3) * (h / w)
    mortar_v = np.clip(1 - np.abs((((u + jitter * 0.5) / colw) % 1.0) - 0.5) * 60 * (colw / course) * (w / h) * 0.5, 0, 1)
    joints = np.clip(mortar_h + mortar_v, 0, 1) * (0.55 + 0.45 * n1)
    dark = 1 - 0.45 * joints
    r *= dark; g *= dark; b *= dark
    # 3. 尖拱：中央一座哥特尖拱的内部更暗更深（像通向地下），拱边一圈微亮石沿 | pointed arch: the interior is darker and deeper, with a faintly lit stone rim
    cx, cy = 0.5, 0.58
    ar = (0.27 if w >= h else 0.34) * min(w, h)         # 拱半宽（像素，按短边） | half width in pixels, by the short side
    ax = (xx - cx * w) / ar                             # 归一化到拱半宽 | normalised to the arch half width
    ay = (yy - cy * h) / ar
    # 两段圆弧相交成尖拱（等边尖拱：圆心在对侧拱脚） | two arcs meeting in a point (equilateral: centres at the opposite springing points)
    d_l = np.sqrt((ax + 1) ** 2 + np.minimum(ay, 0) ** 2); d_r = np.sqrt((ax - 1) ** 2 + np.minimum(ay, 0) ** 2)
    inside_top = (d_l <= 2) & (d_r <= 2) & (ay <= 0)
    inside_bot = (np.abs(ax) <= 1) & (ay > 0)
    inside = inside_top | inside_bot
    edge = np.minimum(np.abs(np.minimum(d_l, d_r) - 2), np.abs(np.abs(ax) - 1) * (ay > 0) + (ay <= 0) * 9)
    lower_fade = np.where(ay > 0, np.clip(1 - ay / 1.6, 0.15, 1), 1.0)   # 拱脚以下的竖沿往下渐隐 | the vertical jambs below the springing fade out downward
    rim = np.clip(1 - edge / 0.035, 0, 1) * (0.6 + 0.4 * n1) * lower_fade
    rim2 = np.clip(1 - edge / 0.11, 0, 1) ** 2 * 0.5 * lower_fade   # 拱沿外一圈柔光 | a soft halo outside the rim
    depth = np.clip((v - cy + 0.45) / 0.9, 0, 1)        # 越往拱底越黑 | darker toward the bottom of the arch
    arch_dark = np.where(inside, 0.55 - 0.45 * depth, 1.0)
    r = r * arch_dark + rim * 0.30 + rim2 * 0.10; g = g * arch_dark + rim * 0.22 + rim2 * 0.07; b = b * arch_dark + rim * 0.13 + rim2 * 0.04
    # 4. 火光：焦点处的暖色辐射，叠一层慢噪声当火焰起伏 | fire glow: warm radial light at the focus with slow noise as flame motion
    dx = (u - cx) * (w / h); dy = (v - cy) * 1.6
    dist = np.sqrt(dx * dx + dy * dy)
    flame = fbm(h, w, rng, 4, 5)
    glow = np.exp(-(dist / 0.30) ** 1.5) * (0.7 + 0.6 * flame) * 0.75
    glow *= np.clip(1.1 - (cy - v) * 2.6, 0.08, 1.0)   # 向上渐弱 | fades upward
    core = np.exp(-(dist / 0.09) ** 2) * 0.5             # 热核 | hot core
    r += glow * 0.95 + core * 1.0; g += glow * 0.36 + core * 0.55; b += glow * 0.06 + core * 0.12
    # 地面反光：拱底一条横向暖光 | floor bounce: a horizontal warm band at the arch floor
    floor = np.exp(-((v - (cy + 0.36)) / 0.03) ** 2) * np.exp(-(dx / 0.5) ** 2) * 0.30
    r += floor * 0.8; g += floor * 0.35; b += floor * 0.08
    img = np.stack([r, g, b], -1)
    # 5. 暗角与顶部压暗 | vignette and darker top
    vig = 1 - 0.85 * np.clip((np.sqrt(((u - 0.5) * 2) ** 2 + ((v - 0.5) * 2) ** 2) - 0.45) / 0.95, 0, 1) ** 1.5
    top = 1 - 0.5 * np.clip((0.4 - v) / 0.4, 0, 1)
    img *= (vig * top)[..., None]
    # 6. 火星：下半屏随机亮点，带柔光 | embers: random bright specks in the lower half with soft halos
    emb = np.zeros((h, w), np.float32)
    n_emb = 140 if w >= h else 110
    for _ in range(n_emb):
        ex = rng.random(); ey = cy - 0.05 + rng.random() ** 1.7 * 0.5 - 0.15 * rng.random()
        if not (0 <= ey <= 1): continue
        px, py = int(ex * (w - 1)), int(ey * (h - 1)); rad = 0.8 + rng.random() * 1.2; amp = 0.25 + rng.random() * 0.6
        y0, y1 = max(0, py - 6), min(h, py + 7); x0, x1 = max(0, px - 6), min(w, px + 7)
        sub_y, sub_x = np.mgrid[y0:y1, x0:x1]
        emb[y0:y1, x0:x1] += amp * np.exp(-(((sub_x - px) ** 2 + (sub_y - py) ** 2) / (2.0 * rad ** 2)))
    img[..., 0] += emb * 1.0; img[..., 1] += emb * 0.5; img[..., 2] += emb * 0.12
    # 7. 颗粒 | grain
    img += (rng.random((h, w, 1)).astype(np.float32) - 0.5) * 0.035
    img = np.clip(img, 0, 1)
    out = Image.fromarray((img * 255 + 0.5).astype(np.uint8), 'RGB')
    # 8. 细金线花纹：四角的尖拱花窗线描 + 顶部一条双线（低透明度，像是壁面雕饰） | thin gold tracery: pointed quatrefoil line art in the corners + a double line at the top (low opacity, like wall carving)
    ov = Image.new('RGBA', (w, h), (0, 0, 0, 0)); dr = ImageDraw.Draw(ov)
    gold = (190, 150, 70, 70); gold2 = (190, 150, 70, 40)
    m = int(min(w, h) * 0.045); s = int(min(w, h) * 0.11)
    def tracery(x, y, flip_x, flip_y):
        # 一枝三叶花饰：三个相交的圆弧 + 一根竖茎 | a trefoil: three intersecting arcs + one stem
        for k in range(3):
            ang = k * 120 - 90; cxk = x + flip_x * (s * 0.5 + s * 0.22 * np.cos(np.radians(ang))); cyk = y + flip_y * (s * 0.5 + s * 0.22 * np.sin(np.radians(ang)))
            rr = s * 0.24; dr.ellipse([cxk - rr, cyk - rr, cxk + rr, cyk + rr], outline=gold, width=2)
        dr.line([x + flip_x * s * 0.5, y + flip_y * s * 0.86, x + flip_x * s * 0.5, y + flip_y * s * 1.45], fill=gold2, width=2)
        dr.arc([x, y, x + flip_x * s * 1.0, y + flip_y * s * 1.0] if flip_x > 0 and flip_y > 0 else sorted_box(x, y, flip_x, flip_y), 0, 360, fill=gold2, width=1)
    def sorted_box(x, y, fx, fy):
        x2, y2 = x + fx * s, y + fy * s; return [min(x, x2), min(y, y2), max(x, x2), max(y, y2)]
    tracery(m, m, 1, 1); tracery(w - m, m, -1, 1); tracery(m, h - m, 1, -1); tracery(w - m, h - m, -1, -1)
    dr.line([m + s * 1.3, m + s * 0.5, w - m - s * 1.3, m + s * 0.5], fill=gold2, width=1)
    dr.line([m + s * 1.3, m + s * 0.5 + 6, w - m - s * 1.3, m + s * 0.5 + 6], fill=(190, 150, 70, 22), width=1)
    ov = ov.filter(ImageFilter.GaussianBlur(0.6))
    out = Image.alpha_composite(out.convert('RGBA'), ov).convert('RGB')
    return out

def main():
    dest = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'assets', 'loadpage')
    os.makedirs(dest, exist_ok=True)
    for name, w, h, seed in (('loadpage_bg_land.jpg', 2048, 1152, 20261029), ('loadpage_bg_port.jpg', 1152, 2048, 20261030)):
        img = render(w, h, seed); p = os.path.join(dest, name)
        img.save(p, 'JPEG', quality=82, optimize=True, progressive=True, subsampling=1)
        sz = os.path.getsize(p); print('%s %dx%d %.0f KB' % (name, w, h, sz / 1024))
        if sz > 1024 * 1024: raise SystemExit('%s 超过 1 MB' % name)

if __name__ == '__main__':
    main()
