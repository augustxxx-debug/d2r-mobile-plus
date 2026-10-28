// 七审 Q7-04：光追 GPU 统计的累加（完成回调线程，持 g_pg_mu）与快照清零（渲染线程）并发跑，TSan 下不得报竞争；样本一个不丢、每次快照的均值一致
// Seventh review Q7-04: the ray-tracing GPU statistics' addition (completion callback thread, holding g_pg_mu) and snapshot-and-reset (render thread) run concurrently; no race under TSan, no sample lost, every snapshot's mean consistent
#include "../nvn/nvn_rt_gpustat.h"
#include <stdio.h>
#define N 200000
static pthread_mutex_t g_pg_mu = PTHREAD_MUTEX_INITIALIZER;
static void *completion(void *unused) { (void)unused; for (int i = 0; i < N; i++) { pthread_mutex_lock(&g_pg_mu); rts_gpu_stat_add(2.0); pthread_mutex_unlock(&g_pg_mu); } return NULL; }
int main(void) {
    pthread_t t; pthread_create(&t, NULL, completion, NULL);
    uint64_t total = 0; int bad = 0; double mean; uint64_t n;
    for (int i = 0; i < N; i++) { rts_gpu_stat_take(&g_pg_mu, &mean, &n); total += n; if (n && mean != 2.0) bad++; }
    pthread_join(t, NULL); rts_gpu_stat_take(&g_pg_mu, &mean, &n); total += n; if (n && mean != 2.0) bad++;
    printf("样本 %llu / %d、均值不一致的快照 %d 次\n", (unsigned long long)total, N, bad);
    if (total != N || bad) { printf("FAIL t_rts_gpustat\n"); return 1; }
    printf("PASS t_rts_gpustat：快照与清零在同一把锁里，样本不丢、均值一致\n"); return 0;
}
