// 子组 lane 号改走 ballot（驱动绕法）：Adreno 740 驱动 512.676.53 在含 OpGroupNonUniformShuffle 的计算着色器里把 SubgroupLocalInvocationId 读成 0（探针 M1 实测；
// ballot / LtMask / BallotBitCount 本身正确），warp32 改写用它选线程束与 ballot 字，于是越界、GPU 挂起。本改写把每个函数里对该 builtin 的 OpLoad 换成
// 「函数第一块开头算一次的 OpGroupNonUniformBallotBitCount ExclusiveScan(OpGroupNonUniformBallot true)」（此时全体 lane 活跃，值 = lane 号），各处 OpLoad 改成 OpCopyObject。
// 放在 warp32 改写之后（warp32 保留 OpLoad、只给它加 & 31 等）。缺的类型 / 常量补在第一个 OpFunction 前；缺 GroupNonUniformBallot capability 时补上。
// 启用：引擎启动自检（xbg_game.cpp laneid_probe，真机跑一次同形探针）判定该 builtin 不可用时才开；标记 gfx-laneid-ballot / 环境 DM_GFX_LANEID_BALLOT = 0 关 / 1 强开 / 2 自动。
// Subgroup lane id through ballot (driver workaround): the Adreno 740 driver 512.676.53 reads SubgroupLocalInvocationId as 0 in compute shaders that also use
// OpGroupNonUniformShuffle (probe M1; ballot / LtMask / BallotBitCount themselves are correct); the warp32 rewrite selects warps and ballot words with it, so writes go out of
// bounds and the GPU hangs. This rewrite replaces every OpLoad of that builtin in a function with "OpGroupNonUniformBallotBitCount ExclusiveScan(OpGroupNonUniformBallot true)
// computed once at the start of the function's first block" (every lane is active there, so the value is the lane id); the loads become OpCopyObject.
// Runs after the warp32 rewrite (which keeps the OpLoad and only adds & 31 etc.). Missing types / constants are added before the first OpFunction; a missing
// GroupNonUniformBallot capability is added. Enabled only when the engine's start-up self-test (xbg_game.cpp laneid_probe, the same-shaped probe run once on the device)
// finds the builtin unusable; marker gfx-laneid-ballot / env DM_GFX_LANEID_BALLOT = 0 off / 1 force on / 2 auto.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
// 返回替换的 OpLoad 数；0 = 没改（out 空）：没有该 builtin、不是 32 位整数、或模块不合法 | returns the number of OpLoads replaced; 0 = unchanged (out empty)
size_t xbg_spirv_laneid_ballot(const uint32_t *in, size_t n, std::vector<uint32_t> &out);
