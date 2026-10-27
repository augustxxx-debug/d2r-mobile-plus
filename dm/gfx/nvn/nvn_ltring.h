// 显示节拍逐回调记录里「上屏命令缓冲 GPU 完成时刻」那一格（五审 Q5-03）：节拍线程占槽、GPU 完成回调（另一个线程）写值、节拍线程晚 60 次回调再读。
// 槽号 = 回调序号 % 环长，所以每格带完整回调序号：seq = 序号 + 1（已写好）/ 序号 + 1 | 待写 / 序号 + 1 | 正在写，三种状态都在一个原子字里切换：
//   占槽（节拍线程）：上一个主人正在写就不占（这次回调的 GPU 时刻记作缺失，不等它）；否则换成「本序号待写」。
//   写值（完成回调）：只有槽仍是「本序号待写」时才写（比较交换成「正在写」→ 写值 → 发布成「已写好」）；槽已被后面的回调复用 = 迟到，丢弃并返回 0。
//   读值（节拍线程）：槽是「本序号已写好」才读，否则当缺失（0）。值字段也是原子的，所以任何交错下都没有数据竞争，也不会读到别的回调的值。
// The "GPU completion time of the present command buffer" cell of the per-callback display link record (fifth review Q5-03): the display link thread claims a slot, the GPU completion handler (another thread) writes the value, and the display link thread reads it 60 callbacks later.
// Slot = callback number % ring length, so each cell carries the full callback number: seq = number + 1 (written) / number + 1 | pending / number + 1 | writing, all three states switched in one atomic word:
//   claim (display link thread): if the previous owner is still writing, do not claim (this callback's GPU time counts as missing; never wait); otherwise switch to "pending for this number".
//   publish (completion handler): write only while the slot is still "pending for this number" (compare-exchange to "writing" → store the value → release to "written"); a slot already reused by a later callback = late, dropped and 0 is returned.
//   read (display link thread): read only when the slot is "written for this number", else missing (0). The value field is atomic too, so no interleaving is a data race and no callback ever reads another callback's value.
#ifndef NVN_LTRING_H
#define NVN_LTRING_H
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#define LTR_PEND (1ull << 62)
#define LTR_BUSY (1ull << 63)
typedef struct { _Atomic uint64_t seq, bits; } ltr_slot;
static inline int ltr_claim(ltr_slot *s, uint64_t k) {
    uint64_t cur = atomic_load_explicit(&s->seq, memory_order_acquire);
    if (cur & LTR_BUSY) return 0;
    return atomic_compare_exchange_strong_explicit(&s->seq, &cur, (k + 1) | LTR_PEND, memory_order_acq_rel, memory_order_acquire);
}
static inline int ltr_publish(ltr_slot *s, uint64_t k, double v) {
    uint64_t want = (k + 1) | LTR_PEND, b;
    if (!atomic_compare_exchange_strong_explicit(&s->seq, &want, (k + 1) | LTR_BUSY, memory_order_acquire, memory_order_relaxed)) return 0;
    memcpy(&b, &v, sizeof b); atomic_store_explicit(&s->bits, b, memory_order_relaxed);
    atomic_store_explicit(&s->seq, k + 1, memory_order_release);
    return 1;
}
static inline double ltr_read(ltr_slot *s, uint64_t k) {
    if (atomic_load_explicit(&s->seq, memory_order_acquire) != k + 1) return 0;
    uint64_t b = atomic_load_explicit(&s->bits, memory_order_relaxed); double v; memcpy(&v, &b, sizeof v); return v;
}
#endif
