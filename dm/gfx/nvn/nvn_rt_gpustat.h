// 光追 GPU 耗时统计（七审 Q7-04）：完成反馈（pg_commit 的回调，在 g_pg_mu 里）累加每个光追通道的毫秒数；渲染线程每 600 帧在同一把锁里一次取快照（均值、帧数）并清零，再在锁外记日志。
// 原先读与清零不取锁，与回调的累加构成数据竞争（审计 TSan 反例），可能丢样本或得到不一致的均值
// Ray-tracing GPU time statistics (seventh review Q7-04): the completion feedback (pg_commit's callback, inside g_pg_mu) adds each ray-tracing pass's milliseconds; every 600 frames the render thread takes one snapshot (mean, count) and resets
// within the same lock, then logs outside it. The read and reset used to run without the lock, racing the callback's additions (the review's TSan reproducer), which could lose samples or give an inconsistent mean
#ifndef NVN_RT_GPUSTAT_H
#define NVN_RT_GPUSTAT_H
#include <pthread.h>
#include <stdint.h>
static double g_rts_gpu_sum; static uint64_t g_rts_gpu_n;   // 只在 g_pg_mu 里读写 | read and written only inside g_pg_mu
static inline void rts_gpu_stat_add(double ms) { g_rts_gpu_sum += ms; g_rts_gpu_n++; }   // 调用方持有 g_pg_mu | the caller holds g_pg_mu
static inline void rts_gpu_stat_take(pthread_mutex_t *mu, double *mean, uint64_t *n) {   // 一致快照 + 清零 | consistent snapshot + reset
    pthread_mutex_lock(mu); *n = g_rts_gpu_n; *mean = g_rts_gpu_n ? g_rts_gpu_sum / (double)g_rts_gpu_n : 0; g_rts_gpu_sum = 0; g_rts_gpu_n = 0; pthread_mutex_unlock(mu);
}
#endif
