#!/usr/bin/env python3
# B174 回归：改写产物里九处「护身符页号 == 0」检查都跳到桩、桩把页 5 当 0，并跳回下一条。
# 用法：t_charm_page5.py [产物目录]（缺省 product.sh path touch）；三种产物都可以测。
# B174 regression: in the rewritten build output, all nine "charm page == 0" checks branch to a stub that treats page 5 as 0 and returns to the next instruction.
# Usage: t_charm_page5.py [output directory] (default product.sh path touch); any of the three kinds can be checked.
import os, struct, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'tools'))
out = sys.argv[1] if len(sys.argv) > 1 else subprocess.check_output(['bash', os.path.join(HERE, '..', '..', 'tools', 'product.sh'), 'path', 'touch'], text=True).strip()
img = open(os.path.join(out, 'main.patched.bin'), 'rb').read()
SITES = (0x46ad48, 0x46bde8, 0x473c64, 0x474014, 0x578c98, 0x643a34, 0x64b464, 0x6bd594, 0x7ed8fc)
def word(a): return struct.unpack_from('<I', img, a)[0]
def target(a):
    w = word(a); assert w >> 26 == 0b000101, f'{a:#x} 不是 b：{w:08x}'
    imm = w & 0x3ffffff; imm -= (imm & 0x2000000) << 1; return a + imm * 4
fails = 0
for s in SITES:
    try:
        st = target(s)
        assert (word(st), word(st + 4), word(st + 8)) == (0x39415508, 0x7100151F, 0x1A8803E8), f'{s:#x} 桩内容不对'
        assert target(st + 12) == s + 4, f'{s:#x} 桩没跳回下一条'
    except AssertionError as e: print('FAIL', e); fails += 1
print(f'charm_page5 {"PASS" if not fails else "FAIL"}：{len(SITES)} 处，产物 {os.path.basename(out)}'); sys.exit(1 if fails else 0)
