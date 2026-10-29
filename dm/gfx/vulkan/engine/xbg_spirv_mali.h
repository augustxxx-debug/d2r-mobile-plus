// Mali（联发科）上载入着色器时的三项改写（见 xbg_spirv_mali.cpp）
// Three load-time shader rewrites for Mali (MediaTek) GPUs (see xbg_spirv_mali.cpp)
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

// 子组窄于 32 道（Mali 固定 16 道）时，给每个 OpGroupNonUniformShuffle 加越界保护：源车道 >= 子组宽度时取本车道自己的值，
// shuffle 本身改读「源车道 & (宽度 - 1)」以保证有定义。按 32 道线程束翻译的蝶形归约（xor 1/2/4/8/16）第 16 步因此变成恒等，
// min / max 归约按 16 道各自算完，由每个子组的 0 号车道原子合并，结果与 32 道相同。subgroup_size 须为小于 32 的 2 的幂；
// 结果类型不是标量的 shuffle 不改。返回改写的 shuffle 数，0 时 out 为空、调用者用原模块。
// On subgroups narrower than 32 lanes (Mali: fixed 16), guard every OpGroupNonUniformShuffle: a source lane >= the subgroup size
// yields the lane's own value, and the shuffle itself reads (source lane & (size - 1)) so it stays defined. The step 16 of a
// butterfly reduction translated for 32-lane warps (xor 1/2/4/8/16) thus becomes the identity; min / max reductions finish within
// each 16-lane subgroup and lane 0 of every subgroup merges them atomically, giving the 32-lane result. subgroup_size must be a power
// of two below 32; shuffles of non-scalar types are left alone. Returns the number of shuffles rewritten (0: out is empty and the
// module is to be used unchanged).
size_t xbg_spirv_shuffle_guard(const uint32_t *words, size_t count, uint32_t subgroup_size, std::vector<uint32_t> &out);

// 去掉 gl_PerVertex 里从不写入的 ClipDistance / CullDistance 成员（成员 2 / 3），连同它们的 OpMemberDecorate / OpMemberName；
// 没有别的 ClipDistance / CullDistance 内建变量时一并去掉 ClipDistance / CullDistance 能力。任何经访问链写到成员 2 / 3 的模块不改。
// Mali 驱动编译器在设备没有 shaderClipDistance 时编这种声明会崩（P0 探针 M3）。返回删掉的成员数，0 时 out 为空。
// Strip the never-written ClipDistance / CullDistance members (2 / 3) of gl_PerVertex with their OpMemberDecorate / OpMemberName,
// and the ClipDistance / CullDistance capabilities when no other such built-in remains. A module that reaches member 2 / 3 through
// an access chain is left alone. The Mali driver compiler crashes on such declarations when the device lacks shaderClipDistance
// (P0 probe M3). Returns the number of members removed (0: out is empty).
size_t xbg_spirv_strip_clipcull(const uint32_t *words, size_t count, std::vector<uint32_t> &out);

// 给从不写入的存储块变量（StorageBuffer，或 Uniform + BufferBlock）加 OpDecorate NonWritable；有存储、原子、复制、或被当参数
// 传给函数的不加。设备 vertexPipelineStoresAndAtomics = 0 时，顶点 / 细分 / 几何阶段缺这个装饰的管线按规范无效。返回装饰的变量数。
// Add OpDecorate NonWritable to storage-block variables (StorageBuffer, or Uniform + BufferBlock) that are never written; variables
// that are stored to, used atomically, copied into or passed to a function are left alone. When the device has
// vertexPipelineStoresAndAtomics = 0, a vertex / tessellation / geometry pipeline lacking it is invalid by the spec. Returns the
// number of variables decorated.
size_t xbg_spirv_nonwritable(const uint32_t *words, size_t count, std::vector<uint32_t> &out);
