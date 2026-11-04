// 存档管理的后台活与选文件的闸（dm_savemgr.c 用，单测 t_savepkg.c 直接测）：同一时间只跑一个后台活；选文件的回调只在「正在等」且没有后台活时才收（审计 20261104 P1-1）。
// The gate for save management's background job and file pick (used by dm_savemgr.c, tested directly by t_savepkg.c): one background job at a time; a pick callback is taken only while one is awaited and no job runs (audit 20261104 P1-1).
#pragma once
#include <stdatomic.h>

typedef struct { _Atomic int busy, done, rc, pick_wait; } dm_savejob;

// 占住后台活：已经有一个在跑就返回 0（调用方不起线程）| claim the job slot: 0 when one is already running (the caller starts no thread)
static inline int dm_savejob_begin(dm_savejob *j) { int idle = 0; if (!atomic_compare_exchange_strong(&j->busy, &idle, 1)) return 0; atomic_store(&j->done, 0); return 1; }
// 工作线程收尾 | the worker finishes
static inline void dm_savejob_finish(dm_savejob *j, int rc) { atomic_store(&j->rc, rc); atomic_store(&j->done, 1); }
// 主线程取结果：取到才放开占位，之后才能起下一个 | the main thread takes the result; only then is the slot released for the next job
static inline int dm_savejob_take(dm_savejob *j, int *rc) { if (!atomic_exchange(&j->done, 0)) return 0; *rc = atomic_load(&j->rc); atomic_store(&j->busy, 0); return 1; }
static inline int dm_savejob_busy(dm_savejob *j) { return atomic_load(&j->busy); }
// 打开系统面板前挂号；回调到了用 accept 销号：没挂号（重复、迟到的回调）或有后台活时不收 | arm before opening the system panel; the callback is accepted once: not armed (a duplicate or late callback) or a running job means it is dropped
static inline void dm_savejob_pick_arm(dm_savejob *j) { atomic_store(&j->pick_wait, 1); }
static inline int dm_savejob_pick_accept(dm_savejob *j) { return atomic_exchange(&j->pick_wait, 0) && !atomic_load(&j->busy); }
