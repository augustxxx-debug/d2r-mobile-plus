// 显示节拍逐回调记录的 GPU 完成时刻（nvn_ltring.h，五审 Q5-03）：迟到的完成回调、槽复用、写到一半时复用、两线程并发
// 加 -DLTR_LEGACY 编出 46e57ec 的写法（槽里一个普通 double，节拍线程清零、完成回调直接写、节拍线程直接读）作对照：语义项失败，-fsanitize=thread 报数据竞争
// GPU completion time of the per-callback display link record (nvn_ltring.h, fifth review Q5-03): late completion, slot reuse, reuse while a write is in flight, two threads concurrently
// With -DLTR_LEGACY the 46e57ec pattern is built instead (a plain double per slot, zeroed by the display link thread, written directly by the completion handler, read directly by the display link thread) as the control: the semantic checks fail and -fsanitize=thread reports a data race
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef LTR_LEGACY
#include <stdint.h>
typedef struct { double gpu_end; } ltr_slot;
static int ltr_claim(ltr_slot *s, uint64_t k) { (void)k; s->gpu_end = 0; return 1; }
static int ltr_publish(ltr_slot *s, uint64_t k, double v) { (void)k; s->gpu_end = v; return 1; }
static double ltr_read(ltr_slot *s, uint64_t k) { (void)k; return s->gpu_end; }
#define LTR_BUSY 0
#else
#include "../nvn/nvn_ltring.h"
#endif
#define LT 512
static ltr_slot g[LT];
static int fail, count;
#define EXPECT(name, cond) do { count++; int ok_ = !!(cond); printf("%s %s\n", name, ok_ ? "✓" : "✗"); fail += !ok_; } while (0)
static double val(uint64_t k) { return 1000.0 + (double)k; }   // 每条回调的「GPU 完成时刻」各不相同，读到别人的值一眼可辨 | each callback gets a distinct value, so reading another callback's value is obvious

// 并发：节拍线程按序占槽、晚 60 条读；完成线程按随机延迟写（最多晚 700 条，超过环长 = 一定会碰到槽已复用） | Concurrent: the link thread claims in order and reads 60 behind; the completion thread writes with random delays (up to 700 late, beyond the ring length = slot reuse is certain)
#define N 200000
static _Atomic uint64_t q_head; static uint64_t q[N];
static _Atomic uint64_t processed;   // 完成线程已处理到第几条（压后的除外） | how far the completion thread has got (except the delayed ones)
static void *completion(void *a) {
    (void)a; unsigned seed = 7; uint64_t next = 0, lag[8] = {0}; int nl = 0;
    while (next < N) {
        uint64_t avail = atomic_load(&q_head);
        if (next < avail) {
            uint64_t k = q[next++]; atomic_store(&processed, next);
            if (nl < 8 && rand_r(&seed) % 97 == 0) { lag[nl++] = k; continue; }   // 少数回调压后很久才完成 | a few callbacks complete much later
            ltr_publish(&g[k % LT], k, val(k));
        }
        for (int i = 0; i < nl; i++) if (avail > lag[i] + 700) { ltr_publish(&g[lag[i] % LT], lag[i], val(lag[i])); lag[i] = lag[--nl]; i--; }
    }
    for (int i = 0; i < nl; i++) ltr_publish(&g[lag[i] % LT], lag[i], val(lag[i]));
    return NULL;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    // 1. 正常：占槽 → 写 → 读 | Normal: claim → publish → read
    EXPECT("占槽成功", ltr_claim(&g[5], 5));
    EXPECT("写入本条", ltr_publish(&g[5], 5, val(5)));
    EXPECT("读回本条的值", ltr_read(&g[5], 5) == val(5));
    // 2. 延迟完成：还没写时读 = 缺失（0），不是上一轮的旧值 | Delayed completion: reading before the write = missing (0), not the previous round's value
    EXPECT("槽复用：占给 5+LT", ltr_claim(&g[5], 5 + LT));
    EXPECT("未写时读 = 0（不是上一轮的值）", ltr_read(&g[5], 5 + LT) == 0);
    // 3. 迟到：5+LT 的完成回调在槽被 5+2LT 复用之后才到，必须丢弃 | Late: the completion of 5+LT arrives after the slot was reused by 5+2LT and must be dropped
    EXPECT("再复用：占给 5+2LT", ltr_claim(&g[5], 5 + 2 * LT));
    EXPECT("迟到的完成回调被拒", !ltr_publish(&g[5], 5 + LT, val(5 + LT)));
    EXPECT("新主人读不到迟到的值", ltr_read(&g[5], 5 + 2 * LT) == 0);
    EXPECT("新主人自己的值照常写入", ltr_publish(&g[5], 5 + 2 * LT, val(5 + 2 * LT)) && ltr_read(&g[5], 5 + 2 * LT) == val(5 + 2 * LT));
    EXPECT("旧序号读不到新主人的值", ltr_read(&g[5], 5 + LT) == 0);
    // 4. 写到一半时复用：上一个主人正在写（中间态），节拍线程不占、不等，这条回调记缺失；写完后旧值只归旧序号 | Reuse while a write is in flight: the link thread neither claims nor waits, this callback counts as missing; once done the value belongs to the old number only
#ifndef LTR_LEGACY
    ltr_claim(&g[9], 9); atomic_store(&g[9].seq, (9 + 1) | LTR_BUSY);
    EXPECT("正在写的槽不被占", !ltr_claim(&g[9], 9 + LT));
    atomic_store(&g[9].seq, 9 + 1);
    EXPECT("写完后新序号读不到旧值", ltr_read(&g[9], 9 + LT) == 0);
#else
    ltr_claim(&g[9], 9); ltr_publish(&g[9], 9, val(9));
    EXPECT("正在写的槽不被占", !ltr_claim(&g[9], 9 + LT));
    EXPECT("写完后新序号读不到旧值", ltr_read(&g[9], 9 + LT) == 0);
#endif
    // 5. 两线程并发：读到的每个非零值都必须是本条回调自己的 | Two threads: every non-zero value read must be the callback's own
    memset(g, 0, sizeof g);   // 线程还没起 | the thread has not started yet
    pthread_t th; pthread_create(&th, NULL, completion, NULL);
    uint64_t wrong = 0, got = 0;
    for (uint64_t k = 0; k < N; k++) {
        ltr_claim(&g[k % LT], k); q[k] = k; atomic_store(&q_head, k + 1);
        if (k >= 60) { while (atomic_load(&processed) < k - 59) sched_yield();   // 模拟真实情况：60 次回调之后绝大多数 GPU 早已完成 | as in practice: 60 callbacks later almost every GPU completion has arrived
            double v = ltr_read(&g[(k - 60) % LT], k - 60); if (v != 0) { got++; if (v != val(k - 60)) wrong++; } }
    }
    pthread_join(th, NULL);
    printf("并发：读到 %llu 条，其中不是本条的值 %llu 条\n", (unsigned long long)got, (unsigned long long)wrong);
    EXPECT("并发：读到的值全部属于本条回调", wrong == 0);
    EXPECT("并发：大部分回调读得到值", got > N / 2);
    printf("== %d 项，失败 %d ==\n", count, fail);
    return fail != 0;
}
