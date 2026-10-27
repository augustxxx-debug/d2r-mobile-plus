// 出图队列「已处理完」水位单测：按显示节拍路径的真实次序（取走 → 提交 → GPU 完成回调、合并丢弃、兜底丢弃）推演，期望水位手算。
// -DPQ_LEGACY 编译同一份用例 = 修复前 nvn_mtl.m 的写法（合并 / 兜底直接把水位写到本次票号），用来证明用例在旧实现上是红的。
// Unit test of the present queue "processed" watermark: replays the real order of the display-link path (take -> commit -> GPU completion callback, merge drop, fallback drop), with hand-computed expected watermarks.
// Compiling the same cases with -DPQ_LEGACY = the pre-fix nvn_mtl.m (merge / fallback wrote the watermark straight to this ticket), to show the cases fail on the old implementation.
#include "../nvn/nvn_pqdone.h"
#include <stdio.h>

static uint64_t done;
#ifdef PQ_LEGACY
static void gpu_done(uint64_t k) { if (k > done) done = k; }   // 旧：上屏完成回调 | old: present completion callback
static void merged(uint64_t first, uint64_t ticket) { (void)first; if (ticket > 1 && done < ticket - 1) done = ticket - 1; }   // 旧：节拍回调合并 | old: display-link merge
static void dropped(uint64_t lo, uint64_t hi) { (void)lo; if (done < hi) done = hi; }   // 旧：兜底丢弃（g_pq_inflight 还没加上的那段空档里） | old: fallback drop (in the gap before g_pq_inflight is incremented)
#else
static uint64_t fin[NVN_PQ_RING];
static void gpu_done(uint64_t k) { nvn_pq_finish(&done, fin, k); }
static void merged(uint64_t first, uint64_t ticket) { for (uint64_t k = first; k < ticket; k++) nvn_pq_finish(&done, fin, k); }
static void dropped(uint64_t lo, uint64_t hi) { for (uint64_t k = lo; k <= hi; k++) nvn_pq_finish(&done, fin, k); }
#endif

static int fail, n;
static void expect(uint64_t want, const char *why) {
    int ok = done == want; n++; fail += !ok;
    printf("%-64s 期望水位 %3llu → 真结果 %3llu %s\n", why, (unsigned long long)want, (unsigned long long)done, ok ? "✓" : "✗");
}

int main(void) {
    // 1. GPU 落后：节拍回调 A 取走 1 并提交（还在 GPU 上）；回调 B 时队里有 2、3：合并丢 2、提交 3
    // 1. GPU behind: display-link callback A takes 1 and commits it (still on the GPU); at callback B the queue holds 2 and 3: merge-drop 2, commit 3
    merged(1, 1); expect(0, "回调 A 取走 1，GPU 未完成");
    merged(2, 3); expect(0, "回调 B 合并丢 2：1 还在 GPU 上，水位不得越过 1");
    // 上屏命令分配器按票号 % 3 复用：票 4 用 1 的那一份，要等水位 ≥ 1。旧写法此刻水位已是 2，会 reset 还在执行的那份
    // The present command allocator is reused by ticket % 3: ticket 4 uses ticket 1's, and must wait for watermark >= 1. With the old code the watermark is already 2 here, and it would reset the one still executing
    n++; { int ok = done < 1; fail += !ok; printf("%-64s %s\n", "票 4 复用分配器槽 1 前：必须仍要等（水位 < 1）", ok ? "✓" : "✗"); }
    gpu_done(1); expect(2, "1 在 GPU 上完成：水位连 2 一起前进到 2");
    gpu_done(3); expect(3, "3 完成");
    // 2. 失活时节拍停：回调 C 取走 4（已从队里拿走，还没提交 / 还在 GPU 上）；出图线程等 6 超时，兜底丢 5、6
    // 2. Display link stops while inactive: callback C takes 4 (out of the queue, not yet committed / still on the GPU); the present thread times out waiting for 6 and the fallback drops 5 and 6
    merged(4, 4); dropped(5, 6); expect(3, "兜底丢 5、6：4 还没完成，水位停在 3");
    gpu_done(4); expect(6, "4 完成：水位到 6");
    // 3. 乱序完成：7、8 都在途，8 的回调先到
    // 3. Out-of-order completion: 7 and 8 both in flight, 8's callback arrives first
    merged(7, 7); merged(8, 8); gpu_done(8); expect(6, "8 先完成、7 未完成：水位停在 6");
    gpu_done(7); expect(8, "7 完成：水位到 8");
    // 4. 重复登记、旧票号：不倒退
    // 4. Duplicate registration, old ticket: no regression
    gpu_done(5); gpu_done(8); expect(8, "重复登记旧票号：水位不变");
    // 5. 环绕：连续 100 张逐张完成
    // 5. Wrap-around: 100 consecutive tickets completed one by one
    for (uint64_t k = 9; k <= 108; k++) gpu_done(k);
    expect(108, "连续 100 张（跨环长）逐张完成");
    printf("== %d 项，失败 %d ==\n", n, fail);
    return fail != 0;
}
