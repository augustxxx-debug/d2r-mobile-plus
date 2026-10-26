"""字体样张缓存（复审，2026-10-27）：同一输出目录换自选字体或原字体，必须重新合成，不沿用旧样张。
Font sample cache (re-audit, 2026-10-27): with the same output directory, a different requested or original font must be recomposed, never reuse an old sample."""
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import font_sample  # noqa: E402


class Cache(unittest.TestCase):
    def setUp(self):
        self.calls = []
        self.orig = font_sample.font_override.compose
        def fake(original, requested, target, weight):
            self.calls.append((Path(requested).read_bytes(), original, weight))
            Path(target).write_bytes(b'composed:' + Path(requested).read_bytes() + original + bytes([weight]))
        font_sample.font_override.compose = fake

    def tearDown(self):
        font_sample.font_override.compose = self.orig

    def test_changed_inputs_recompose(self):
        with tempfile.TemporaryDirectory() as d:
            out = Path(d); a, b = out / 'a.ttf', out / 'b.ttf'; a.write_bytes(b'font-A'); b.write_bytes(b'font-B')
            p1 = font_sample.compose_all(b'orig-1', a, [3], out)[3]
            p2 = font_sample.compose_all(b'orig-1', a, [3], out)[3]
            self.assertEqual(p1, p2); self.assertEqual(len(self.calls), 1)               # 同输入复用 | same inputs reuse
            p3 = font_sample.compose_all(b'orig-1', b, [3], out)[3]
            self.assertNotEqual(p3, p1); self.assertEqual(len(self.calls), 2)            # 换自选字体重合成 | new requested font recomposes
            self.assertIn(b'font-B', p3.read_bytes())
            p4 = font_sample.compose_all(b'orig-2', b, [3], out)[3]
            self.assertNotEqual(p4, p3); self.assertEqual(len(self.calls), 3)            # 换原字体重合成 | new original recomposes
            p5 = font_sample.compose_all(b'orig-2', b, [4], out)[4]
            self.assertNotEqual(p5, p4); self.assertEqual(len(self.calls), 4)            # 换档位重合成 | new level recomposes


if __name__ == '__main__':
    unittest.main(verbosity=1)
