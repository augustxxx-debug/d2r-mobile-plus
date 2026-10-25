"""可选主字体：保留原字体缺字回退和排版度量，所有产物只在 Gen 内生成。
Optional main font: retain fallback glyphs and layout metrics; generate only inside Gen.
"""
import copy
import hashlib
import io
import os
from pathlib import Path
import tempfile

GAME_FONT = 'data/hd/ui/fonts/blizzardglobal-v5_81.ttf'


def source_path():
    value = os.environ.get('DM_UI_FONT', '')
    if not value:
        return None
    path = Path(value).expanduser()
    if not path.is_file():
        raise ValueError('DM_UI_FONT must point to a readable local font file')
    return path


def source_fingerprint():
    path = source_path()
    return hashlib.sha256(path.read_bytes()).hexdigest() if path else 'original'


def compose(original, requested, output):
    """优先使用自选字体，原字体补齐缺字；不改写输入文件。 | Prefer the requested font, retain missing glyphs from the original, and leave inputs unchanged."""
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
    if custom['head'].unitsPerEm != base['head'].unitsPerEm:
        scale_upem(custom, base['head'].unitsPerEm)
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
    digest = hashlib.sha256(b'font-override-v1\0' + original + path.read_bytes()).hexdigest()
    if output.is_file() and stamp.is_file() and stamp.read_text() == digest:
        return str(output)
    with tempfile.TemporaryDirectory(dir=directory) as work:
        target = Path(work) / 'theme.ttf'
        compose(original, path, target)
        os.replace(target, output)
    stamp.write_text(digest)
    return str(output)
