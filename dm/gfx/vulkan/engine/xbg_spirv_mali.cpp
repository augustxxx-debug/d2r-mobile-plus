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

namespace {
enum : uint32_t {
    OpMemberName = 6, OpCapability = 17, OpTypeBool = 20, OpTypeInt = 21, OpTypeFloat = 22, OpTypeArray = 28, OpTypeRuntimeArray = 29,
    OpTypeStruct = 30, OpTypePointer = 32, OpConstant = 43, OpFunction = 54, OpFunctionCall = 57, OpVariable = 59, OpStore = 62,
    OpCopyMemory = 63, OpCopyMemorySized = 64, OpAccessChain = 65, OpInBoundsAccessChain = 66, OpPtrAccessChain = 67, OpDecorate = 71,
    OpMemberDecorate = 72, OpCopyObject = 83, OpSelect = 169, OpULessThan = 176, OpBitwiseAnd = 199, OpGroupNonUniformShuffle = 345,
    OpInBoundsPtrAccessChain = 70,
    DecBlock = 2, DecBufferBlock = 3, DecBuiltIn = 11, DecNonWritable = 24, BuiltInClipDistance = 3, BuiltInCullDistance = 4,
    CapClipDistance = 32, CapCullDistance = 33, StorageUniform = 2, StorageStorageBuffer = 12,
};
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
bool is_atomic(uint32_t op) { return (op >= 227 && op <= 242) || op == 4415 /* OpAtomicFAddEXT */; }
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
    // 经访问链落到成员 2 / 3（直接或经数组 gl_in[] / gl_out[]）就不改 | a chain reaching member 2 / 3 (directly or through gl_in[] / gl_out[]) refuses
    bool reached = false;
    walk(w, n, [&](size_t i, uint32_t op, uint32_t wc) {
        if ((op != OpAccessChain && op != OpInBoundsAccessChain && op != OpPtrAccessChain && op != OpInBoundsPtrAccessChain) || wc < 5) return;
        auto base = var_type.find(w[i + 3]); if (base == var_type.end()) return;
        uint32_t t = pointee.count(base->second) ? pointee[base->second] : 0;
        size_t k = i + 4 + ((op == OpPtrAccessChain || op == OpInBoundsPtrAccessChain) ? 1 : 0);
        for (; k < i + wc && t; ++k) {
            if (t == target) { auto c = consts.find(w[k]); if (c == consts.end() || c->second >= 2) reached = true; break; }
            if (elem.count(t)) { t = elem[t]; continue; }
            auto s = struct_members.find(t); if (s == struct_members.end()) break;
            auto c = consts.find(w[k]); if (c == consts.end() || c->second >= s->second.size()) break;
            t = s->second[c->second];
        }
        var_type[w[i + 2]] = w[i + 1];   // 访问链结果记它自己的指针类型，链上再取链也追得到 | a chain result keeps its own pointer type, so chains of chains are followed
    });
    if (reached) return 0;
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
    // 访问链与复制按出现顺序传递根（SPIR-V 里定义先于使用，一遍够）| chains and copies pass the root along in order (definitions precede uses)
    walk(w, n, [&](size_t i, uint32_t op, uint32_t wc) {
        auto from = [&](uint32_t id) { auto f = root.find(id); return f == root.end() ? 0u : f->second; };
        if ((op == OpAccessChain || op == OpInBoundsAccessChain || op == OpPtrAccessChain || op == OpInBoundsPtrAccessChain) && wc >= 4) { if (uint32_t r = from(w[i + 3])) root[w[i + 2]] = r; }
        else if (op == OpCopyObject && wc == 4) { if (uint32_t r = from(w[i + 3])) root[w[i + 2]] = r; }
        else if (op == OpStore && wc >= 3) { if (uint32_t r = from(w[i + 1])) written.insert(r); }
        else if ((op == OpCopyMemory || op == OpCopyMemorySized) && wc >= 3) { if (uint32_t r = from(w[i + 1])) written.insert(r); }
        else if (op == 228 /* OpAtomicStore：指针在第 1 个操作数 | pointer is operand 1 */ && wc >= 2) { if (uint32_t r = from(w[i + 1])) written.insert(r); }
        else if (is_atomic(op) && wc >= 4) { if (uint32_t r = from(w[i + 3])) written.insert(r); }   // 其余原子操作（含 OpAtomicLoad，保守算写）| the other atomics (OpAtomicLoad included, conservatively)
        else if (op == OpFunctionCall && wc >= 4) { for (uint32_t k = 4; k < wc; ++k) if (uint32_t r = from(w[i + k])) written.insert(r); }
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
