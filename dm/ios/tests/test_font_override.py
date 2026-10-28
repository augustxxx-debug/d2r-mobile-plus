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
            synthetic(base, [65, 0x4e00, 0x4e8c], 1000, 'GameBase')
            synthetic(custom, [65, 0x4e8c, 0x4e09], 2048, 'Requested')
            before = (base.read_bytes(), custom.read_bytes())
            font_override.compose(before[0], custom, output)
            with TTFont(output) as merged:
                # 自选字体独有的 0x4e09 不补进来：只换原字体已有的码位（四审：全量补字把字体撑过游戏能读的体积）
                # 0x4e09, only in the requested font, is not added: only codepoints the game font has are replaced (fourth audit: adding them all overflowed the size the game loads)
                self.assertEqual(set(merged.getBestCmap()), {65, 0x4e00, 0x4e8c})
                self.assertEqual(merged['head'].unitsPerEm, 1000)
                self.assertEqual(merged['hhea'].ascent, 750)
                self.assertEqual(merged['hhea'].descent, -250)
                self.assertEqual(merged['name'].getDebugName(1), 'GameBase')
                self.assertEqual(merged['hmtx'][merged.getBestCmap()[65]][0], 1000)
            self.assertEqual(before, (base.read_bytes(), custom.read_bytes()))

    def test_composites_and_hinting_are_flattened_and_output_is_reproducible(self):
        """四审：霞鹜文楷带复合字形与 prep 表；合成结果要与原字体同构（只有普通轮廓、无微调表），且两次合成逐字节相同（手机与电脑做的导入包要一致）。
        Fourth audit: LXGW WenKai has composite glyphs and a prep table; the composition must match the game font's structure (plain outlines only, no hinting
        tables), and two compositions must be byte-identical (packs made on the phone and on a computer must match)."""
        from fontTools.ttLib.tables._g_l_y_f import Glyph, GlyphComponent
        from fontTools.ttLib import newTable
        with tempfile.TemporaryDirectory() as work:
            base, custom, out1, out2 = [Path(work) / x for x in ('base.ttf', 'custom.ttf', 'a.ttf', 'b.ttf')]
            synthetic(base, [65, 0x4e00, 0x4e8c, 0x4e09], 1000, 'GameBase')
            synthetic(custom, [0x4e8c, 0x4e09], 1000, 'Requested')
            with TTFont(custom) as f:   # 把「三」做成引用「二」的复合字形，再加 prep / fpgm 微调表 | make 0x4e09 a composite of 0x4e8c and add prep / fpgm
                comp = Glyph(); comp.numberOfContours = -1; c = GlyphComponent(); c.glyphName = 'uni4E8C'; c.x = 0; c.y = 100; c.flags = 0x0001 | 0x0002
                comp.components = [c]; f['glyf']['uni4E09'] = comp
                for tag in ('prep', 'fpgm'):
                    t = newTable(tag); from fontTools.ttLib.tables import ttProgram; t.program = ttProgram.Program(); t.program.fromBytecode(b'\xb0\x00'); f[tag] = t
                f.save(custom)
            with TTFont(custom) as f: self.assertTrue(f['glyf']['uni4E09'].isComposite())
            original = base.read_bytes()
            font_override.compose(original, custom, out1); font_override.compose(original, custom, out2)
            self.assertEqual(out1.read_bytes(), out2.read_bytes())
            with TTFont(out1) as merged:
                glyf = merged['glyf']
                self.assertEqual([n for n in merged.getGlyphOrder() if glyf[n].isComposite()], [])
                self.assertFalse(any(t in merged for t in ('prep', 'fpgm', 'cvt ')))
                cmap = merged.getBestCmap(); self.assertGreater(glyf[cmap[0x4e09]].numberOfContours, 0)

    def test_size_cap_refuses_to_write_an_oversized_font(self):
        """四审真机：合并后 17.9 / 20.6 MiB 的字体让安卓主菜单闪退，15.6 MiB 正常；超过 FONT_BYTES_MAX 要报错且不留输出文件。
        Fourth-audit device runs: 17.9 / 20.6 MiB merged fonts crashed the Android main menu, 15.6 MiB worked; over FONT_BYTES_MAX must fail and leave no output."""
        self.assertLess(font_override.FONT_BYTES_MAX, 16 * 1024 * 1024)
        with tempfile.TemporaryDirectory() as work:
            base, custom, output = [Path(work) / x for x in ('base.ttf', 'custom.ttf', 'out.ttf')]
            synthetic(base, [65, 0x4e00], 1000, 'GameBase')
            synthetic(custom, [0x4e00], 1000, 'Requested')
            with patch.object(font_override, 'FONT_BYTES_MAX', 64):
                with self.assertRaisesRegex(ValueError, 'MiB'):
                    font_override.compose(base.read_bytes(), custom, output)
            self.assertFalse(output.exists())
            font_override.compose(base.read_bytes(), custom, output)
            self.assertTrue(output.is_file())

    def test_empty_mapped_letters_fall_back_without_changing_spaces(self):
        with tempfile.TemporaryDirectory() as work:
            base, custom, output = [Path(work)/x for x in ('base.ttf','custom.ttf','out.ttf')]
            synthetic(base,[32,65,0xf1],1000,'GameBase')
            synthetic(custom,[32,65,0xf1],1000,'Requested')
            for path, chars in ((base,[32]),(custom,[32,0xf1])):
                with TTFont(path) as font:
                    for c in chars: font['glyf'][font.getBestCmap()[c]]=TTGlyphPen(None).glyph()
                    font.save(path)
            before=(base.read_bytes(),custom.read_bytes())
            font_override.compose(before[0],custom,output)
            with TTFont(output) as merged:
                cmap=merged.getBestCmap()
                self.assertGreater(merged['glyf'][cmap[0xf1]].numberOfContours,0)
                self.assertEqual(merged['glyf'][cmap[32]].numberOfContours,0)
            self.assertEqual(before,(base.read_bytes(),custom.read_bytes()))

    def test_shared_non_cjk_codepoints_keep_game_glyphs(self):
        # 楷体等 GB 字库的数字比大写字母高、% 偏小，带重音拉丁 / 西里尔 / 希腊 / 通用标点（· ’ —）是全角：
        # 两边都有的码位只有中日文汉字、标点与全角形式取自选字体，其余保留游戏原字形（HUD「Lv 17 · 23%」错位、俄语 / 西语正文被撑开）
        # GB fonts such as the Kai face carry digits taller than capitals, a small %, and full-width accented Latin / Cyrillic / Greek /
        # general punctuation (· ’ —): for codepoints both fonts cover, only CJK ideographs, punctuation and full-width forms come from the
        # requested font, the rest keep the game glyphs (HUD "Lv 17 · 23%" misaligned, Russian / Spanish text spread apart)
        game = [0x31, 0x25, 0x4c, 0xb7, 0xe1, 0x416, 0x3a9, 0x2019, 0x2014]
        cjk = [0x4e00, 0x3002, 0xff1a, 0x3042]
        with tempfile.TemporaryDirectory() as work:
            base, custom, output = [Path(work) / x for x in ('base.ttf', 'custom.ttf', 'out.ttf')]
            synthetic(base, game + cjk, 1000, 'GameBase')
            synthetic(custom, game + cjk, 1000, 'Requested')
            with TTFont(base) as font:
                cmap = font.getBestCmap()
                for c in game + cjk:
                    font['hmtx'][cmap[c]] = (300, 0)
                font.save(base)
            font_override.compose(base.read_bytes(), custom, output)
            with TTFont(output) as merged:
                cmap = merged.getBestCmap()
                self.assertEqual({c: merged['hmtx'][cmap[c]][0] for c in game}, {c: 300 for c in game})
                self.assertEqual({c: merged['hmtx'][cmap[c]][0] for c in cjk}, {c: 1000 for c in cjk})

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
            synthetic(base, [65, 0x4e00, 0x4e8c], 1000, 'GameBase')
            synthetic(custom, [65, 0x4e8c], 2048, 'Requested')
            builder = FontBuilder(font=TTFont(custom))
            builder.setupVerticalMetrics({name: (2048, 0) for name in builder.font.getGlyphOrder()})
            builder.setupVerticalHeader(ascent=1536, descent=-512)
            builder.save(custom)
            font_override.compose(base.read_bytes(), custom, output)
            with TTFont(output) as merged:
                self.assertEqual(set(merged.getBestCmap()), {65, 0x4e00, 0x4e8c})
                self.assertNotIn('vhea', merged)

    def test_weight_thickens_requested_glyphs_only_and_keeps_metrics(self):
        with tempfile.TemporaryDirectory() as work:
            base, custom, plain, bold = [Path(work) / x for x in ('base.ttf', 'custom.ttf', 'plain.ttf', 'bold.ttf')]
            synthetic(base, [65, 0x4e00, 0x4e8c, 0x4e8d, 0xe001], 1000, 'GameBase')
            synthetic(custom, [65, 0x4e8c, 0x4e8d, 0xe001], 2048, 'Requested')
            data = base.read_bytes()
            font_override.compose(data, custom, plain)
            font_override.compose(data, custom, bold, weight=2)
            with TTFont(plain) as a, TTFont(bold) as b:
                ca, cb = a.getBestCmap(), b.getBestCmap()
                for c in (0x4e8c, 0x4e8d):   # 自选字形：更粗、字宽不变 | requested glyphs: thicker, same advance
                    ga, gb = a['glyf'][ca[c]], b['glyf'][cb[c]]
                    self.assertEqual(a['hmtx'][ca[c]][0], b['hmtx'][cb[c]][0])
                    self.assertGreater(gb.xMax - gb.xMin, ga.xMax - ga.xMin)
                    self.assertGreater(gb.yMax - gb.yMin, ga.yMax - ga.yMin)
                    self.assertLess(gb.xMin, ga.xMin)   # 回中：左右各长半个加粗量 | re-centred: grows on both sides
                    self.assertEqual(b['hmtx'][cb[c]][1], gb.xMin)
                    growth = (gb.xMax - gb.xMin) - (ga.xMax - ga.xMin)   # 直边各长半量，锐角顶点外推更多 | straight edges grow by half each; the acute vertex more
                    self.assertGreaterEqual(growth, 2 * font_override.WEIGHT_STEP)
                    self.assertLessEqual(growth, 4 * font_override.WEIGHT_STEP)
                for c in (65, 0x4e00, 0xe001):   # 原字体回退字形、共有的西文与图标：原样 | fallback glyphs, shared Western letters and icons: unchanged
                    self.assertEqual(list(a['glyf'][ca[c]].coordinates), list(b['glyf'][cb[c]].coordinates))
                    self.assertEqual(a['hmtx'][ca[c]], b['hmtx'][cb[c]])
                for key in ('ascent', 'descent', 'lineGap'):
                    self.assertEqual(getattr(a['hhea'], key), getattr(b['hhea'], key))
                self.assertEqual(b['head'].unitsPerEm, 1000)

    def test_weight_levels_grow_monotonically_and_zero_is_identity(self):
        with tempfile.TemporaryDirectory() as work:
            custom = Path(work) / 'custom.ttf'
            synthetic(custom, [65], 1000, 'Requested')
            widths = []
            for level in range(font_override.WEIGHT_MAX + 1):
                with TTFont(custom) as font:
                    name = font.getBestCmap()[65]
                    before = list(font['glyf'][name].coordinates)
                    font_override.embolden(font, level * font_override.WEIGHT_STEP)
                    glyph = font['glyf'][name]
                    if level == 0:
                        self.assertEqual(before, list(glyph.coordinates))
                    widths.append(glyph.xMax - glyph.xMin)
            self.assertEqual(widths, sorted(widths))
            self.assertEqual(len(set(widths)), len(widths))

    def test_weight_level_env_and_fingerprint(self):
        with tempfile.TemporaryDirectory() as work:
            font = Path(work) / 'input.ttf'
            synthetic(font, [65], 1000, 'Requested')
            digest = hashlib.sha256(font.read_bytes()).hexdigest()
            with patch.dict(os.environ, DM_UI_FONT=str(font), DM_UI_FONT_WEIGHT=''):
                self.assertEqual(font_override.weight_level(), 0)
                self.assertEqual(font_override.source_fingerprint(), digest)
                os.environ['DM_UI_FONT_WEIGHT'] = '2'
                self.assertEqual(font_override.weight_level(), 2)
                self.assertEqual(font_override.source_fingerprint(), digest + '+w2')
                for bad in ('x', '-1', str(font_override.WEIGHT_MAX + 1), '1.5'):
                    os.environ['DM_UI_FONT_WEIGHT'] = bad
                    with self.assertRaises(ValueError):
                        font_override.weight_level()

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
