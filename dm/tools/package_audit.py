#!/usr/bin/env python3
"""包内容审计（CLAUDE.md §2.8）：把 IPA / APK / .app 解到最里层（含 zip 内 zip）扫另一端的平台词，并列体积。
用法：package_audit.py <ios|android> <包或 .app 目录> [--out 报告.md]。有未登记的命中、或 src.zip 里有白名单外的文件，退出码 1。
命中只有三种能放行，都写在下面的 EXEMPT：第三方运行时与许可证原文（改不得）；手机端生成器源码的注释（不是文案）；iOS 主程序里原版游戏自带的串（不是我们写的）。
Package content audit: unpacks an IPA / APK / .app down to the innermost level (zips inside zips included), scans for the other platform's words and lists sizes.
Usage: package_audit.py <ios|android> <package or .app directory> [--out report.md]. Exit 1 on an unregistered hit, or on a file in src.zip outside the whitelist.
Only three kinds of hit may pass, all listed in EXEMPT: third-party runtimes and licence texts (not ours to change); comments in the on-phone generator's source (not UI text); strings the original game ships inside the iOS main program (not written by us)."""
import argparse, collections, importlib.util, io, re, sys, zipfile
from pathlib import Path

DM = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(DM / 'ios/tools')); import platform_words
# (平台, 路径正则, 词, 理由)；词为 None = 该路径任何词都放行 | (platform, path pattern, word, reason); word None = any word passes for that path
EXEMPT = (
    ('android', r'assets/dmgen/runtime\.zip!.*', None, '第三方：Python 标准库与依赖，手机端生成器的运行时'),
    ('android', r'(assets/dmgen/THIRD_PARTY_NOTICES\.txt|assets/.*NOTICE.*|.*LICENSE.*)', None, '第三方许可证原文，必须随包且不得改'),
    ('android', r'lib/arm64-v8a/libdmgen_.*\.so', None, '第三方：Python、SPIRV-Tools、着色器翻译工具'),
    ('android', r'assets/dmgen/src\.zip!(?!dm/i18n/).*', None, '手机端生成器源码里的注释与目标名，不是文案；文件集由白名单管，文案表不放行'),
    ('ios', r'(Payload/)?DmIOS\.app/DmIOS|DmIOS', 'Vulkan', '原版游戏自带的串（NVN 调试回调旁），不是我们写的'),
    ('ios', r'(Payload/)?DmIOS\.app/DmIOS|DmIOS', 'Android', '原版游戏自带的串（NVRM / BufferQueue 旁），不是我们写的'),
)


def pattern(word):
    # 只认整词：Metal 不算 Metallic，iOS 不算 BIOS；MetalFX 这类后接大写的仍算 Metal | whole words only: Metallic is not Metal, BIOS is not iOS; MetalFX still counts as Metal
    if word.isascii() and word[0].isalpha(): return re.compile(rb'(?<![A-Za-z])' + re.escape(word.encode()) + rb'(?![a-z])')
    return re.compile(re.escape(word.encode()))


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
    pats = [(w, pattern(w)) for w in words]
    bad, passed, sizes, inner, by_file = [], collections.defaultdict(collections.Counter), collections.Counter(), [], collections.defaultdict(collections.Counter)
    allowed = src_whitelist() if platform == 'android' else None
    for path, data in walk(source_of(package)):
        sizes[path.split('!')[0]] += len(data)
        if '/src.zip!' in path:
            name = path.split('src.zip!', 1)[1]; inner.append((name, len(data)))
            if name not in allowed: bad.append((path, '白名单外的文件', 0))
        for w, pat in pats:
            n = len(pat.findall(data))
            if not n: continue
            why = exempt(platform, path, w)
            if why:
                passed[why][w] += n
                if '/src.zip!' in path: by_file[path.split('src.zip!', 1)[1]][w] += n
            else: bad.append((path, w, n))
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
