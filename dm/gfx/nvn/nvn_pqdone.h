// 出图队列「已处理完」水位（纯函数，单测 tests/t_pqdone.c）。
// 票号 = 第几次出图入队（从 1 起）。每张的结局只有三种：上屏命令缓冲在 GPU 上执行完、被合并 / 兜底丢弃、没交出去（取不到 drawable 等）。
// 各自在结局处登记 nvn_pq_finish；水位 done = 「1..done 全部有了结局」的最大值，只沿连续前缀前进。
// 水位的两个用户都要求这层含义：取交换链贴图前等它上一次出图（hle_nvn.c → mtl_present_wait）、上屏命令分配器复用前等上一轮（present_now）。
// 原先直接写水位：显示节拍回调合并丢帧时把水位推到「本次 − 1」，更早那张已提交、还在 GPU 上执行的上屏一并被当成完成；
// GPU 一落后（下拉通知栏时系统合成模糊抢 GPU、A16 重负载），下一轮就 reset 了还在执行的命令分配器（未定义行为，画面出块状垃圾）。
// 环长 NVN_PQ_RING 须大于同时未了结的票数：出图入队处保证 in − done < 8。
// Present queue "processed" watermark (pure functions, unit test tests/t_pqdone.c).
// Ticket = which present enqueue it is (starting from 1). Each ticket ends in one of three ways: its present command buffer finished on the GPU, it was merged / dropped by a fallback, or it was never handed over (no drawable, etc.).
// Each records nvn_pq_finish where it ends; the watermark done = the largest value such that "1..done have all ended", advancing only along the contiguous prefix.
// Both users of the watermark need exactly this: waiting for a swapchain texture's previous present before acquiring it (hle_nvn.c -> mtl_present_wait), and waiting for the previous round before reusing a present command allocator (present_now).
// Previously the watermark was written directly: when the display-link callback merged dropped frames it pushed the watermark to "this one - 1", so an older present already committed and still executing on the GPU counted as done too;
// once the GPU fell behind (the system compositing a blur over the pulled-down notification shade takes GPU time; heavy load on A16), the next round reset a command allocator that was still executing (undefined behavior, blocky garbage on screen).
// The ring length NVN_PQ_RING must exceed the number of tickets not yet ended at once: the present enqueue guarantees in - done < 8.
#pragma once
#include <stdint.h>

#define NVN_PQ_RING 32

static inline void nvn_pq_finish(uint64_t *done, uint64_t fin[NVN_PQ_RING], uint64_t k) {
    if (k <= *done) return;
    fin[k % NVN_PQ_RING] = k;
    while (fin[(*done + 1) % NVN_PQ_RING] == *done + 1) (*done)++;
}
