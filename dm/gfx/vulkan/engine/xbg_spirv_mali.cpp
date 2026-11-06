// Mali（联发科）上载入着色器时的三项改写。P0 在 OPPO CPH2913（MT6995，Mali-G2-Ultra-NX MC12，驱动 r56p0）上的探针：子组固定 16 道；
// 微场景 M3（声明了 ClipDistance 的管线）让驱动编译器 cmpbe_v2_compile_multiple_shaders 空指针；vertexPipelineStoresAndAtomics = 0。
// 游戏的 6,931 个着色器里 2,346 个顶点 / 细分着色器声明了 ClipDistance / CullDistance 却从不写，2,296 个顶点着色器的存储块缺
// NonWritable；5 个计算着色器按 32 道线程束写（分析见 dm-work/run/android/mali-20261029/cs5/ANALYSIS.md）。离线原型是
// xgs_strip_clipcull.py（同一规则，在 host GPU 模拟器上验证层 0 错误）。这里放到载入时做，导入包与设备无关。
// Three load-time shader rewrites for Mali (MediaTek). The P0 probe on the OPPO CPH2913 (MT6995, Mali-G2-Ultra-NX MC12, driver r56p0):
// subgroups fixed at 16 lanes; micro-scene M3 (a pipeline declaring ClipDistance) crashed the driver compiler on a null pointer
// (cmpbe_v2_compile_multiple_shaders); vertexPipelineStoresAndAtomics = 0. Of the game's 6,931 shaders, 2,346 vertex / tessellation
// shaders declare ClipDistance / CullDistance without ever writing them, 2,296 vertex shaders' storage blocks lack NonWritable, and
// 5 compute shaders are written for 32-lane warps (analysis in dm-work/run/android/mali-20261029/cs5/ANALYSIS.md). The offline
// prototype was xgs_strip_clipcull.py (same rules, 0 validation errors on the host-GPU emulator). Done here at load time, so the import
// pack stays device-independent.
#include "xbg_spirv_mali.h"

#include <map>
#include <set>
#if defined(__has_include)
#if __has_include("../../../third_party/spirv-tools/spirv-headers/include/spirv/unified1/spirv.h")
#include "../../../third_party/spirv-tools/spirv-headers/include/spirv/unified1/spirv.h"
#define XBG_MALI_HAVE_SPIRV_H 1
#endif
#endif

// 操作码名与值照 SPIR-V 头 dm/third_party/spirv-tools/spirv-headers/include/spirv/unified1/spirv.h（vulkan-sdk-1.4.357.0）的 SpvOp*；
// 树里有这个头时逐个 static_assert 核对。| Opcode names and values follow SpvOp* in the SPIR-V header
// dm/third_party/spirv-tools/spirv-headers/include/spirv/unified1/spirv.h (vulkan-sdk-1.4.357.0); checked by static_assert below when present.
namespace {
enum : uint32_t {
    OpSourceContinued = 2, OpSource = 3, OpSourceExtension = 4, OpName = 5, OpMemberName = 6, OpString = 7, OpLine = 8, OpExtension = 10,
    OpExtInstImport = 11, OpExtInst = 12, OpMemoryModel = 14, OpEntryPoint = 15, OpExecutionMode = 16, OpCapability = 17,
    OpTypeVoid = 19, OpTypeBool = 20, OpTypeInt = 21, OpTypeFloat = 22, OpTypeArray = 28, OpTypeRuntimeArray = 29, OpTypeStruct = 30,
    OpTypePointer = 32, OpTypeFunction = 33, OpTypeForwardPointer = 39, OpConstantTrue = 41, OpConstant = 43, OpSpecConstantOp = 52,
    OpFunction = 54, OpFunctionCall = 57, OpVariable = 59, OpLoad = 61, OpStore = 62, OpCopyMemory = 63, OpCopyMemorySized = 64,
    OpAccessChain = 65, OpInBoundsAccessChain = 66, OpPtrAccessChain = 67, OpArrayLength = 68, OpInBoundsPtrAccessChain = 70,
    OpDecorate = 71, OpMemberDecorate = 72, OpDecorationGroup = 73, OpGroupDecorate = 74, OpGroupMemberDecorate = 75,
    OpVectorShuffle = 79, OpCompositeExtract = 81, OpCompositeInsert = 82, OpCopyObject = 83, OpSelect = 169, OpULessThan = 176,
    OpBitwiseAnd = 199, OpAtomicLoad = 227, OpAtomicStore = 228, OpAtomicXor = 242, OpNoLine = 317, OpModuleProcessed = 330,
    OpExecutionModeId = 331, OpDecorateId = 332, OpGroupNonUniformShuffle = 345,
    // 浮点原子（SPV_EXT_shader_atomic_float_min_max / _add）；旧代码当 FAdd 用的 4415 在 spirv.h 里不是任何操作码 | float atomics; the 4415 used before is no opcode at all
    OpAtomicFMinEXT = 5614, OpAtomicFMaxEXT = 5615, OpDecorateString = 5632, OpMemberDecorateString = 5633, OpAtomicFAddEXT = 6035,
    DecBlock = 2, DecBufferBlock = 3, DecBuiltIn = 11, DecNonWritable = 24, BuiltInClipDistance = 3, BuiltInCullDistance = 4,
    CapClipDistance = 32, CapCullDistance = 33, StorageUniform = 2, StorageStorageBuffer = 12,
};
#ifdef XBG_MALI_HAVE_SPIRV_H
#define XBG_MALI_OP(name) static_assert(name == uint32_t(Spv##name), #name " differs from spirv.h");
XBG_MALI_OP(OpSourceContinued) XBG_MALI_OP(OpSource) XBG_MALI_OP(OpSourceExtension) XBG_MALI_OP(OpName) XBG_MALI_OP(OpMemberName)
XBG_MALI_OP(OpString) XBG_MALI_OP(OpLine) XBG_MALI_OP(OpExtension) XBG_MALI_OP(OpExtInstImport) XBG_MALI_OP(OpExtInst)
XBG_MALI_OP(OpMemoryModel) XBG_MALI_OP(OpEntryPoint) XBG_MALI_OP(OpExecutionMode) XBG_MALI_OP(OpCapability) XBG_MALI_OP(OpTypeVoid)
XBG_MALI_OP(OpTypeBool) XBG_MALI_OP(OpTypeInt) XBG_MALI_OP(OpTypeFloat) XBG_MALI_OP(OpTypeArray) XBG_MALI_OP(OpTypeRuntimeArray)
XBG_MALI_OP(OpTypeStruct) XBG_MALI_OP(OpTypePointer) XBG_MALI_OP(OpTypeFunction) XBG_MALI_OP(OpTypeForwardPointer)
XBG_MALI_OP(OpConstantTrue) XBG_MALI_OP(OpConstant) XBG_MALI_OP(OpSpecConstantOp) XBG_MALI_OP(OpFunction) XBG_MALI_OP(OpFunctionCall)
XBG_MALI_OP(OpVariable) XBG_MALI_OP(OpLoad) XBG_MALI_OP(OpStore) XBG_MALI_OP(OpCopyMemory) XBG_MALI_OP(OpCopyMemorySized)
XBG_MALI_OP(OpAccessChain) XBG_MALI_OP(OpInBoundsAccessChain) XBG_MALI_OP(OpPtrAccessChain) XBG_MALI_OP(OpArrayLength)
XBG_MALI_OP(OpInBoundsPtrAccessChain) XBG_MALI_OP(OpDecorate) XBG_MALI_OP(OpMemberDecorate) XBG_MALI_OP(OpDecorationGroup)
XBG_MALI_OP(OpGroupDecorate) XBG_MALI_OP(OpGroupMemberDecorate) XBG_MALI_OP(OpVectorShuffle) XBG_MALI_OP(OpCompositeExtract)
XBG_MALI_OP(OpCompositeInsert) XBG_MALI_OP(OpCopyObject) XBG_MALI_OP(OpSelect) XBG_MALI_OP(OpULessThan) XBG_MALI_OP(OpBitwiseAnd)
XBG_MALI_OP(OpAtomicLoad) XBG_MALI_OP(OpAtomicStore) XBG_MALI_OP(OpAtomicXor) XBG_MALI_OP(OpNoLine) XBG_MALI_OP(OpModuleProcessed)
XBG_MALI_OP(OpExecutionModeId) XBG_MALI_OP(OpDecorateId) XBG_MALI_OP(OpGroupNonUniformShuffle) XBG_MALI_OP(OpAtomicFMinEXT)
XBG_MALI_OP(OpAtomicFMaxEXT) XBG_MALI_OP(OpDecorateString) XBG_MALI_OP(OpMemberDecorateString) XBG_MALI_OP(OpAtomicFAddEXT)
#undef XBG_MALI_OP
#endif
inline uint32_t inst(uint32_t op, uint32_t words) { return (words << 16) | op; }
// 逐条遍历；指令格式不对返回 false | Walk every instruction; false on a malformed one
template <class F> bool walk(const uint32_t *w, size_t n, F f) {
    for (size_t i = 5; i < n;) {
        uint32_t wc = w[i] >> 16; if (!wc || i + wc > n) return false;
        f(i, w[i] & 0xffffu, wc); i += wc;
    }
    return true;
}
bool header_ok(const uint32_t *w, size_t n) { return w && n >= 5 && w[0] == 0x07230203u; }
// 指针在第 3 个字（Result Type、Result 之后）的原子操作：OpAtomicLoad … OpAtomicXor（不含 OpAtomicStore）与三种浮点原子
// Atomics with the pointer in word 3 (after Result Type and Result): OpAtomicLoad ... OpAtomicXor (not OpAtomicStore) and the three float atomics
bool is_atomic(uint32_t op) {
    return (op >= OpAtomicLoad && op <= OpAtomicXor && op != OpAtomicStore) || op == OpAtomicFMinEXT || op == OpAtomicFMaxEXT || op == OpAtomicFAddEXT;
}
bool is_chain(uint32_t op) { return op == OpAccessChain || op == OpInBoundsAccessChain || op == OpPtrAccessChain || op == OpInBoundsPtrAccessChain; }
// 元数据指令（模块头、调试名、装饰、入口接口）：里面出现指针 id 不算使用 | metadata (module header, debug names, decorations, entry
// interfaces): a pointer id there is not a use
bool is_meta(uint32_t op) {
    switch (op) {
    case OpSourceContinued: case OpSource: case OpSourceExtension: case OpName: case OpMemberName: case OpString: case OpLine:
    case OpExtension: case OpExtInstImport: case OpMemoryModel: case OpEntryPoint: case OpExecutionMode: case OpCapability:
    case OpDecorate: case OpMemberDecorate: case OpDecorationGroup: case OpGroupDecorate: case OpGroupMemberDecorate: case OpNoLine:
    case OpModuleProcessed: case OpExecutionModeId: case OpDecorateId: case OpDecorateString: case OpMemberDecorateString: return true;
    default: return false;
    }
}
// 把指令里可能是 id 的字逐个交给 f；元数据、类型与常量不给（指针值不会出现在那里）。认得字面量位置的几种常见指令跳过字面量，
// 其余指令全部操作数都给——字面量碰巧等于某个 id 只会让改写更保守，不会改错。
// Hand f every word of an instruction that may be an id; none for metadata, types and constants (a pointer value cannot appear there).
// The common instructions whose literal positions are known skip them; every other instruction hands over all operands — a literal that
// happens to equal an id only makes the rewrite more conservative, never wrong.
template <class F> void id_words(const uint32_t *w, size_t i, uint32_t op, uint32_t wc, F f) {
    if (is_meta(op) || (op >= OpTypeVoid && op <= OpSpecConstantOp)) return;
    uint32_t lit_from = wc, lit_to = wc;   // [lit_from, lit_to) 是字面量 | literal words
    if (op == OpCompositeExtract) lit_from = 4;
    else if (op == OpCompositeInsert || op == OpVectorShuffle) lit_from = 5;
    else if (op == OpVariable) { lit_from = 3; lit_to = 4; }   // 存储类 | storage class
    else if (op == OpExtInst) { lit_from = 4; lit_to = 5; }    // 扩展指令号 | extended instruction number
    for (uint32_t k = 1; k < wc; ++k) if (k < lit_from || k >= lit_to) f(w[i + k]);
}
}

size_t xbg_spirv_shuffle_guard(const uint32_t *w, size_t n, uint32_t sg, std::vector<uint32_t> &out) {
    out.clear();
    if (!header_ok(w, n) || sg == 0 || sg >= 32 || (sg & (sg - 1))) return 0;
    uint32_t bound = w[3], t_uint = 0, t_bool = 0; size_t first_fn = 0, shuffles = 0;
    std::set<uint32_t> scalar;   // 标量类型 | scalar types
    std::map<uint32_t, uint32_t> uconst;   // 32 位无符号常量值 → id | 32-bit unsigned constant value -> id
    if (!walk(w, n, [&](size_t i, uint32_t op, uint32_t wc) {
        if (op == OpTypeInt && wc == 4) { scalar.insert(w[i + 1]); if (w[i + 2] == 32 && w[i + 3] == 0 && !t_uint) t_uint = w[i + 1]; }
        if (op == OpTypeFloat && wc >= 3) scalar.insert(w[i + 1]);
        if (op == OpTypeBool && wc == 2) { scalar.insert(w[i + 1]); if (!t_bool) t_bool = w[i + 1]; }
        if (op == OpFunction && !first_fn) first_fn = i;
        if (op == OpGroupNonUniformShuffle) ++shuffles;
    })) return 0;
    if (!shuffles || !first_fn || !t_uint) return 0;   // shuffle 的源车道必须是 32 位无符号整数，没有这个类型就不是我们认识的模块 | the id operand must be a 32-bit uint
    walk(w, n, [&](size_t i, uint32_t op, uint32_t wc) { if (op == OpConstant && wc == 4 && w[i + 1] == t_uint && !uconst.count(w[i + 3])) uconst[w[i + 3]] = w[i + 2]; });
    std::vector<uint32_t> decl;
    if (!t_bool) { t_bool = bound++; decl.insert(decl.end(), {inst(OpTypeBool, 2), t_bool}); }
    auto constant = [&](uint32_t v) { auto f = uconst.find(v); if (f != uconst.end()) return f->second; uint32_t id = bound++; decl.insert(decl.end(), {inst(OpConstant, 4), t_uint, id, v}); uconst[v] = id; return id; };
    const uint32_t c_size = constant(sg), c_mask = constant(sg - 1);
    size_t done = 0;
    out.assign(w, w + 5); out.reserve(n + decl.size() + shuffles * 14);
    walk(w, n, [&](size_t i, uint32_t op, uint32_t wc) {
        if (i == first_fn) out.insert(out.end(), decl.begin(), decl.end());
        if (op == OpGroupNonUniformShuffle && wc == 6 && scalar.count(w[i + 1])) {
            const uint32_t type = w[i + 1], result = w[i + 2], scope = w[i + 3], value = w[i + 4], id = w[i + 5];
            const uint32_t in_range = bound++, masked = bound++, shuffled = bound++;
            out.insert(out.end(), {inst(OpULessThan, 5), t_bool, in_range, id, c_size});
            out.insert(out.end(), {inst(OpBitwiseAnd, 5), t_uint, masked, id, c_mask});
            out.insert(out.end(), {inst(OpGroupNonUniformShuffle, 6), type, shuffled, scope, value, masked});
            out.insert(out.end(), {inst(OpSelect, 6), type, result, in_range, shuffled, value});
            ++done; return;
        }
        out.insert(out.end(), w + i, w + i + wc);
    });
    if (!done) { out.clear(); return 0; }
    out[3] = bound;
    return done;
}

size_t xbg_spirv_strip_clipcull(const uint32_t *w, size_t n, std::vector<uint32_t> &out) {
    out.clear();
    if (!header_ok(w, n)) return 0;
    uint32_t target = 0; std::set<uint32_t> members;   // gl_PerVertex 结构与其 Clip / Cull 成员号 | gl_PerVertex and its Clip / Cull member indexes
    std::map<uint32_t, uint32_t> consts, pointee, elem;   // 常量值；指针 → 所指类型；数组 → 元素类型 | constant values; pointer -> pointee; array -> element
    std::map<uint32_t, uint32_t> var_type;                // 变量 / 访问链结果 → 指针类型 | variable / chain result -> pointer type
    std::map<uint32_t, std::vector<uint32_t>> struct_members;
    bool other_clip_builtin = false;
    if (!walk(w, n, [&](size_t i, uint32_t op, uint32_t wc) {
        if (op == OpMemberDecorate && wc == 5 && w[i + 3] == DecBuiltIn && (w[i + 4] == BuiltInClipDistance || w[i + 4] == BuiltInCullDistance)) {
            if (target && target != w[i + 1]) other_clip_builtin = true; else { target = w[i + 1]; members.insert(w[i + 2]); }
        }
        if (op == OpDecorate && wc == 4 && w[i + 2] == DecBuiltIn && (w[i + 3] == BuiltInClipDistance || w[i + 3] == BuiltInCullDistance)) other_clip_builtin = true;
        if (op == OpConstant && wc == 4) consts[w[i + 2]] = w[i + 3];
        if (op == OpTypePointer && wc == 4) pointee[w[i + 1]] = w[i + 3];
        if ((op == OpTypeArray && wc == 4) || (op == OpTypeRuntimeArray && wc == 3)) elem[w[i + 1]] = w[i + 2];
        if (op == OpTypeStruct) struct_members[w[i + 1]] = std::vector<uint32_t>(w + i + 2, w + i + wc);
        if (op == OpVariable && wc >= 4) var_type[w[i + 2]] = w[i + 1];
    })) return 0;
    // 只认 Position(0)、PointSize(1)、ClipDistance(2)、CullDistance(3) 这一种形状 | only the Position, PointSize, ClipDistance, CullDistance layout
    if (!target || struct_members[target].size() < 3 || struct_members[target].size() > 4) return 0;
    for (uint32_t m : members) if (m < 2) return 0;
    if (members.size() != struct_members[target].size() - 2) return 0;
    // 只有「经访问链落到成员 0 / 1」这一种用法可以证明 Clip / Cull 没被写；整块写（OpStore 整个块或 gl_out[i]、OpCopyMemory(Sized) 拷进来、
    // 构造整块值再存）和任何分析不了的用法都不改。G = gl_PerVertex 与它的数组（gl_in[] / gl_out[]），GP = 指向 G（或指向这种指针）的
    // 指针类型，P = 类型在 GP 里的变量与访问链结果。P 只许当访问链的基址（以及出现在元数据里）；产出 G 类型值或 GP 类型指针的其他指令
    // （载入、构造、Undef、常量、Phi、Select、CopyObject、函数形参 / 返回值）一律不改；别的结构 / 函数类型里含 G / GP 也不改。
    // Only "access chains reaching member 0 / 1" prove Clip / Cull unwritten. Whole-block writes (OpStore of the block or of gl_out[i],
    // OpCopyMemory(Sized) into it, a composite constructed and then stored) and every use that cannot be analysed refuse. G = gl_PerVertex and
    // its arrays (gl_in[] / gl_out[]), GP = pointer types to G (or to such pointers), P = variables and chain results typed in GP. P may only be a
    // chain base (or appear in metadata); any other instruction yielding a G-typed value or a GP-typed pointer (load, construct, undef, constant,
    // phi, select, copy, function parameter / return) refuses, and so does another struct / function type containing G / GP.
    std::set<uint32_t> G{target}, GP, P;
    bool reached = false, unsafe = false;
    walk(w, n, [&](size_t i, uint32_t op, uint32_t wc) {
        auto guarded = [&](uint32_t id) { return G.count(id) || GP.count(id); };
        if (op == OpTypeStruct || op == OpTypeFunction) { if (w[i + 1] != target) for (uint32_t k = 2; k < wc; ++k) if (guarded(w[i + k])) unsafe = true; return; }
        if ((op == OpTypeArray && wc == 4) || (op == OpTypeRuntimeArray && wc == 3)) { if (G.count(w[i + 2])) G.insert(w[i + 1]); return; }
        if (op == OpTypePointer && wc == 4) { if (guarded(w[i + 3])) GP.insert(w[i + 1]); return; }
        if (is_meta(op) || (op >= OpTypeVoid && op <= OpTypeForwardPointer)) return;   // 其余类型带字面量，不含 G | other types carry literals, not G
        if (wc >= 2 && G.count(w[i + 1])) { unsafe = true; return; }   // 整块值（含常量）；无结果类型的指令这里是 id，撞不上类型号 | a whole-block value (constants included)
        if (op >= OpConstantTrue && op <= OpSpecConstantOp) return;
        if (op == OpVariable && wc >= 4) { if (GP.count(w[i + 1])) P.insert(w[i + 2]); if (wc >= 5 && P.count(w[i + 4])) unsafe = true; return; }
        if (is_chain(op) && wc >= 4) {
            if (GP.count(w[i + 1])) P.insert(w[i + 2]);   // 停在整块（gl_out[i]）上的链结果仍受看管 | a chain stopping at a whole block (gl_out[i]) stays guarded
            auto base = var_type.find(w[i + 3]);
            if (base != var_type.end()) {
                // 经访问链落到成员 2 / 3（直接或经数组 gl_in[] / gl_out[]）就不改 | a chain reaching member 2 / 3 (directly or through gl_in[] / gl_out[]) refuses
                uint32_t t = pointee.count(base->second) ? pointee[base->second] : 0;
                size_t k = i + 4 + ((op == OpPtrAccessChain || op == OpInBoundsPtrAccessChain) ? 1 : 0);
                for (; k < i + wc && t; ++k) {
                    if (t == target) { auto c = consts.find(w[k]); if (c == consts.end() || c->second >= 2) reached = true; break; }
                    if (elem.count(t)) { t = elem[t]; continue; }
                    auto s = struct_members.find(t); if (s == struct_members.end()) break;
                    auto c = consts.find(w[k]); if (c == consts.end() || c->second >= s->second.size()) break;
                    t = s->second[c->second];
                }
            }
            var_type[w[i + 2]] = w[i + 1];   // 访问链结果记它自己的指针类型，链上再取链也追得到 | a chain result keeps its own pointer type, so chains of chains are followed
            return;
        }
        if (wc >= 2 && GP.count(w[i + 1])) { unsafe = true; return; }   // 别的指令产出这种指针 | another instruction yielding such a pointer
        id_words(w, i, op, wc, [&](uint32_t id) { if (P.count(id)) unsafe = true; });   // 存储、拷贝、载入、传参……都算整块用法 | store, copy, load, call...: whole-block uses
    });
    if (reached || unsafe) return 0;
    const size_t removed = struct_members[target].size() - 2;
    out.assign(w, w + 5); out.reserve(n);
    walk(w, n, [&](size_t i, uint32_t op, uint32_t wc) {
        if (op == OpTypeStruct && w[i + 1] == target) { out.insert(out.end(), {inst(OpTypeStruct, 4), target, w[i + 2], w[i + 3]}); return; }
        if ((op == OpMemberDecorate || op == OpMemberName) && wc >= 3 && w[i + 1] == target && w[i + 2] >= 2) return;
        if (op == OpCapability && wc == 2 && (w[i + 1] == CapClipDistance || w[i + 1] == CapCullDistance) && !other_clip_builtin) return;
        out.insert(out.end(), w + i, w + i + wc);
    });
    return removed;
}

size_t xbg_spirv_nonwritable(const uint32_t *w, size_t n, std::vector<uint32_t> &out) {
    out.clear();
    if (!header_ok(w, n)) return 0;
    std::set<uint32_t> block, buffer_block, decorated;
    std::map<uint32_t, std::pair<uint32_t, uint32_t>> ptr;   // 指针类型 → (存储类, 所指类型) | pointer type -> (storage class, pointee)
    std::vector<uint32_t> vars; std::map<uint32_t, uint32_t> var_ptr;
    if (!walk(w, n, [&](size_t i, uint32_t op, uint32_t wc) {
        if (op == OpDecorate && wc == 3 && w[i + 2] == DecBlock) block.insert(w[i + 1]);
        if (op == OpDecorate && wc == 3 && w[i + 2] == DecBufferBlock) buffer_block.insert(w[i + 1]);
        if (op == OpDecorate && wc == 3 && w[i + 2] == DecNonWritable) decorated.insert(w[i + 1]);
        if (op == OpTypePointer && wc == 4) ptr[w[i + 1]] = {w[i + 2], w[i + 3]};
        if (op == OpVariable && wc >= 4) { vars.push_back(w[i + 2]); var_ptr[w[i + 2]] = w[i + 1]; }
    })) return 0;
    // 候选：StorageBuffer 存储类的块，或 Uniform 存储类的 BufferBlock（Uniform 里的 Block 是 UBO，本来就只读）
    // Candidates: blocks in StorageBuffer, or BufferBlock in Uniform (a Block in Uniform is a UBO, read-only anyway)
    std::map<uint32_t, uint32_t> root;   // 指针值 → 候选变量 | pointer value -> candidate variable
    for (uint32_t v : vars) {
        auto p = ptr.find(var_ptr[v]); if (p == ptr.end() || decorated.count(v)) continue;
        const uint32_t sc = p->second.first, t = p->second.second;
        if ((sc == StorageStorageBuffer && (block.count(t) || buffer_block.count(t))) || (sc == StorageUniform && buffer_block.count(t))) root[v] = v;
    }
    if (root.empty()) return 0;
    std::set<uint32_t> written;
    auto from = [&](uint32_t id) { auto f = root.find(id); return f == root.end() ? 0u : f->second; };
    // 第一遍：访问链与复制按出现顺序传递根（基址必然支配链，定义先于使用，一遍够）| pass one: chains and copies pass the root along in
    // order (a base dominates its chain, so definitions precede uses)
    walk(w, n, [&](size_t i, uint32_t op, uint32_t wc) {
        if ((is_chain(op) && wc >= 4) || (op == OpCopyObject && wc == 4)) { if (uint32_t r = from(w[i + 3])) root[w[i + 2]] = r; }
    });
    // 第二遍查使用（OpPhi 可以引用后面才定义的链，所以放在传根之后）：只有载入、OpArrayLength、拷贝的源是读；其余任何出现——存储、
    // 原子（含 OpAtomicLoad，保守算写）、传参、Select / Phi、存进别的变量、认不出的指令——都算可能写。
    // Pass two checks uses (OpPhi may name a chain defined later, hence after root passing): only loads, OpArrayLength and a copy's source
    // read; any other appearance — store, atomic (OpAtomicLoad included, conservatively), call argument, select / phi, being stored into another
    // variable, an unknown instruction — counts as a possible write.
    walk(w, n, [&](size_t i, uint32_t op, uint32_t wc) {
        auto mark = [&](uint32_t id) { if (uint32_t r = from(id)) written.insert(r); };
        if (is_chain(op) || op == OpCopyObject || op == OpLoad || op == OpArrayLength) return;
        if ((op == OpCopyMemory || op == OpCopyMemorySized) && wc >= 3) { mark(w[i + 1]); return; }   // 目标写，源读 | target written, source read
        if (op == OpVariable) { if (wc >= 5) mark(w[i + 4]); return; }   // 当初值 = 指针外泄 | as an initializer the pointer escapes
        if (op == OpAtomicStore && wc >= 2) mark(w[i + 1]);   // 指针在第 1 个操作数 | pointer is operand 1
        if (is_atomic(op) && wc >= 4) mark(w[i + 3]);
        id_words(w, i, op, wc, mark);
    });
    std::vector<uint32_t> add;
    for (uint32_t v : vars) if (root.count(v) && root[v] == v && !written.count(v)) add.push_back(v);
    if (add.empty()) return 0;
    out.assign(w, w + 5); out.reserve(n + add.size() * 3); bool placed = false;
    walk(w, n, [&](size_t i, uint32_t op, uint32_t wc) {
        if (!placed && (op == OpDecorate || op == OpMemberDecorate)) { for (uint32_t v : add) out.insert(out.end(), {inst(OpDecorate, 3), v, DecNonWritable}); placed = true; }
        out.insert(out.end(), w + i, w + i + wc);
    });
    if (!placed) { out.clear(); return 0; }
    return add.size();
}
