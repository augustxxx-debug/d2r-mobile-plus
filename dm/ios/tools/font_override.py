"""可选主字体：只替换中日文字形（CJK_RANGES），西文、数字与缺字保留原字体，排版度量不变，所有产物只在 Gen 内生成。
Optional main font: replace CJK glyphs only (CJK_RANGES); Western letters, digits and missing glyphs keep the original font, layout
metrics stay; generate only inside Gen.

字重：`DM_UI_FONT_WEIGHT=0..8` 按档把自选字体的每一笔描边加深（每档 WEIGHT_STEP‰ em），用 FreeType FT_Outline_EmboldenXY 的
同一套点位外推算法做在轮廓上；加粗后轮廓回中，字宽（advance）和行高度量不变，原字体回退的字形与私用区图标不动。
Weight: `DM_UI_FONT_WEIGHT=0..8` darkens every stroke of the requested font by level (WEIGHT_STEP per mille em per level), using the
point-offset algorithm of FreeType's FT_Outline_EmboldenXY on the outlines; the result is re-centred so advances and line metrics stay
unchanged, and fallback glyphs from the original font and private-use icons are untouched.
"""
import copy
import hashlib
import io
import math
import os
from pathlib import Path
import tempfile

GAME_FONT = 'data/hd/ui/fonts/blizzardglobal-v5_81.ttf'
# 每档加深量（千分之一 em）：目的是把细笔画填实、不是做粗体。楷体横竖笔画约 35–60‰ em，20 px 字号下 1 档每笔只加约 0.1 px，
# 2–3 档把反走样发灰的细笔画补到接近一个整像素，6 档以上才接近"加粗"（FreeType 合成粗体用 em/24 ≈ 42‰）。
# Darkening per level in thousandths of an em: the aim is to fill in thin strokes, not to make a bold face. Kai stems measure about 35–60‰ em;
# at 20 px level 1 adds only ~0.1 px per stroke, levels 2–3 bring anti-aliased grey hairlines close to a full pixel, and only level 6 and
# above approach a synthetic bold (FreeType uses em/24 ≈ 42‰).
WEIGHT_STEP = 5
WEIGHT_MAX = 8
# 两边都有的码位里只有这些区段取自选字体：中日文部首、标点、假名、注音、汉字（含扩展与兼容区）、竖排 / 全角形式。
# GB 字库（楷体等）的拉丁 / 西里尔 / 希腊 / 通用标点多为全角，数字比大写字母高、% 偏小，游戏按原字体排的数字与西文会错位、被撑开；
# 这些码位一律保留游戏原字形。自选字体独有的码位（扩展汉字等）照样补进来。
# For codepoints both fonts cover, only these ranges come from the requested font: CJK radicals, punctuation, kana, bopomofo,
# ideographs (extensions and compatibility included), vertical / full-width forms. GB fonts (Kai and the like) carry mostly full-width
# Latin / Cyrillic / Greek / general punctuation, digits taller than capitals and a small %, which misalign and spread numbers and
# Western text laid out for the original font; those codepoints keep the game glyphs. Codepoints only the requested font has
# (extension ideographs and so on) are still added.
CJK_RANGES = ((0x2e80, 0x2fff), (0x3000, 0x30ff), (0x3100, 0x312f), (0x31a0, 0x31ff), (0x3200, 0x33ff),
              (0x3400, 0x4dbf), (0x4e00, 0x9fff), (0xf900, 0xfaff), (0xfe10, 0xfe1f), (0xfe30, 0xfe4f),
              (0xff00, 0xffef), (0x20000, 0x3ffff))


def is_cjk(codepoint):
    return any(lo <= codepoint <= hi for lo, hi in CJK_RANGES)


def source_path():
    value = os.environ.get('DM_UI_FONT', '')
    if not value:
        return None
    path = Path(value).expanduser()
    if not path.is_file():
        raise ValueError('DM_UI_FONT must point to a readable local font file')
    return path


def weight_level():
    """`DM_UI_FONT_WEIGHT` 档位，缺省 0 = 原轮廓。 | The `DM_UI_FONT_WEIGHT` level; default 0 keeps the outlines."""
    value = os.environ.get('DM_UI_FONT_WEIGHT', '').strip()
    if not value:
        return 0
    try:
        level = int(value)
    except ValueError:
        level = -1
    if not 0 <= level <= WEIGHT_MAX:
        raise ValueError('DM_UI_FONT_WEIGHT must be an integer level 0..%d' % WEIGHT_MAX)
    return level


def source_fingerprint():
    path = source_path()
    if not path:
        return 'original'
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    level = weight_level()
    return digest if level == 0 else '%s+w%d' % (digest, level)


def _embolden_points(points, half_x, half_y, truetype):
    """FreeType FT_Outline_EmboldenXY 的单条轮廓：每个点沿相邻两边的角平分线外推（半量已传入），锐角处限制位移防止线段翻转。
    One contour of FreeType's FT_Outline_EmboldenXY: each point moves along the bisector of its two edges (half strengths are passed in),
    with the shift limited at sharp turns so short segments do not fold over."""
    last = len(points) - 1
    i, j, k = last, 0, -1
    v_in, l_in, anchor, l_anchor = (0.0, 0.0), 0.0, (0.0, 0.0), 0.0
    while j != i and i != k:
        if j != k:
            out = (points[j][0] - points[i][0], points[j][1] - points[i][1])
            l_out = math.hypot(out[0], out[1])
            if l_out == 0:
                j = j + 1 if j < last else 0
                continue
            out = (out[0] / l_out, out[1] / l_out)
        else:
            out, l_out = anchor, l_anchor
        if l_in != 0:
            if k < 0:
                k, anchor, l_anchor = i, v_in, l_in
            d = v_in[0] * out[0] + v_in[1] * out[1]
            if d > -0.9375:   # 转角小于约 160° 才位移 | shift only when the turn is less than ~160 degrees
                d += 1.0
                shift_x, shift_y = v_in[1] + out[1], v_in[0] + out[0]
                q = out[0] * v_in[1] - out[1] * v_in[0]
                if truetype:
                    shift_x, q = -shift_x, -q
                else:
                    shift_y = -shift_y
                limit = min(l_in, l_out)
                shift_x = shift_x * half_x / d if half_x * q <= limit * d else shift_x * limit / q
                shift_y = shift_y * half_y / d if half_y * q <= limit * d else shift_y * limit / q
            else:
                shift_x = shift_y = 0.0
            while i != j:
                points[i][0] += half_x + shift_x
                points[i][1] += half_y + shift_y
                i = i + 1 if i < last else 0
        else:
            i = j
        v_in, l_in = out, l_out
        j = j + 1 if j < last else 0


def _orientation_truetype(contours):
    """按 FreeType FT_Outline_Get_Orientation 用控制点多边形的有向面积判定轮廓方向。 | Outline direction from the signed area of the control polygon, as FreeType's FT_Outline_Get_Orientation does."""
    area = 0.0
    for points in contours:
        prev = points[-1]
        for cur in points:
            area += (cur[1] - prev[1]) * (cur[0] + prev[0])
            prev = cur
    return area <= 0


def embolden(font, strength):
    """把每条笔画加粗 strength 个字体单位，再把轮廓回中半个 strength：字宽不变，字形在格内居中变粗；复合字形随部件一起变化。
    Thicken every stroke by strength font units and re-centre the outline by half of it: advances stay, glyphs grow in place; composite
    glyphs follow their components."""
    if strength <= 0:
        return
    glyf, hmtx = font['glyf'], font['hmtx']
    half = strength / 2.0
    for name in font.getGlyphOrder():
        glyph = glyf[name]
        if glyph.isComposite() or glyph.numberOfContours <= 0:
            continue
        coords, ends = glyph.coordinates, glyph.endPtsOfContours
        contours, start = [], 0
        for end in ends:
            contours.append([[float(x), float(y)] for x, y in coords[start:end + 1]])
            start = end + 1
        truetype = _orientation_truetype(contours)
        for points in contours:
            _embolden_points(points, half, half, truetype)
        flat = [(round(x - half), round(y - half)) for points in contours for x, y in points]
        glyph.coordinates = type(coords)(flat)
    for name in font.getGlyphOrder():
        glyph = glyf[name]
        if glyph.numberOfContours == 0 and not glyph.isComposite():
            continue
        glyph.recalcBounds(glyf)
        hmtx[name] = (hmtx[name][0], glyph.xMin)
    if 'OS/2' in font:
        font['OS/2'].usWeightClass = min(900, font['OS/2'].usWeightClass + 100 * int(math.ceil(strength / (WEIGHT_STEP * font['head'].unitsPerEm / 1000.0))))


def compose(original, requested, output, weight=0):
    """中日文码位优先使用自选字体，其余与原字体缺字由原字体补齐；不改写输入文件。 | Prefer the requested font for CJK codepoints, retain the original glyphs for the rest and for missing glyphs, and leave inputs unchanged."""
    from fontTools import subset
    from fontTools.merge import Merger
    from fontTools.ttLib import TTFont
    from fontTools.ttLib.scaleUpem import scale_upem

    base = TTFont(io.BytesIO(original))
    custom = TTFont(requested)
    if 'glyf' not in custom or 'glyf' not in base or 'fvar' in custom:
        raise ValueError('Use a static TrueType font for this experimental override')
    base_chars = set(base.getBestCmap())
    custom_chars = set(custom.getBestCmap())
    if not custom_chars:
        raise ValueError('The requested font has no Unicode character map')
    # 私用区通常放游戏图标，不能让另一款游戏的字形覆盖它们。
    # Private-use codepoints often carry game icons; another game's glyphs must not replace them.
    symbols = {c for c in base_chars & custom_chars if 0xe000 <= c <= 0xf8ff or 0xf0000 <= c <= 0xffffd or 0x100000 <= c <= 0x10fffd}
    # Some fonts map accented letters to empty placeholders. Preserve real game glyphs
    # for those codepoints; genuine whitespace remains untouched when both are empty.
    base_map, custom_map = base.getBestCmap(), custom.getBestCmap()
    placeholders = {c for c in base_chars & custom_chars
                    if custom['glyf'][custom_map[c]].numberOfContours == 0
                    and base['glyf'][base_map[c]].numberOfContours != 0}
    western = {c for c in base_chars & custom_chars if not is_cjk(c)}
    protected = symbols | placeholders | western
    if protected:
        keep = subset.Subsetter()
        keep.populate(unicodes=custom_chars - protected)
        keep.subset(custom)
        custom_chars -= protected

    if custom['head'].unitsPerEm != base['head'].unitsPerEm:
        scale_upem(custom, base['head'].unitsPerEm)
    # 加粗只作用于自选字体的字形：原字体回退的字形和图标在下面合并时保持原样。
    # Weight applies to the requested font's glyphs only: fallback glyphs and icons merged below keep their outlines.
    if weight:
        embolden(custom, weight * WEIGHT_STEP * base['head'].unitsPerEm / 1000.0)
    # 游戏主字体没有竖排表；去掉自选字体独有的竖排表，避免合并器处理缺失表时失败。
    # The game main font has no vertical tables; remove custom-only vertical tables to avoid missing-table merge failures.
    if 'vhea' not in base:
        for tag in ('vhea', 'vmtx'):
            if tag in custom:
                del custom[tag]
    names = copy.deepcopy(base['name'])
    hhea = {key: getattr(base['hhea'], key) for key in ('ascent', 'descent', 'lineGap')}
    os2 = {key: getattr(base['OS/2'], key) for key in ('sTypoAscender', 'sTypoDescender', 'sTypoLineGap', 'usWinAscent', 'usWinDescent')}
    options = subset.Options()
    options.notdef_glyph = True
    fallback = subset.Subsetter(options=options)
    fallback.populate(unicodes=base_chars - custom_chars)
    fallback.subset(base)
    with tempfile.TemporaryDirectory() as work:
        primary = Path(work) / 'primary.ttf'
        secondary = Path(work) / 'fallback.ttf'
        custom.save(primary)
        base.save(secondary)
        merged = Merger().merge([str(primary), str(secondary)])
        # 原版注册名和行度量不变；字体版权记录沿用所选字体自身的记录。
        # Retain game registration names and line metrics; keep the requested font's copyright records.
        for record in names.names:
            if record.nameID in (1, 2, 4, 6, 16, 17):
                merged['name'].setName(record.toUnicode(), record.nameID, record.platformID, record.platEncID, record.langID)
        for key, value in hhea.items():
            setattr(merged['hhea'], key, value)
        for key, value in os2.items():
            setattr(merged['OS/2'], key, value)
        if not (base_chars | custom_chars) <= set(merged.getBestCmap()):
            raise ValueError('Font composition lost Unicode coverage')
        merged.save(output)
        merged.close()
    base.close()
    custom.close()


def prepare(romfs):
    """未配置时清掉旧字体产物；配置时按内容指纹重建。 | Clear stale output when disabled; otherwise rebuild by content fingerprint."""
    import gen_paths
    directory = Path(gen_paths.gen_dir('UIFonts'))
    output, stamp = directory / 'theme.ttf', directory / 'fingerprint.txt'
    path = source_path()
    if path is None:
        output.unlink(missing_ok=True)
        stamp.unlink(missing_ok=True)
        return None
    import romfs_art
    original = romfs_art.read(romfs, GAME_FONT)
    weight = weight_level()
    # 指纹带档位和每档的量：改标定也要重建 | The fingerprint carries the level and the per-level amount, so a recalibration rebuilds too
    digest = hashlib.sha256(b'font-override-v7\0' + ('w%d/%d\0' % (weight, WEIGHT_STEP)).encode() + original + path.read_bytes()).hexdigest()
    if output.is_file() and stamp.is_file() and stamp.read_text() == digest:
        return str(output)
    with tempfile.TemporaryDirectory(dir=directory) as work:
        target = Path(work) / 'theme.ttf'
        compose(original, path, target, weight)
        os.replace(target, output)
    stamp.write_text(digest)
    return str(output)


def main(argv=None):
    """命令行：在 Mac 上单独合成一份字体看效果（不经过 Gen 目录与指纹）。 | CLI: compose one font on a Mac for inspection, outside Gen and its fingerprint."""
    import argparse
    parser = argparse.ArgumentParser(description='Compose the optional UI font outside a build.')
    parser.add_argument('original', help='original game font (blizzardglobal-v5_81.ttf) or a RomFS directory')
    parser.add_argument('requested', nargs='?', default=None, help='requested font; default DM_UI_FONT')
    parser.add_argument('output')
    parser.add_argument('--weight', type=int, default=None, help='weight level 0..%d; default DM_UI_FONT_WEIGHT' % WEIGHT_MAX)
    args = parser.parse_args(argv)
    original = Path(args.original)
    if original.is_dir():
        import romfs_art
        data = romfs_art.read(str(original), GAME_FONT)
    else:
        data = original.read_bytes()
    requested = Path(args.requested) if args.requested else source_path()
    if requested is None:
        parser.error('no requested font: pass one or set DM_UI_FONT')
    weight = args.weight if args.weight is not None else weight_level()
    if not 0 <= weight <= WEIGHT_MAX:
        parser.error('--weight must be 0..%d' % WEIGHT_MAX)
    compose(data, requested, args.output, weight)
    print(args.output)


if __name__ == '__main__':
    main()
