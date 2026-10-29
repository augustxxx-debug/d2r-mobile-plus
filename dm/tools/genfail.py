#!/usr/bin/env python3
# 生成失败时让日志自己说清原因（用户 2026-10-30：「日志分辨不出问题，传了日志有啥用」，缺陷台账 B026）：
#   root_causes / fail_report：某一步子进程失败时，从它的完整输出里挑出最里层的报错（Python 回溯的异常行，跳过外层包装的 CalledProcessError），
#     放在最前面，后面附该步输出的最后若干行（至少 80 行，stdout 与 stderr 合在一起）——以前只取最后一行，只看得到外层的「退出码 1」。
#   check_files / verify_hashes：按 dm/data/game_files_<版本>.tsv（49 个文件的字节数与 SHA-256）核对玩家放的游戏文件：开头只比大小（快），
#     缺文件 / 大小不对直接写成一句话；生成失败后再对大小一致的文件算一遍 SHA-256，耗时写进日志。
# 被 d2rm_pack.run（电脑上的 iOS 壳工具与安卓生成共用）与 d2rm_android.py 调；不依赖第三方包，桌面上单测（tests/test_genfail.py）。
# Make the log state the cause by itself when generation fails (user 2026-10-30, defect ledger B026):
#   root_causes / fail_report: when a step's child process fails, pick the innermost error out of its full output (the exception line of a Python traceback,
#     skipping the outer CalledProcessError wrapper), put it first, then append the last lines of the step's output (at least 80, stdout and stderr together)
#     -- previously only the last line was kept, which only showed the outer "exit code 1".
#   check_files / verify_hashes: check the player's game files against dm/data/game_files_<version>.tsv (bytes and SHA-256 of 49 files): sizes only at the start
#     (fast), a missing file / wrong size is written as one sentence; after a failure the files with the right size are hashed once, with the time in the log.
# Called by d2rm_pack.run (shared by the iOS shell tool on a computer and the Android generation) and d2rm_android.py; no third-party packages, unit tested on a desktop.
import hashlib, os, re, time

TAIL = 120          # 失败报告附该步输出的最后多少行（要求至少 80） | how many last lines of the step's output the failure report carries (at least 80 required)
LINE_MAX = 400      # 单行最长（超长的截断，防一行二进制垃圾撑爆日志） | longest line kept (longer ones are cut so one line of binary junk cannot blow up the log)
WRAPPERS = ('CalledProcessError', 'SystemExit', 'KeyboardInterrupt')
OPTIONAL = {'ExeFS/subsdk0'}   # 生成不读它：缺了只提一句，不算不完整 | not read by the generation: mentioned if missing, not counted as incomplete

_EXC = re.compile(r'^([A-Za-z_][\w.]*(?:Error|Exception|Exit|Interrupt|Warning)|AssertionError)\b(:.*)?$')


def _clip(s):
    return s if len(s) <= LINE_MAX else s[:LINE_MAX] + '…'


def root_causes(text, limit=6):
    """从一步的完整输出里挑出原因行，最里层的在前 | pick the cause lines out of a step's full output, innermost first

    次序：① 回溯的异常行里不是外层包装（CalledProcessError 等）的，按出现先后（子进程先打印，所以里层在前）；② 「[x] …」行与「【红】」行；
    ③ 其余像异常名开头的行；④ 外层包装的异常行（只在前面什么都没有时才有用）。去重，最多 limit 行。
    Order: (1) traceback exception lines that are not an outer wrapper, in order of appearance (child processes print first, so the inner ones come first);
    (2) "[x] ..." lines and lines with 【红】; (3) other lines that start like an exception name; (4) wrapper exception lines (useful only when nothing came before). Deduplicated, at most limit lines."""
    lines = text.splitlines(); tb, mark, exc, wrap = [], [], [], []
    i = 0
    while i < len(lines):
        if lines[i].startswith('Traceback (most recent call last):'):
            j = i + 1
            while j < len(lines) and (lines[j][:1] in (' ', '\t') or not lines[j].strip()): j += 1
            if j < len(lines):
                e = lines[j].strip()
                # 只有异常名、没有消息（如 assert 失败的 AssertionError）：带上触发它的那行代码 | a bare exception name with no message (e.g. AssertionError from a failed assert): add the code line that raised it
                if ':' not in e and j - 1 > i and lines[j - 1].strip() and not set(lines[j - 1].strip()) <= set('^~ '): e += '（' + lines[j - 1].strip() + '）'
                elif ':' not in e and j - 2 > i and set(lines[j - 1].strip()) <= set('^~ '): e += '（' + lines[j - 2].strip() + '）'
                (wrap if e.split(':', 1)[0].split('（')[0].split('.')[-1] in WRAPPERS else tb).append(e)
            i = j + 1; continue
        s = lines[i].strip()
        if s.startswith('[x]') or '【红】' in s: mark.append(s)
        elif _EXC.match(s): (wrap if s.split(':', 1)[0].split('.')[-1] in WRAPPERS else exc).append(s)
        i += 1
    out = []
    for s in tb + mark + exc + wrap:
        s = _clip(s)
        if s not in out: out.append(s)
    return out[:limit]


def fail_report(what, code, text, log=None, tail=TAIL):
    """一步失败的报告（die 的正文）：一句话、根因行、该步输出的最后 tail 行 | the report of a failed step (the body passed to die): one sentence, cause lines, the last tail lines of the step's output"""
    lines = text.splitlines(); causes = root_causes(text); last = lines[-tail:]
    r = ['%s 没成功（退出码 %d）' % (what, code)]
    r.append('根因（最里层的报错）：' + ('' if causes else '输出里没有找到报错行，见下面的输出末尾'))
    r += ['  ' + c for c in causes]
    r.append('----- 「%s」输出的最后 %d 行（共 %d 行%s）-----' % (what, len(last), len(lines), '，完整输出 ' + log if log else ''))
    r += [_clip(l) for l in last]
    r.append('----- 以上 -----')
    return '\n'.join(r)


def load_manifest(path):
    """game_files_<版本>.tsv → [(相对路径, 字节数, sha256)] | game_files_<version>.tsv -> [(relative path, bytes, sha256)]"""
    rows = []
    with open(path, encoding='utf-8') as f:
        for l in f:
            if not l.strip() or l.startswith('#'): continue
            p, n, h = l.rstrip('\n').split('\t'); rows.append((p, int(n), h))
    return rows


def check_files(game, rows):
    """只比大小：返回 {'missing': [...], 'size': [(路径, 应为, 实为)], 'ok': [...], 'optional_missing': [...]}
    sizes only: returns {'missing': [...], 'size': [(path, expected, actual)], 'ok': [...], 'optional_missing': [...]}"""
    r = {'missing': [], 'size': [], 'ok': [], 'optional_missing': []}
    for p, n, _ in rows:
        f = os.path.join(game, p)
        if not os.path.isfile(f): r['optional_missing' if p in OPTIONAL else 'missing'].append(p); continue
        got = os.path.getsize(f)
        if got != n: r['size'].append((p, n, got))
        else: r['ok'].append(p)
    return r


def _shown(p):   # 玩家放文件的地方：ExeFS/… 与 data/…（手机上没有 RomFS 这一层） | where the player puts files: ExeFS/... and data/... (no RomFS level on the phone)
    return p[len('RomFS/'):] if p.startswith('RomFS/') else p


def _mb(n):
    return '%.1f MB' % (n / 1048576) if n >= 1048576 else '%d 字节' % n


def kind_of(res):
    """缺文件 / 大小不对 → 'files'；只有版本清单（build_manifest.psv / .build.config）大小不对、其余都对 → 'version'；都对 → ''
    missing / wrong size -> 'files'; only the version manifests (build_manifest.psv / .build.config) differ in size and everything else matches -> 'version'; all fine -> ''"""
    if res['missing']: return 'files'
    if not res['size']: return ''
    ver = {'RomFS/data/build_manifest.psv', 'RomFS/data/.build.config'}
    return 'version' if all(p in ver for p, _, _ in res['size']) else 'files'


def describe(res, total):
    """第一屏的几句话（玩家看得懂的说法） | the first-screen sentences (in words players understand)"""
    out = []
    for p in res['missing']:
        out.append('缺 %s（隐藏文件，要一起拷）' % _shown(p) if p.endswith('/.build.config') else '缺 %s' % _shown(p))
    for p, want, got in res['size']:
        out.append('%s 大小不对：应为 %d 字节，实为 %d 字节（%s）' % (_shown(p), want, got, '可能没拷完' if got < want else '可能不是 3.3.94012 的文件'))
    if not out: out.append('游戏文件 %d 个都在，大小都对' % len(res['ok']))
    if res['optional_missing']: out.append('（%s 没有也不影响）' % '、'.join(_shown(p) for p in res['optional_missing']))
    return out


def verify_hashes(game, rows, only=None, chunk=1 << 22):
    """对大小一致的文件算 SHA-256：返回 (内容不对的 [路径], 核了几个, 字节数, 秒) | SHA-256 of the files whose size matches: returns ([paths with wrong content], files checked, bytes, seconds)"""
    t0 = time.time(); bad = []; n = 0; total = 0
    for p, size, h in rows:
        if only is not None and p not in only: continue
        f = os.path.join(game, p)
        if not os.path.isfile(f) or os.path.getsize(f) != size: continue
        d = hashlib.sha256()
        with open(f, 'rb') as fh:
            for b in iter(lambda: fh.read(chunk), b''): d.update(b)
        n += 1; total += size
        if d.hexdigest() != h: bad.append(p)
    return bad, n, total, time.time() - t0


def describe_hashes(bad, n, total, secs):
    head = '内容核对（SHA-256）：%d 个文件 %s，用时 %.0f 秒' % (n, _mb(total), secs)
    if not bad: return [head + '：全部一致']
    return [head + '：%d 个内容不对' % len(bad)] + ['%s 内容不对（可能拷坏了，重新拷一次）' % _shown(p) for p in bad]
