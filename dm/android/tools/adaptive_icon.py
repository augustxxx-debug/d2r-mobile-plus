#!/usr/bin/env python3
"""从 iOS 的 AppIcon1024.png 生成安卓自适应图标的三层资源。

输入是 REQ14 已带「MOBILE PLUS」字样的 1024 方图（本脚本不再加字）。
输出（默认写进 dm/android/app/src/main/res/）：
  mipmap-xxxhdpi/ic_launcher_foreground.png  432×432 RGBA 前景
  mipmap-xxxhdpi/ic_launcher_monochrome.png  432×432 RGBA 单色剪影（白色 + alpha）
  values/ic_launcher_colors.xml              背景纯色 @color/ic_launcher_background
--preview <目录> 另出圆形 / 圆角方形（squircle 近似）/ 水滴形三张 192px 预览。

几何：自适应图标每层 108dp；66dp 安全圆是任何遮罩下都可见的区域。
xxxhdpi = 4px/dp，所以画布 432px、安全圆半径 66/2×4 = 132px。
实测（亮度 > 18，原图 1024、中心 511.5,511.5）：
  纹章外环半径约 440 px（≈0.86 × 半幅 512）；
  尖角最远 519.6 px；四角暗雾到约 556.8 px（亮度 ≤ 18 按背景处理）。
内容外接圆半径取 519.6 px → 缩放 s = (132 - 12) / 519.6 ≈ 0.23095，
其中 12px（3dp）留给安全圆内侧的羽化带：内容占满到半径 120px，
alpha 在 120→132px 线性淡出，132px 以外完全透明，保证不透明像素不出安全圆。
整幅 1024 方图按 s 缩到约 236px（实际取整 236，绘制比 0.23047）居中——
四角本来就和背景同色，再被径向羽化抹掉，与纯色背景无缝。

只依赖 Pillow；提交生成的 PNG，构建时不跑 Python。
"""

from __future__ import annotations

import argparse
import math
from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageStat

REPO_ROOT = Path(__file__).resolve().parents[3]
DEFAULT_INPUT = REPO_ROOT / "dm/ios/Assets.xcassets/AppIcon.appiconset/AppIcon1024.png"
DEFAULT_RES = REPO_ROOT / "dm/android/app/src/main/res"

LAYER_DP = 108              # 自适应图标层边长
SAFE_DIAMETER_DP = 66       # 系统保证可见的安全圆直径
VISIBLE_DP = 72             # 启动器通常显示的窗口（预览按它裁）
DP_PX = 4                   # xxxhdpi
LAYER_PX = LAYER_DP * DP_PX                     # 432
SAFE_RADIUS_PX = SAFE_DIAMETER_DP * DP_PX / 2   # 132
FEATHER_PX = 2 * DP_PX                          # 8：安全圆内侧的羽化带（2dp，比 3dp 让 PLUS 再大一点）
CONTENT_LUMINANCE_THRESHOLD = 18                # 低于它的暗雾算背景
VISIBLE_PX = VISIBLE_DP * DP_PX                 # 288

# PLUS 字样在原图中的大致范围（任务卡给的坐标），测试用它检查映射后不被裁。
PLUS_BOX_SOURCE = (270, 860, 760, 940)
# 必须完整落在安全圆（羽化带以内）的要点：PLUS 四角、MOBILE 四角、纹章外环的顶 / 左 / 右。外环之外的尖刺是装饰，交给安全圆羽化淡出。
# 真机奥丁 60aac2e3：按「最远亮像素」（外圈尖刺，半径约 520）定缩放时 PLUS 太小；改按这些要点定缩放，并把图案中心下移到让上下要点等距，约大 22%。
# Points that must sit fully inside the safe circle (inside the feather): the PLUS and MOBILE corners and the top / left / right of the emblem ring. The spikes beyond the ring
# are decoration and fade out at the safe circle. Thor 60aac2e3: scaling by the farthest bright pixel (the outer spikes, radius ~520) left PLUS too small; scaling by these points
# with the artwork centre moved down so the top and bottom points are equidistant makes it about 22% larger.
MOBILE_BOX_SOURCE = (175, 750, 860, 830)
RING_POINTS_SOURCE = ((512, 130), (110, 470), (915, 470))
def key_points_source():
    pts = []
    for x0, y0, x1, y1 in (PLUS_BOX_SOURCE, MOBILE_BOX_SOURCE):
        pts += [(x0, y0), (x1, y0), (x0, y1), (x1, y1)]
    return pts + list(RING_POINTS_SOURCE)

PREVIEW_SHAPES = (
    ("circle", "preview_circle.png"),
    ("squircle", "preview_squircle.png"),
    ("teardrop", "preview_teardrop.png"),
)


@dataclass(frozen=True)
class Layout:
    content_radius_src: float   # 原图内容外接圆半径（px）
    scale: float                # 连续缩放比（原图 px → 输出 px）
    side: int                   # 缩放后整幅图的边长（px）
    draw_scale: float           # 取整后的实际绘制比 = side / 原图边长
    offset: int                 # 缩放图在 432 画布上的左上角 x
    offset_y: int = -1          # 左上角 y（-1 = 同 offset，居中）

    def oy(self) -> int:
        return self.offset if self.offset_y < 0 else self.offset_y

    def map_point(self, x_src: float, y_src: float, src_size: int = 1024) -> tuple[float, float]:
        """原图像素中心（索引坐标）→ 输出画布像素中心（索引坐标）。"""
        c = (src_size - 1) / 2.0
        return (
            self.offset + (self.side - 1) / 2.0 + (x_src - c) * self.draw_scale,
            self.oy() + (self.side - 1) / 2.0 + (y_src - c) * self.draw_scale,
        )


def background_color(img: Image.Image, block: int = 64) -> tuple[int, int, int]:
    """四角 block×block 的暗色均值（四角都是纯深色，避免取到图案）。"""
    rgb = img.convert("RGB")
    w, h = rgb.size
    boxes = [
        (0, 0, block, block),
        (w - block, 0, w, block),
        (0, h - block, block, h),
        (w - block, h - block, w, h),
    ]
    means = [ImageStat.Stat(rgb.crop(box)).mean for box in boxes]
    return tuple(round(sum(m[i] for m in means) / len(means)) for i in range(3))  # type: ignore[return-value]


def measure_content_radius(img: Image.Image) -> float:
    """亮度超过阈值的像素离画布中心的最远距离（像素中心计）。"""
    rgb = img.convert("RGB")
    w, h = rgb.size
    cx = (w - 1) / 2.0
    cy = (h - 1) / 2.0
    max_d2 = 0.0
    for index, (r, g, b) in enumerate(rgb.getdata()):
        if 0.299 * r + 0.587 * g + 0.114 * b <= CONTENT_LUMINANCE_THRESHOLD:
            continue
        y, x = divmod(index, w)
        dx = x - cx
        dy = y - cy
        d2 = dx * dx + dy * dy
        if d2 > max_d2:
            max_d2 = d2
    return math.sqrt(max_d2)


def layout_for(img: Image.Image) -> Layout:
    content_radius = measure_content_radius(img)   # 只记进报告（外圈尖刺），不再拿它定缩放 | reported only (the outer spikes); no longer sets the scale
    pts = [(x * img.width / 1024.0, y * img.height / 1024.0) for x, y in key_points_source()]
    cx = (img.width - 1) / 2.0
    # 图案中心（原图坐标）竖直方向取让要点最远距离最小的那一行 | the artwork centre row that minimises the farthest key point
    best_cy, best_d = None, None
    for cy10 in range(int(img.height * 0.40 * 10), int(img.height * 0.70 * 10)):
        cy = cy10 / 10.0
        d = max(math.hypot(x - cx, y - cy) for x, y in pts)
        if best_d is None or d < best_d: best_cy, best_d = cy, d
    scale = (SAFE_RADIUS_PX - FEATHER_PX) / best_d
    side = max(1, round(img.width * scale)); draw = side / img.width
    canvas_c = (LAYER_PX - 1) / 2.0
    ox = round(canvas_c - (side - 1) / 2.0)
    oy = round(canvas_c - (best_cy - (img.height - 1) / 2.0) * draw - (side - 1) / 2.0)
    return Layout(content_radius, scale, side, draw, ox, oy)


def radial_alpha_mask() -> Image.Image:
    """安全圆内全不透明、内侧羽化带线性淡出、圆外透明。"""
    cx = (LAYER_PX - 1) / 2.0
    inner = SAFE_RADIUS_PX - FEATHER_PX
    data = bytearray(LAYER_PX * LAYER_PX)
    for y in range(LAYER_PX):
        dy = y - cx
        row = y * LAYER_PX
        for x in range(LAYER_PX):
            r = math.hypot(x - cx, dy)
            if r <= inner:
                value = 255
            elif r >= SAFE_RADIUS_PX:
                value = 0
            else:
                value = round(255 * (SAFE_RADIUS_PX - r) / FEATHER_PX)
            data[row + x] = value
    return Image.frombytes("L", (LAYER_PX, LAYER_PX), bytes(data))


def paste_clipped(layer: Image.Image, img: Image.Image, x: int, y: int) -> None:
    """把 img 贴到 layer 的 (x, y)，超出画布的部分裁掉（缩放图可能比 432 画布大、偏移可为负）。"""
    sx0, sy0 = max(0, -x), max(0, -y)
    sx1, sy1 = min(img.width, LAYER_PX - x), min(img.height, LAYER_PX - y)
    if sx1 <= sx0 or sy1 <= sy0: return
    part = img.crop((sx0, sy0, sx1, sy1))
    if part.mode == "RGBA" and layer.mode == "RGBA": layer.alpha_composite(part, (x + sx0, y + sy0))
    else: layer.paste(part, (x + sx0, y + sy0))


def build_foreground(img: Image.Image, layout: Layout) -> Image.Image:
    scaled = img.convert("RGBA").resize((layout.side, layout.side), Image.LANCZOS)
    layer = Image.new("RGBA", (LAYER_PX, LAYER_PX), (0, 0, 0, 0))
    paste_clipped(layer, scaled, layout.offset, layout.oy())
    layer.putalpha(ImageChops.multiply(layer.getchannel("A"), radial_alpha_mask()))
    return layer


def build_monochrome(img: Image.Image, layout: Layout, background: tuple[int, int, int]) -> Image.Image:
    """白色 + 亮度 alpha 的剪影，几何与前景一致（主题图标用）。"""
    scaled = img.convert("RGB").resize((layout.side, layout.side), Image.LANCZOS)
    background_luma = 0.299 * background[0] + 0.587 * background[1] + 0.114 * background[2]
    low = min(background_luma, 254.0)
    lut = [max(0, min(255, round((v - low) * 255.0 / (255.0 - low)))) for v in range(256)]
    silhouette = scaled.convert("L").point(lut)
    alpha = Image.new("L", (LAYER_PX, LAYER_PX), 0)
    paste_clipped(alpha, silhouette, layout.offset, layout.oy())
    layer = Image.new("RGBA", (LAYER_PX, LAYER_PX), (255, 255, 255, 0))
    layer.putalpha(ImageChops.multiply(alpha, radial_alpha_mask()))
    return layer


def shape_mask(kind: str, size: int = 192, supersample: int = 4) -> Image.Image:
    """启动器遮罩的近似：圆 / 超椭圆（squircle）/ 三圆角一尖角（水滴）。"""
    n = size * supersample
    mask = Image.new("L", (n, n), 0)
    draw = ImageDraw.Draw(mask)
    if kind == "circle":
        draw.ellipse((0, 0, n - 1, n - 1), fill=255)
    elif kind == "squircle":
        # |x|^4 + |y|^4 <= 1，标准 squircle 的近似。
        points = bytearray(n * n)
        c = (n - 1) / 2.0
        radius = n / 2.0
        for y in range(n):
            ny = abs(y - c) / radius
            ny4 = ny ** 4
            row = y * n
            for x in range(n):
                nx = abs(x - c) / radius
                if nx ** 4 + ny4 <= 1.0:
                    points[row + x] = 255
        mask = Image.frombytes("L", (n, n), bytes(points))
    elif kind == "teardrop":
        # 三个圆角 + 左下尖角，近似 Android 的 teardrop 遮罩。
        draw.rounded_rectangle(
            (0, 0, n - 1, n - 1), radius=n // 2 - 8,
            corners=(True, True, True, False), fill=255,
        )
    else:
        raise ValueError(f"unknown preview shape: {kind}")
    return mask.resize((size, size), Image.LANCZOS)


def render_previews(background: tuple[int, int, int], foreground: Image.Image,
                    out_dir: Path, size: int = 192) -> list[Path]:
    """按启动器可见窗口（72dp）裁出前景，套三种遮罩各存一张 192px 预览。"""
    canvas = Image.new("RGBA", (LAYER_PX, LAYER_PX), (*background, 255))
    canvas.alpha_composite(foreground)
    margin = (LAYER_PX - VISIBLE_PX) // 2
    visible = canvas.crop((margin, margin, margin + VISIBLE_PX, margin + VISIBLE_PX))
    visible = visible.resize((size, size), Image.LANCZOS)
    out_dir.mkdir(parents=True, exist_ok=True)
    paths = []
    for kind, name in PREVIEW_SHAPES:
        out = Image.new("RGBA", (size, size), (0, 0, 0, 0))
        out.paste(visible, (0, 0), shape_mask(kind, size))
        path = out_dir / name
        out.save(path)
        paths.append(path)
    return paths


def write_color_resource(res_path: Path, background: tuple[int, int, int]) -> Path:
    color = "#{:02X}{:02X}{:02X}".format(*background)
    path = res_path / "values/ic_launcher_colors.xml"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        '<?xml version="1.0" encoding="utf-8"?>\n'
        "<resources>\n"
        "    <!-- 背景 = AppIcon1024.png 四角 64×64 块的暗色均值。 -->\n"
        f'    <color name="ic_launcher_background">{color}</color>\n'
        "</resources>\n",
        encoding="utf-8",
    )
    return path


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--input", type=Path, default=DEFAULT_INPUT, help="AppIcon1024.png")
    parser.add_argument("--res", type=Path, default=DEFAULT_RES, help="安卓 res 根目录")
    parser.add_argument("--preview", type=Path, help="另出三张 192px 预览图的目录")
    args = parser.parse_args(argv)

    with Image.open(args.input) as source:
        source.load()
        background = background_color(source)
        layout = layout_for(source)
        foreground = build_foreground(source, layout)
        monochrome = build_monochrome(source, layout, background)

    mipmap = args.res / "mipmap-xxxhdpi"
    mipmap.mkdir(parents=True, exist_ok=True)
    foreground_path = mipmap / "ic_launcher_foreground.png"
    monochrome_path = mipmap / "ic_launcher_monochrome.png"
    foreground.save(foreground_path)
    monochrome.save(monochrome_path)
    color_path = write_color_resource(args.res, background)

    if args.preview:
        for path in render_previews(background, foreground, args.preview):
            print(f"preview: {path}")

    print(f"background: #{background[0]:02X}{background[1]:02X}{background[2]:02X}")
    print(f"content radius: {layout.content_radius_src:.1f}px -> scale {layout.draw_scale:.5f} "
          f"({layout.side}px square, offset {layout.offset},{layout.oy()})")
    print(f"foreground: {foreground_path}")
    print(f"monochrome: {monochrome_path}")
    print(f"color: {color_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
