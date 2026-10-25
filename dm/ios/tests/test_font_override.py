#!/usr/bin/env python3
"""用合成字体验证可选字体的缺字回退、度量和缓存，不携带游戏或商业字体。
Verify fallback, metrics and caching with synthetic fonts; no game or commercial fonts.
"""
import hashlib
import io
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import font_override

try:
    from fontTools.fontBuilder import FontBuilder
    from fontTools.pens.ttGlyphPen import TTGlyphPen
    from fontTools.ttLib import TTFont
except ImportError:
    FontBuilder = None


def synthetic(path, chars, units, family):
    builder = FontBuilder(units, isTTF=True)
    names = ['.notdef'] + ['uni%04X' % c for c in chars]
    builder.setupGlyphOrder(names)
    builder.setupCharacterMap(dict(zip(chars, names[1:])))
    glyphs = {}
    for name in names:
        pen = TTGlyphPen(None)
        pen.moveTo((0, 0)); pen.lineTo((units // 3, 0))
        pen.lineTo((units // 3, units // 2)); pen.closePath()
        glyphs[name] = pen.glyph()
    builder.setupGlyf(glyphs)
    builder.setupHorizontalMetrics({name: (units, 0) for name in names})
    builder.setupHorizontalHeader(ascent=units * 3 // 4, descent=-units // 4)
    builder.setupNameTable({'familyName': family, 'styleName': 'Regular',
                           'fullName': family, 'psName': family})
    builder.setupOS2(sTypoAscender=units * 3 // 4, sTypoDescender=-units // 4,
                     usWinAscent=units * 3 // 4, usWinDescent=units // 4)
    builder.setupPost()
    builder.save(path)


@unittest.skipUnless(FontBuilder, 'optional fontTools dependency is not installed')
class FontComposition(unittest.TestCase):
    def test_coverage_metrics_and_unchanged_inputs(self):
        with tempfile.TemporaryDirectory() as work:
            base, custom, output = [Path(work) / x for x in ('base.ttf', 'custom.ttf', 'out.ttf')]
            synthetic(base, [65, 0x4e00], 1000, 'GameBase')
            synthetic(custom, [65, 0x4e8c], 2048, 'Requested')
            before = (base.read_bytes(), custom.read_bytes())
            font_override.compose(before[0], custom, output)
            with TTFont(output) as merged:
                self.assertEqual(set(merged.getBestCmap()), {65, 0x4e00, 0x4e8c})
                self.assertEqual(merged['head'].unitsPerEm, 1000)
                self.assertEqual(merged['hhea'].ascent, 750)
                self.assertEqual(merged['hhea'].descent, -250)
                self.assertEqual(merged['name'].getDebugName(1), 'GameBase')
                self.assertEqual(merged['hmtx'][merged.getBestCmap()[65]][0], 1000)
            self.assertEqual(before, (base.read_bytes(), custom.read_bytes()))

    def test_game_private_use_symbols_keep_original_metrics(self):
        with tempfile.TemporaryDirectory() as work:
            base, custom, output = [Path(work) / x for x in ('base.ttf', 'custom.ttf', 'out.ttf')]
            synthetic(base, [65, 0xe001], 1000, 'GameBase')
            synthetic(custom, [65, 0xe001], 1000, 'Requested')
            with TTFont(custom) as font:
                glyph = font.getBestCmap()[0xe001]
                font['hmtx'][glyph] = (432, 0)
                font.save(custom)
            font_override.compose(base.read_bytes(), custom, output)
            with TTFont(output) as merged:
                self.assertEqual(merged['hmtx'][merged.getBestCmap()[0xe001]][0], 1000)

    def test_custom_vertical_tables_with_horizontal_game_font(self):
        with tempfile.TemporaryDirectory() as work:
            base, custom, output = [Path(work) / x for x in ('base.ttf', 'custom.ttf', 'out.ttf')]
            synthetic(base, [65, 0x4e00], 1000, 'GameBase')
            synthetic(custom, [65, 0x4e8c], 2048, 'Requested')
            builder = FontBuilder(font=TTFont(custom))
            builder.setupVerticalMetrics({name: (2048, 0) for name in builder.font.getGlyphOrder()})
            builder.setupVerticalHeader(ascent=1536, descent=-512)
            builder.save(custom)
            font_override.compose(base.read_bytes(), custom, output)
            with TTFont(output) as merged:
                self.assertEqual(set(merged.getBestCmap()), {65, 0x4e00, 0x4e8c})
                self.assertNotIn('vhea', merged)

    def test_fingerprint_and_disabled_cleanup(self):
        with tempfile.TemporaryDirectory() as work:
            font = Path(work) / 'input.ttf'
            synthetic(font, [65], 1000, 'Requested')
            with patch.dict(os.environ, DM_UI_FONT=str(font), DM_GEN_ASSETS=work):
                self.assertEqual(font_override.source_fingerprint(), hashlib.sha256(font.read_bytes()).hexdigest())
                font.write_bytes(font.read_bytes() + b'changed')
                self.assertEqual(font_override.source_fingerprint(), hashlib.sha256(font.read_bytes()).hexdigest())
                output = Path(work) / 'UIFonts'
                output.mkdir()
                (output / 'theme.ttf').write_bytes(b'stale')
                (output / 'fingerprint.txt').write_text('stale')
                os.environ['DM_UI_FONT'] = ''
                self.assertIsNone(font_override.prepare('unused'))
                self.assertFalse((output / 'theme.ttf').exists())
                self.assertEqual(font_override.source_fingerprint(), 'original')

    def test_missing_input_fails_early(self):
        with patch.dict(os.environ, DM_UI_FONT='/nonexistent/font.ttf'):
            with self.assertRaises(ValueError):
                font_override.source_path()


if __name__ == '__main__':
    unittest.main()
