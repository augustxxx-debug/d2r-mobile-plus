// 直接调用生产的 R16 比较采样视图判断；只做整数/布尔运算，不需要 Vulkan 或 GPU。
// Exercises the production R16 compare-sampling view decision directly; integer and boolean work only, no
// Vulkan or GPU.
#include "../xbg_compare_view.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <vector>

static int failures;
#define CHECK(expression) do { if(!(expression)){printf("FAIL line %d: %s\n",__LINE__,#expression);++failures;} } while(0)

// 一台「D16 完全可用」的设备：Adreno 740 的 NVN caps 里 124 号（D16_UNORM）确实带采样，线性过滤要看具体驱动，
// 所以两个变体都测。
// A device where D16 is fully usable; Adreno caps differ on linear filtering, so both variants are tested.
static xbg_compare_view_input r16(bool compare, bool linear) {
    xbg_compare_view_input in{};
    in.nvn_format = 0x6;      // NVN_FORMAT_R16 -> VK_FORMAT_R16_UNORM
    in.target = 1;            // NVN_TEXTURE_TARGET_2D
    in.data_bytes = 128 * 128 * 2;   // B078 那张 128x128 R16
    in.color_aspect = true;
    in.layers = 1;
    in.multisampled = false;
    in.compare = compare;
    in.linear = linear;
    in.d16_sampled = true;
    in.d16_compare = true;
    in.d16_linear = true;
    return in;
}

// Fold8 上的唯一 R16 比较用例：serial 44，NVN target 8（立方体），128x128，6 层，共 6 张面。
// The only R16 compare case on Fold8: serial 44, NVN target 8 (cube), 128x128, 6 layers, six faces.
static xbg_compare_view_input cube_r16(uint32_t layers, uint32_t target = 8) {
    xbg_compare_view_input in = r16(true, false);
    in.target = target;   // 8 = 立方体，9 = 立方体数组 | 8 = cube, 9 = cube array
    in.layers = layers;
    in.data_bytes = xbg_compare_mirror_bytes(128, 128, 1, layers);
    return in;
}

// 把整张副本的拷贝区域填进一块模拟的 arena：中转是 Arena::alloc 的子分配，它从 base 开始，前后都是别人的数据。
// 逐条核对地址必须是 base + 相对偏移、首尾相接覆盖 [base, base+bytes)，并且这段之外一个字节都不许动。
// Replays the copy's regions into a simulated arena: the staging buffer is an Arena::alloc sub-allocation that starts
// at base, with other tasks' bytes in front and behind. Every region must be base + its offset inside the copy, the
// regions must tile [base, base+bytes), and nothing outside that window may change.
static void check_copy_window(uint64_t base, uint32_t w, uint32_t h, uint32_t levels, uint32_t layers) {
    const uint64_t bytes = xbg_compare_mirror_bytes(w, h, levels, layers);
    const uint32_t count = xbg_compare_mirror_regions(levels, layers);
    const uint64_t head = 4096, tail = 4096;   // 别人的数据 | other tasks' bytes
    std::vector<uint8_t> arena(size_t(base + bytes + head + tail), 0xAB);
    std::vector<xbg_mirror_region> plan(count);
    uint64_t relative = 0;
    const uint32_t got = xbg_compare_mirror_regions_fill(base, w, h, levels, layers, plan.data(), count, &relative);
    bool ok = got == count && relative == bytes;
    uint64_t expect = 0;
    for(uint32_t i = 0; ok && i < got; i++) {
        const uint64_t region = uint64_t(plan[i].width) * plan[i].height * 2;
        // 相对偏移首尾相接、绝对地址 = 基址 + 相对偏移、不越出中转窗口
        // contiguous relative offsets, absolute = base + relative, inside the staging window
        ok = plan[i].relative_offset == expect && plan[i].buffer_offset == base + expect &&
             plan[i].buffer_offset + region <= base + bytes;
        if(!ok) printf("FAIL copy window base=%llu %ux%u lv=%u ly=%u region %u at %llu/%llu\n",
                       (unsigned long long)base, w, h, levels, layers, i,
                       (unsigned long long)plan[i].buffer_offset, (unsigned long long)(base + expect));
        expect += region;
    }
    if(ok && expect != bytes) { ok = false; printf("FAIL copy window base=%llu bytes=%llu\n",
                                                   (unsigned long long)base, (unsigned long long)expect); }
    if(ok) {   // 图像 -> 中转：只有 [base, base+bytes) 会变 | image -> staging: only the window changes
        for(uint32_t i = 0; i < got; i++) {
            const uint64_t region = uint64_t(plan[i].width) * plan[i].height * 2;
            memset(&arena[size_t(plan[i].buffer_offset)], 0x5A, size_t(region));
        }
        for(uint64_t i = 0; i < base + bytes + head + tail; i++) {
            const uint8_t want = (i >= base && i < base + bytes) ? 0x5A : 0xAB;
            if(arena[size_t(i)] != want) {
                ok = false;
                printf("FAIL copy window base=%llu %ux%u lv=%u ly=%u changed byte %llu (%llu..%llu)\n",
                       (unsigned long long)base, w, h, levels, layers, (unsigned long long)i,
                       (unsigned long long)base, (unsigned long long)(base + bytes));
                break;
            }
        }
    }
    if(!ok) ++failures;
}


int main(void) {
    // 没开比较采样：任何格式都保持原视图，这条路径一个字都不许改（B078 里同一张贴图也有非比较用法）。
    // No comparison: every format keeps its view; this path must not change at all (B078: the same texture is
    // also sampled without comparison).
    CHECK(xbg_compare_view_plan_for(r16(false, false)) == XBG_COMPARE_VIEW_COLOR);
    CHECK(xbg_compare_view_plan_for(r16(false, true)) == XBG_COMPARE_VIEW_COLOR);
    {   // 设备连 D16 都不支持时，没开比较仍然不受影响 | a device without D16 at all still keeps the colour view
        xbg_compare_view_input in = r16(false, false);
        in.d16_sampled = in.d16_compare = in.d16_linear = false;
        CHECK(xbg_compare_view_plan_for(in) == XBG_COMPARE_VIEW_COLOR);
    }

    // 本来就是深度格式的贴图：比较采样合法，不能换成副本。
    // An image that is already a depth format: comparison is legal, and must not be mirrored.
    {   xbg_compare_view_input in = r16(true, true); in.color_aspect = false;
        CHECK(xbg_compare_view_plan_for(in) == XBG_COMPARE_VIEW_COLOR); }

    // 目标场景：R16_UNORM 视图 + 比较采样 + D16 可用 -> 改用 D16 深度副本。
    // The case at hand: R16_UNORM view + comparison + usable D16 -> use a D16 depth copy.
    CHECK(xbg_compare_view_plan_for(r16(true, false)) == XBG_COMPARE_VIEW_DEPTH_MIRROR);
    CHECK(xbg_compare_view_plan_for(r16(true, true)) == XBG_COMPARE_VIEW_DEPTH_MIRROR);

    // 设备上的 D16 不支持深度比较（验证层会照报不误）：不许硬换。
    // The device's D16 lacks depth comparison (the layer would report it just the same): do not force it.
    {   xbg_compare_view_input in = r16(true, false); in.d16_compare = false;
        CHECK(xbg_compare_view_plan_for(in) == XBG_COMPARE_VIEW_UNAVAILABLE);
        CHECK(!__builtin_strcmp(xbg_compare_view_unavailable_reason(in), "d16-no-depth-compare")); }
    {   xbg_compare_view_input in = r16(true, false); in.d16_sampled = false;
        CHECK(xbg_compare_view_plan_for(in) == XBG_COMPARE_VIEW_UNAVAILABLE);
        CHECK(!__builtin_strcmp(xbg_compare_view_unavailable_reason(in), "d16-not-sampled")); }

    // 采样器要线性过滤而 D16 不支持：换上 D16 会撞上采样器的线性过滤检查（xbg_fatalf），
    // 所以只能保持旧路径并记下原因，不许崩。
    // The sampler asks for linear filtering and D16 lacks it: switching to D16 would trip the sampler's
    // linear-filtering check (xbg_fatalf), so keep the old path and record why, never crash.
    {   xbg_compare_view_input in = r16(true, true); in.d16_linear = false;
        CHECK(xbg_compare_view_plan_for(in) == XBG_COMPARE_VIEW_UNAVAILABLE);
        CHECK(!__builtin_strcmp(xbg_compare_view_unavailable_reason(in), "d16-no-linear-filter")); }
    {   // 同一个设备上最近点采样仍然可以换 | on that same device nearest sampling can still be mirrored
        xbg_compare_view_input in = r16(true, false); in.d16_linear = false;
        CHECK(xbg_compare_view_plan_for(in) == XBG_COMPARE_VIEW_DEPTH_MIRROR); }

    // 其它 16 位/单通道彩色格式没有无损的深度格式可换（R8 会丢精度，R16_SFLOAT 没有对应的深度格式）。
    // Other 16-bit / single-channel colour formats have no lossless depth target (R8 loses precision, and
    // R16_SFLOAT has no matching depth format).
    {   xbg_compare_view_input in = r16(true, false); in.nvn_format = 0x1;    // R8_UNORM
        CHECK(xbg_compare_view_plan_for(in) == XBG_COMPARE_VIEW_UNAVAILABLE);
        CHECK(!__builtin_strcmp(xbg_compare_view_unavailable_reason(in), "format-not-r16")); }
    {   xbg_compare_view_input in = r16(true, false); in.nvn_format = 0x25;   // RGBA8
        CHECK(xbg_compare_view_plan_for(in) == XBG_COMPARE_VIEW_UNAVAILABLE); }

    // 多采样彩色图上的比较采样同样违例，但副本还要处理解析，现有路径只认单采样：不做，记原因。
    // Comparison on a multisampled colour image violates the same rule, but a copy needs resolve handling and
    // the upload path here is single-sampled only: do not, record why.
    {   xbg_compare_view_input in = r16(true, false); in.multisampled = true;
        CHECK(xbg_compare_view_plan_for(in) == XBG_COMPARE_VIEW_UNAVAILABLE);
        CHECK(!__builtin_strcmp(xbg_compare_view_unavailable_reason(in), "multisampled")); }

    // 体纹理/1D 纹理：副本的视图类型与采样不符，不做，记原因。
    // Volume and 1D textures: the copy's view type would not match the sampling, so no copy; record why.
    {   xbg_compare_view_input in = r16(true, false); in.target = 2;   // 3D
        CHECK(xbg_compare_view_plan_for(in) == XBG_COMPARE_VIEW_UNAVAILABLE);
        CHECK(!__builtin_strcmp(xbg_compare_view_unavailable_reason(in), "not-2d")); }
    // NVN target 2（3D）/ 0（1D）/ 0xA（buffer）都不是 2D 形状 | NVN targets 2 (3D), 0 (1D) and 0xA (buffer) are not 2D-shaped
    CHECK(!xbg_compare_2d_target(2));
    CHECK(!xbg_compare_2d_target(0));
    CHECK(!xbg_compare_2d_target(0xA));
    CHECK(xbg_compare_2d_target(1));
    CHECK(xbg_compare_2d_target(4));
    CHECK(xbg_compare_cube_target(8));
    CHECK(xbg_compare_cube_target(9));
    CHECK(!xbg_compare_cube_target(4));

    // Fold8 的现场：6 层立方体的 R16_UNORM 比较采样。它和 2D 是同一个规范违例（R16_UNORM 不支持
    // SAMPLED_IMAGE_DEPTH_COMPARISON），尺寸只有 32 KB，D16 采样/比较/线性都可用，所以必须做副本——
    // 立方体就是 6 层的 2D 数组，视图类型按 CUBE 建，6 层全拷。
    // The Fold8 case: R16_UNORM compare sampling of a 6-layer cube. It is the same spec violation as the 2D case
    // (R16_UNORM lacks SAMPLED_IMAGE_DEPTH_COMPARISON), it is only 32 KB, and D16 sampled/compare/linear all work,
    // so it must be mirrored: a cube is a 6-layer 2D array, the copy gets a CUBE view and all six layers.
    {   xbg_compare_view_input in = cube_r16(6, 8);
        CHECK(xbg_compare_view_plan_for(in) == XBG_COMPARE_VIEW_DEPTH_MIRROR);
        CHECK(xbg_compare_mirror_view_kind(8, 6) == XBG_MIRROR_VIEW_CUBE); }
    // 视图类型由 target 与层数共同决定，缺一个就会错配：
    // target 8 是单个立方体，只有正好 6 层合法（12 层没有合法的 CUBE 视图）；
    // target 9 是立方体数组，合法层数都是 6 的倍数，6 层也是 CUBE_ARRAY——cube-array shader 要的是数组视图，
    // 六层并不改变这一语义（源图的 Image::view 对 target 9 也建 CUBE_ARRAY）。
    // Both the target and the layer count decide the view: target 8 is a single cube and only six layers are legal
    // (12 has no legal CUBE view); target 9 is a cube array, so every legal multiple of six is CUBE_ARRAY, six
    // included — a cube-array shader needs an array view and six layers does not change that (the source's own
    // Image::view builds CUBE_ARRAY for target 9 as well).
    CHECK(xbg_compare_mirror_view_kind(8, 6) == XBG_MIRROR_VIEW_CUBE);
    CHECK(xbg_compare_mirror_view_kind(8, 12) == XBG_MIRROR_VIEW_NONE);   // target 8 十二层非法 | illegal
    CHECK(xbg_compare_mirror_view_kind(8, 7) == XBG_MIRROR_VIEW_NONE);
    CHECK(xbg_compare_mirror_view_kind(8, 0) == XBG_MIRROR_VIEW_NONE);
    CHECK(xbg_compare_mirror_view_kind(9, 6) == XBG_MIRROR_VIEW_CUBE_ARRAY);
    CHECK(xbg_compare_mirror_view_kind(9, 12) == XBG_MIRROR_VIEW_CUBE_ARRAY);
    CHECK(xbg_compare_mirror_view_kind(9, 18) == XBG_MIRROR_VIEW_CUBE_ARRAY);
    CHECK(xbg_compare_mirror_view_kind(9, 7) == XBG_MIRROR_VIEW_NONE);
    CHECK(xbg_compare_mirror_view_kind(9, 0) == XBG_MIRROR_VIEW_NONE);
    {   xbg_compare_view_input in = cube_r16(12, 9);   // 立方体数组 | a cube array
        CHECK(xbg_compare_view_plan_for(in) == XBG_COMPARE_VIEW_DEPTH_MIRROR); }
    {   xbg_compare_view_input in = cube_r16(6, 9);    // 六层的立方体数组也是 CUBE_ARRAY | six-layer array too
        CHECK(xbg_compare_view_plan_for(in) == XBG_COMPARE_VIEW_DEPTH_MIRROR); }
    {   xbg_compare_view_input in = cube_r16(12, 8);   // target 8 十二层没有合法视图 -> 保持旧路径
        CHECK(xbg_compare_view_plan_for(in) == XBG_COMPARE_VIEW_UNAVAILABLE);
        CHECK(!__builtin_strcmp(xbg_compare_view_unavailable_reason(in), "cube-layers")); }
    {   xbg_compare_view_input in = cube_r16(7, 9);
        CHECK(xbg_compare_view_plan_for(in) == XBG_COMPARE_VIEW_UNAVAILABLE);
        CHECK(!__builtin_strcmp(xbg_compare_view_unavailable_reason(in), "cube-layers")); }
    {   xbg_compare_view_input in = cube_r16(0, 8);
        CHECK(xbg_compare_view_plan_for(in) == XBG_COMPARE_VIEW_UNAVAILABLE);
        CHECK(!__builtin_strcmp(xbg_compare_view_unavailable_reason(in), "cube-layers")); }
    // 2D 数组的层数不受立方体的倍数限制 | a 2D array has no multiple-of-six rule
    CHECK(xbg_compare_mirror_view_kind(4, 6) == XBG_MIRROR_VIEW_2D_ARRAY);
    CHECK(xbg_compare_mirror_view_kind(1, 1) == XBG_MIRROR_VIEW_2D);
    CHECK(xbg_compare_mirror_view_kind(1, 0) == XBG_MIRROR_VIEW_NONE);
    CHECK(xbg_compare_mirror_view_kind(2, 6) == XBG_MIRROR_VIEW_NONE);    // 3D 没有 2D 视图 | no 2D view for 3D

    // 副本的字节数与拷贝区域都按「层」算：6 层立方体是 6 份，不是一份（把它当 2D 单层就是读错层的根源）。
    // The copy's bytes and regions count layers: a 6-layer cube is six of them, not one (treating it as a single 2D
    // layer is how the wrong layer gets read).
    CHECK(xbg_compare_mirror_bytes(128, 128, 1, 6) == 128u * 128u * 2u * 6u);
    CHECK(xbg_compare_mirror_bytes(128, 128, 1, 1) == 128u * 128u * 2u);
    CHECK(xbg_compare_mirror_bytes(128, 128, 2, 6) == (128u * 128u + 64u * 64u) * 2u * 6u);
    CHECK(xbg_compare_mirror_bytes(1, 1, 3, 6) == (1u + 1u + 1u) * 2u * 6u);   // 级别小于 1 时按 1 像素 | clamps at 1
    CHECK(xbg_compare_mirror_regions(1, 6) == 6);
    CHECK(xbg_compare_mirror_regions(2, 6) == 12);
    CHECK(xbg_compare_mirror_regions(1, 1) == 1);

    // 中转偏移（Kimi P0-2）：Arena::alloc 返回的是块内子分配，Buffer.offset 非零；vkCmdCopy* 把
    // bufferOffset 当地址用，不会替你加这一段。区域必须以它（mid.offset）为基址，而校验仍按相对总字节。
    // 这里对 0/256/40960/1 MiB 四个基址跑真实区域生成，逐字节核对只有副本自己那一段被写。
    // Staging offset (Kimi P0-2): Arena::alloc returns a sub-allocation with a non-zero Buffer.offset, and
    // vkCmdCopy* treats bufferOffset as an address without adding it. Regions must be based on that offset while
    // the size check stays relative. Real region generation runs for four bases, checking byte for byte that only
    // the copy's own window is written.
    for(uint64_t base : {uint64_t(0), uint64_t(256), uint64_t(40960), uint64_t(1) << 20}) {
        check_copy_window(base, 128, 128, 1, 6);    // Fold8：单级别 6 层 | Fold8: one level, six layers
        check_copy_window(base, 128, 128, 2, 6);    // mip 链 | a mip chain
        check_copy_window(base, 32, 32, 3, 12);     // 立方体数组 + mip | cube array with mips
        check_copy_window(base, 1, 1, 3, 6);        // 最小的级别（每个区域只有 2 字节）| smallest levels
    }
    // 装不下就不写：一条都不留，也不会给出一个「部分字节数」让调用方误以为拷完了。
    // If the regions do not fit, nothing is written and no partial byte count is reported.
    {   std::vector<xbg_mirror_region> small(3);
        uint64_t relative = 12345;
        CHECK(xbg_compare_mirror_regions_fill(256, 128, 128, 1, 6, small.data(), 3, &relative) == 0);
        CHECK(relative == 0); }

    // 上限按整张副本算：6 层立方体只有单层四分之一大小时也已经超了上限，不能因为只看了一层就放行。
    // The cap counts the whole copy: a 6-layer cube that is a quarter of the cap per layer is already over it, and
    // must not pass just because one layer would fit.
    {   xbg_compare_view_input in = cube_r16(6);
        in.data_bytes = 1024u * 1024u * 2u * 6u;   // 12 MiB
        CHECK(xbg_compare_view_plan_for(in) == XBG_COMPARE_VIEW_UNAVAILABLE);
        CHECK(!__builtin_strcmp(xbg_compare_view_unavailable_reason(in), "too-large")); }
    {   xbg_compare_view_input in = cube_r16(6);
        in.data_bytes = xbg_compare_mirror_bytes(512, 512, 1, 6);   // 3 MiB，仍在 4 MiB 以内 | under 4 MiB
        CHECK(in.data_bytes < XBG_COMPARE_MIRROR_MAX_BYTES);
        CHECK(xbg_compare_view_plan_for(in) == XBG_COMPARE_VIEW_DEPTH_MIRROR); }

    // 立方体上的同一套否定条件（设备不符/多采样/子视图）与 2D 一致 | the same refusals hold for cubes as for 2D
    {   xbg_compare_view_input in = cube_r16(6); in.d16_compare = false;
        CHECK(xbg_compare_view_plan_for(in) == XBG_COMPARE_VIEW_UNAVAILABLE);
        CHECK(!__builtin_strcmp(xbg_compare_view_unavailable_reason(in), "d16-no-depth-compare")); }
    {   xbg_compare_view_input in = cube_r16(6); in.subresource_view = true;
        CHECK(xbg_compare_view_plan_for(in) == XBG_COMPARE_VIEW_UNAVAILABLE);
        CHECK(!__builtin_strcmp(xbg_compare_view_unavailable_reason(in), "subresource-view")); }

    // 立方体副本的刷新与同步判据和 2D 完全共用（每提交一次、写戳变就重拷、mutable 重建作废），多帧连续提交必须每帧重拷。
    // The cube copy shares the 2D refresh/sync rules (once per submission, refill when the write stamp moves, invalidate
    // on a mutable rebuild), and consecutive frames must each refill it.
    {   xbg_mirror_stamp s; xbg_mirror_stamp_clear(s);
        const uint64_t bytes = xbg_compare_mirror_bytes(128, 128, 1, 6);
        CHECK(bytes == 196608u);
        for(uint64_t frame = 1; frame <= 3; ++frame) {
            CHECK(xbg_mirror_refresh_needed(frame, 7, s));   // 每一帧都要刷 | every frame refills
            xbg_mirror_stamp_filled(frame, 7, s);
            CHECK(!xbg_mirror_refresh_needed(frame, 7, s));  // 同帧第二个绑定不重复拷 | no second copy in one frame
        }
        CHECK(xbg_mirror_refresh_needed(3, 8, s));           // 源写戳前进 -> 重拷 | source write -> refill
        xbg_mirror_stamp_filled(3, 8, s);
        xbg_mirror_stamp_clear(s);                           // mutable 重建 -> 作废 | rebuild -> invalidate
        CHECK(xbg_mirror_refresh_needed(3, 8, s)); }

    // 绑定的是子视图（只取部分层/级别）时不能替：副本覆盖整张图，换上去会读到别的层。
    // A subresource view (a layer/level subrange) cannot be substituted: the copy covers the whole image, so
    // binding it would read other layers.
    {   xbg_compare_view_input in = r16(true, false); in.subresource_view = true;
        CHECK(xbg_compare_view_plan_for(in) == XBG_COMPARE_VIEW_UNAVAILABLE);
        CHECK(!__builtin_strcmp(xbg_compare_view_unavailable_reason(in), "subresource-view")); }

    // 太大的图不做副本（每帧一次的整图拷贝会把帧时间吃掉）| a too-large image gets no copy (a whole-image copy per
    // frame would eat the frame); an empty one is a bug in the caller, not a silent copy
    {   xbg_compare_view_input in = r16(true, false); in.data_bytes = XBG_COMPARE_MIRROR_MAX_BYTES + 1;
        CHECK(xbg_compare_view_plan_for(in) == XBG_COMPARE_VIEW_UNAVAILABLE);
        CHECK(!__builtin_strcmp(xbg_compare_view_unavailable_reason(in), "too-large")); }
    {   xbg_compare_view_input in = r16(true, false); in.data_bytes = XBG_COMPARE_MIRROR_MAX_BYTES;
        CHECK(xbg_compare_view_plan_for(in) == XBG_COMPARE_VIEW_DEPTH_MIRROR); }
    {   xbg_compare_view_input in = r16(true, false); in.data_bytes = 0;
        CHECK(xbg_compare_view_plan_for(in) == XBG_COMPARE_VIEW_UNAVAILABLE); }

    // 副本刷新判据的完整生命周期（生产代码里 compare_mirror_sync 与 upgrade_mutable 用的就是这三个函数）：
    // 首次建 memo/首次绑定 → 下一提交命中 memo → 同提交里源被写 → mutable 重建。
    // The copy's refresh lifecycle (compare_mirror_sync and upgrade_mutable use exactly these three functions):
    // first memo, next-submission hit, a source write inside the same submission, and a mutable rebuild.
    {   xbg_mirror_stamp s; xbg_mirror_stamp_clear(s);
        CHECK(xbg_mirror_never_filled(s));
        // 首次建 memo、首次比较绑定：没有内容，必须刷 | first bind: nothing filled yet, must refresh
        CHECK(xbg_mirror_refresh_needed(1, 5, s));
        xbg_mirror_stamp_filled(1, 5, s);
        // 同提交第二个比较绑定：一次提交只拷一次 | second compare bind in the same submission: one copy per submit
        CHECK(!xbg_mirror_refresh_needed(1, 5, s));
        // 下一提交命中同一 memo（源写戳没动）：副本还是上一帧的，仍要刷
        // next submission hits the same memo (source write stamp unchanged): the copy still holds last frame
        CHECK(xbg_mirror_refresh_needed(2, 5, s));
        xbg_mirror_stamp_filled(2, 5, s);
        CHECK(!xbg_mirror_refresh_needed(2, 5, s));
        // 同提交里 clear/copy 写了源图（写戳前进）：之后再做比较采样必须重拷，不能读旧副本
        // a clear/copy wrote the source in the same submission: a later compare sample must re-copy
        CHECK(xbg_mirror_refresh_needed(2, 6, s));
        xbg_mirror_stamp_filled(2, 6, s);
        CHECK(!xbg_mirror_refresh_needed(2, 6, s));
        // mutable 重建换了源图的 VkImage/内存：填戳作废 | mutable rebuild swapped the source: invalidate the stamp
        xbg_mirror_stamp_clear(s);
        CHECK(xbg_mirror_never_filled(s));
        CHECK(xbg_mirror_refresh_needed(2, 6, s));
    }

    // 副本最终屏障的目标阶段：比较采样可能来自 compute，也可能是顶点/细分/几何阶段的采样，只挡片元不够。
    // The copy's final barrier destination: compare sampling can come from compute or vertex/tessellation/geometry,
    // so a fragment-only barrier is not enough.
    CHECK(XBG_COMPARE_MIRROR_DST_STAGE != 0x80u);   // VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
    CHECK(xbg_compare_mirror_dst_stage(false) == XBG_COMPARE_MIRROR_DST_STAGE);
    CHECK(xbg_compare_mirror_dst_stage(true) == XBG_COMPARE_MIRROR_DST_STAGE);

    // 原因字符串与决策一致 | the reason strings agree with the decisions
    CHECK(!__builtin_strcmp(xbg_compare_view_reason(XBG_COMPARE_VIEW_COLOR), "no-compare-or-depth"));
    CHECK(!__builtin_strcmp(xbg_compare_view_reason(XBG_COMPARE_VIEW_DEPTH_MIRROR), "d16-mirror"));
    CHECK(!__builtin_strcmp(xbg_compare_view_reason(XBG_COMPARE_VIEW_UNAVAILABLE), "unavailable"));

    if(failures){printf("compare_view_test: %d failure(s)\n", failures); return 1;}
    printf("compare_view_test: ok\n");
    return 0;
}
