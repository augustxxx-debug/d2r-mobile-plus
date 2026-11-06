# 显存池预算回归（B085）：在原映像上按 patch_nso.py 的 CUTS 打剪口，用 unicorn 跑显存管理器初始化配置那一段（main+0x8524a4..0x8524e8），
# 读回栈上配置表三组 {预算, 粒度}：[sp+0x238] 渲染目标 512 MB（260 → 512 的旧剪口）、[sp+0x240] 池 0（CPU 可见，每帧 8 MiB 临时环取块的池）512 MB（本剪口，原 400 MB）、
# [sp+0x248] 池 2 710 MB 不变；三组粒度 10 MB / 1 MB / 2 MB 不变。另跑两个对照：不打剪口读回原版 260 / 400 / 710；只撤掉池 0 剪口读回 400（证明本测试拦得住回退）。
# 池 0 为什么要放大：72 只怪同屏时三帧在途 + 备用的 8 MiB 块把 400 MB 用完，main+0x174b018 返回空、main+0x173e808 读空崩溃（Mac 宿主 145 秒必崩，512 MB 跑满 300 秒、峰值 417 MB）
# 用法：gpu_pool_budget.py <main.orig.bin>
# Video memory pool budget regression (B085): apply the CUTS from patch_nso.py to the original image, run the video memory manager's init config block (main+0x8524a4..0x8524e8) in unicorn,
# and read back the three {budget, granule} pairs of the config table on the stack: [sp+0x238] render targets 512 MB (the older 260 → 512 cut), [sp+0x240] pool 0 (CPU-visible, where the per-frame 8 MiB transient rings take blocks) 512 MB (this cut, originally 400 MB),
# [sp+0x248] pool 2 710 MB unchanged; the granules 10 MB / 1 MB / 2 MB unchanged. Two controls as well: without cuts the original 260 / 400 / 710 is read back; with only the pool 0 cut withdrawn, 400 is read back (proving this test catches a regression).
# Why pool 0 grows: with 72 monsters on screen the 8 MiB blocks in flight for three frames plus spares use up 400 MB, main+0x174b018 returns null and main+0x173e808 reads null and crashes (Mac host crashes at 145 s every time; with 512 MB it runs the full 300 s, peak 417 MB)
# Usage: gpu_pool_budget.py <main.orig.bin>
import ast, struct, sys
from pathlib import Path
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM
from unicorn.arm64_const import UC_ARM64_REG_SP, UC_ARM64_REG_X26, UC_ARM64_REG_X28

HERE = Path(__file__).resolve().parent
img = Path(sys.argv[1]).read_bytes()
src = (HERE / '../../tools/patch_nso.py').read_text(encoding='utf-8')
cuts = None
for node in ast.parse(src).body:
    if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == 'CUTS' for t in node.targets):
        cuts = ast.literal_eval(node.value)
assert cuts, 'patch_nso.py 里没找到 CUTS'
ents = {e[0]: e for e in next(iter(cuts.values()))}
MB = 1 << 20
START, END = 0x8524a4, 0x8524e8

def run(offs):
    b = bytearray(img)
    for off in offs:
        _, ob, nb = ents[off][:3]
        assert b[off:off + len(ob) // 2] == bytes.fromhex(ob), f'{off:#x} 原字节不符：映像 {b[off:off + len(ob) // 2].hex()} ≠ 剪口登记 {ob}'
        b[off:off + len(nb) // 2] = bytes.fromhex(nb)
    u = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    base = START & ~0xfff; u.mem_map(base, 0x2000); u.mem_write(base, bytes(b[base:base + 0x2000]))
    u.mem_map(0x7000000, 0x10000)   # 栈 | stack
    u.mem_map(0x7100000, 0x1000)    # x26 指向的对象（0x8524c0 stur x8,[x26,#0xf4]） | the object x26 points at (0x8524c0 stur x8,[x26,#0xf4])
    u.reg_write(UC_ARM64_REG_SP, 0x7008000); u.reg_write(UC_ARM64_REG_X26, 0x7100000); u.reg_write(UC_ARM64_REG_X28, 0x7100800)
    u.emu_start(START, END)
    rd = lambda o: struct.unpack('<II', u.mem_read(0x7008000 + o, 8))
    return rd(0x238), rd(0x240), rd(0x248)

ok = True
def check(name, got, want):
    global ok
    good = got == want
    ok &= good
    print(f"{'通过' if good else '【红】'} {name}：读回 {[(a // MB, g // MB) for a, g in got]} MB，期望 {[(a // MB, g // MB) for a, g in want]} MB")

assert 0x8524d0 in ents, 'CUTS 里没有池 0 预算剪口 0x8524d0（B085）'
check('原版（不打剪口）', run([]), ((260 * MB, 10 * MB), (400 * MB, 1 * MB), (710 * MB, 2 * MB)))
check('现役剪口（渲染目标 512 + 池 0 512）', run([0x8524c4, 0x8524d0]), ((512 * MB, 10 * MB), (512 * MB, 1 * MB), (710 * MB, 2 * MB)))
neg = run([0x8524c4])
print(f"对照：撤掉池 0 剪口读回池 0 = {neg[1][0] // MB} MB（应为 400，表示本测试能拦住回退）")
ok &= neg[1][0] == 400 * MB
print('GPU_POOL_BUDGET', 'PASS' if ok else 'FAIL')
sys.exit(0 if ok else 1)
