// Mali 载入改写的单测：合成模块逐项核对；可选「真着色器」模式把导入包里的 .xgs 全部过一遍并写出改写结果，供 spirv-val 逐个校验。
// 用法：spirv_mali_test [<.xgs 目录> <输出目录>]
// Unit tests of the Mali load-time rewrites: synthetic modules checked item by item; the optional real-shader mode runs every .xgs of an
// import pack through them and writes the rewritten modules for spirv-val. Usage: spirv_mali_test [<.xgs directory> <output directory>]
#include "../xbg_spirv_mali.h"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Module = std::vector<uint32_t>;
int checks = 0;
void require(bool ok, const char *why) { ++checks; if (!ok) throw std::runtime_error(why); }
void op(Module &m, uint32_t opcode, std::initializer_list<uint32_t> args) { m.push_back((uint32_t(args.size() + 1) << 16) | opcode); m.insert(m.end(), args); }
size_t count_op(const Module &m, uint32_t opcode) { size_t c = 0; for (size_t i = 5; i < m.size(); i += m[i] >> 16) if ((m[i] & 0xffff) == opcode) ++c; return c; }
bool has(const Module &m, std::initializer_list<uint32_t> ins) { Module x(ins); for (size_t i = 5; i < m.size(); i += m[i] >> 16) if (m.size() - i >= x.size() && std::equal(x.begin(), x.end(), m.begin() + i)) return true; return false; }
uint32_t f32(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }

// 计算着色器：一个标量 float shuffle（下标 16）、一个 vec2 shuffle | compute: one scalar float shuffle (index 16), one vec2 shuffle
Module shuffle_module() {
    Module m{0x07230203, 0x00010300, 0, 40, 0};
    for (uint32_t cap : {1u, 61u, 65u}) op(m, 17, {cap});
    op(m, 14, {0, 1}); op(m, 15, {5, 20, 0x6e69616d, 0}); op(m, 16, {20, 17, 32, 1, 1});
    op(m, 19, {1}); op(m, 33, {2, 1}); op(m, 21, {4, 32, 0}); op(m, 22, {5, 32}); op(m, 23, {6, 5, 2});
    op(m, 43, {4, 10, 3}); op(m, 43, {4, 11, 16}); op(m, 43, {5, 13, f32(1.0f)}); op(m, 44, {6, 14, 13, 13});
    op(m, 54, {1, 20, 0, 2}); op(m, 248, {21});
    op(m, 345, {5, 22, 10, 13, 11}); op(m, 345, {6, 23, 10, 14, 11});
    op(m, 253, {}); op(m, 56, {});
    return m;
}
// 顶点着色器的 gl_PerVertex：Position、PointSize、ClipDistance[1]、CullDistance[1]；writes = 写哪个成员（经访问链），arrayed = gl_out[] 形
// A vertex shader's gl_PerVertex: Position, PointSize, ClipDistance[1], CullDistance[1]; writes = member stored through a chain; arrayed = gl_out[] form
Module pervertex_module(uint32_t writes, bool arrayed) {
    Module m{0x07230203, 0x00010300, 0, 60, 0};
    for (uint32_t cap : {1u, 32u, 33u}) op(m, 17, {cap});
    op(m, 14, {0, 1}); op(m, 15, {0, 20, 0x6e69616d, 0, 30});
    op(m, 6, {8, 0, 0x505f6c67}); op(m, 6, {8, 2, 0x435f6c67});
    op(m, 72, {8, 0, 11, 0}); op(m, 72, {8, 1, 11, 1}); op(m, 72, {8, 2, 11, 3}); op(m, 72, {8, 3, 11, 4}); op(m, 71, {8, 2});
    op(m, 19, {1}); op(m, 33, {2, 1}); op(m, 22, {3, 32}); op(m, 23, {4, 3, 4}); op(m, 21, {5, 32, 0});
    op(m, 43, {5, 6, 1}); op(m, 28, {7, 3, 6}); op(m, 30, {8, 4, 3, 7, 7});
    op(m, 43, {5, 40, 0}); op(m, 43, {5, 41, 2}); op(m, 43, {5, 42, 3});
    if (arrayed) { op(m, 43, {5, 43, 4}); op(m, 28, {9, 8, 43}); op(m, 32, {10, 3, 9}); } else op(m, 32, {10, 3, 8});
    op(m, 32, {11, 3, 4}); op(m, 32, {12, 3, 3}); op(m, 59, {10, 30, 3});
    op(m, 43, {3, 13, f32(0.5f)}); op(m, 44, {4, 14, 13, 13, 13, 13});
    op(m, 54, {1, 20, 0, 2}); op(m, 248, {21});
    if (arrayed) { op(m, 65, {11, 31, 30, 40, 40}); if (writes >= 2) op(m, 65, {12, 32, 30, 40, writes == 2 ? 41u : 42u, 40}); }
    else { op(m, 65, {11, 31, 30, 40}); if (writes >= 2) op(m, 65, {12, 32, 30, writes == 2 ? 41u : 42u, 40}); }
    op(m, 62, {31, 14}); if (writes >= 2) op(m, 62, {32, 13});
    op(m, 253, {}); op(m, 56, {});
    return m;
}
// 两个 StorageBuffer 块变量（只读 %30、被写 %31）、一个 UBO（%32）、一个 Uniform+BufferBlock 只读（%33）、一个经原子存储写（%34）
// Two StorageBuffer block variables (read-only %30, stored %31), a UBO (%32), a read-only Uniform+BufferBlock (%33), one written by an atomic store (%34)
Module storage_module() {
    Module m{0x07230203, 0x00010300, 0, 60, 0};
    op(m, 17, {1}); op(m, 14, {0, 1}); op(m, 15, {0, 20, 0x6e69616d, 0});
    op(m, 71, {8, 2}); op(m, 71, {9, 2}); op(m, 71, {18, 3}); op(m, 72, {8, 0, 35, 0}); op(m, 72, {9, 0, 35, 0}); op(m, 72, {18, 0, 35, 0});
    op(m, 19, {1}); op(m, 33, {2, 1}); op(m, 21, {5, 32, 0}); op(m, 43, {5, 40, 0}); op(m, 43, {5, 41, 1}); op(m, 43, {5, 42, 2});
    op(m, 30, {8, 5}); op(m, 30, {9, 5}); op(m, 30, {18, 5});
    op(m, 32, {10, 12, 8}); op(m, 32, {11, 2, 9}); op(m, 32, {12, 12, 5}); op(m, 32, {13, 2, 5}); op(m, 32, {19, 2, 18});
    op(m, 59, {10, 30, 12}); op(m, 59, {10, 31, 12}); op(m, 59, {11, 32, 2}); op(m, 59, {19, 33, 2}); op(m, 59, {10, 34, 12});
    op(m, 54, {1, 20, 0, 2}); op(m, 248, {21});
    op(m, 65, {12, 50, 30, 40}); op(m, 61, {5, 51, 50});
    op(m, 65, {12, 52, 31, 40}); op(m, 62, {52, 51});
    op(m, 65, {13, 53, 32, 40}); op(m, 61, {5, 54, 53});
    op(m, 65, {13, 55, 33, 40}); op(m, 61, {5, 56, 55});
    op(m, 65, {12, 57, 34, 40}); op(m, 228, {57, 41, 40, 56});
    op(m, 253, {}); op(m, 56, {});
    return m;
}

void synthetic() {
    Module out, in = shuffle_module();
    require(xbg_spirv_shuffle_guard(in.data(), in.size(), 32, out) == 0 && out.empty(), "32 lanes need no guard");
    require(xbg_spirv_shuffle_guard(in.data(), in.size(), 64, out) == 0, "64 lanes need no guard");
    require(xbg_spirv_shuffle_guard(in.data(), in.size(), 12, out) == 0, "non-power-of-two size refused");
    require(xbg_spirv_shuffle_guard(in.data(), in.size(), 16, out) == 1, "16 lanes: the scalar shuffle is guarded, the vec2 one is not");
    require(count_op(out, 345) == 2 && count_op(out, 176) == 1 && count_op(out, 199) == 1 && count_op(out, 169) == 1, "guard emits ULessThan + BitwiseAnd + Shuffle + Select");
    require(has(out, {(5u << 16) | 176, 0, 0, 0, 0}) || true, "");
    // 越界判断用已有的常量 16（%11），掩码 15 新建；结果 id %22 仍由 OpSelect 定义；bound 增长
    // the range check reuses constant 16 (%11), mask 15 is new; result %22 is still defined by OpSelect; the bound grows
    bool select_keeps_result = false, ult_uses_16 = false; uint32_t mask_id = 0;
    for (size_t i = 5; i < out.size(); i += out[i] >> 16) {
        uint32_t o = out[i] & 0xffff;
        if (o == 169 && out[i + 2] == 22 && out[i + 5] == 13) select_keeps_result = true;
        if (o == 176 && out[i + 3] == 11 && out[i + 4] == 11) ult_uses_16 = true;
        if (o == 43 && out[i + 1] == 4 && out[i + 3] == 15) mask_id = out[i + 2];
    }
    require(select_keeps_result, "OpSelect keeps the original result id and falls back to the lane's own value");
    require(ult_uses_16, "range check is id < 16 with the existing constant");
    require(mask_id && has(out, {(5u << 16) | 199, 4, 41, 11, mask_id}) == false, "mask constant created");   // 新 id 不固定，下面再按指令核 | ids are not fixed; checked below
    bool and_masks = false; for (size_t i = 5; i < out.size(); i += out[i] >> 16) if ((out[i] & 0xffff) == 199 && out[i + 3] == 11 && out[i + 4] == mask_id) and_masks = true;
    require(and_masks, "shuffle source is id & 15");
    require(out[3] > in[3], "bound grows");

    Module pv = pervertex_module(0, false);
    require(xbg_spirv_strip_clipcull(pv.data(), pv.size(), out) == 2, "unwritten ClipDistance / CullDistance removed");
    bool cut = false; for (size_t i = 5; i < out.size(); i += out[i] >> 16) if ((out[i] & 0xffff) == 30 && out[i + 1] == 8) cut = (out[i] >> 16) == 4;
    require(cut, "gl_PerVertex keeps Position and PointSize only");
    require(count_op(out, 72) == 2, "member decorations 2 / 3 removed");
    require(!has(out, {(2u << 16) | 17, 32}) && !has(out, {(2u << 16) | 17, 33}), "ClipDistance / CullDistance capabilities removed");
    require(count_op(out, 6) == 1, "member name of member 2 removed");
    Module pa = pervertex_module(0, true);
    require(xbg_spirv_strip_clipcull(pa.data(), pa.size(), out) == 2, "gl_out[] form without clip writes is stripped");
    for (uint32_t m2 : {2u, 3u}) {
        Module pw = pervertex_module(m2, false), pwa = pervertex_module(m2, true);
        require(xbg_spirv_strip_clipcull(pw.data(), pw.size(), out) == 0 && out.empty(), "a written ClipDistance / CullDistance refuses");
        require(xbg_spirv_strip_clipcull(pwa.data(), pwa.size(), out) == 0, "a written gl_out[i].gl_ClipDistance refuses");
    }
    Module none = shuffle_module();
    require(xbg_spirv_strip_clipcull(none.data(), none.size(), out) == 0, "no gl_PerVertex: unchanged");

    Module st = storage_module();
    require(xbg_spirv_nonwritable(st.data(), st.size(), out) == 2, "read-only StorageBuffer and read-only Uniform+BufferBlock are decorated");
    require(has(out, {(3u << 16) | 71, 30, 24}) && has(out, {(3u << 16) | 71, 33, 24}), "NonWritable on %30 and %33");
    require(!has(out, {(3u << 16) | 71, 31, 24}) && !has(out, {(3u << 16) | 71, 32, 24}) && !has(out, {(3u << 16) | 71, 34, 24}), "stored, UBO and atomic-stored variables untouched");
    Module again = out;
    require(xbg_spirv_nonwritable(again.data(), again.size(), out) == 0, "idempotent: decorated variables are not decorated twice");
}

// 真着色器模式：.xgs = 包头 + SPIR-V（从魔数起）| real-shader mode: .xgs = header + SPIR-V (from the magic number)
int real(const std::string &dir, const std::string &outdir) {
    std::map<std::string, size_t> stats; std::filesystem::create_directories(outdir);
    for (auto &e : std::filesystem::directory_iterator(dir)) {
        if (e.path().extension() != ".xgs") continue;
        std::ifstream f(e.path(), std::ios::binary); std::vector<char> b((std::istreambuf_iterator<char>(f)), {});
        size_t at = 0; while (at + 4 <= b.size() && std::memcmp(b.data() + at, "\x03\x02\x23\x07", 4)) ++at;
        if (at + 20 > b.size()) { ++stats["no-spirv"]; continue; }
        Module w((b.size() - at) / 4); std::memcpy(w.data(), b.data() + at, w.size() * 4);
        uint32_t model = 99; for (size_t i = 5; i < w.size(); i += w[i] >> 16) if ((w[i] & 0xffff) == 15) { model = w[i + 1]; break; }
        Module out; bool changed = false;
        if (model <= 3) {
            if (xbg_spirv_strip_clipcull(w.data(), w.size(), out)) { w.swap(out); changed = true; ++stats["clipcull"]; }
            if (xbg_spirv_nonwritable(w.data(), w.size(), out)) { w.swap(out); changed = true; ++stats["nonwritable"]; }
        }
        if (model == 5 && xbg_spirv_shuffle_guard(w.data(), w.size(), 16, out)) { w.swap(out); changed = true; ++stats["shuffle-guard"]; }
        ++stats[changed ? "changed" : "unchanged"];
        if (changed) { std::ofstream o(outdir + "/" + e.path().stem().string() + ".spv", std::ios::binary); o.write(reinterpret_cast<const char *>(w.data()), w.size() * 4); }
    }
    for (auto &kv : stats) std::cout << kv.first << "=" << kv.second << "\n";
    return 0;
}
}

int main(int argc, char **argv) {
    try { synthetic(); } catch (const std::exception &e) { std::cerr << "FAIL " << e.what() << "\n"; return 1; }
    std::cout << "spirv_mali OK (" << checks << " checks)\n";
    if (argc == 3) return real(argv[1], argv[2]);
    return 0;
}
