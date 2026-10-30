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
# 必需 = 生成器与运行时真正读的：主程序、sdk、.build.config、build_manifest.psv（casc_overlay 查路径）与 18 个资源档案（同运行时 romfs_validate.c 的 need[]）。
# 清单里其余的（subsdk0、06–16 配音档案、build_key.txt 与另外几张提取清单）只记日志、不拦——文档只要求玩家放这 18 个档案（八审 P1-1）
# Required = what the generator and the runtime actually read: main, sdk, .build.config, build_manifest.psv (casc_overlay looks up paths in it) and the 18
# resource archives (the same as need[] in the runtime's romfs_validate.c). The rest of the list (subsdk0, the 06-16 voice archives, build_key.txt and the other
# extraction manifests) is only logged, never blocking -- the docs ask players for these 18 archives only (eighth review P1-1)
ARCHIVES = tuple('%02d-0000000%d.data' % (c, k) for c in (0, 1, 2, 3, 4, 5, 17) for k in ((1, 2, 3, 4, 5, 6) if c == 0 else (1, 2)))
REQUIRED = {'ExeFS/main', 'ExeFS/sdk', 'RomFS/data/.build.config', 'RomFS/data/build_manifest.psv'} | {'RomFS/data/' + a for a in ARCHIVES}
# 3.3.94012 主程序 NSO 头 +0x40 的模块号（前 20 字节）：对不上 = 别的版本（八审 P2-3） | module id at +0x40 of the 3.3.94012 main NSO header (first 20 bytes):
# a mismatch = another version (eighth review P2-3)
MAIN_MODULE_ID = 'db0507f36b84200ac05f75d76ed5026f903e6c90'

_RELAY = re.compile(r'没成功（退出码 -?\d+）|退出码 -?\d+，它的输出见上')
_EXC = re.compile(r'^([A-Za-z_][\w.]*(?:Error|Exception|Exit|Interrupt|Warning)|AssertionError)\b(:.*)?$')


def _clip(s):
    return s if len(s) <= LINE_MAX else s[:LINE_MAX] + '…'


def root_causes(text, limit=6):
    """从一步的完整输出里挑出原因行，最里层的在前 | pick the cause lines out of a step's full output, innermost first

    次序：① 回溯的异常行里不是外层包装（CalledProcessError 等）的，按出现先后（子进程先打印，所以里层在前）；② 「[x] …」行与「【红】」行；
    ③ 其余像异常名开头的行；④ 外层包装与转述行——只在①–③一条都没有时才给。去重，最多 limit 行。
    Order: (1) traceback exception lines that are not an outer wrapper, in order of appearance (child processes print first, so the inner ones come first);
    (2) "[x] ..." lines and lines with 【红】; (3) other lines that start like an exception name; (4) wrapper and relay lines -- given only when (1)-(3) found nothing. Deduplicated, at most limit lines."""
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
        # 「[x] <某步> 没成功（退出码 N）」「…退出码 N，它的输出见上」只是外层在转述，算包装；警告不是原因，丢掉（八审 P2-4）
        # "[x] <step> 没成功（退出码 N）" and "...退出码 N，它的输出见上" only relay an outer failure and count as wrappers; warnings are not causes and are dropped (eighth review P2-4)
        if s.startswith('[x]') and _RELAY.search(s): wrap.append(s)
        elif s.startswith('[x]') or '【红】' in s: mark.append(s)
        elif _EXC.match(s) and s.split(':', 1)[0].endswith('Warning'): pass
        elif _EXC.match(s): (wrap if s.split(':', 1)[0].split('.')[-1] in WRAPPERS else exc).append(s)
        i += 1
    out = []
    for s in (tb + mark + exc) or wrap:   # 包装 / 转述行只在什么别的都没找到时才给（八审 P2-4） | wrapper / relay lines only when nothing else was found (eighth review P2-4)
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
    """只比大小。返回 {'missing', 'size': [(路径, 应为, 实为)], 'ok', 'optional', 'rename', 'holes', 'main_id'}：
    missing / size 只含必需文件；optional = 清单外可选文件里缺的或大小不对的（只记日志）；rename = 只有不带点的 build.config；
    holes = 旁边有 .data.holes 的剔零档案（生成器不认）；main_id = 主程序 NSO 模块号（读不出是 None）
    Sizes only. Returns {'missing', 'size': [(path, expected, actual)], 'ok', 'optional', 'rename', 'holes', 'main_id'}: missing / size cover required files only;
    optional = optional files that are missing or of a different size (logged only); rename = only an undotted build.config; holes = zero-stripped archives with a
    .data.holes next to them (the generator cannot read them); main_id = the main NSO module id (None when unreadable)"""
    r = {'missing': [], 'size': [], 'ok': [], 'optional': [], 'rename': [], 'holes': [], 'main_id': None}
    for p, n, _ in rows:
        f = os.path.join(game, p); req = p in REQUIRED
        if not os.path.isfile(f):
            if p == 'RomFS/data/.build.config' and os.path.isfile(os.path.join(game, 'RomFS/data/build.config')): r['rename'].append(p); continue
            (r['missing'] if req else r['optional']).append(p); continue
        if req and os.path.isfile(f + '.holes'): r['holes'].append(p); continue
        got = os.path.getsize(f)
        if got == n: r['ok'].append(p)
        elif req: r['size'].append((p, n, got))
        else: r['optional'].append(p)
    try:
        with open(os.path.join(game, 'ExeFS/main'), 'rb') as fh: head = fh.read(0x60)
        if head[:4] == b'NSO0' and len(head) >= 0x54: r['main_id'] = head[0x40:0x54].hex()
    except OSError: pass
    return r


def _shown(p):   # 玩家放文件的地方：ExeFS/… 与 data/…（手机上没有 RomFS 这一层） | where the player puts files: ExeFS/... and data/... (no RomFS level on the phone)
    return p[len('RomFS/'):] if p.startswith('RomFS/') else p


def _mb(n):
    return '%.1f MB' % (n / 1048576) if n >= 1048576 else '%d 字节' % n


def kind_of(res):
    """'' = 能生成；'version' = 不是这一版（主程序模块号不同，或必需文件里两个以上偏大，或只有版本清单不同）；'files' = 缺 / 没拷完 / 要改名 / 剔零档案
    '' = can generate; 'version' = another version (main module id differs, or two or more required files are larger, or only the version manifests differ);
    'files' = missing / incompletely copied / needs renaming / zero-stripped archive"""
    if res['main_id'] and res['main_id'] != MAIN_MODULE_ID: return 'version'
    if res['missing'] or res['rename'] or res['holes']: return 'files'
    if not res['size']: return ''
    if sum(1 for _, want, got in res['size'] if got > want) >= 2: return 'version'
    ver = {'RomFS/data/build_manifest.psv', 'RomFS/data/.build.config'}
    return 'version' if all(p in ver for p, _, _ in res['size']) else 'files'


def describe(res, total):
    """第一屏的几句话（玩家看得懂的说法）；可选文件只在最后一句里记个数（带「不影响」，App 不放进失败页正文）
    the first-screen sentences (in words players understand); optional files only get a count in the last sentence (with 「不影响」, which the App keeps out of the failure screen body)"""
    out = []
    if res['main_id'] and res['main_id'] != MAIN_MODULE_ID: out.append('ExeFS/main 不是 Switch 版 3.3.94012 的主程序（模块号 %s…）' % res['main_id'][:12])
    for p in res['missing']:
        out.append('缺 %s（隐藏文件，要一起拷）' % _shown(p) if p.endswith('/.build.config') else '缺 %s' % _shown(p))
    for p in res['rename']: out.append('data/build.config 要改名为 .build.config（前面加一个点）')
    for p in res['holes']: out.append('%s 是剔零档案（旁边有 %s.holes），手机上生成要完整的档案：从电脑重新拷完整的 %s' % (_shown(p), os.path.basename(p), os.path.basename(p)))
    for p, want, got in res['size']:
        out.append('%s 大小不对：应为 %d 字节，实为 %d 字节（%s）' % (_shown(p), want, got, '可能没拷完' if got < want else '可能不是 3.3.94012 的文件'))
    if not out: out.append('需要的游戏文件 %d 个都在，大小都对' % sum(1 for p in res['ok'] if p in REQUIRED))
    if res['optional']: out.append('（另有 %d 个可选文件没放或大小不同：配音档案、提取清单等，生成用不到，不影响）' % len(res['optional']))
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
