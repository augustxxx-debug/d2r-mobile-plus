// B131：NVN R16（格式 0x6）贴图被带深度比较的采样器采样时，Vulkan 的 R16_UNORM 视图不支持
// VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_DEPTH_COMPARISON_BIT，验证层报 VUID-vkCmdDrawIndexed-None-06479，
// 规范上结果未定义（B078 第 5 节：serial 44、128x128、cmp_func=2，同一条贴图也有非比较用法）。
// 这里只放「该用哪种视图」的纯判断，不碰 Vulkan，宿主可以单测。
// B131: when an NVN R16 texture (format 0x6) is sampled by a sampler with depth comparison, its
// VK_FORMAT_R16_UNORM view does not support VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_DEPTH_COMPARISON_BIT, so the
// validation layer reports VUID-vkCmdDrawIndexed-None-06479 and the result is undefined (B078 section 5:
// serial 44, 128x128, cmp_func=2; the same texture is also sampled without comparison).
// This header holds only the pure "which view" decision, with no Vulkan, so the host can unit-test it.
#pragma once
#include <stdint.h>

// 16 位单通道 UNORM 的 NVN 格式：只有 0x6（NVN_FORMAT_R16）映射到 VK_FORMAT_R16_UNORM。
// 0x32 一类本来就是深度格式，不在这个修复范围内。
// The 16-bit single-channel UNORM NVN format: only 0x6 (NVN_FORMAT_R16) maps to VK_FORMAT_R16_UNORM.
// Depth formats such as 0x32 are outside this fix.
static const uint32_t XBG_NVN_FORMAT_R16 = 0x6;

// 视图选择的三种结果：保持彩色视图（不涉及这个违例）、改用 D16 深度副本、或没有安全的做法。
// Three outcomes: keep the colour view (no violation involved), use a D16 depth copy, or nothing safe.
enum xbg_compare_view_plan {
    XBG_COMPARE_VIEW_COLOR = 0,      // 保持原彩色视图 | keep the colour view
    XBG_COMPARE_VIEW_DEPTH_MIRROR = 1, // 用 D16 深度副本采样 | sample a D16 depth copy
    XBG_COMPARE_VIEW_UNAVAILABLE = 2   // 有违例但没有安全的修法：保持旧路径并记录原因 | violation, no safe fix: old path, log why
};

// 单张副本的大小上限：副本每帧最多刷新一次，太大的图会把帧时间吃掉（B078 那张 128x128 只有 32 KB）。
// Size cap for one copy: it refreshes at most once per frame, and a big image would eat the frame (B078's is 128x128,
// 32 KB).
static const uint64_t XBG_COMPARE_MIRROR_MAX_BYTES = 4ull << 20;

// 副本每个提交最多填一次；源图的写戳变了（GPU 写进去）或者源图被重建（mutable 升级换了 VkImage/内存）
// 就必须重填。判据放在这里，绑定路径、描述符记忆命中路径和 mutable 重建共用同一份。
// The copy is filled at most once per submission; it has to be refilled when the source's write stamp moves (a GPU
// write) or when the source was rebuilt (a mutable upgrade swapped the VkImage/memory). The predicate lives here so
// the binding path, the descriptor-memo hit path and the mutable rebuild share one copy of it.
struct xbg_mirror_stamp {
    uint64_t filled_submission;
    uint64_t filled_write;
};
static const uint64_t XBG_MIRROR_NEVER = ~0ull;   // 还没填过 | never filled

inline void xbg_mirror_stamp_clear(xbg_mirror_stamp &s) {
    s.filled_submission = XBG_MIRROR_NEVER;
    s.filled_write = XBG_MIRROR_NEVER;
}

inline bool xbg_mirror_never_filled(const xbg_mirror_stamp &s) {
    return s.filled_submission == XBG_MIRROR_NEVER;
}

inline bool xbg_mirror_refresh_needed(uint64_t submission, uint64_t last_write, const xbg_mirror_stamp &s) {
    return s.filled_submission != submission || s.filled_write != last_write;
}

inline void xbg_mirror_stamp_filled(uint64_t submission, uint64_t last_write, xbg_mirror_stamp &s) {
    s.filled_submission = submission;
    s.filled_write = last_write;
}

// 副本最终的 TRANSFER_DST → GENERAL 屏障，目标阶段必须覆盖实际的比较采样阶段：compute 走
// VK_PIPELINE_BIND_POINT_COMPUTE，采样也可能出现在顶点/细分/几何阶段，只写 FRAGMENT_SHADER 会漏掉依赖。
// 这里保守取 ALL_COMMANDS（xbg_game.cpp 里有 static_assert 把它钉在 Vulkan 枚举上）。
// The copy's final TRANSFER_DST -> GENERAL barrier must cover the stages that actually sample: compute binds a
// compute pipeline, and sampling can also happen in vertex/tessellation/geometry stages, so a FRAGMENT_SHADER-only
// destination leaves dependencies out. ALL_COMMANDS is the conservative choice (pinned to the Vulkan enum in
// xbg_game.cpp by a static_assert).
static const uint32_t XBG_COMPARE_MIRROR_DST_STAGE = 0x00010000u;   // VK_PIPELINE_STAGE_ALL_COMMANDS_BIT
inline uint32_t xbg_compare_mirror_dst_stage(bool compute) {
    (void)compute;   // 两种消费方都是全阶段 | both consumers get every stage
    return XBG_COMPARE_MIRROR_DST_STAGE;
}

// 一次绑定能拿到的全部事实。布尔字段都来自设备/描述符，不含猜测。
// Everything one binding knows. Every flag comes from the device or the descriptor; none is a guess.
struct xbg_compare_view_input {
    uint32_t nvn_format;       // tex_t.d.format
    uint64_t data_bytes;       // 这份内容的大小（层×级别×像素）| size of the content (layers x levels x pixels)
    bool color_aspect;         // 图像是彩色（R16_UNORM 走这条）| image has the colour aspect (R16_UNORM takes this one)
    bool two_dimensional;      // 2D 或 2D 数组（副本的视图类型只有这两种）| 2D or 2D array (the only view types the copy makes)
    bool multisampled;         // 图像是多采样 | image is multisampled
    bool subresource_view;     // 绑定用的是子视图（只取部分层/级别）；副本覆盖整张图，不能替 | the binding is a
                               // subresource view (a layer/level subrange); the copy covers the whole image and cannot stand in
    bool compare;              // 采样器开了深度比较 | the sampler has depth comparison enabled
    bool linear;               // 采样器要求线性过滤 | the sampler asks for linear filtering
    bool d16_sampled;          // D16_UNORM 支持 SAMPLED_IMAGE | D16_UNORM supports SAMPLED_IMAGE
    bool d16_compare;          // D16_UNORM 支持 SAMPLED_IMAGE_DEPTH_COMPARISON | D16_UNORM supports depth comparison
    bool d16_linear;           // D16_UNORM 支持 SAMPLED_IMAGE_FILTER_LINEAR | D16_UNORM supports linear filtering
};

// 规范上最小的一步：把 16 位 UNORM 的值原样放进 D16_UNORM（两者都是 16 位 UNORM，位相同即数值相同），
// 让驱动用深度比较硬件算，而不是在彩色视图上做未定义的事。
// 只有这几种情况不做：没开比较（本来合法）、图像本来就是深度格式（合法）、多采样（副本要另算，
// 现有上传路径只处理单采样）、不是 R16（别的格式没有对应的无损深度格式）、
// 或者设备上的 D16 不支持深度比较 / 不支持采样器要求的线性过滤（硬换会撞上采样器的线性过滤检查）。
// The spec-minimal step: put the same 16-bit UNORM values in a D16_UNORM image (both are 16-bit UNORM, so
// identical bits mean an identical value) and let the driver compare in depth hardware instead of doing
// something undefined on a colour view. Skipped when comparison is off (legal already), when the image is
// already depth (legal), when it is multisampled (a copy needs separate treatment and the upload path is
// single-sampled only), when the format is not R16 (no lossless depth format to mirror into), or when the
// device's D16 lacks depth comparison or the linear filtering the sampler asks for (forcing it would trip
// the sampler's linear-filtering check).
inline xbg_compare_view_plan xbg_compare_view_plan_for(const xbg_compare_view_input &in) {
    if(!in.compare) return XBG_COMPARE_VIEW_COLOR;
    if(!in.color_aspect) return XBG_COMPARE_VIEW_COLOR;   // 已经是深度格式：比较合法 | already depth: comparison is legal
    if(in.nvn_format != XBG_NVN_FORMAT_R16) return XBG_COMPARE_VIEW_UNAVAILABLE;
    if(!in.two_dimensional) return XBG_COMPARE_VIEW_UNAVAILABLE;
    if(in.multisampled) return XBG_COMPARE_VIEW_UNAVAILABLE;
    if(in.subresource_view) return XBG_COMPARE_VIEW_UNAVAILABLE;
    if(!in.d16_sampled || !in.d16_compare) return XBG_COMPARE_VIEW_UNAVAILABLE;
    if(!in.data_bytes || in.data_bytes > XBG_COMPARE_MIRROR_MAX_BYTES) return XBG_COMPARE_VIEW_UNAVAILABLE;
    if(in.linear && !in.d16_linear) return XBG_COMPARE_VIEW_UNAVAILABLE;
    return XBG_COMPARE_VIEW_DEPTH_MIRROR;
}

// 记录用的一句话原因（日志里跟着 serial 走，方便在报告机日志里定位）。
// One-line reason for the log, next to the serial, so the report-device log can be read directly.
inline const char *xbg_compare_view_reason(xbg_compare_view_plan plan) {
    switch(plan) {
    case XBG_COMPARE_VIEW_COLOR: return "no-compare-or-depth";
    case XBG_COMPARE_VIEW_DEPTH_MIRROR: return "d16-mirror";
    case XBG_COMPARE_VIEW_UNAVAILABLE: return "unavailable";
    }
    return "invalid";
}

// 选出「保持旧路径」时的具体原因，只用于日志。
// The concrete reason when the old path is kept; only for the log.
inline const char *xbg_compare_view_unavailable_reason(const xbg_compare_view_input &in) {
    if(in.nvn_format != XBG_NVN_FORMAT_R16) return "format-not-r16";
    if(!in.two_dimensional) return "not-2d";
    if(in.multisampled) return "multisampled";
    if(in.subresource_view) return "subresource-view";
    if(!in.d16_sampled) return "d16-not-sampled";
    if(!in.d16_compare) return "d16-no-depth-compare";
    if(!in.data_bytes || in.data_bytes > XBG_COMPARE_MIRROR_MAX_BYTES) return "too-large";
    if(in.linear && !in.d16_linear) return "d16-no-linear-filter";
    return "none";
}
