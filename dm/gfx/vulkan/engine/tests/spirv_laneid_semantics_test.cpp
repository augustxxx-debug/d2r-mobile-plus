// lane 号改写（xbg_spirv_laneid.cpp）的语义回归（一审 A2）：用一个带活跃车道掩码的小 SIMT 解释器（分支、早退、函数调用、ballot / ExclusiveScan）
// 跑改写前后的合成模块，逐车道比对读到的 lane 号。用例：分歧控制流里只有 lane 3/7/11… 调用辅助函数、入口早退后余下车道调用辅助函数、辅助函数多基本块里读、
// 带符号 lane 类型、SPIR-V 1.4 入口接口；每个改写输出另写 .spv 供 spirv-val。
// clang++ -std=c++17 tests/spirv_laneid_semantics_test.cpp xbg_spirv_laneid.cpp -o <仓外产物>; 运行：<产物> [<写 .spv 的新目录>]
// Semantic regression for the lane-id rewrite (first audit A2): a small SIMT interpreter with active-lane masks (branches, early return, function calls,
// ballot / ExclusiveScan) runs synthetic modules before and after the rewrite and compares the lane id every lane reads. Cases: only lanes 3/7/11... call a helper under
// divergent control flow; the lanes left after an early return in the entry call a helper; a helper reading in several basic blocks; a signed lane type; SPIR-V 1.4 entry
// interfaces. Each rewritten output is also written as .spv for spirv-val.
#include "../xbg_spirv_laneid.h"
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
using Module = std::vector<uint32_t>;
void require(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }
void op(Module &m, uint32_t opcode, std::initializer_list<uint32_t> args) { m.push_back((uint32_t(args.size() + 1) << 16) | opcode); m.insert(m.end(), args); }
enum Kind { DivergentCall, EarlyExit, HelperBlocks, SignedLane };
// id：1 void 2 fn() 3 bool 4 uint 5 v4uint 6 uint fn() 7 *Input uint 8 *Private uint 9 *Input int 10 int 12 c0 13 c3 15 c4 16 c1 18 lane 19 index 24 out 25 out2 20 main 22 helper
Module module(Kind kind, bool v14) {
    Module m{0x07230203, v14 ? 0x00010400u : 0x00010300u, 0, 100, 0};
    for (uint32_t cap : {1u, 61u, 65u}) op(m, 17, {cap});
    op(m, 14, {0, 1});
    if (v14) op(m, 15, {5, 20, 0x6e69616d, 0, 18, 19, 24, 25}); else op(m, 15, {5, 20, 0x6e69616d, 0, 18, 19});
    op(m, 16, {20, 17, 64, 1, 1});
    op(m, 71, {18, 11, 41}); op(m, 71, {19, 11, 29});
    op(m, 19, {1}); op(m, 33, {2, 1}); op(m, 20, {3}); op(m, 21, {4, 32, 0}); op(m, 21, {10, 32, 1}); op(m, 23, {5, 4, 4}); op(m, 33, {6, 4});
    op(m, 32, {7, 1, 4}); op(m, 32, {8, 6, 4}); op(m, 32, {9, 1, 10});
    op(m, 43, {4, 12, 0}); op(m, 43, {4, 13, 3}); op(m, 43, {4, 15, 4}); op(m, 43, {4, 16, 1});
    op(m, 59, {kind == SignedLane ? 9u : 7u, 18, 1}); op(m, 59, {7, 19, 1}); op(m, 59, {8, 24, 6}); op(m, 59, {8, 25, 6});
    // 辅助函数 | helper
    op(m, 54, {4, 22, 0, 6}); op(m, 248, {60});
    if (kind == HelperBlocks) {   // 第一块不读；按 index 奇偶分两支，各自在自己的块里读 lane 号再返回 | no read in the first block; split on index parity, each branch reads in its own block
        op(m, 61, {4, 62, 19}); op(m, 199, {4, 63, 62, 16}); op(m, 170, {3, 64, 63, 12});
        op(m, 247, {66, 0}); op(m, 250, {64, 67, 68});
        op(m, 248, {67}); op(m, 61, {4, 69, 18}); op(m, 254, {69});
        op(m, 248, {68}); op(m, 61, {4, 70, 18}); op(m, 254, {70});
        op(m, 248, {66}); op(m, 255, {});
    } else if (kind == SignedLane) { op(m, 61, {10, 61, 18}); op(m, 124, {4, 62, 61}); op(m, 254, {62}); }
    else { op(m, 61, {4, 61, 18}); op(m, 254, {61}); }
    op(m, 56, {});
    // 入口 | entry
    op(m, 54, {1, 20, 0, 2}); op(m, 248, {40}); op(m, 61, {4, 41, 19});
    if (kind == EarlyExit) {   // index < 4 的车道先返回，余下车道调辅助函数，并在后面的块里直接读一次 | lanes with index < 4 return first; the rest call the helper and also read directly in a later block
        op(m, 176, {3, 43, 41, 15}); op(m, 247, {46, 0}); op(m, 250, {43, 44, 46});
        op(m, 248, {44}); op(m, 253, {});
        op(m, 248, {46}); op(m, 57, {4, 45, 22}); op(m, 62, {24, 45}); op(m, 61, {4, 47, 18}); op(m, 62, {25, 47}); op(m, 253, {});
    } else if (kind == HelperBlocks) { op(m, 57, {4, 45, 22}); op(m, 62, {24, 45}); op(m, 253, {}); }
    else {   // 只有 index & 3 == 3（lane 3/7/11…）进分支调辅助函数 | only index & 3 == 3 (lanes 3/7/11...) take the branch and call the helper
        op(m, 199, {4, 42, 41, 13}); op(m, 170, {3, 43, 42, 13}); op(m, 247, {46, 0}); op(m, 250, {43, 44, 46});
        op(m, 248, {44}); op(m, 57, {4, 45, 22}); op(m, 62, {24, 45}); op(m, 249, {46});
        op(m, 248, {46}); op(m, 253, {});
    }
    op(m, 56, {});
    return m;
}
// ---- SIMT 解释器 | SIMT interpreter ----
using V4 = std::array<uint32_t, 4>;
struct Machine {
    const Module &m; unsigned width; std::map<uint32_t, size_t> label_at, fn_first_label; std::map<uint32_t, uint32_t> builtin;
    std::map<uint32_t, std::vector<V4>> val; std::map<uint32_t, std::vector<uint32_t>> mem; uint32_t entry = 0;
    Machine(const Module &mod, unsigned w) : m(mod), width(w) {
        uint32_t cur_fn = 0; bool want = false;
        for (size_t at = 5; at < m.size();) {
            const uint32_t *p = &m[at]; uint32_t wc = p[0] >> 16, code = p[0] & 0xffff; require(wc && at + wc <= m.size(), "framing");
            if (code == 15) entry = p[2];
            if (code == 71 && p[2] == 11) builtin[p[1]] = p[3];
            if (code == 43) val[p[2]] = std::vector<V4>(width, V4{p[3], 0, 0, 0});
            if (code == 41) val[p[2]] = std::vector<V4>(width, V4{1, 0, 0, 0});
            if (code == 59) mem[p[2]] = std::vector<uint32_t>(width, 0xffffffffu);
            if (code == 54) { cur_fn = p[2]; want = true; }
            if (code == 248) { label_at[p[1]] = at; if (want) { fn_first_label[cur_fn] = at; want = false; } }
            at += wc;
        }
    }
    std::vector<V4> &v(uint32_t id) { auto &x = val[id]; if (x.empty()) x.assign(width, V4{}); return x; }
    struct Frame { std::vector<bool> returned; std::vector<uint32_t> ret; };
    void run(Frame &f, size_t at, std::vector<bool> mask, uint32_t stop) {
        for (;;) {
            bool any = false; for (unsigned l = 0; l < width; l++) { mask[l] = mask[l] && !f.returned[l]; any |= mask[l]; } if (!any) return;
            uint32_t merge = 0; at += m[at] >> 16;   // 跳过 OpLabel | skip OpLabel
            for (;;) {
                const uint32_t *p = &m[at]; uint32_t wc = p[0] >> 16, code = p[0] & 0xffff; at += wc;
                auto each = [&](auto fn) { for (unsigned l = 0; l < width; l++) if (mask[l]) fn(l); };
                if (code == 247) { merge = p[1]; continue; }
                if (code == 249) { if (p[1] == stop) return; at = label_at.at(p[1]); break; }
                if (code == 250) {
                    std::vector<bool> mt(width), mf(width); for (unsigned l = 0; l < width; l++) { mt[l] = mask[l] && v(p[1])[l][0]; mf[l] = mask[l] && !v(p[1])[l][0]; }
                    require(merge, "conditional branch without merge");
                    if (p[2] != merge) run(f, label_at.at(p[2]), mt, merge);
                    if (p[3] != merge) run(f, label_at.at(p[3]), mf, merge);
                    for (unsigned l = 0; l < width; l++) mask[l] = (mt[l] || mf[l]) && !f.returned[l];
                    if (merge == stop) return; at = label_at.at(merge); break;
                }
                if (code == 253) { each([&](unsigned l) { f.returned[l] = true; }); return; }
                if (code == 254) { each([&](unsigned l) { f.ret[l] = v(p[1])[l][0]; f.returned[l] = true; }); return; }
                if (code == 255) { each([&](unsigned) { require(false, "reached OpUnreachable"); }); return; }
                if (code == 57) { Frame g{std::vector<bool>(width), std::vector<uint32_t>(width, 0xdeadu)}; run(g, fn_first_label.at(p[3]), mask, 0); each([&](unsigned l) { v(p[2])[l][0] = g.ret[l]; }); continue; }
                if (code == 59) { mem[p[2]].assign(width, 0); continue; }
                if (code == 61) { auto b = builtin.find(p[3]); each([&](unsigned l) { v(p[2])[l] = V4{b != builtin.end() ? (b->second == 41 || b->second == 29 ? l : throw std::runtime_error("builtin")) : mem.at(p[3])[l], 0, 0, 0}; }); continue; }
                if (code == 62) { each([&](unsigned l) { mem.at(p[1])[l] = v(p[2])[l][0]; }); continue; }
                if (code == 83 || code == 124) { each([&](unsigned l) { v(p[2])[l] = v(p[3])[l]; }); continue; }
                if (code == 170 || code == 176 || code == 199) { each([&](unsigned l) { uint32_t a = v(p[3])[l][0], b = v(p[4])[l][0]; v(p[2])[l][0] = code == 170 ? a == b : code == 176 ? a < b : a & b; }); continue; }
                if (code == 339) { V4 b{}; each([&](unsigned l) { if (v(p[4])[l][0]) b[l / 32] |= 1u << (l % 32); }); each([&](unsigned l) { v(p[2])[l] = b; }); continue; }
                if (code == 342) { require(p[4] == 2 || p[4] == 1 || p[4] == 0, "group op"); each([&](unsigned l) { V4 b = v(p[5])[l]; uint32_t c = 0;
                    for (unsigned x = 0; x < 128; x++) if ((b[x / 32] >> (x % 32)) & 1) { if (p[4] == 0 || x < l || (p[4] == 1 && x == l)) c++; } v(p[2])[l][0] = c; }); continue; }
                throw std::runtime_error("unhandled opcode " + std::to_string(code));
            }
        }
    }
    void exec() { Frame f{std::vector<bool>(width), std::vector<uint32_t>(width)}; run(f, fn_first_label.at(entry), std::vector<bool>(width, true), 0); }
};
std::vector<uint32_t> outputs(const Module &mod, unsigned width, uint32_t var) { Machine x(mod, width); x.exec(); return x.mem.at(var); }
}
int main(int argc, char **argv) { try {
    std::filesystem::path dump = argc > 1 ? argv[1] : "";
    if (!dump.empty()) { require(!std::filesystem::exists(dump), "dump directory must be new"); std::filesystem::create_directories(dump); }
    const char *names[] = {"divergent_call", "early_exit", "helper_blocks", "signed_lane"};
    int checked = 0;
    for (int k = 0; k < 4; k++) for (int v14 = 0; v14 < 2; v14++) for (unsigned width : {32u, 64u}) {
        Module in = module(Kind(k), v14 != 0), out; size_t n = xbg_spirv_laneid_ballot(in.data(), in.size(), out);
        require(n >= 1, "rewritten");
        for (uint32_t var : {24u, 25u}) {
            auto want = outputs(in, width, var), got = outputs(out, width, var);
            for (unsigned l = 0; l < width; l++) if (want[l] != got[l]) {
                char b[160]; snprintf(b, sizeof b, "%s v1.%d width %u var %u lane %u: builtin %u, rewritten %u", names[k], v14 ? 4 : 3, width, var, l, want[l], got[l]); throw std::runtime_error(b); }
            for (unsigned l = 0; l < width; l++) if (var == 24 && k == DivergentCall) require(((l & 3) == 3) == (want[l] == l), "fixture: only lanes 3/7/11... call the helper");
            ++checked;
        }
        if (v14) {   // 1.4 起入口接口要列出全部用到的全局变量：改写新增的 Private 变量也要在里面 | from 1.4 the entry interface lists every global used, including a Private variable the rewrite adds
            size_t at = 5; while (at < out.size() && (out[at] & 0xffff) != 15) at += out[at] >> 16; require(at < out.size(), "entry point");
            uint32_t wc = out[at] >> 16; std::vector<uint32_t> iface(out.begin() + long(at) + 5, out.begin() + long(at + wc)); require(iface.size() == 5 && iface.back() != 18 && iface.back() != 19 && iface.back() != 24 && iface.back() != 25, "interface gained the rewrite's variable");
        }
        if (!dump.empty() && width == 32) { std::ofstream f(dump / (std::string(names[k]) + (v14 ? "_v14" : "_v13") + ".spv"), std::ios::binary); f.write(reinterpret_cast<const char *>(out.data()), std::streamsize(out.size() * 4)); }
    }
    printf("PASS spirv_laneid_semantics: %d lane-by-lane comparisons (divergent helper call, early exit, multi-block helper, signed lane; SPIR-V 1.3/1.4; subgroup 32/64)\n", checked);
    return 0;
} catch (const std::exception &e) { fprintf(stderr, "FAIL %s\n", e.what()); return 1; } }
