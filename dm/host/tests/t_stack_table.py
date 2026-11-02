#!/usr/bin/env python3
# 药水堆叠：数据表与宿主的对账（秒级，不需要数据包）。
#   ① 上限只有一个数：dm/ios/tools/stack_potions.py 的 MAX_STACK 必须等于 dm/host/dm_stack_rule.h 的 DMSTK_CAP。
#      两处不一致时，表允许叠到 N 瓶、宿主按另一个数封顶 / 并叠，存档里就会出现超上限的叠或半截合并。
#   ② 补丁本身：拿一张合成表跑 stack_potions._misc / _types，逐行查四个数量列与两个标志、非药水行不许动；
#      表已经改过、行宽变了要报错（不静默过去）。
#   ③ 接线：这两张表要真的进覆盖层（gen_romfs_overlay.py 调它、overlay_contract.py 登记它），
#      否则改了表但包里还是原版表，游戏里一点变化都没有。
#   ④ 控件名：宿主按名字找数量框的按钮与角标（dm_stack_rule.h ↔ pouch_layout.py / wide_stash.py），写错一个字就点不到、角标不出现。
# Potion stacking: reconcile the data tables with the host (takes a second, no game data needed).
#   ① One cap only: MAX_STACK in dm/ios/tools/stack_potions.py must equal DMSTK_CAP in dm/host/dm_stack_rule.h.
#      When they differ the table allows N bottles while the host caps or merges at another number, and a save ends up with
#      stacks over the cap or half-finished merges.
#   ② The patch itself: run stack_potions._misc / _types on a synthetic table and check the four count columns, the two flags,
#      and that non-potion rows are untouched; an already-patched table or a changed row width must raise (no silent pass).
#   ③ Wiring: both tables really have to reach the overlay (gen_romfs_overlay.py calls it, overlay_contract.py lists it),
#      otherwise the table is changed but the package still ships the original and nothing changes in the game.
#   ④ Widget names: the host finds the quantity-box buttons and the badges by name (dm_stack_rule.h vs pouch_layout.py /
#      wide_stash.py); one wrong letter and the tap misses or the badge never appears.
import os, re, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
HOST = os.path.dirname(HERE)                  # dm/host
TOOLS = os.path.join(os.path.dirname(HOST), 'ios', 'tools')
sys.path.insert(0, TOOLS)
MISC_STRIDE, TYPE_STRIDE = 448, 232
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

    # ③ 接线：表要进覆盖层
    over = open(os.path.join(TOOLS, 'gen_romfs_overlay.py'), encoding='utf-8').read()
    check('import stack_potions' in over and 'stack_potions.build(' in over, 'gen_romfs_overlay.py 调 stack_potions.build')
    contract = open(os.path.join(TOOLS, 'overlay_contract.py'), encoding='utf-8').read()
    check('stack_potions.py' in contract, 'overlay_contract.py 登记了 stack_potions.py')

    # ④ 控件名：宿主找的名字布局里必须有
    pouch = open(os.path.join(TOOLS, 'pouch_layout.py'), encoding='utf-8').read()
    stash = open(os.path.join(TOOLS, 'wide_stash.py'), encoding='utf-8').read()
    rule = open(os.path.join(HOST, 'dm_stack_rule.h'), encoding='utf-8').read()
    for name in ('TouchQtyOne', 'TouchQtyHalf', 'TouchQtyAll', 'TouchQtySellOne', 'TouchQtySellHalf', 'TouchQtySellAll'):
        check(('"%s"' % name) in rule and ("'%s'" % name) in pouch, '数量框按钮 %s（规则表 ↔ 物品栏布局）' % name)
    for name in ('TouchQtyN%02d', 'TouchQtyBg', 'TouchQtyTitle'):
        check(name in pouch, '物品栏角标 / 拆分框控件 %s' % name)
    for name in ('TouchQtyStash', 'TouchQtyS%02d'):
        check(name in stash, '仓库角标控件 %s' % name)

    print('t_stack_table: %d failures' % fails)
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
