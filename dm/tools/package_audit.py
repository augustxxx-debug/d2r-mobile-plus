#!/usr/bin/env python3
"""包内容审计（CLAUDE.md §2.8）：把 IPA / APK / .app 解到最里层（含 zip 内 zip）扫另一端的平台词，并列体积。
用法：package_audit.py <ios|android> <包或 .app 目录> [--out 报告.md]。有未登记的命中、或 src.zip 里有白名单外的文件，退出码 1。
不分大小写。CASC .data 里压缩存放的文字也解开扫（覆盖层），解不完就算不通过；给目录也行，比如手机端生成的 overlay。
命中只有这几种能放行：EXEMPT 里的第三方运行时与许可证原文（改不得）、手机端生成器源码（不是文案）、字体与图片的二进制数据（偶然的字节）；原生库里整串是登记过的标识符（SYMBOLS）；iOS 主程序内嵌的原版游戏映像段。
Package content audit: unpacks an IPA / APK / .app down to the innermost level (zips inside zips included), scans for the other platform's words and lists sizes.
Usage: package_audit.py <ios|android> <package or .app directory> [--out report.md]. Exit 1 on an unregistered hit, or on a file in src.zip outside the whitelist.
Any case. Text stored compressed in CASC .data (the overlay) is inflated and scanned too, and a stream that cannot be inflated fails the audit; a directory works as well, such as an overlay made on the phone.
Only these may pass: in EXEMPT, third-party runtimes and licence texts (not ours to change), the on-phone generator's source (not UI text), binary data of fonts and images (chance bytes); strings in native libraries that are registered identifiers as a whole (SYMBOLS); the original game image segments embedded in the iOS main program."""
import argparse, collections, importlib.util, io, re, struct, sys, zipfile, zlib
from pathlib import Path

DM = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(DM / 'ios/tools')); import platform_words
# (平台, 路径正则, 词, 理由)；词为 None = 该路径任何词都放行 | (platform, path pattern, word, reason); word None = any word passes for that path
EXEMPT = (
    ('android', r'assets/dmgen/runtime\.zip!.*', None, '第三方：Python 标准库与依赖，手机端生成器的运行时'),
    ('android', r'(assets/dmgen/THIRD_PARTY_NOTICES\.txt|assets/.*NOTICE.*|.*LICENSE.*)', None, '第三方许可证原文，必须随包且不得改'),
    ('android', r'lib/arm64-v8a/libdmgen_.*\.so', None, '第三方：Python、SPIRV-Tools、着色器翻译工具'),
    ('android', r'assets/dmgen/src\.zip!(?!dm/i18n/).*', None, '手机端生成器源码，不是文案；文件集由白名单管，文案表不放行，生成出来的覆盖层另扫'),
    ('android', r'.*\.(ttf|png|jpg|m4a)', None, '字体、图片、音频的二进制数据，偶然的字节不是文字'),
)


def game_image_ranges(data):
    """Mach-O 里内嵌原版游戏映像的段（__DMmainW / __DMsdkW）的文件区间：只有这里的命中才算原版自带（内审 K-05） | file ranges of the segments holding the embedded original game images: only hits there count as the original's own"""
    if data[:4] != b'\xcf\xfa\xed\xfe': return []
    ncmds = struct.unpack_from('<I', data, 16)[0]; off, out = 32, []
    for _ in range(ncmds):
        cmd, size = struct.unpack_from('<II', data, off)
        if cmd == 0x19 and data[off + 8:off + 24].rstrip(b'\0') in (b'__DMmainW', b'__DMsdkW'):
            fileoff, filesize = struct.unpack_from('<QQ', data, off + 40); out.append((fileoff, fileoff + filesize))
        off += size
    return out


# 原生库里允许带平台词的标识符，逐个登记：单词的界面文字（MetalFX、iCloud）不能靠「整串是标识符」混过去（内审 L-01）。多了新的就审一眼再加 | the identifiers in native libraries allowed to hold a platform word, each registered: single-word UI text (MetalFX, iCloud) must not pass as an identifier (internal audit L-01). Look before adding a new one
SYMBOLS = re.compile(rb'VK_STRUCTURE_TYPE_(EXPORT|IMPORT)_METAL_[A-Z_]+_EXT|VK_STRUCTURE_TYPE_(METAL|IOS)_SURFACE_CREATE_INFO_(EXT|MVK)|mtl_metalfx_(auto_selected|scale_hint)|dm_icloud_supported|icloud')


def is_symbol(data, start, end):
    """原生库里这个词所在的那一串整串是登记过的标识符（Vulkan 枚举名、公共代码的符号、设置键） | in a native library, the whole string holding the word is a registered identifier (Vulkan enum names, shared-code symbols, a settings key)"""
    s = data.rfind(b'\0', max(0, start - 4096), start) + 1; e = data.find(b'\0', end, end + 4096)
    return SYMBOLS.fullmatch(data[s:e if e >= 0 else end + 4096]) is not None


ARCHIVE_LIMIT = 1 << 31   # 一个 .data 解开后的总上限 2 GiB，超了算失败而不是少扫 | at most 2 GiB inflated per .data; beyond it the audit fails rather than scanning less
INCOMPLETE_MIN = 256     # 解到这么多还出错才算真有一段流；更短的是字节巧合长得像 zlib 头，比这还短的损坏流分不出来 | an error after this much means a real stream; shorter ones are chance bytes that look like a zlib header, and a damaged stream shorter than this cannot be told apart
FEED = 256


def split_archive(data, limit=ARCHIVE_LIMIT):
    """把 .data 分成解开的 zlib 段和其余原样字节，都按全部词扫；压缩流本身的字节丢掉，短词在里面只会碰巧命中（外审 X-03）。不解析档案格式：裸 n 条目、BLTE N 块、档案头都落在原样字节里。
    返回 (解开的, 原样的, 没解完的说明)；有说明就算审计失败。
    Splits a .data into inflated zlib segments and the remaining plain bytes, both scanned for every word; the compressed bytes themselves are dropped, short words there only match by chance (external review X-03). The archive format is not parsed: raw n entries, BLTE N chunks and headers all land in the plain bytes.
    Returns (inflated, plain, notes on what could not be inflated); any note fails the audit."""
    out, plain, notes, pos, kept, total = [], [], [], 0, 0, 0
    while True:
        pos = data.find(b'\x78', pos)
        if pos < 0 or pos + 2 > len(data): break
        if data[pos + 1] not in (0x01, 0x5e, 0x9c, 0xda) or (data[pos] << 8 | data[pos + 1]) % 31: pos += 1; continue
        d = zlib.decompressobj(); chunks, size, off, why = [], 0, pos, '被截断'
        try:
            while off < len(data) and not d.eof:
                feed = data[off:off + FEED]
                piece = d.decompress(feed, 1 << 24); chunks.append(piece); size += len(piece); off += len(feed) - len(d.unconsumed_tail) - len(d.unused_data)
                if total + size > limit: why = '超过 %d 字节上限' % limit; break
        except zlib.error:
            why = '数据错'
        if d.eof and total + size <= limit:
            out.append(b''.join(chunks)); total += size; plain.append(data[kept:pos]); pos = kept = off; continue
        if size >= INCOMPLETE_MIN or off - pos >= INCOMPLETE_MIN: notes.append('第 %d 字节起的 zlib 段没解完（%s，已解 %d 字节）' % (pos, why, size))
        if why.startswith('超过'): break
        pos += 1
    plain.append(data[kept:])
    return b'\0'.join(out), b''.join(plain), notes


INCOMPLETE = []   # walk 里解不完的 .data，audit 收尾时算进未登记 | .data files walk could not inflate; audit counts them as unregistered


def walk(source, prefix=''):
    """逐个产出 (路径, 字节)；zip 内的 zip 接着解，路径用 ! 连接 | yields (path, bytes); a zip inside a zip is unpacked too, paths joined with !"""
    for name, read in source:
        data = read()
        if name.lower().endswith(('.zip', '.apk', '.ipa', '.jar')):
            try:
                inner = zipfile.ZipFile(io.BytesIO(data))
            except zipfile.BadZipFile:
                yield prefix + name, data; continue
            yield from walk(((n, (lambda z=inner, n=n: z.read(n))) for n in inner.namelist() if not n.endswith('/')), prefix + name + '!')
        else:
            if name.lower().endswith('.data'):
                inflated, plain, notes = split_archive(data)
                yield prefix + name, plain
                yield prefix + name + '!zlib', inflated
                for note in notes: INCOMPLETE.append((prefix + name, note, 0))
            else:
                yield prefix + name, data


def source_of(package):
    p = Path(package)
    if p.is_dir(): return ((str(f.relative_to(p.parent)), f.read_bytes) for f in sorted(p.rglob('*')) if f.is_file() and not f.is_symlink())
    z = zipfile.ZipFile(p)
    return ((n, (lambda n=n: z.read(n))) for n in z.namelist() if not n.endswith('/'))


def exempt(platform, path, word):
    for plat, pat, w, why in EXEMPT:
        if plat == platform and (w is None or w == word) and re.fullmatch(pat, path): return why
    return None


def src_whitelist():
    spec = importlib.util.spec_from_file_location('pack_gen_assets', DM / 'android/tools/pack_gen_assets.py')
    m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
    return set(m.generator_files())


def audit(platform, package):
    words = platform_words.IOS_WORDS if platform == 'android' else platform_words.ANDROID_WORDS
    pats = [(w, platform_words.pattern(w)) for w in words]
    bad, passed, sizes, inner, by_file = [], collections.defaultdict(collections.Counter), collections.Counter(), [], collections.defaultdict(collections.Counter)
    allowed = src_whitelist() if platform == 'android' else None
    for path, data in walk(source_of(package)):
        sizes[path.split('!')[0]] += len(data)
        if '/src.zip!' in path:
            name = path.split('src.zip!', 1)[1]; inner.append((name, len(data)))
            if name not in allowed: bad.append((path, '白名单外的文件', 0))
        ranges = game_image_ranges(data) if platform == 'ios' else []
        native = path.endswith('.so') or bool(ranges)
        for w, pat in pats:
            spots = [m.span() for m in pat.finditer(data)]
            mine = [(s, e) for s, e in spots if not any(a <= s < b for a, b in ranges)]
            if len(mine) < len(spots): passed['iOS 主程序内嵌的原版游戏映像（__DMmainW / __DMsdkW），不是我们写的'][w] += len(spots) - len(mine)
            if native:
                names = [(s, e) for s, e in mine if is_symbol(data, s, e)]
                if names: passed['原生库的链接符号、枚举名与设置键，不是文字'][w] += len(names)
                mine = [x for x in mine if x not in names]
            n = len(mine)
            if not n: continue
            why = exempt(platform, path, w)
            if why:
                passed[why][w] += n
                if '/src.zip!' in path: by_file[path.split('src.zip!', 1)[1]][w] += n
            else: bad.append((path, w, n))
    bad += INCOMPLETE; INCOMPLETE.clear()
    return bad, passed, sizes, inner, by_file


def report(platform, package, bad, passed, sizes, inner, by_file):
    p = Path(package); out = ['# 包内容审计：%s `%s`' % (platform, p.name), '', '总大小 %.1f MiB（解开后 %.1f MiB）。' % (p.stat().st_size / 1048576 if p.is_file() else sum(sizes.values()) / 1048576, sum(sizes.values()) / 1048576), '']
    out += ['## 结论', '', '**不通过**：%d 处未登记。' % len(bad) if bad else '**通过**：没有未登记的平台词。', '']
    if bad:
        out += ['| 路径 | 词 | 次数 |', '|---|---|---|'] + ['| `%s` | %s | %d |' % b for b in bad[:200]] + ['']
    if passed:
        out += ['## 放行的命中', '', '| 理由 | 词与次数 |', '|---|---|'] + ['| %s | %s |' % (why, '、'.join('%s %d' % kv for kv in sorted(c.items()))) for why, c in passed.items()] + ['']
    if by_file:
        out += ['## `src.zip` 里放行的文件', '', '| 文件 | 词与次数 |', '|---|---|'] + ['| `%s` | %s |' % (f, '、'.join('%s %d' % kv for kv in sorted(c.items()))) for f, c in sorted(by_file.items())] + ['']
    out += ['## 体积（解开后，最大的 15 项）', '', '| 路径 | MiB |', '|---|---|'] + ['| `%s` | %.2f |' % (k, v / 1048576) for k, v in sizes.most_common(15)] + ['']
    if inner:
        groups = collections.Counter(); count = collections.Counter()
        for n, s in inner: g = '/'.join(n.split('/')[:3]); groups[g] += s; count[g] += 1
        out += ['## `src.zip` 内容（%d 个文件，解开后 %.2f MiB）' % (len(inner), sum(s for _, s in inner) / 1048576), '', '| 目录 | 文件数 | KiB |', '|---|---|---|'] + ['| `%s` | %d | %d |' % (g, count[g], s // 1024) for g, s in groups.most_common()] + ['']
    return '\n'.join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('platform', choices=('ios', 'android')); ap.add_argument('package'); ap.add_argument('--out', type=Path)
    a = ap.parse_args()
    bad, passed, sizes, inner, by_file = audit(a.platform, a.package)
    text = report(a.platform, a.package, bad, passed, sizes, inner, by_file)
    if a.out: a.out.write_text(text + '\n', encoding='utf-8')
    print('包内容审计 %s：%s（未登记 %d 处）' % (a.platform, '不通过' if bad else '通过', len(bad)))
    for b in bad[:20]: print('  %s  %s  %d' % b)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
