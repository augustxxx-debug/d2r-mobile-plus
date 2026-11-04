"""自适应启动器图标：提交的 PNG 与源图重新生成的结果逐像素一致，前景不越安全圆，PLUS 不被裁。
Adaptive launcher icon: committed PNGs match a fresh generation pixel for pixel, the foreground stays
inside the 66dp safe circle, and the PLUS lettering is not clipped.
"""
import importlib.util
import math
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[3]
TOOL = ROOT / 'dm/android/tools/adaptive_icon.py'
RES = ROOT / 'dm/android/app/src/main/res'
SOURCE = ROOT / 'dm/ios/Assets.xcassets/AppIcon.appiconset/AppIcon1024.png'

spec = importlib.util.spec_from_file_location('adaptive_icon', TOOL)
icon = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = icon  # Python 3.9 的 dataclass 需要模块已登记
spec.loader.exec_module(icon)

COMMITTED = {
    'foreground': RES / 'mipmap-xxxhdpi/ic_launcher_foreground.png',
    'monochrome': RES / 'mipmap-xxxhdpi/ic_launcher_monochrome.png',
}
BACKGROUND = icon.LAYER_PX / 2 - 0.5  # 像素中心坐标 215.5
OPAQUE = 32  # 羽化带里 alpha < 32 的像素允许越过安全圆


def radius(x, y):
    return math.hypot(x - BACKGROUND, y - BACKGROUND)


class AdaptiveIcon(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # 临时目录落在 $TMPDIR（mkdtemp 默认跟随 TMPDIR），不碰仓库。
        cls.temp = Path(tempfile.mkdtemp(prefix='adaptive_icon_test.'))
        cls.generated_res = cls.temp / 'res'
        cls.generated_preview = cls.temp / 'preview'
        cls.script_run = subprocess.run(
            [sys.executable, str(TOOL), '--input', str(SOURCE),
             '--res', str(cls.generated_res), '--preview', str(cls.generated_preview)],
            capture_output=True, text=True,
        )
        with Image.open(SOURCE) as source:
            source.load()
            cls.layout = icon.layout_for(source)
            cls.background = icon.background_color(source)

    def test_script_runs(self):
        self.assertEqual(self.script_run.returncode, 0, self.script_run.stderr or self.script_run.stdout)
        self.assertIn('scale', self.script_run.stdout)

    def test_committed_pngs_match_a_fresh_generation(self):
        """源图改了却忘记重生成时，这里会失败。"""
        for name, committed in COMMITTED.items():
            generated = self.generated_res / 'mipmap-xxxhdpi' / committed.name
            self.assertTrue(generated.is_file(), generated)
            with Image.open(committed) as a, Image.open(generated) as b:
                self.assertEqual(a.size, (icon.LAYER_PX, icon.LAYER_PX), name)
                # 直接比原始字节：RGBA 的 difference().getbbox() 只看 alpha 通道，颜色变了也会漏掉 | compare raw bytes: getbbox() on an RGBA difference looks only at alpha and misses colour changes
                self.assertEqual(a.convert('RGBA').tobytes(), b.convert('RGBA').tobytes(), f'{name}: committed PNG is stale; rerun {TOOL.name}')

    def test_color_resource_matches_the_source_corners(self):
        committed = RES / 'values/ic_launcher_colors.xml'
        generated = self.generated_res / 'values/ic_launcher_colors.xml'
        self.assertEqual(committed.read_text(encoding='utf-8'), generated.read_text(encoding='utf-8'))
        color = '#{:02X}{:02X}{:02X}'.format(*self.background)
        self.assertIn(f'<color name="ic_launcher_background">{color}</color>', committed.read_text(encoding='utf-8'))

    def test_foreground_stays_inside_the_safe_circle(self):
        with Image.open(COMMITTED['foreground']) as image:
            alpha = image.convert('RGBA').getchannel('A')
            width, height = alpha.size
            pixels = list(alpha.getdata())
        self.assertEqual((width, height), (icon.LAYER_PX, icon.LAYER_PX))
        opaque = 0
        for index, value in enumerate(pixels):
            if value < OPAQUE:
                continue
            opaque += 1
            y, x = divmod(index, width)
            self.assertLessEqual(
                radius(x, y), icon.SAFE_RADIUS_PX,
                f'opaque pixel ({x},{y}) alpha={value} is outside the {icon.SAFE_DIAMETER_DP}dp safe circle',
            )
        self.assertGreater(opaque, 10000, 'foreground looks empty')

    def test_plus_lettering_inside_the_safe_circle(self):
        x0, y0, x1, y1 = icon.PLUS_BOX_SOURCE
        mapped = [self.layout.map_point(x, y) for x in (x0, x1) for y in (y0, y1)]
        farthest = max(radius(x, y) for x, y in mapped)
        self.assertLess(farthest, icon.SAFE_RADIUS_PX,
                        f'PLUS box maps to radius {farthest:.1f}px, safe circle is {icon.SAFE_RADIUS_PX}px')
        # 该区域确实有内容（不是被缩没或整片透明）。
        with Image.open(COMMITTED['foreground']) as image:
            alpha = image.convert('RGBA').getchannel('A')
            left = math.floor(min(x for x, _ in mapped))
            right = math.ceil(max(x for x, _ in mapped))
            top = math.floor(min(y for _, y in mapped))
            bottom = math.ceil(max(y for _, y in mapped))
            visible = sum(1 for value in alpha.crop((left, top, right + 1, bottom + 1)).getdata() if value >= 128)
        self.assertGreater(visible, 50, 'PLUS box has almost no opaque foreground')

    def test_key_points_fit_and_plus_is_larger(self):
        # 真机奥丁 60aac2e3：PLUS 太小。PLUS / MOBILE 四角与纹章外环要点都在羽化带以内，且绘制比比旧做法（按外圈尖刺定，0.2305）大至少 15%
        # Thor 60aac2e3: PLUS too small. All key points sit inside the feather, and the draw scale beats the old one (set by the outer spikes, 0.2305) by at least 15%
        inner = icon.SAFE_RADIUS_PX - icon.FEATHER_PX
        for x, y in icon.key_points_source():
            mx, my = self.layout.map_point(x, y)
            self.assertLessEqual(radius(mx, my), inner + 1.0, f'key point ({x},{y}) maps to radius {radius(mx, my):.1f}px > {inner}px')
        self.assertGreater(self.layout.draw_scale, 0.2305 * 1.15, f'draw scale {self.layout.draw_scale:.4f}')

    def test_three_previews_are_generated(self):
        for _, name in icon.PREVIEW_SHAPES:
            path = self.generated_preview / name
            self.assertTrue(path.is_file(), path)
            with Image.open(path) as image:
                self.assertEqual(image.size, (192, 192), name)
                rgba = image.convert('RGBA')
                self.assertGreater(sum(1 for value in rgba.getchannel('A').getdata() if value), 0, name)
                # 遮罩形状外必须透明（圆形的左上角）。
                if name == 'preview_circle.png':
                    self.assertEqual(rgba.getpixel((0, 0))[3], 0, name)


if __name__ == '__main__':
    unittest.main(verbosity=2)
