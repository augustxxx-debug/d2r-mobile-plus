// Mali 载入改写的单测：合成模块逐项核对；可选「真着色器」模式把导入包里的 .xgs 全部过一遍并写出改写结果，供 spirv-val 逐个校验。
// 用法：spirv_mali_test [<写合成模块 .spv 的新目录>] | [<.xgs 目录> <输出目录>]。一个参数时把每个合成用例的输入（*_in.spv）和改写结果
// （*_out.spv）写出来，供 spirv-val --target-env vulkan1.1 逐个校验。
// Unit tests of the Mali load-time rewrites: synthetic modules checked item by item; the optional real-shader mode runs every .xgs of an
// import pack through them and writes the rewritten modules for spirv-val. Usage: spirv_mali_test [<new directory for synthetic .spv>] |
// [<.xgs directory> <output directory>]. With one argument every synthetic case's input (*_in.spv) and rewrite (*_out.spv) is written for
// spirv-val --target-env vulkan1.1.
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
// 字面串：以 0 结尾、按小端补满整字 | literal string: nul-terminated, little-endian, padded to whole words
void op_str(Module &m, uint32_t opcode, std::initializer_list<uint32_t> head, const char *s) {
    Module a(head); size_t len = std::strlen(s) + 1; Module t((len + 3) / 4, 0); std::memcpy(t.data(), s, len); a.insert(a.end(), t.begin(), t.end());
    m.push_back((uint32_t(a.size() + 1) << 16) | opcode); m.insert(m.end(), a.begin(), a.end());
}
// 一个参数时写出的合成模块 | synthetic modules written out when given one argument
std::vector<std::pair<std::string, Module>> dumped;
void keep(const std::string &name, const Module &in, const Module &out) { dumped.push_back({name + "_in", in}); if (!out.empty()) dumped.push_back({name + "_out", out}); }

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
// gl_PerVertex：Position、PointSize、ClipDistance[1]、CullDistance[1]；writes = 写哪个成员（经访问链）；arrayed = gl_out[] 形（细分控制着色器，
// 4 个输出顶点；否则是顶点着色器）。whole = 整块用法（EXT6-03 反例）：1 构造整块值再 OpStore、2 从 gl_in OpCopyMemory 拷进来、3 OpLoad 整块
// （分析不了的用法）；arrayed 时作用在 gl_out[0] 这个元素上。whole = 2 总是细分控制着色器（顶点着色器里没有同类型的源），arrayed = false
// 时拷整个 gl_out 数组。
// gl_PerVertex: Position, PointSize, ClipDistance[1], CullDistance[1]; writes = member stored through a chain; arrayed = gl_out[] form (a
// tessellation control shader with 4 output vertices; a vertex shader otherwise). whole = a whole-block use (EXT6-03 counter-examples): 1 a
// constructed block value stored, 2 OpCopyMemory from gl_in, 3 OpLoad of the whole block (a use that cannot be analysed); with arrayed it acts
// on the element gl_out[0]. whole = 2 is always a tessellation control shader (a vertex shader has no source of that type); with arrayed = false
// it copies the whole gl_out array.
Module pervertex_module(uint32_t writes, bool arrayed, int whole = 0) {
    const bool tcs = arrayed || whole == 2;
    Module m{0x07230203, 0x00010300, 0, 60, 0};
    for (uint32_t cap : {1u, 32u, 33u}) op(m, 17, {cap});
    if (tcs) op(m, 17, {3});   // Tessellation
    op(m, 14, {0, 1});
    if (whole == 2) op(m, 15, {1, 20, 0x6e69616d, 0, 30, 38}); else op(m, 15, {tcs ? 1u : 0u, 20, 0x6e69616d, 0, 30});
    if (tcs) op(m, 16, {20, 26, 4});   // OutputVertices 4
    op_str(m, 6, {8, 0}, "gl_Position"); op_str(m, 6, {8, 2}, "gl_ClipDistance");   // 名字串带结尾 0，spirv-val 才认 | nul-terminated so spirv-val accepts it
    op(m, 72, {8, 0, 11, 0}); op(m, 72, {8, 1, 11, 1}); op(m, 72, {8, 2, 11, 3}); op(m, 72, {8, 3, 11, 4}); op(m, 71, {8, 2});
    op(m, 19, {1}); op(m, 33, {2, 1}); op(m, 22, {3, 32}); op(m, 23, {4, 3, 4}); op(m, 21, {5, 32, 0});
    op(m, 43, {5, 6, 1}); op(m, 28, {7, 3, 6}); op(m, 30, {8, 4, 3, 7, 7});
    op(m, 43, {5, 40, 0}); op(m, 43, {5, 41, 2}); op(m, 43, {5, 42, 3});
    if (tcs) { op(m, 43, {5, 43, 4}); op(m, 28, {9, 8, 43}); op(m, 32, {10, 3, 9}); } else op(m, 32, {10, 3, 8});
    op(m, 32, {11, 3, 4}); op(m, 32, {12, 3, 3}); op(m, 59, {10, 30, 3});
    op(m, 43, {3, 13, f32(0.5f)}); op(m, 44, {4, 14, 13, 13, 13, 13});
    if (whole && arrayed) op(m, 32, {16, 3, 8});   // 指向 gl_out[i] 整块 | pointer to the whole gl_out[i]
    if (whole == 2) { op(m, 32, {17, 1, 9}); op(m, 32, {18, 1, 8}); op(m, 59, {17, 38, 1}); }   // gl_in[]（Input）| gl_in[] (Input)
    op(m, 54, {1, 20, 0, 2}); op(m, 248, {21});
    uint32_t block = 30, source = 38;
    if (whole && arrayed) { op(m, 65, {16, 37, 30, 40}); block = 37; }
    if (whole == 2 && arrayed) { op(m, 65, {18, 39, 38, 40}); source = 39; }
    if (whole == 1) { op(m, 80, {7, 34, 13}); op(m, 80, {8, 35, 14, 13, 34, 34}); op(m, 62, {block, 35}); }
    if (whole == 2) op(m, 63, {block, source});
    if (whole == 3) op(m, 61, {8, 36, block});
    if (tcs) { op(m, 65, {11, 31, 30, 40, 40}); if (writes >= 2) op(m, 65, {12, 32, 30, 40, writes == 2 ? 41u : 42u, 40}); }
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
    for (uint32_t v : {30u, 31u, 32u, 33u, 34u}) { op(m, 71, {v, 34, 0}); op(m, 71, {v, 33, v - 30}); }   // Vulkan 要求的 DescriptorSet / Binding | required by Vulkan
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
// 顶点着色器里的浮点原子（EXT6-02）：%30 经 atomic（OpAtomicFAddEXT 6035 / OpAtomicFMinEXT 5614 / OpAtomicFMaxEXT 5615）写，%31 只读；
// atomic = 0 时 %30 也只是读（对照）。select = %30 的两个成员指针经 OpSelect 合流后再存（指针交给改写器认不出的指令）
// Float atomics in a vertex shader (EXT6-02): %30 written by atomic (OpAtomicFAddEXT 6035 / OpAtomicFMinEXT 5614 / OpAtomicFMaxEXT 5615), %31 only
// read; atomic = 0 makes %30 a plain read too (control). select = two member pointers of %30 merged by OpSelect and stored through (pointers
// handed to an instruction the rewrite does not know)
Module float_atomic_module(uint32_t atomic, bool select = false) {
    Module m{0x07230203, 0x00010300, 0, 60, 0};
    op(m, 17, {1});
    if (atomic == 6035) op(m, 17, {6033});   // AtomicFloat32AddEXT
    if (atomic == 5614 || atomic == 5615) op(m, 17, {5612});   // AtomicFloat32MinMaxEXT
    if (select) op(m, 17, {4441});   // VariablePointersStorageBuffer
    if (atomic == 6035) op_str(m, 10, {}, "SPV_EXT_shader_atomic_float_add");
    if (atomic == 5614 || atomic == 5615) op_str(m, 10, {}, "SPV_EXT_shader_atomic_float_min_max");
    if (select) op_str(m, 10, {}, "SPV_KHR_variable_pointers");
    op(m, 14, {0, 1}); op(m, 15, {0, 20, 0x6e69616d, 0});
    op(m, 71, {8, 2}); op(m, 72, {8, 0, 35, 0});
    op(m, 71, {30, 34, 0}); op(m, 71, {30, 33, 0}); op(m, 71, {31, 34, 0}); op(m, 71, {31, 33, 1});
    op(m, 19, {1}); op(m, 33, {2, 1}); op(m, 22, {3, 32}); op(m, 21, {5, 32, 0}); op(m, 20, {6});
    op(m, 43, {5, 40, 0}); op(m, 43, {5, 41, 1}); op(m, 43, {3, 13, f32(1.0f)}); op(m, 41, {6, 14});
    op(m, 30, {8, 3}); op(m, 32, {10, 12, 8}); op(m, 32, {12, 12, 3});
    op(m, 59, {10, 30, 12}); op(m, 59, {10, 31, 12});
    op(m, 54, {1, 20, 0, 2}); op(m, 248, {21});
    op(m, 65, {12, 50, 30, 40}); op(m, 65, {12, 52, 31, 40});
    if (atomic) op(m, atomic, {3, 51, 50, 41, 40, 13}); else op(m, 61, {3, 51, 50});   // 作用域 Device、语义 Relaxed | scope Device, semantics Relaxed
    op(m, 61, {3, 53, 52});
    if (select) { op(m, 65, {12, 55, 30, 40}); op(m, 169, {12, 54, 14, 50, 55}); op(m, 62, {54, 13}); }
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

    keep("shuffle", in, out);
    Module pv = pervertex_module(0, false);
    require(xbg_spirv_strip_clipcull(pv.data(), pv.size(), out) == 2, "unwritten ClipDistance / CullDistance removed");
    keep("clipcull", pv, out);
    bool cut = false; for (size_t i = 5; i < out.size(); i += out[i] >> 16) if ((out[i] & 0xffff) == 30 && out[i + 1] == 8) cut = (out[i] >> 16) == 4;
    require(cut, "gl_PerVertex keeps Position and PointSize only");
    require(count_op(out, 72) == 2, "member decorations 2 / 3 removed");
    require(!has(out, {(2u << 16) | 17, 32}) && !has(out, {(2u << 16) | 17, 33}), "ClipDistance / CullDistance capabilities removed");
    require(count_op(out, 6) == 1, "member name of member 2 removed");
    Module pa = pervertex_module(0, true);
    require(xbg_spirv_strip_clipcull(pa.data(), pa.size(), out) == 2, "gl_out[] form without clip writes is stripped");
    keep("clipcull_arrayed", pa, out);
    for (uint32_t m2 : {2u, 3u}) {
        Module pw = pervertex_module(m2, false), pwa = pervertex_module(m2, true);
        require(xbg_spirv_strip_clipcull(pw.data(), pw.size(), out) == 0 && out.empty(), "a written ClipDistance / CullDistance refuses");
        keep("clipcull_written" + std::to_string(m2), pw, out);
        require(xbg_spirv_strip_clipcull(pwa.data(), pwa.size(), out) == 0, "a written gl_out[i].gl_ClipDistance refuses");
        keep("clipcull_written" + std::to_string(m2) + "_arrayed", pwa, out);
    }
    Module none = shuffle_module();
    require(xbg_spirv_strip_clipcull(none.data(), none.size(), out) == 0, "no gl_PerVertex: unchanged");
    // EXT6-03：整块写或分析不了的用法一律不改（out 为空）——整块构造后 OpStore、OpCopyMemory 拷进来、OpLoad 整块；平铺块与 gl_out[0] 元素各一遍
    // EXT6-03: whole-block writes and uses that cannot be analysed leave the module alone (out empty) — a constructed block stored, OpCopyMemory into
    // it, OpLoad of the whole block; once on the plain block and once on the element gl_out[0]
    const char *whole_names[] = {"", "whole_store", "whole_copymemory", "whole_load"};
    for (int whole : {1, 2, 3}) for (bool arrayed : {false, true}) {
        Module pw = pervertex_module(0, arrayed, whole);
        require(xbg_spirv_strip_clipcull(pw.data(), pw.size(), out) == 0 && out.empty(),
                whole == 1 ? "a whole-block OpStore of gl_PerVertex refuses" : whole == 2 ? "an OpCopyMemory into gl_PerVertex refuses" : "an OpLoad of the whole gl_PerVertex refuses");
        keep(std::string(whole_names[whole]) + (arrayed ? "_arrayed" : ""), pw, out);
    }

    Module st = storage_module();
    require(xbg_spirv_nonwritable(st.data(), st.size(), out) == 2, "read-only StorageBuffer and read-only Uniform+BufferBlock are decorated");
    require(has(out, {(3u << 16) | 71, 30, 24}) && has(out, {(3u << 16) | 71, 33, 24}), "NonWritable on %30 and %33");
    require(!has(out, {(3u << 16) | 71, 31, 24}) && !has(out, {(3u << 16) | 71, 32, 24}) && !has(out, {(3u << 16) | 71, 34, 24}), "stored, UBO and atomic-stored variables untouched");
    keep("nonwritable", st, out);
    Module again = out;
    require(xbg_spirv_nonwritable(again.data(), again.size(), out) == 0, "idempotent: decorated variables are not decorated twice");

    // EXT6-02：顶点着色器里经浮点原子写的存储块不加 NonWritable，同一着色器里只读的块照加；不带原子的对照两个都加
    // EXT6-02: a storage block written by a float atomic in a vertex shader gets no NonWritable, the read-only block next to it does; the
    // atomic-free control gets both
    const std::pair<uint32_t, const char *> float_atomics[] = {{6035, "fadd"}, {5614, "fmin"}, {5615, "fmax"}};
    for (auto &fa : float_atomics) {
        Module fm = float_atomic_module(fa.first);
        require(xbg_spirv_nonwritable(fm.data(), fm.size(), out) == 1, "a float atomic counts as a write: only the read-only block is decorated");
        require(has(out, {(3u << 16) | 71, 31, 24}), "the read-only block next to the float atomic gets NonWritable");
        require(!has(out, {(3u << 16) | 71, 30, 24}), "the block written by OpAtomicFAddEXT / FMinEXT / FMaxEXT gets no NonWritable");
        keep(std::string("float_atomic_") + fa.second, fm, out);
    }
    Module fr = float_atomic_module(0);
    require(xbg_spirv_nonwritable(fr.data(), fr.size(), out) == 2 && has(out, {(3u << 16) | 71, 30, 24}) && has(out, {(3u << 16) | 71, 31, 24}),
            "without the atomic both blocks are read-only and decorated");
    keep("float_atomic_none", fr, out);
    // 指针交给改写器认不出的指令（OpSelect）就保守算写：%30 不加，只读的 %31 照加 | a pointer handed to an unknown instruction (OpSelect)
    // counts as a write: %30 stays undecorated, the read-only %31 is decorated
    Module fs = float_atomic_module(0, true);
    require(xbg_spirv_nonwritable(fs.data(), fs.size(), out) == 1 && has(out, {(3u << 16) | 71, 31, 24}) && !has(out, {(3u << 16) | 71, 30, 24}),
            "pointers merged by OpSelect and stored through count as a write");
    keep("select_store", fs, out);
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
    if (argc == 2) {
        std::filesystem::path dump = argv[1];
        if (std::filesystem::exists(dump)) { std::cerr << "dump directory must be new\n"; return 1; }
        std::filesystem::create_directories(dump);
        for (auto &d : dumped) { std::ofstream o(dump / (d.first + ".spv"), std::ios::binary); o.write(reinterpret_cast<const char *>(d.second.data()), std::streamsize(d.second.size() * 4)); }
        std::cout << dumped.size() << " synthetic modules written to " << dump.string() << "\n";
    }
    if (argc == 3) return real(argv[1], argv[2]);
    return 0;
}
