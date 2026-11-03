// 直接调用生产的 R16 比较采样视图判断；只做整数/布尔运算，不需要 Vulkan 或 GPU。
// Exercises the production R16 compare-sampling view decision directly; integer and boolean work only, no
// Vulkan or GPU.
#include "../xbg_compare_view.h"
#include <stdio.h>

static int failures;
#define CHECK(expression) do { if(!(expression)){printf("FAIL line %d: %s\n",__LINE__,#expression);++failures;} } while(0)

// 一台「D16 完全可用」的设备：Adreno 740 的 NVN caps 里 124 号（D16_UNORM）确实带采样，线性过滤要看具体驱动，
// 所以两个变体都测。
// A device where D16 is fully usable; Adreno caps differ on linear filtering, so both variants are tested.
static xbg_compare_view_input r16(bool compare, bool linear) {
    xbg_compare_view_input in{};
    in.nvn_format = 0x6;      // NVN_FORMAT_R16 -> VK_FORMAT_R16_UNORM
    in.data_bytes = 128 * 128 * 2;   // B078 那张 128x128 R16
    in.color_aspect = true;
    in.two_dimensional = true;
    in.multisampled = false;
    in.compare = compare;
    in.linear = linear;
    in.d16_sampled = true;
    in.d16_compare = true;
    in.d16_linear = true;
    return in;
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

    // 体纹理/立方体纹理的视图类型与 2D 不同：副本不做，记原因（免得换成一个驱动不认的视图）。
    // Volume and cube textures need other view types: no copy, record why (rather than build a view the driver
    // will reject).
    {   xbg_compare_view_input in = r16(true, false); in.two_dimensional = false;
        CHECK(xbg_compare_view_plan_for(in) == XBG_COMPARE_VIEW_UNAVAILABLE);
        CHECK(!__builtin_strcmp(xbg_compare_view_unavailable_reason(in), "not-2d")); }

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
