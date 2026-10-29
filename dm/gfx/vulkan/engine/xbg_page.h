// 宿主页大小：安卓设备可能跑 16 KB 页内核（联发科 MT6995 支持，ro.product.cpu.pagesize.max = 16384），mprotect 的地址与长度必须按真实页对齐；
// 回写跟踪（wtrack）与事件页保护（evguard）原先写死 4 KB，16 KB 内核上 mprotect 会 EINVAL。启动时取一次 sysconf(_SC_PAGESIZE)。
// The host page size: an Android device may run a 16 KB-page kernel (the MediaTek MT6995 supports it, ro.product.cpu.pagesize.max = 16384), and
// mprotect needs addresses and lengths aligned to the real page; the writeback tracker (wtrack) and the event page guard (evguard) assumed 4 KB,
// so mprotect would fail with EINVAL on a 16 KB kernel. sysconf(_SC_PAGESIZE) is read once at start-up.
#pragma once
#include <cstddef>
#include <cstdint>
#include <unistd.h>

struct xbg_page { size_t size; unsigned shift; };
inline xbg_page xbg_page_of(size_t size) { return {size, unsigned(__builtin_ctzl(size))}; }
inline xbg_page xbg_page_host() { long s = sysconf(_SC_PAGESIZE); size_t z = s > 0 && !(size_t(s) & (size_t(s) - 1)) ? size_t(s) : 4096; return xbg_page_of(z); }
inline uintptr_t xbg_page_floor(uintptr_t a, const xbg_page &p) { return a & ~uintptr_t(p.size - 1); }
inline uintptr_t xbg_page_ceil(uintptr_t a, const xbg_page &p) { return (a + p.size - 1) & ~uintptr_t(p.size - 1); }
