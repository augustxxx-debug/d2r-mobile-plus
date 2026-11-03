#!/usr/bin/env python3
# 药水堆叠：数据表与宿主的对账（秒级，不需要数据包）。
#   ① 上限只有一个数：dm/ios/tools/stack_potions.py 的 MAX_STACK 必须等于 dm/host/dm_stack_rule.h 的 DMSTK_CAP。
#      两处不一致时，表允许叠到 N 瓶、宿主按另一个数封顶 / 并叠，存档里就会出现超上限的叠或半截合并。
#   ② 补丁本身：拿一张合成表跑 stack_potions._misc / _types，逐行查四个数量列与两个标志、非药水行不许动；
#      表已经改过、行宽变了要报错（不静默过去）。
#   ③ 接线：这两张表要真的进覆盖层。以前这里只扫两个子串，把生产调用整行注释掉仍然 42 项全绿（审计 P2-02），
#      所以现在两步都真跑：用 fixture RomFS 调生产 stack_potions.wire（build 四张表 + REPL.update 消费），
#      再用 AST 看 gen_romfs_overlay.py 里那一次调用是不是真的可执行代码（注释里的同名子串不算）。
#   ④ 控件名与角标格数：宿主按名字找数量框的按钮与角标（dm_stack_rule.h ↔ pouch_layout.py / wide_stash.py），
#      写错一个字就点不到、角标不出现；DMSTK_BADGE_MAX 必须等于两份布局各生成的角标控件数。
# Potion stacking: reconcile the data tables with the host (takes a second, no game data needed).
#   ① One cap only: MAX_STACK in dm/ios/tools/stack_potions.py must equal DMSTK_CAP in dm/host/dm_stack_rule.h.
#      When they differ the table allows N bottles while the host caps or merges at another number, and a save ends up with
#      stacks over the cap or half-finished merges.
#   ② The patch itself: run stack_potions._misc / _types on a synthetic table and check the four count columns, the two flags,
#      and that non-potion rows are untouched; an already-patched table or a changed row width must raise (no silent pass).
#   ③ Wiring: both tables really have to reach the overlay. This used to be two substring scans, and commenting out the
#      production line still passed all 42 checks (audit P2-02). Now both halves are executed: a fixture RomFS drives the
#      production stack_potions.wire (build four tables + REPL.update consuming them), and an AST read of
#      gen_romfs_overlay.py proves that call is real code (the same substring inside a comment does not count).
#   ④ Widget names and badge capacity: the host finds the quantity-box buttons and the badges by name (dm_stack_rule.h vs
#      pouch_layout.py / wide_stash.py); one wrong letter and the tap misses or the badge never appears. DMSTK_BADGE_MAX
#      must equal the number of badge widgets both layouts generate.
import ast, hashlib, os, re, struct, sys, tempfile, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
HOST = os.path.dirname(HERE)                  # dm/host
TOOLS = os.path.join(os.path.dirname(HOST), 'ios', 'tools')
sys.path.insert(0, TOOLS)
MISC_STRIDE, TYPE_STRIDE = 448, 232
# 四张表在包内的逻辑路径（excel/ 与 excel/base/ 各一份） | logical paths of the four tables (one pair under excel/ and excel/base/)
TABLE_PATHS = ('data/global/excel/misc.bin', 'data/global/excel/base/misc.bin',
               'data/global/excel/itemtypes.bin', 'data/global/excel/base/itemtypes.bin')
fails = 0


def check(ok, what, detail=''):
    global fails
    if not ok:
        fails += 1
        print('FAIL %s%s' % (what, (': ' + detail) if detail else ''))
    else:
        print('ok   %s' % what)


def host_cap():
    for line in open(os.path.join(HOST, 'dm_stack_rule.h'), encoding='utf-8'):
        m = re.match(r'\s*#define\s+DMSTK_CAP\s+(\d+)\s*$', line)
        if m:
            return int(m.group(1))
    return None


def host_badge_max():
    for line in open(os.path.join(HOST, 'dm_stack_rule.h'), encoding='utf-8'):
        m = re.match(r'\s*#define\s+DMSTK_BADGE_MAX\s+(\d+)\s*$', line)
        if m:
            return int(m.group(1))
    return None


def misc_blob(codes, others=1):
    rows = len(codes) + others
    b = bytearray(struct.pack('<I', rows) + bytes(MISC_STRIDE * rows))
    for i, c in enumerate(codes):
        r = 4 + i * MISC_STRIDE
        b[r + 128:r + 131] = c
        struct.pack_into('<III', b, r + 236, 0, 0, 0)   # min / max / spawn
        b[r + 322] = 0                                 # stackable
        b[r + 339] = 1                                 # compactsave
    return b


def type_blob(codes, others=1):
    rows = len(codes) + others
    b = bytearray(struct.pack('<I', rows) + bytes(TYPE_STRIDE * rows))
    for i, c in enumerate(codes):
        r = 4 + i * TYPE_STRIDE
        b[r:r + 4] = c
        b[r + 19] = 0                                  # AutoStack
    return b


def rows_of(b, stride):
    n = struct.unpack_from('<I', b, 0)[0]
    return n, [(4 + i * stride, bytes(b[4 + i * stride:4 + (i + 1) * stride])) for i in range(n)]


def fixture_romfs(root, tables):
    """造一个最小 RomFS：data/build_manifest.psv + 一个 zlib 数据档，格式按 touch_layout._read_entry
    （|4 = 解压后 md5、|6 = 16 字节键（[9][10] 是档号、[11:16] 是偏移）、|7 = 压缩长度、|8 = 'z'、|9 = 包内逻辑路径）。
    A minimal RomFS: data/build_manifest.psv plus one zlib data archive, in the format touch_layout._read_entry expects."""
    data = os.path.join(root, 'data')
    os.makedirs(data, exist_ok=True)
    blob, lines = b'', []
    for logical, payload in tables:
        comp = zlib.compress(bytes(payload))
        key = bytes(9) + bytes((1, 0)) + len(blob).to_bytes(5, 'big')
        lines.append('data:%s|0|0|5|%s|%d|%s|%d|z|data:%s|table' % (
            logical, hashlib.md5(payload).hexdigest(), len(comp), key.hex(), len(comp), logical))
        blob += comp
    open(os.path.join(data, '%02d-%08d.data' % (1, 0)), 'wb').write(blob)
    open(os.path.join(data, 'build_manifest.psv'), 'w', encoding='utf-8').write('\n'.join(lines) + '\n')


def wiring_calls(src):
    """gen_romfs_overlay.py 里真正会执行的 stack_potions 调用：[(函数名, 行号, [参数源码], 作用域)]，
    作用域 'module' = 生成器执行时会走到（含 with / for / if 里），'function' = 在函数定义里，没人调就不执行。
    注释不在 AST 里，所以“整行注释掉但子串还在”的变异看不见 —— 这正是 P2-02 的反例。
    The stack_potions calls that really execute in gen_romfs_overlay.py: name, line, argument source, scope.
    Scope 'module' means the generator reaches it while running (also inside with / for / if); 'function' means
    it sits in a def and only runs if called. Comments are not in the AST, so the "commented out but the
    substring is still there" mutation is invisible — exactly the P2-02 counterexample."""
    tree = ast.parse(src)
    parents = {}
    for n in ast.walk(tree):
        for c in ast.iter_child_nodes(n):
            parents[c] = n
    calls = []
    for node in ast.walk(tree):
        if not (isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute)
                and isinstance(node.func.value, ast.Name) and node.func.value.id == 'stack_potions'):
            continue
        p, scope = parents.get(node), 'module'
        while p is not None:
            if isinstance(p, (ast.FunctionDef, ast.AsyncFunctionDef, ast.Lambda)):
                scope = 'function'
                break
            p = parents.get(p)
        calls.append((node.func.attr, node.lineno, [ast.unparse(a) for a in node.args], scope))
    return calls


def badge_widgets(path, fmt):
    """布局生成器里按该名字生成的角标控件数（找 `for ... in range(N)` / 列表推导里的 range(N) 与名字格式）。
    How many badge widgets of that name a layout generator makes (the range(N) enclosing the name format)."""
    tree = ast.parse(open(path, encoding='utf-8').read())
    parents = {}
    for n in ast.walk(tree):
        for c in ast.iter_child_nodes(n):
            parents[c] = n
    found = set()
    for node in ast.walk(tree):
        if not (isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and node.func.id == 'range' and node.args):
            continue
        arg = node.args[0]
        if not (isinstance(arg, ast.Constant) and isinstance(arg.value, int)):
            continue
        p, named = parents.get(node), False
        while p is not None and not named:
            if isinstance(p, (ast.For, ast.ListComp, ast.comprehension)):
                named = any(isinstance(c, ast.Constant) and isinstance(c.value, str) and fmt in c.value for c in ast.walk(p))
            p = parents.get(p)
        if named:
            found.add(arg.value)
    return found


def main():
    import stack_potions as sp
    cap_c = host_cap()
    check(cap_c == 99, '宿主上限 DMSTK_CAP = 99', str(cap_c))
    check(sp.MAX_STACK == cap_c, '数据表上限与宿主上限一致', '%s vs %s' % (sp.MAX_STACK, cap_c))

    # ② 补丁：合成的 misc.bin / itemtypes.bin
    codes = sorted(sp.CODES)
    check(len(codes) == 15, '要改 15 个药水码', str(len(codes)))
    check(codes == [b'hp1', b'hp2', b'hp3', b'hp4', b'hp5', b'mp1', b'mp2', b'mp3', b'mp4', b'mp5', b'rvl', b'rvs', b'vps', b'wms', b'yps'],
          '药水码清单（红 1–5、蓝 1–5、紫小/大、体力、解冻、解毒）', repr(codes))
    m = misc_blob(codes)
    untouched_before = dict(rows_of(m, MISC_STRIDE)[1])[4 + len(codes) * MISC_STRIDE]
    sp._misc(m)
    n, rows = rows_of(m, MISC_STRIDE)
    for i, c in enumerate(codes):
        r = 4 + i * MISC_STRIDE
        mn, mx, spawn = struct.unpack_from('<III', m, r + 236)
        check((mn, mx, spawn) == (1, sp.MAX_STACK, 1) and m[r + 322] == 1 and m[r + 339] == 0,
              '%s 改成 可堆叠 / 不简单 / 1..%d / 每次出 1' % (c.decode(), sp.MAX_STACK),
              'min %d max %d spawn %d stackable %d compactsave %d' % (mn, mx, spawn, m[r + 322], m[r + 339]))
    check(dict(rows)[4 + len(codes) * MISC_STRIDE] == untouched_before, '非药水行一字未动')
    try:
        sp._misc(m)
        check(False, '表已经改过时再补一次要报错')
    except SystemExit:
        check(True, '表已经改过时再补一次要报错（不静默）')
    try:
        sp._misc(bytearray(struct.pack('<I', 2) + bytes(400 * 2)))
        check(False, '行宽不是 448 要报错')
    except SystemExit:
        check(True, '行宽不是 448 要报错（原版表变了会当场停）')

    t = type_blob(sorted(sp.TYPES))
    other_before = dict(rows_of(t, TYPE_STRIDE)[1])[4 + len(sp.TYPES) * TYPE_STRIDE]
    sp._types(t)
    for i, c in enumerate(sorted(sp.TYPES)):
        r = 4 + i * TYPE_STRIDE
        check(t[r + 19] == 1, '%s 开 AutoStack（捡起自动并叠）' % c.decode(), str(t[r + 19]))
    check(dict(rows_of(t, TYPE_STRIDE)[1])[4 + len(sp.TYPES) * TYPE_STRIDE] == other_before, '非药水类型行一字未动')

    # ③ 接线：生成器里真有这一次调用（AST），且生产函数按同一条路真跑一遍（fixture RomFS）
    #    AST: the generator really calls it; fixture RomFS: the production function really runs the same path
    src = open(os.path.join(TOOLS, 'gen_romfs_overlay.py'), encoding='utf-8').read()
    calls = [(c[0], c[2]) for c in wiring_calls(src)]
    scopes = {c[3] for c in wiring_calls(src)}
    check(('wire', ['romfs', 't', 'REPL']) in calls, 'gen_romfs_overlay.py 真调 stack_potions.wire(romfs, t, REPL)', repr(calls))
    check(scopes == {'module'}, '这一次调用在生成器的执行路径上（不在没人调的函数里）', repr(scopes))
    check(not any(name == 'build' for name, _ in calls), '生成器不绕过 wire 直接调 build（build 不登记 REPL）', repr(calls))
    commented = '# ' + '\n# '.join(l.strip() for l in src.splitlines() if 'stack_potions.wire(' in l) + '\n'
    check(wiring_calls(commented) == [], '反例：生产调用整行注释后 AST 看不见它（旧子串检查在这里会绿）', repr(wiring_calls(commented)))
    mocked = [(c[0], c[2]) for c in wiring_calls(src.replace('wire(romfs,t,REPL)', 'wire(romfs,t,{})'))]
    check(('wire', ['romfs', 't', 'REPL']) not in mocked, '反例：不把真 REPL 交给 wire 时正检查不认', repr(mocked))
    contract = open(os.path.join(TOOLS, 'overlay_contract.py'), encoding='utf-8').read()
    check('stack_potions.py' in contract, 'overlay_contract.py 登记了 stack_potions.py')
    import overlay_contract
    check('dm/ios/tools/stack_potions.py' in overlay_contract.SOURCE_INPUTS, 'overlay_contract.SOURCE_INPUTS 真把 stack_potions.py 算进源清单（注释里的名字不算）')

    tables = [(p, misc_blob(codes) if p.endswith('misc.bin') else type_blob(sorted(sp.TYPES))) for p in TABLE_PATHS]
    with tempfile.TemporaryDirectory(prefix='t_stack_table_') as tmp:
        romfs, outdir = os.path.join(tmp, 'romfs'), os.path.join(tmp, 'out')
        os.makedirs(outdir)
        fixture_romfs(romfs, tables)
        repl = {}
        outmap, stats = sp.wire(romfs, outdir, repl)
        check(stats == {'files': 4} and len(outmap) == 4, '生产 wire 从 RomFS 建出四张表', repr(stats))
        check(sorted(outmap) == sorted(p.lower() for p in TABLE_PATHS), '四张表的逻辑路径与 RomFS 里的四份对应', repr(sorted(outmap)))
        check(sorted(repl) == sorted(p.lower() for p in TABLE_PATHS), '覆盖层 REPL 收到并登记这四张表', repr(sorted(repl)))
        check(all(os.path.isfile(repl[k]) and open(repl[k], 'rb').read() == open(outmap[k], 'rb').read() for k in repl),
              'REPL 里登记的就是刚写出的那四个文件')
        check(sorted(os.path.basename(repl[k]) for k in repl) ==
              ['sp_base_itemtypes.bin', 'sp_base_misc.bin', 'sp_itemtypes.bin', 'sp_misc.bin'],
              '四张表的文件名', repr(sorted(os.path.basename(repl[k]) for k in repl)))
        for logical, payload in tables:
            is_misc = logical.endswith('misc.bin')
            stride = MISC_STRIDE if is_misc else TYPE_STRIDE
            want = bytearray(payload)
            (sp._misc if is_misc else sp._types)(want)
            got = open(outmap[logical.lower()], 'rb').read()
            check(got == bytes(want), '%s 内容 = 生产规则算出来的表' % os.path.basename(logical))
            cnt, out_rows = rows_of(got, stride)
            targets = set(codes if is_misc else sp.TYPES)
            seen, others_ok = set(), True
            for i in range(cnt):
                off = 4 + i * stride
                code = out_rows[i][1][128:131] if is_misc else out_rows[i][1][:4]
                if code in targets:
                    seen.add(code)
                    ok = (struct.unpack_from('<III', got, off + 236) == (1, sp.MAX_STACK, 1) and got[off + 322] == 1
                          and got[off + 339] == 0) if is_misc else got[off + 19] == 1
                    detail = ('min %d max %d spawn %d stackable %d compactsave %d' % (
                        struct.unpack_from('<I', got, off + 236)[0], struct.unpack_from('<I', got, off + 240)[0],
                        struct.unpack_from('<I', got, off + 244)[0], got[off + 322], got[off + 339])) if is_misc \
                        else ('AutoStack %d' % got[off + 19])
                    check(ok, '%s %s：目标行走过生产 build' % (os.path.basename(logical), code.decode()), detail)
                elif out_rows[i][1] != payload[off:off + stride]:
                    others_ok = False
            check(seen == targets, '%s：%d 个目标行都在（缺一个都会静默漏改）' % (os.path.basename(logical), len(targets)), repr(sorted(targets - seen)))
            check(others_ok, '%s：非目标行一字未动' % os.path.basename(logical))
        # 反例：RomFS 里的表缺一个药水码，生产 build 必须当场报错（不许静默出一份半改的表）
        # Counterexample: a table missing one potion code must make the production build raise (no silent half-patched table)
        romfs2, outdir2 = os.path.join(tmp, 'romfs_missing'), os.path.join(tmp, 'out2')
        os.makedirs(outdir2)
        fixture_romfs(romfs2, [(p, misc_blob(codes[1:]) if p.endswith('misc.bin') else type_blob(sorted(sp.TYPES))) for p in TABLE_PATHS])
        try:
            sp.build(romfs2, outdir2)
            check(False, '缺药水码的 RomFS 表要让生产 build 报错（不静默）')
        except SystemExit:
            check(True, '缺药水码的 RomFS 表让生产 build 报错（不静默）')

    # ④ 控件名：宿主找的名字布局里必须有；角标格数与两份布局的控件数一致
    pouch = open(os.path.join(TOOLS, 'pouch_layout.py'), encoding='utf-8').read()
    stash = open(os.path.join(TOOLS, 'wide_stash.py'), encoding='utf-8').read()
    rule = open(os.path.join(HOST, 'dm_stack_rule.h'), encoding='utf-8').read()
    for name in ('TouchQtyOne', 'TouchQtyHalf', 'TouchQtyAll', 'TouchQtySellOne', 'TouchQtySellHalf', 'TouchQtySellAll'):
        check(('"%s"' % name) in rule and ("'%s'" % name) in pouch, '数量框按钮 %s（规则表 ↔ 物品栏布局）' % name)
    for name in ('TouchQtyN%02d', 'TouchQtyBg', 'TouchQtyTitle'):
        check(name in pouch, '物品栏角标 / 拆分框控件 %s' % name)
    for name in ('TouchQtyStash', 'TouchQtyS%02d'):
        check(name in stash, '仓库角标控件 %s' % name)
    badge_max = host_badge_max()
    check(badge_max == 24, '宿主角标格数 DMSTK_BADGE_MAX = 24', str(badge_max))
    check(badge_widgets(os.path.join(TOOLS, 'pouch_layout.py'), 'TouchQtyN%02d') == {badge_max},
          '物品栏布局的 TouchQtyN 控件数 = DMSTK_BADGE_MAX', repr(badge_widgets(os.path.join(TOOLS, 'pouch_layout.py'), 'TouchQtyN%02d')))
    check(badge_widgets(os.path.join(TOOLS, 'wide_stash.py'), 'TouchQtyS%02d') == {badge_max},
          '仓库布局的 TouchQtyS 控件数 = DMSTK_BADGE_MAX', repr(badge_widgets(os.path.join(TOOLS, 'wide_stash.py'), 'TouchQtyS%02d')))

    print('t_stack_table: %d failures' % fails)
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
