// lane 号改走 ballot 的改写（xbg_spirv_laneid.cpp）：合成模块里入口函数与辅助函数各读一次 SubgroupLocalInvocationId（且有 shuffle），
// 改写后只有入口函数第一块开头有 ballot + ExclusiveScan（GroupOperation 2）位数并存进新增的 Private 变量，各函数的 OpLoad 改读它、结果 id 不变、bound 增加、缺的常量补齐；
// 没有该 builtin 的模块不改。逐车道语义（分歧调用、早退）见 spirv_laneid_semantics_test.cpp。
// clang++ -std=c++17 tests/spirv_laneid_test.cpp xbg_spirv_laneid.cpp -o <仓外产物>
// The lane-id-through-ballot rewrite (xbg_spirv_laneid.cpp): a synthetic module whose entry and helper functions each load SubgroupLocalInvocationId (with a shuffle present);
// after the rewrite only the entry's first block starts with ballot + ExclusiveScan (GroupOperation 2) bit count stored into an added Private variable, every function's OpLoad
// reads that variable, result ids are unchanged, bound grows, missing constants are added; a module without the builtin is left alone. Lane-by-lane semantics (divergent calls,
// early exit): spirv_laneid_semantics_test.cpp.
#include "../xbg_spirv_laneid.h"
#include <cstdio>
#include <stdexcept>
#include <vector>
using Module = std::vector<uint32_t>;
static void require(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }
static void op(Module &m, uint32_t opcode, std::initializer_list<uint32_t> args) { m.push_back((uint32_t(args.size() + 1) << 16) | opcode); m.insert(m.end(), args); }
// 固定 id：1 void 2 fn-type 3 bool 4 uint 5 v4uint 7 ptr-input-uint 12 const0 13 const3 18 lane var 20 main 21 label 22 helper 23 helper-label 30/31 loads 40 shuffle
static Module module(bool with_lane, bool with_consts) {
    Module m{0x07230203, 0x00010300, 0, 64, 0};
    for (uint32_t cap : {1u, 61u, 65u}) op(m, 17, {cap});   // Shader, GroupNonUniform, GroupNonUniformShuffle（无 Ballot：改写要补）| no Ballot: the rewrite must add it
    op(m, 14, {0, 1});
    if (with_lane) op(m, 15, {5, 20, 0x6e69616d, 0, 18}); else op(m, 15, {5, 20, 0x6e69616d, 0});
    op(m, 16, {20, 17, 64, 1, 1});
    if (with_lane) op(m, 71, {18, 11, 41});
    op(m, 19, {1}); op(m, 33, {2, 1}); op(m, 20, {3}); op(m, 21, {4, 32, 0}); if (with_consts) op(m, 23, {5, 4, 4});
    op(m, 32, {7, 1, 4}); op(m, 43, {4, 12, 0}); if (with_consts) { op(m, 43, {4, 13, 3}); op(m, 41, {3, 14}); }
    if (with_lane) op(m, 59, {7, 18, 1});
    op(m, 54, {1, 22, 0, 2}); op(m, 248, {23}); if (with_lane) op(m, 61, {4, 31, 18}); op(m, 253, {}); op(m, 56, {});   // helper
    op(m, 54, {1, 20, 0, 2}); op(m, 248, {21}); op(m, 59, {7, 50, 7}); if (with_lane) op(m, 61, {4, 30, 18}); op(m, 345, {4, 40, 12, 12, 12}); op(m, 253, {}); op(m, 56, {});
    return m;
}
struct Inst { size_t at; uint32_t op, wc; };
static std::vector<Inst> insts(const Module &m) { std::vector<Inst> v; for (size_t i = 5; i < m.size();) { uint32_t wc = m[i] >> 16; v.push_back({i, m[i] & 0xffff, wc}); require(wc && i + wc <= m.size(), "bad framing"); i += wc; } return v; }
int main() { try {
    for (int consts = 0; consts < 2; consts++) {
        Module in = module(true, consts != 0), out; size_t k = xbg_spirv_laneid_ballot(in.data(), in.size(), out);
        require(k == 2, "two lane loads replaced");
        require(out[3] > in[3], "bound grew");
        auto v = insts(out); bool cap = false; int ballots = 0, counts = 0, stores = 0, priv_loads = 0, loads_of_lane = 0; uint32_t cur_fn = 0, count_id = 0, priv = 0;
        for (auto &x : v) if (x.op == 59 && out[x.at + 3] == 6) priv = out[x.at + 2];   // 新增的 Private 变量 | the added Private variable
        require(priv != 0, "Private lane variable declared");
        for (auto &x : v) {
            if (x.op == 17 && out[x.at + 1] == 64) cap = true;
            if (x.op == 54) cur_fn = out[x.at + 2];
            if (x.op == 339) { ballots++; require(cur_fn == 20, "ballot only in the entry"); require(out[x.at + 3] == 13 || !consts, "subgroup scope constant reused when present"); }
            if (x.op == 342) { counts++; require(out[x.at + 4] == 2, "GroupOperation ExclusiveScan = 2"); count_id = out[x.at + 2]; }
            if (x.op == 62 && out[x.at + 1] == priv) { stores++; require(out[x.at + 2] == count_id && cur_fn == 20, "entry stores the scan into the Private variable"); }
            if (x.op == 61 && out[x.at + 3] == 18) loads_of_lane++;
            if (x.op == 61 && out[x.at + 3] == priv) { priv_loads++; require(out[x.at + 2] == 30 || out[x.at + 2] == 31, "result ids preserved"); }
        }
        require(cap && ballots == 1 && counts == 1 && stores == 1 && priv_loads == 2 && loads_of_lane == 0, "ballot capability added; one ballot + count + store in the entry; both loads read the Private variable");
        // main 的第一块：OpLabel、OpVariable 之后才是 ballot、位数、存 | main's first block: ballot, count, store only after OpLabel and the OpVariable
        for (size_t i = 0; i + 1 < v.size(); i++) if (v[i].op == 248 && out[v[i].at + 1] == 21) { require(v[i + 1].op == 59 && v[i + 2].op == 339 && v[i + 3].op == 342 && v[i + 4].op == 62, "ballot after the leading OpVariable"); }
        if (!consts) { int tv4 = 0, c3 = 0, ctrue = 0; for (auto &x : v) { if (x.op == 23 && out[x.at + 3] == 4) tv4++; if (x.op == 43 && out[x.at + 3] == 3) c3++; if (x.op == 41) ctrue++; } require(tv4 == 1 && c3 == 1 && ctrue == 1, "missing v4uint / scope / true added once"); }
    }
    { Module in = module(false, true), out; require(xbg_spirv_laneid_ballot(in.data(), in.size(), out) == 0 && out.empty(), "module without the builtin is unchanged"); }
    { Module in = module(true, true), out; in[0] = 0; require(xbg_spirv_laneid_ballot(in.data(), in.size(), out) == 0, "bad magic rejected"); }
    puts("PASS spirv_laneid: lane loads -> one entry ballot ExclusiveScan stored in a Private variable, ids preserved, declarations added, no-builtin unchanged");
    return 0;
} catch (const std::exception &e) { fprintf(stderr, "FAIL %s\n", e.what()); return 1; } }
