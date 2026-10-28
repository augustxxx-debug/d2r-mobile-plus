// 命令分配器超阈值换新（RC1 资源审查：17 Pro 光追关 30 分钟局 9 个分配器 38 → 76 MB，Mac 五幕轮换 114 → 185 MB——MTL4CommandAllocator 的内存长到编过的最大一帧就不回落，reset 只标记可复用）。
// 一次提交里编 5 万条拷贝把主提交的分配器撑到超阈值，之后接着跑空提交：每复用 DM_CMDALLOC_EVERY 次查一次，超过 DM_CMDALLOC_MB 且空闲就换新的。
// 要求：分配器总量回落、换新计数 ≥ 1、换新后的提交照样正确（拷贝结果逐字节对）。--off（DM_CMDALLOC_MB=0）：不换、总量不回落（证明这个测试分得出改没改）。旧接口版没有 MTL4 分配器：跳过（77）
// Command allocators replaced above a threshold (RC1 resource review: 17 Pro RT-off 30-minute run, 9 allocators 38 → 76 MB; Mac five-act rotation 114 → 185 MB - an MTL4CommandAllocator's memory grows to the largest frame ever encoded and never shrinks, reset only marks it reusable).
// One submission encodes about 300k copies to push the main allocator over the threshold, then empty submissions follow: every DM_CMDALLOC_EVERY reuses the size is checked and an idle allocator above DM_CMDALLOC_MB is replaced.
// Required: the allocator total drops, the replace count is ≥ 1, and submissions after the replacement are still correct (copy result byte-exact). --off (DM_CMDALLOC_MB=0): no replacement, the total does not drop (proves the test tells the change apart). The older-API build has no MTL4 allocators: skipped (77)
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int fail, count;
#define EXPECT(name, want, got) do { long long w_=(long long)(want), g_=(long long)(got); count++; printf("%s 期望 %lld → 真结果 %lld %s\n", name, w_, g_, w_==g_?"✓":"✗"); fail += w_ != g_; } while (0)
#if !DM_NVN_M3
static uint64_t main_alloc_bytes(void) { uint64_t s = 0; for (int i = 0; i < g_alloc_n; i++) if (g_alloc4[i]) s += g_alloc4[i].allocatedSize; return s; }
#endif
int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
#if DM_NVN_M3
    (void)argc; (void)argv; puts("SKIP t_cmdalloc：旧接口版没有 MTL4 命令分配器 | older-API build has no MTL4 command allocators"); return 77;
#else
    int off = argc > 1 && !strcmp(argv[1], "--off");
    setenv("DM_CMDALLOC_EVERY", "2", 1); setenv("DM_CMDALLOC_MB", off ? "0" : "4", 1);
    mtl_init();
    uint8_t bytes[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    id<MTLBuffer> src = [g_dev newBufferWithBytes:bytes length:sizeof bytes options:MTLResourceStorageModeShared];
    id<MTLBuffer> dst = [g_dev newBufferWithLength:sizeof bytes options:MTLResourceStorageModeShared];
    res_add(src); res_add(dst);
    {   // 撑大：一次提交里 5 万条 4 字节拷贝（Mac 上约 40 MB） | inflate: 300k 4-byte copies in one submission
        mtl_sub_begin(); id b = blit();
        for (int i = 0; i < 50000; i++) [b copyFromBuffer:src sourceOffset:0 toBuffer:dst destinationOffset:0 size:4];
        enc_b_end(g_blit); g_blit = nil; uint64_t f = mtl_sub_commit(); mtl_wait(f, 30000000000ull);
    }
    uint64_t big = main_alloc_bytes(); printf("撑大后主提交分配器合计 %.1f MB | main allocators after inflating\n", big / 1048576.0);
    EXPECT("撑大后超过阈值 4 MB", 1, big > (4ull << 20));
    for (int i = 0; i < 8; i++) { mtl_sub_begin(); uint64_t f = mtl_sub_commit(); mtl_wait(f, 10000000000ull); }   // 空提交：同一槽复用（每次都等完，挑到的总是空闲的槽 0） | empty submissions: the same slot is reused
    memset(dst.contents, 0, sizeof bytes);
    { mtl_sub_begin(); id b = blit(); [b copyFromBuffer:src sourceOffset:0 toBuffer:dst destinationOffset:0 size:sizeof bytes]; enc_b_end(g_blit); g_blit = nil; uint64_t f = mtl_sub_commit(); mtl_wait(f, 10000000000ull); }
#ifdef DM_CMDALLOC_HAVE   // 新代码才有换新计数；旧代码只看总量（运行时失败而不是编译失败） | only the new code has the replace count; the old code is judged by the total alone (fails at run time, not at compile time)
    unsigned long long nrec = atomic_load(&g_cmdalloc_recycled);
#else
    unsigned long long nrec = 0;
#endif
    uint64_t after = main_alloc_bytes(); printf("空提交之后合计 %.1f MB，换新 %llu 次 | after empty submissions\n", after / 1048576.0, nrec);
    EXPECT("换新后的提交照样正确：拷贝结果逐字节对", 0, memcmp(dst.contents, bytes, sizeof bytes));
    if (off) {
        EXPECT("--off：没换新", 0, (long long)nrec);
        EXPECT("--off：总量不回落（仍超阈值）", 1, after > (4ull << 20));
    } else {
#ifdef DM_CMDALLOC_HAVE
        EXPECT("换新次数 ≥ 1", 1, nrec >= 1);
#endif
        EXPECT("总量回落到阈值以下", 1, after < (4ull << 20));
    }
    printf("== %d 项，失败 %d ==\n", count, fail);
    return fail != 0;
#endif
}
