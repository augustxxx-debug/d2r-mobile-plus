#!/usr/bin/env python3
# 药水真堆叠（用户 2026-10-31 定，上限 99）：覆盖层里改两份 misc.bin 与 itemtypes.bin，走法同 wide_stash.py 的 inventory.bin（读玩家 romfs、改字节、写进覆盖层）。
#   misc.bin 每行 448 字节（表头 4 字节）：+128 物品码、+236 minstack、+240 maxstack、+244 spawnstack（u32）、+322 stackable、+339 compactsave（与 misc.txt 逐行对出）。
#     compactsave 要清零：简单物品没有数量字段，数量落不了盘。旧存档里的简单药水仍按自身标志读，读进来是 1 瓶。
#   itemtypes.bin 每行 232 字节：+0 类型码、+19 AutoStack（捡起自动并叠，钥匙就是这一列）。
#   只改表时喝一口整叠就没了（原版使用物品后删整件），宿主服务端使用入口负责只扣 1（dm_stack.c）。
#   这两张表进联机 manifest：四端必须同一提交，不能做成设置开关；设置项只控交互。
# True potion stacking (user 2026-10-31, cap 99): patches both misc.bin and itemtypes.bin into the overlay, the same way wide_stash.py handles inventory.bin (read the player's romfs, change bytes, write to the overlay).
#   misc.bin rows are 448 bytes (4-byte header): +128 item code, +236 minstack, +240 maxstack, +244 spawnstack (u32), +322 stackable, +339 compactsave (aligned row by row with misc.txt).
#     compactsave must be cleared: simple items have no quantity field, so the count could not be saved. Simple potions in old saves are still read by their own flag and load as 1 bottle.
#   itemtypes.bin rows are 232 bytes: +0 type code, +19 AutoStack (merge on pickup, the column keys use).
#   With the tables alone one sip removes the whole stack (the game deletes the whole item after use); the host's server use entry takes off just 1 (dm_stack.c).
#   These two tables are in the co-op manifest: all four ends must be on the same commit and this cannot be a settings switch; settings only control interaction.
import os, struct
import wide_stash

MAX_STACK = 99
CODES = {b'hp1', b'hp2', b'hp3', b'hp4', b'hp5', b'mp1', b'mp2', b'mp3', b'mp4', b'mp5', b'rvs', b'rvl', b'vps', b'yps', b'wms'}
TYPES = {b'hpot', b'mpot', b'rpot', b'spot', b'apot', b'wpot'}

def _misc(m):
    n = struct.unpack_from('<I', m, 0)[0]; st = (len(m) - 4) // n
    if st != 448: raise SystemExit('stack_potions：misc.bin 每行 %d 字节，不是 448（原版表变了？）' % st)
    done = set()
    for i in range(n):
        r = 4 + i * st; c = bytes(m[r + 128:r + 131])
        if c not in CODES: continue
        if m[r + 322] != 0 or m[r + 339] != 1: raise SystemExit('stack_potions：%s 不是不可堆叠的简单物品（原版表变了？）' % c.decode())
        struct.pack_into('<III', m, r + 236, 1, MAX_STACK, 1); m[r + 322] = 1; m[r + 339] = 0; done.add(c)
    if done != CODES: raise SystemExit('stack_potions：misc.bin 缺 %s' % sorted(CODES - done))

def _types(t):
    n = struct.unpack_from('<I', t, 0)[0]; st = (len(t) - 4) // n
    if st != 232: raise SystemExit('stack_potions：itemtypes.bin 每行 %d 字节，不是 232（原版表变了？）' % st)
    done = set()
    for i in range(n):
        r = 4 + i * st; c = bytes(t[r:r + 4])
        if c in TYPES:
            if t[r + 19] != 0: raise SystemExit('stack_potions：%s 的 AutoStack 已开（原版表变了？）' % c.decode())
            t[r + 19] = 1; done.add(c)
    if done != TYPES: raise SystemExit('stack_potions：itemtypes.bin 缺 %s' % sorted(TYPES - done))

def build(romfs, outdir):
    out = {}
    for sub in ('', 'base_'):
        path, m = wide_stash._entry(romfs, '/global/excel/' + sub.replace('_', '/') + 'misc.bin'); m = bytearray(m); _misc(m)
        p = os.path.join(outdir, 'sp_' + sub + 'misc.bin'); open(p, 'wb').write(m); out[path] = p
        path, t = wide_stash._entry(romfs, '/global/excel/' + sub.replace('_', '/') + 'itemtypes.bin'); t = bytearray(t); _types(t)
        p = os.path.join(outdir, 'sp_' + sub + 'itemtypes.bin'); open(p, 'wb').write(t); out[path] = p
    return out, {'files': len(out)}
