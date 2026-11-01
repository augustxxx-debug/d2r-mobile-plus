#!/usr/bin/env python3
"""包内容审计（CLAUDE.md §2.8）：把 IPA / APK / .app 解到最里层（含 zip 内 zip）扫另一端的平台词，并列体积。
用法：package_audit.py <ios|android> <包或 .app 目录> [--out 报告.md]。有未登记的命中、或 src.zip 里有白名单外的文件，退出码 1。
不分大小写。CASC .data 里压缩存放的文字也解开扫（覆盖层：zlib、BLTE 的 N / Z 块），解不完、不认识的编码都算不通过；给目录也行，比如手机端生成的 overlay。
命中只有这几种能放行：EXEMPT 里的第三方运行时与许可证原文（改不得）、手机端生成器源码（不是文案）、字体与图片的二进制数据（偶然的字节）；原生库里整串是登记过的标识符（SYMBOLS）；iOS 主程序内嵌的原版游戏映像段。
Package content audit: unpacks an IPA / APK / .app down to the innermost level (zips inside zips included), scans for the other platform's words and lists sizes.
Usage: package_audit.py <ios|android> <package or .app directory> [--out report.md]. Exit 1 on an unregistered hit, or on a file in src.zip outside the whitelist.
Any case. Text stored compressed in CASC .data (the overlay: zlib, BLTE N / Z chunks) is decoded and scanned too, and a stream that cannot be decoded or an unknown encoding fails the audit; a directory works as well, such as an overlay made on the phone.
Only these may pass: in EXEMPT, third-party runtimes and licence texts (not ours to change), the on-phone generator's source (not UI text), binary data of fonts and images (chance bytes); strings in native libraries that are registered identifiers as a whole (SYMBOLS); the original game image segments embedded in the iOS main program."""
import argparse, collections, hashlib, importlib.util, io, os, re, struct, sys, zipfile, zlib
from pathlib import Path

DM = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(DM / 'ios/tools')); import platform_words
# (平台, 路径正则, 词, 理由)；词为 None = 该路径任何词都放行 | (platform, path pattern, word, reason); word None = any word passes for that path
EXEMPT = (
    ('android', r'assets/dmgen/runtime\.zip!(lib|py)/.*', None, '第三方：Python 标准库与依赖，手机端生成器的运行时（整包哈希核过 runtime.txt）'),
    ('android', r'(assets/dmgen/THIRD_PARTY_NOTICES\.txt|assets/.*NOTICE.*|.*LICENSE.*)', None, '第三方许可证原文，必须随包且不得改'),
    ('android', r'lib/arm64-v8a/libdmgen_(py|spvopt|spvval)\.so', None, '第三方：Python 启动器、SPIRV-Tools（文件哈希核过 runtime.txt）'),
    ('android', r'lib/arm64-v8a/libdmgen_xlate\.so', None, '本仓代码：手机端生成器的着色器翻译工具 dmsh_xlate，不是文案（文件哈希核过 runtime.txt）'),
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


def _text_byte_before(data, at):
    """at 前面紧邻的是文字：可打印 ASCII、空白，或正好在这里结束的 UTF-8 字符 | what sits right before at is text: printable ASCII, whitespace, or a UTF-8 character ending here"""
    if at <= 0: return True   # 文件头：没有证据说它不是文字 | start of file: no evidence it is not text
    c = data[at - 1]
    if c < 0x80: return c in b'\t\n\r' or 0x20 <= c < 0x7f
    for n in (2, 3, 4):
        if at >= n:
            try:
                if len(data[at - n:at].decode('utf-8')) == 1: return True
            except UnicodeDecodeError: pass
    return False


def _text_byte_after(data, at):
    """at 处开始的是文字（NUL 不算：它是串尾）| what starts at at is text (NUL is not: it ends a string)"""
    if at >= len(data): return True
    c = data[at]
    if c < 0x80: return c in b'\t\n\r' or 0x20 <= c < 0x7f
    for n in (2, 3, 4):
        try:
            if len(data[at:at + n].decode('utf-8')) == 1: return True
        except UnicodeDecodeError: pass
    return False


def is_chance_bytes(data, start, end):
    """原生库里这个词是调试信息等二进制段里偶然的字节，如 b'\\x93\\x01IOS\\0'（B113）。只看词紧邻的两边，不看整串：两边都不是文字、前面不是串头（NUL）、也不是长度前缀，才算偶然。
    In a native library, the word is chance bytes in debug info or another binary section (B113). Only the bytes right next to the word count, not the whole run: chance needs no text on either side, and the byte before must be neither a string start (NUL) nor a length prefix."""
    if start <= 0 or data[start - 1] == 0 or data[start - 1] == end - start: return False
    return not _text_byte_before(data, start) and not _text_byte_after(data, end)


ELF_BINARY_SECTIONS = {b'.text', b'.eh_frame', b'.eh_frame_hdr', b'.gcc_except_table', b'.debug_info', b'.debug_loc', b'.debug_abbrev', b'.debug_ranges', b'.debug_line', b'.debug_aranges', b'.symtab', b'.rela.dyn', b'.rela.plt', b'.relr.dyn', b'.got', b'.got.plt'}


def elf_binary_ranges(data):
    """ELF64 里存机器码和二进制结构的段（偏移区间）。这些段里的词只是字节，邻字判断在调试信息这种大块数据上会误报（如 .debug_info 的 b'\\0IOS@\\0'）；.rodata、.data、.debug_str、.dynstr 不在内，照旧按文字扫
    Sections of an ELF64 holding machine code and binary structures (offset ranges). A word there is only bytes, and the neighbour rule misfires on big blocks like debug info; .rodata, .data, .debug_str and .dynstr are not listed and are scanned as text"""
    if data[:5] != b'\x7fELF\x02' or len(data) < 64: return []
    try:
        shoff = struct.unpack_from('<Q', data, 0x28)[0]; shent, shnum, shstr = struct.unpack_from('<HHH', data, 0x3a)
        sec = [struct.unpack_from('<IIQQQQ', data, shoff + i * shent) for i in range(shnum)]; so = sec[shstr][4]; out = []
        for n, _t, _f, _a, off, size in sec:
            e = data.index(b'\0', so + n)
            if data[so + n:e] in ELF_BINARY_SECTIONS: out.append((off, off + size))
        return out
    except (struct.error, IndexError, ValueError): return []


RUNTIME_LIBS = {'libdmgen_py.so', 'libdmgen_spvopt.so', 'libdmgen_spvval.so', 'libdmgen_xlate.so'}   # gen_runtime.sh 产出的四个 | the four gen_runtime.sh produces
ARCHIVE_LIMIT = 1 << 31   # 一个 .data 解开后的总上限 2 GiB，超了算失败而不是少扫 | at most 2 GiB inflated per .data; beyond it the audit fails rather than scanning less
INCOMPLETE_MIN = 256     # 解到这么多还出错才明说是坏流；更短的留在原样字节里，由「未识别的二进制段」兜住 | an error after this much is named as a damaged stream; shorter ones stay in the plain bytes and fail as an unrecognized binary segment
FEED = 256
# 合法的 zlib 头：CM = 8、窗口 CINFO ≤ 7、FCHECK 对、没有预设字典（外审 X-04）；以及 BLTE 容器的魔数 | every legal zlib header: CM = 8, window CINFO ≤ 7, FCHECK right, no preset dictionary (external review X-04); and the BLTE container magic
ARCHIVE_MARK = re.compile(b'BLTE|' + b'|'.join(re.escape(bytes((c, f))) for c in range(0x08, 0x88, 0x10) for f in range(256) if (c << 8 | f) % 31 == 0 and not f & 0x20))
BINARY = re.compile(rb'[\x00-\x08\x0b\x0c\x0e-\x1f\x7f]')   # 文本里不该有的控制字符 | control characters text should not hold


def inflate(data, pos, end, budget):
    """从 pos 解一段 zlib，流式喂入防解压炸弹。返回 (解出的, 结束位置, 失败原因 | None) | inflates one zlib stream from pos, fed in small pieces against bombs. Returns (output, end position, reason or None)"""
    d = zlib.decompressobj(); chunks, size, off = [], 0, pos
    try:
        while off < end and not d.eof:
            feed = data[off:min(off + FEED, end)]
            piece = d.decompress(feed, 1 << 24); chunks.append(piece); size += len(piece); off += len(feed) - len(d.unconsumed_tail) - len(d.unused_data)
            if size > budget: return b''.join(chunks), off, '超过上限'
    except zlib.error:
        return b''.join(chunks), off, '数据错'
    return b''.join(chunks), off, None if d.eof else '被截断'


def parse_blte(data, pos, budget):
    """解一个 BLTE 容器：头表、每块的长度和 MD5 都要对；只认 N（原样）和 Z（zlib）。LZ4、加密、递归和未知编码解不开，明确失败，不当明文放过（外审 X-04）。
    返回 (解出的, 结束位置, 失败说明 | None)，头表字节不算内容 | Decodes one BLTE container: the table, each chunk's size and MD5 must hold; only N (plain) and Z (zlib) are known. LZ4, encryption, nesting and unknown modes cannot be decoded and fail outright instead of passing as plain text (external review X-04). Returns (output, end, failure or None); table bytes are not content."""
    if pos + 12 > len(data): return b'', pos + 4, 'BLTE 头表不全'
    head, flags, count = struct.unpack_from('>IBB', data, pos + 4)[0], data[pos + 8], int.from_bytes(data[pos + 9:pos + 12], 'big')
    if flags != 0x0f or not count or head != 12 + 24 * count or pos + head > len(data): return b'', pos + 4, 'BLTE 头表解析不了（头长 %d、标志 %#x、块数 %d）' % (head, flags, count)
    out, off, size = [], pos + head, 0
    for i in range(count):
        csize, usize = struct.unpack_from('>II', data, pos + 12 + 24 * i)
        if not csize or off + csize > len(data): return b''.join(out), pos + 4, 'BLTE 第 %d 块越界（长 %d）' % (i, csize)
        chunk = data[off:off + csize]; off += csize
        if hashlib.md5(chunk).digest() != data[pos + 16 + 24 * i + 4:pos + 16 + 24 * i + 20]: return b''.join(out), off, 'BLTE 第 %d 块 MD5 不符' % i
        mode = chunk[:1]
        if mode == b'N': piece = chunk[1:]
        elif mode == b'Z':
            piece, used, why = inflate(chunk, 1, len(chunk), budget - size)
            if why or used != len(chunk): return b''.join(out), off, 'BLTE 第 %d 块 zlib 没解完（%s）' % (i, why or '尾部有多余字节')
        else: return b''.join(out), off, 'BLTE 第 %d 块编码 %r 不支持，内容没扫' % (i, mode)
        if len(piece) != usize: return b''.join(out), off, 'BLTE 第 %d 块解出 %d 字节，头表写 %d' % (i, len(piece), usize)
        out.append(piece); size += len(piece)
        if size > budget: return b''.join(out), off, '超过上限'
    return b''.join(out), off, None


def split_archive(data, limit=ARCHIVE_LIMIT):
    """把 .data 分成解开的内容和其余原样字节，都按全部词扫；压缩流和 BLTE 头表本身的字节丢掉，短词在里面只会碰巧命中（外审 X-03）。
    原样字节里剩下的只能是文本（裸 n 条目）和首尾的 0 填充；不是文本的就是不认识的编码，算失败，不当明文扫完就放过（外审 X-04）。各段之间用 0 隔开，词不会跨段拼出来（内审 N-01）。
    返回 (解开的, 原样的, 覆盖缺口的说明)；有说明就算审计失败。
    Splits a .data into decoded content and the remaining plain bytes, both scanned for every word; the compressed bytes and BLTE tables themselves are dropped, short words there only match by chance (external review X-03).
    What remains plain may only be text (raw n entries) and NUL padding at its ends; anything else is an unknown encoding and fails rather than passing after a plain scan (external review X-04). Segments are joined with NUL so no word forms across them (internal audit N-01).
    Returns (decoded, plain, notes on coverage gaps); any note fails the audit."""
    out, plain, notes, pos, kept, total = [], [], [], 0, 0, 0
    while True:
        m = ARCHIVE_MARK.search(data, pos)
        if not m: break
        pos = m.start()
        if m.group() == b'BLTE':
            piece, end, why = parse_blte(data, pos, limit - total)
            if why: notes.append('第 %d 字节起：%s' % (pos, why))
            if why and '上限' in why: return b'\0'.join(out), b'\0'.join(plain), notes
            if end > pos + 4:   # 头表对得上才算容器，块内出错也已写进说明 | a table that holds makes it a container; errors inside are already noted
                out.append(piece); total += len(piece); plain.append(data[kept:pos]); pos = kept = end
            else: pos += 4
            continue
        piece, end, why = inflate(data, pos, len(data), limit - total)
        if why is None:
            out.append(piece); total += len(piece); plain.append(data[kept:pos]); pos = kept = end; continue
        if why.startswith('超过'):
            notes.append('第 %d 字节起的 zlib 段没解完（%s，已解 %d 字节）' % (pos, why, len(piece))); return b'\0'.join(out), b'\0'.join(plain), notes
        if len(piece) >= INCOMPLETE_MIN or end - pos >= INCOMPLETE_MIN: notes.append('第 %d 字节起的 zlib 段没解完（%s，已解 %d 字节）' % (pos, why, len(piece)))
        pos += 1
    plain.append(data[kept:])
    plain = [p.strip(b'\0') for p in plain]; plain = [p for p in plain if p]
    binary = []
    for p in plain:
        try: p.decode('utf-8')
        except UnicodeDecodeError: binary.append(p); continue
        if BINARY.search(p): binary.append(p)
    if binary: notes.append('有 %d 段共 %d 字节不是文本，也不是认识的压缩格式，内容没扫（开头 %s）' % (len(binary), sum(map(len, binary)), binary[0][:8].hex()))
    return b'\0'.join(out), b'\0'.join(plain), notes


def runtime_problems(package):
    """手机端生成器的运行时：runtime.zip 与 libdmgen_*.so 必须和生成时写的 runtime.txt 一字不差，libdmgen 只认登记的四个（B117）。返回未登记项
    The on-phone generator's runtime: runtime.zip and libdmgen_*.so must match the runtime.txt written at generation, and only the four registered libdmgen exist (B117). Returns unregistered items"""
    p = Path(package)
    if p.is_dir() or not zipfile.is_zipfile(p): return []
    z = zipfile.ZipFile(p); names = set(z.namelist()); bad = []
    libs = {n for n in names if re.fullmatch(r'lib/arm64-v8a/libdmgen_[^/]*\.so', n)}; has = 'assets/dmgen/runtime.zip' in names
    for n in sorted(libs):
        if os.path.basename(n) not in RUNTIME_LIBS: bad.append((n, '未登记的 libdmgen 文件', 0))
    if not has and not libs: return bad
    if 'assets/dmgen/runtime.txt' not in names: return bad + [('assets/dmgen/runtime.txt', '没有生成时的清单', 0)]
    want = dict(reversed(ln.split(None, 1)) for ln in z.read('assets/dmgen/runtime.txt').decode('utf-8', 'replace').splitlines() if re.match(r'[0-9a-f]{64}\s', ln))
    if has:
        if hashlib.sha256(z.read('assets/dmgen/runtime.zip')).hexdigest() != want.get('assets/dmgen/runtime.zip'): bad.append(('assets/dmgen/runtime.zip', '和 runtime.txt 的哈希不符', 0))
        for n in zipfile.ZipFile(io.BytesIO(z.read('assets/dmgen/runtime.zip'))).namelist():
            if not n.endswith('/') and not n.startswith(('lib/', 'py/')): bad.append(('assets/dmgen/runtime.zip!' + n, 'lib/ py/ 之外的文件', 0))
    for n in sorted(libs):
        if os.path.basename(n) in RUNTIME_LIBS and hashlib.sha256(z.read(n)).hexdigest() != want.get('jniLibs/arm64-v8a/' + os.path.basename(n)): bad.append((n, '和 runtime.txt 的哈希不符', 0))
    return bad


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
        elf = None
        for w, pat in pats:
            spots = [m.span() for m in pat.finditer(data)]
            mine = [(s, e) for s, e in spots if not any(a <= s < b for a, b in ranges)]
            if len(mine) < len(spots): passed['iOS 主程序内嵌的原版游戏映像（__DMmainW / __DMsdkW），不是我们写的'][w] += len(spots) - len(mine)
            if native:
                if path.endswith('.so'):
                    if elf is None: elf = elf_binary_ranges(data)
                    inbin = [(s, e) for s, e in mine if any(a <= s < b for a, b in elf)]
                    if inbin: passed['ELF 机器码和调试结构段里的字节，不是文字'][w] += len(inbin)
                    mine = [x for x in mine if x not in inbin]
                names = [(s, e) for s, e in mine if is_symbol(data, s, e)]
                if names: passed['原生库的链接符号、枚举名与设置键，不是文字'][w] += len(names)
                mine = [x for x in mine if x not in names]
                chance = [(s, e) for s, e in mine if is_chance_bytes(data, s, e)]
                if chance: passed['原生库二进制段里偶然的字节，不是文字'][w] += len(chance)
                mine = [x for x in mine if x not in chance]
            n = len(mine)
            if not n: continue
            why = exempt(platform, path, w)
            if why:
                passed[why][w] += n
                if '/src.zip!' in path: by_file[path.split('src.zip!', 1)[1]][w] += n
            else: bad.append((path, w, n))
    bad += INCOMPLETE; INCOMPLETE.clear()
    if platform == 'android': bad += runtime_problems(package)
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
