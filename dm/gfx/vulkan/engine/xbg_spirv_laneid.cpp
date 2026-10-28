// 说明见 xbg_spirv_laneid.h。| See xbg_spirv_laneid.h.
#include "xbg_spirv_laneid.h"

namespace {
enum : uint32_t {
    OpCapability = 17, OpTypeBool = 20, OpTypeInt = 21, OpTypeVector = 23, OpConstantTrue = 41, OpConstant = 43,
    OpFunction = 54, OpVariable = 59, OpLoad = 61, OpDecorate = 71, OpCopyObject = 83, OpBitcast = 124, OpLabel = 248,
    OpGroupNonUniformBallot = 339, OpGroupNonUniformBallotBitCount = 342,
    CapGroupNonUniformBallot = 64, DecBuiltIn = 11, BuiltInSubgroupLocalInvocationId = 41, ScopeSubgroup = 3, GroupOpExclusiveScan = 1,
};
inline uint32_t inst(uint32_t op, uint32_t words) { return (words << 16) | op; }
}

size_t xbg_spirv_laneid_ballot(const uint32_t *in, size_t n, std::vector<uint32_t> &out) {
    out.clear();
    if (!in || n < 5 || in[0] != 0x07230203u) return 0;
    uint32_t bound = in[3];
    // 第 1 遍：builtin 变量、类型 / 常量、第一个 capability 与第一个函数的位置 | pass 1: the builtin variable, types / constants, first capability and first function
    uint32_t lane_var = 0, t_bool = 0, t_uint = 0, t_v4u = 0, c_true = 0, c_sub = 0;
    size_t first_cap = 0, first_func = 0; bool has_cap = false;
    std::vector<uint32_t> int_width(bound, 0), int_sign(bound, 0), ptr_to(bound, 0);
    for (size_t i = 5; i < n;) {
        uint32_t wc = in[i] >> 16, op = in[i] & 0xffff;
        if (!wc || i + wc > n) return 0;
        if (op == OpFunction) { first_func = i; break; }
        if (op == OpCapability) { if (!first_cap) first_cap = i; if (in[i + 1] == CapGroupNonUniformBallot) has_cap = true; }
        else if (op == OpDecorate && wc == 4 && in[i + 2] == DecBuiltIn && in[i + 3] == BuiltInSubgroupLocalInvocationId && in[i + 1] < bound) lane_var = in[i + 1];
        else if (op == OpTypeBool && wc == 2 && !t_bool) t_bool = in[i + 1];
        else if (op == OpTypeInt && wc == 4 && in[i + 1] < bound) { int_width[in[i + 1]] = in[i + 2]; int_sign[in[i + 1]] = in[i + 3]; if (in[i + 2] == 32 && !in[i + 3] && !t_uint) t_uint = in[i + 1]; }
        else if (op == OpTypeVector && wc == 4 && t_uint && in[i + 2] == t_uint && in[i + 3] == 4 && !t_v4u) t_v4u = in[i + 1];
        else if (op == 32 && wc == 4 && in[i + 1] < bound) ptr_to[in[i + 1]] = in[i + 3];   // OpTypePointer
        else if (op == OpConstantTrue && wc == 3 && !c_true) c_true = in[i + 2];
        else if (op == OpConstant && wc == 4 && t_uint && in[i + 1] == t_uint && in[i + 3] == ScopeSubgroup && !c_sub) c_sub = in[i + 2];
        i += wc;
    }
    if (!lane_var || !first_func || !first_cap) return 0;
    // 第 2 遍（声明区）：lane 变量的类型必须是 32 位整数 | the lane variable must be a 32-bit integer
    uint32_t lane_t = 0;
    for (size_t i = 5; i < first_func;) { uint32_t wc = in[i] >> 16, op = in[i] & 0xffff; if (op == OpVariable && wc >= 4 && in[i + 2] == lane_var && in[i + 1] < bound) lane_t = ptr_to[in[i + 1]]; i += wc; }
    if (!lane_t || lane_t >= bound || int_width[lane_t] != 32) return 0;
    // 补声明 | missing declarations
    std::vector<uint32_t> add;
    if (!t_uint) { t_uint = bound++; add.insert(add.end(), {inst(OpTypeInt, 4), t_uint, 32, 0}); }
    if (!t_bool) { t_bool = bound++; add.insert(add.end(), {inst(OpTypeBool, 2), t_bool}); }
    if (!t_v4u) { t_v4u = bound++; add.insert(add.end(), {inst(OpTypeVector, 4), t_v4u, t_uint, 4}); }
    if (!c_true) { c_true = bound++; add.insert(add.end(), {inst(OpConstantTrue, 3), t_bool, c_true}); }
    if (!c_sub) { c_sub = bound++; add.insert(add.end(), {inst(OpConstant, 4), t_uint, c_sub, ScopeSubgroup}); }
    // 第 3 遍：输出 | pass 3: emit
    out.reserve(n + add.size() + 64);
    out.insert(out.end(), in, in + 5);
    size_t replaced = 0; uint32_t fn_lane = 0; bool in_function = false, want_label = false;
    for (size_t i = 5; i < n;) {
        uint32_t wc = in[i] >> 16, op = in[i] & 0xffff;
        if (i == first_cap && !has_cap) { out.insert(out.end(), {inst(OpCapability, 2), CapGroupNonUniformBallot}); has_cap = true; }
        if (i == first_func) out.insert(out.end(), add.begin(), add.end());
        if (op == OpFunction) { in_function = true; want_label = true; fn_lane = 0; }
        if (op == OpLabel && want_label) {   // 函数第一块：OpLabel、开头的 OpVariable 之后插入 ballot + 位数 | first block: after OpLabel and the leading OpVariables
            want_label = false; out.insert(out.end(), in + i, in + i + wc); i += wc;
            while (i < n && (in[i] & 0xffff) == OpVariable) { uint32_t vc = in[i] >> 16; out.insert(out.end(), in + i, in + i + vc); i += vc; }
            uint32_t ballot = bound++; fn_lane = bound++;
            out.insert(out.end(), {inst(OpGroupNonUniformBallot, 5), t_v4u, ballot, c_sub, c_true, inst(OpGroupNonUniformBallotBitCount, 6), t_uint, fn_lane, c_sub, GroupOpExclusiveScan, ballot});
            continue;
        }
        if (op == OpLoad && wc >= 4 && in[i + 3] == lane_var && in_function && fn_lane) {
            uint32_t rtype = in[i + 1], rid = in[i + 2];
            if (rtype == t_uint) out.insert(out.end(), {inst(OpCopyObject, 4), rtype, rid, fn_lane});
            else out.insert(out.end(), {inst(OpBitcast, 4), rtype, rid, fn_lane});   // 带符号的 32 位整数 | signed 32-bit integer
            ++replaced; i += wc; continue;
        }
        out.insert(out.end(), in + i, in + i + wc); i += wc;
    }
    if (!replaced) { out.clear(); return 0; }
    out[3] = bound;
    return replaced;
}
