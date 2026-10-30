// 药水真堆叠（表见 dm/ios/tools/stack_potions.py）：宿主这边的部分，都在服务端，只调原版函数、只发原版包。
// 数量 = 物品属性 70（0x46，回城书、钥匙同一个）：读 main+0x5b9290(物品, 0x46, 0)，加减 main+0x5bc5a0(物品, 0x46, 差值, 0)，通知客户端 main+0x63cc80(客户端, 物品, 1, 0x46, 值, 0)（同原版 0x64f188–0x64f1bc）。
// 喝药：原版使用物品（命令 0x26，0x6d3xxx）摘下并释放整件，只改表喝一口整叠就没了。背包、腰带键、自动喝药、佣兵喂药都走这一个命令，所以在命令分派入口（diag.c c2s_obs）接住：
//   数量 ≥ 2 → 留一份位流 → 让原版处理这条包（能不能喝、药效、列内补位都是原版的）→ 物品没了就在原位建回数量少 1 的一叠。外层那条包换成原版会忽略的（目标类型非法，0x6d30a0 直接返回）。
//   没喝成（原版拒了）物品还在，什么都不做。
// 散瓶：商人和怪物给的是单瓶，旧存档读进来数量是 0。服务端拍里把背包同种散瓶并进一叠，封顶 99（DM_STACK_MAX）。
// 紫药：rvs / rvl 表里开着 AdvancedStashStackable（可进材料页），写档走 8 位那个字段（物品数据 +0x9c，dm_pouch.c），属性 70 不落盘。所以背包和腰带里的紫药把数量同步到 +0x9c，读档后再抄回属性。
// True potion stacking (tables in dm/ios/tools/stack_potions.py): the host side, all on the server, calling only original functions and sending only original packets.
// The count is item stat 70 (0x46, same as tomes and keys): read main+0x5b9290(item, 0x46, 0), adjust main+0x5bc5a0(item, 0x46, delta, 0), notify the client main+0x63cc80(client, item, 1, 0x46, value, 0) (same as the original 0x64f188–0x64f1bc).
// Drinking: the original use-item (command 0x26, 0x6d3xxx) detaches and frees the whole item, so with the tables alone one sip removes the stack. Inventory, belt keys, auto-drink and feeding the merc all go through this one command, so it is caught at the command dispatch entry (c2s_obs in diag.c):
//   count >= 2 -> keep a bitstream copy -> let the original handle this packet (whether it can be drunk, the effect and the column shift are all original) -> if the item is gone, rebuild a stack one smaller in place. The outer packet is replaced with one the original ignores (illegal target type, 0x6d30a0 returns at once).
//   If not drunk (rejected by the original) the item is still there and nothing is done.
// Loose bottles: vendors and monsters give single bottles and old saves load with count 0. In the server tick, loose bottles of a kind in the inventory merge into one stack, capped at 99 (DM_STACK_MAX).
// Rejuvenation: rvs / rvl have AdvancedStashStackable on in the table (they can go to the materials page), so saving uses that 8-bit field (item data +0x9c, dm_pouch.c) and stat 70 is not saved. So rejuvenation potions in the inventory and belt mirror the count to +0x9c, and it is copied back to the stat after loading.
// 进腰带：原版整叠搬走。命令 0x53 同样接住：让原版把这一叠移进腰带，再把腰带里那件改成 1 瓶、背包原格建回其余的（背包格刚腾出来，一定放得下）。自动补腰带（dm_beltauto.c）和长按进腰带都走它，所以都是一瓶一瓶补。
// 方块：配方按件数算（qty=3 要 3 件），叠算一件。合成命令 0x20 之前把方块里的叠拆成单瓶；方块里只有一叠 = 拆半；只有同种两叠 = 合并（都不合成）。
// 拆分界面（物品栏格子上的数字与拆 1 / 拆半 / 全部）：控件在 pouch_layout.py，都叫 TouchQty*。触屏长按、手柄按住 X 打开。服务端做，界面只发请求。拆出来的记在名单里不再被自动合并，直到下次进局。
// Test switch DM_TEST_STACK_LOG=<ticks>: every this many server ticks log the potions in the inventory and belt with their counts.
// Into the belt: the original moves the whole stack. Command 0x53 is caught the same way: let the original move the stack to the belt, then set the belt item to 1 bottle and rebuild the rest in the inventory cell it just vacated (always free). Auto-refill (dm_beltauto.c) and long press both use it, so the belt fills one bottle at a time.
// Cube: recipes count items (qty=3 needs 3 items) and a stack counts as one. Before the transmute command 0x20 stacks in the cube are split into single bottles; a lone stack in the cube = split in half; exactly two stacks of a kind = merge (no transmute in either case).
// Split UI (the digits on inventory cells and split 1 / half / all): widgets in pouch_layout.py, all named TouchQty*. Opened by a touch long press or holding X on a controller. The server does the work, the UI only sends requests. Split-off items go on a list and are not auto-merged again until the next run.
#include "dm_widget.h"
#include "hle.h"
#include "dm_i18n.h"
#include "dm_cursor_item.h"
#include "dm_input.h"
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DM_STACK_MAX 99
#define COMPACT 0x200000u   // 物品数据 +0x18 的简单物品位：写档看物品自己这一位，旧档读进来的药水要清掉，否则数量不落盘 | Simple-item bit in item data +0x18: saving looks at the item's own bit, so potions from old saves must clear it or the count is not saved
int dm_item_write(uint8_t *b, uint64_t it, uint8_t *buf); uint64_t dm_item_make(uint8_t *b, uint64_t G, uint64_t room, const uint8_t *buf, int len, int *socks_out); uint64_t dm_diag_srv_item(uint32_t id);
static uint8_t *B;
static _Atomic int g_notice_full;
static int is_potion(uint64_t it) { int (*is)(uint64_t, int) = (int (*)(uint64_t, int))(B + 0x574bc0); return is(it, 77) || is(it, 78) || is(it, 79) || is(it, 80) || is(it, 81) || is(it, 82); }
static uint32_t code_of(uint64_t it) { return ((uint32_t (*)(uint64_t))(B + 0x570260))(it); }
static int is_rejuv(uint64_t it) { uint32_t c = code_of(it); const char *k = (const char *)&c; return k[0] == 'r' && k[1] == 'v' && (k[2] == 's' || k[2] == 'l'); }
static int qty(uint64_t it) { return ((int (*)(uint64_t, int, int))(B + 0x5b9290))(it, 0x46, 0); }
static void set_qty(uint64_t player, uint64_t it, int n) {
    ((void (*)(uint64_t, int, int, int))(B + 0x5bc5a0))(it, 0x46, n - qty(it), 0);   // 0x5bc5a0 是加减（原版 0x647558 传 -数量） | 0x5bc5a0 adds (the original at 0x647558 passes -count)
    uint64_t d = *(uint64_t *)(it + 0x10); if (d) { *(uint32_t *)(d + 0x18) &= ~COMPACT; if (is_rejuv(it)) *(uint8_t *)(d + 0x9c) = (uint8_t)(n >= 2 ? n : 0); }
    uint64_t pd = *(uint32_t *)player == 0 ? *(uint64_t *)(player + 0x10) : 0, cl = pd ? *(uint64_t *)(pd + 0xf0) : 0;
    if (cl) ((void (*)(uint64_t, uint64_t, int, int, int, int))(B + 0x63cc80))(cl, it, 1, 0x46, n, 0);
}
static uint64_t inv_first(uint64_t player) { uint64_t inv = *(uint64_t *)(player + 0x90); return inv && *(uint32_t *)inv == 0x01020304 ? *(uint64_t *)(inv + 0x10) : 0; }

static uint32_t g_exempt[32]; static unsigned g_nex;   // 拆出来的，不再自动合并 | Split off: not auto-merged again
static int exempt(uint32_t id) { for (int i = 0; i < 32; i++) if (g_exempt[i] == id) return 1; return 0; }
static void exempt_add(uint32_t id) { g_exempt[g_nex++ % 32] = id; }
// 这一页的第一个空格（页的格表：物品栏 +0x20 起每项 0x20，下标 页 + 2；+0x10 宽、+0x11 高、+0x18 格指针）。只给 1×1 的药水用
// The first free cell of a page (page grids: item container +0x20, 0x20 per entry, index page + 2; +0x10 width, +0x11 height, +0x18 cell pointers). For 1×1 potions only
static int free_cell(uint64_t player, int page, int *x, int *y) {
    uint64_t inv = *(uint64_t *)(player + 0x90), pg = inv ? *(uint64_t *)(inv + 0x20) : 0; if (!pg || (uint64_t)page + 2 >= *(uint64_t *)(inv + 0x28)) return 0;
    uint64_t e = pg + (uint64_t)(page + 2) * 0x20, cells = *(uint64_t *)(e + 0x18); int w = *(uint8_t *)(e + 0x10), h = *(uint8_t *)(e + 0x11); if (!cells) return 0;
    for (int yy = 0; yy < h; yy++) for (int xx = 0; xx < w; xx++) if (!*(uint64_t *)(cells + 8 * (uint64_t)(yy * w + xx))) { *x = xx; *y = yy; return 1; }
    return 0;
}
// 放回：位流建物品，数量改成 n，放到 (mode, x, y)。腰带：列内已被原版补位，落点问原版腰带找位（同类那一列的最低空格）；背包：原格
// Put back: build the item from the bitstream, set the count to n, place at (mode, x, y). Belt: the column has been shifted by the original, so ask the original belt finder (lowest free cell of that kind's column); inventory: the same cell
static uint32_t put_back(uint64_t G, uint64_t player, const uint8_t *bits, int len, int n, uint32_t mode, int page, int x, int y) {
    uint64_t pp = *(uint64_t *)(player + 0x38), room = pp ? *(uint64_t *)(pp + 0x20) : 0; if (!room) return 0;
    uint64_t nu = dm_item_make(B, G, room, bits, len, NULL); if (!nu) return 0;
    uint64_t nd = *(uint64_t *)(nu + 0x10), ip = *(uint64_t *)(nu + 0x38), inv = *(uint64_t *)(player + 0x90); if (!nd || !ip || !inv) { ((void (*)(uint64_t, uint64_t))(B + 0x658be0))(G, nu); return 0; }
    if (mode == 2) { uint32_t ns = 0; if (((int (*)(uint64_t, uint64_t, uint32_t *, int))(B + 0x561de0))(inv, nu, &ns, 1)) x = (int)ns; else { ((void (*)(uint64_t, uint64_t))(B + 0x658be0))(G, nu); return 0; } y = 0; }
    else *(uint8_t *)(nd + 0x55) = (uint8_t)page;
    *(int32_t *)(ip + 0x10) = x; *(int32_t *)(ip + 0x14) = y;
    uint64_t one = nu, ox = 0; ((int (*)(uint64_t, uint64_t, uint64_t *, uint64_t *))(B + 0x6f1940))(G, player, &one, &ox);
    if (!*(uint64_t *)(nd + 0xa0)) { ((void (*)(uint64_t, uint64_t))(B + 0x658be0))(G, nu); return 0; }
    if (mode == 2) ((void (*)(uint64_t, uint64_t, uint64_t, int, int, int, int, int, uint64_t))(B + 0x642f70))(G, player, nu, 1, 0, 2, 0xff, 0, (uint64_t)(uint32_t)x);
    else ((void (*)(uint64_t, uint64_t, uint64_t, int, int, int, int, int, uint64_t))(B + 0x642f70))(G, player, nu, 1, 2, 0, page, 0, (uint64_t)(x & 0xffff) | (uint64_t)y << 16);
    set_qty(player, nu, n); LOG("堆叠：放回 %.3s×%d（方式 %u，格 %d,%d，新 id %u）", (const char *)&(uint32_t){ code_of(nu) }, n, mode, x, y, *(uint32_t *)(nu + 8));
    return *(uint32_t *)(nu + 8);
}

// 把一叠拆出 k 瓶（新一叠，数量 k）放到这一页的空格。没空格返回 0
// Split k bottles off a stack (a new stack of k) into a free cell of this page. Returns 0 when there is no free cell
static uint32_t split_off(uint64_t G, uint64_t player, uint64_t it, int k, int page) {
    int n = qty(it), x, y; if (k < 1 || k >= n || !free_cell(player, page, &x, &y)) return 0;
    uint8_t buf[1024]; int bl = dm_item_write(B, it, buf); if (!bl) return 0;
    uint32_t nid = put_back(G, player, buf, bl, k, 0, page, x, y); if (!nid) return 0;
    set_qty(player, it, n - k); exempt_add(nid); return nid;
}
static void remove_item(uint64_t G, uint64_t player, uint64_t it, int x, int y);
// 合成前（返回 1 = 这次不合成）。方块页 = 3
// Before a transmute (returns 1 = no transmute this time). The cube page is 3
static int cube_gate(uint64_t G, uint64_t player) {
    uint64_t list[12]; int nl = 0, others = 0;
    for (uint64_t it = inv_first(player), k = 0; it && k < 2048; k++) { uint64_t d = *(uint64_t *)(it + 0x10); if (!d) break;
        if (*(uint32_t *)(it + 0xc) == 0 && *(uint8_t *)(d + 0x55) == 3) { if (is_potion(it) && nl < 12) list[nl++] = it; else others++; }
        it = *(uint64_t *)(d + 0xb0); }
    if (!nl) return 0;
    if (!others && nl == 1 && qty(list[0]) >= 2 && !(qty(list[0]) == 3 && is_rejuv(list[0]) && ((const char *)&(uint32_t){ code_of(list[0]) })[2] == 's')) {   /* 3 瓶小紫留给配方 3 小紫 → 大紫 | 3 small rejuvs are left to the recipe 3 rvs -> rvl */ int n = qty(list[0]); uint32_t c = code_of(list[0]); split_off(G, player, list[0], n / 2, 3); LOG("堆叠：方块里只有一叠 %.3s×%d → 拆半", (const char *)&c, n); return 1; }
    if (!others && nl == 2 && code_of(list[0]) == code_of(list[1]) && qty(list[0]) + qty(list[1]) <= DM_STACK_MAX && qty(list[0]) >= 2 && qty(list[1]) >= 2) {
        int t = qty(list[0]) + qty(list[1]); uint32_t c = code_of(list[0]); uint64_t pp = *(uint64_t *)(list[1] + 0x38); if (pp) { set_qty(player, list[0], t); remove_item(G, player, list[1], *(int32_t *)(pp + 0x10), *(int32_t *)(pp + 0x14)); LOG("堆叠：方块里两叠 %.3s → 合并成 %d", (const char *)&c, t); return 1; } }
    for (int i = 0; i < nl; i++) while (qty(list[i]) >= 2) { if (!split_off(G, player, list[i], 1, 3)) { LOG("堆叠：方块放不下，%.3s 留 %d 瓶一叠", (const char *)&(uint32_t){ code_of(list[i]) }, qty(list[i])); break; } }
    return 0;
}

// 命令分派入口（diag.c c2s_obs，regs：x0 游戏、x1 玩家、x2 包、x3 长度，可改）。包字段照 main+0x2cecf0：+6 目标类型，+0xa 物品 id
// Command dispatch entry (c2s_obs in diag.c; regs: x0 game, x1 player, x2 packet, x3 length, writable). Fields as in main+0x2cecf0: +6 target type, +0xa item id
void dm_stack_c2s(uint64_t *regs) {
    static int inside; uint64_t G = regs[0], player = regs[1]; uint8_t *pk = (uint8_t *)(uintptr_t)regs[2]; uint32_t len = (uint32_t)regs[3];
    { static int lg = -1; if (lg < 0) lg = getenv("DM_TEST_STACK_PKT") != NULL; if (lg && pk && len >= 1 && pk[0] != 0x01 && pk[0] != 0x02 && pk[0] != 0x03 && pk[0] != 0x04 && pk[0] != 0x6d) { char h[160]; int k = 0; for (uint32_t i = 0; i < len && i < 40; i++) k += snprintf(h + k, sizeof h - k, "%02x", pk[i]); LOG("堆叠：包 %s", h); } }   // 测试开关：记命令包 | Test switch: log command packets
    if (inside || !B || !G || !player || !pk || !len) return;
    static uint8_t noop[0x24]; uint32_t id = 0;
    if (pk[0] == 0x20) { if (cube_gate(G, player)) { memset(noop, 0, sizeof noop); noop[0] = 0x26; noop[6] = 0xff; regs[2] = (uint64_t)(uintptr_t)noop; regs[3] = sizeof noop; } return; }
    if (pk[0] == 0x26 && len >= 0x24) memcpy(&id, pk + 0xa, 4);
    else if (pk[0] == 0x53 && len >= 5) memcpy(&id, pk + 1, 4);
    else return;
    uint64_t it = dm_diag_srv_item(id); if (!it || !is_potion(it)) return;
    int n = qty(it); if (n < 2) return;
    uint8_t buf[1024]; int bl = dm_item_write(B, it, buf); uint64_t pp = *(uint64_t *)(it + 0x38), d = *(uint64_t *)(it + 0x10); uint32_t mode = *(uint32_t *)(it + 0xc), code = code_of(it); if (!bl || !pp || !d) return;
    int x = *(int32_t *)(pp + 0x10), y = *(int32_t *)(pp + 0x14), page = *(uint8_t *)(d + 0x55), ex = exempt(id); uint8_t op = pk[0];
    inside = 1; ((void (*)(uint64_t, uint64_t, uint8_t *, uint32_t))(B + 0x6e5b40))(G, player, pk, len); inside = 0;
    uint64_t still = dm_diag_srv_item(id); uint32_t nid = 0;
    if (op == 0x53) {
        if (!still || *(uint32_t *)(still + 0xc) != 2) LOG("堆叠：%.3s×%d（id %u）原版没进腰带，不动", (const char *)&code, n, id);
        else { set_qty(player, still, 1); nid = put_back(G, player, buf, bl, n - 1, mode, page, x, y); if (nid && ex) exempt_add(nid); if (!nid) LOG("【红】堆叠：%.3s 进腰带后放不回背包（格 %d,%d），丢了 %d 瓶", (const char *)&code, x, y, n - 1); }
    }
    else if (still && *(uint64_t *)(still + 0x10)) LOG("堆叠：%.3s×%d（id %u）原版没喝成，不动", (const char *)&code, n, id);
    else { nid = put_back(G, player, buf, bl, n - 1, mode, page, x, y); if (nid && ex) exempt_add(nid); if (!nid) LOG("【红】堆叠：%.3s 放不回去（方式 %u，格 %d,%d），丢了 %d 瓶", (const char *)&code, mode, x, y, n - 1); }
    memset(noop, 0, sizeof noop); noop[0] = 0x26; noop[6] = 0xff; regs[2] = (uint64_t)(uintptr_t)noop; regs[3] = sizeof noop;
}

// 摘下并释放（同百宝囊收进）
// Detach and free (same as stowing into the pouch)
static void remove_item(uint64_t G, uint64_t player, uint64_t it, int x, int y) {
    ((void (*)(uint64_t))(B + 0x55e430))(it);
    ((void (*)(uint64_t, uint64_t, uint64_t, int, int, int, int, int, int))(B + 0x642f70))(G, player, it, 0x20, 4, 0, 0, y, x);
    ((void (*)(uint64_t, uint64_t))(B + 0x658be0))(G, it);
}
static void normalize(uint64_t G, uint64_t player) {
    uint64_t inv = *(uint64_t *)(player + 0x90); if (inv && *(uint64_t *)(inv + 0x40)) return;   // 手上拿着东西：不动背包 | Something is in hand: leave the inventory alone
    uint64_t head[16] = { 0 }; int hq[16] = { 0 }, nh = 0, merged = 0;
    for (uint64_t it = inv_first(player), k = 0; it && k < 2048; k++) { uint64_t d = *(uint64_t *)(it + 0x10), nx = d ? *(uint64_t *)(d + 0xb0) : 0; if (!d) break;
        if (is_potion(it)) { uint32_t mode = *(uint32_t *)(it + 0xc); int q = qty(it);
            if (is_rejuv(it) && q >= 2 && *(uint8_t *)(d + 0x9c) != q) *(uint8_t *)(d + 0x9c) = (uint8_t)q;   // 原版并叠只改属性 | Native merging only changes the stat
            if (is_rejuv(it) && q < 2 && *(uint8_t *)(d + 0x9c) >= 2) { q = *(uint8_t *)(d + 0x9c); set_qty(player, it, q); LOG("堆叠：读档后紫药 %.3s 数量抄回 %d", (const char *)&(uint32_t){ code_of(it) }, q); }
            else if (q < 1) { q = 1; set_qty(player, it, 1); }
            if (mode == 0 && *(uint8_t *)(d + 0x55) == 0 && !merged && q < DM_STACK_MAX && !exempt(*(uint32_t *)(it + 8))) { uint32_t c = code_of(it); int h = -1; for (int i = 0; i < nh && h < 0; i++) if (code_of(head[i]) == c && hq[i] < DM_STACK_MAX) h = i;
                if (h < 0) { if (nh < 16) { head[nh] = it; hq[nh++] = q; } }
                else { uint64_t pp = *(uint64_t *)(it + 0x38); int take = q; if (hq[h] + take > DM_STACK_MAX) take = DM_STACK_MAX - hq[h];
                    hq[h] += take; set_qty(player, head[h], hq[h]); if (take == q && pp) remove_item(G, player, it, *(int32_t *)(pp + 0x10), *(int32_t *)(pp + 0x14)); else set_qty(player, it, q - take);
                    LOG("堆叠：并进 %.3s，那一叠现在 %d", (const char *)&c, hq[h]); merged = 1; } } }   // 链表被动过，这一拍到此为止 | The list was changed, stop for this tick
        if (merged) break; it = nx; }
    if (merged) { int dm_request_save(void); dm_request_save(); }
}

static _Atomic uint64_t g_split_req;   // 物品 id << 8 | 档（1 拆 1、2 拆半、3 全部） | item id << 8 | mode (1 split 1, 2 half, 3 all)
void dm_stack_srv_tick(uint8_t *b, uint64_t G, uint64_t player) {
    B = b; static uint64_t lp; if (player != lp) { lp = player; memset(g_exempt, 0, sizeof g_exempt); } if (!player) return;
    { uint64_t r = atomic_exchange(&g_split_req, 0); if (r) { uint32_t id = (uint32_t)(r >> 8); int m = (int)(r & 0xff); uint64_t it = dm_diag_srv_item(id), d = it ? *(uint64_t *)(it + 0x10) : 0;
        if (d && *(uint32_t *)(it + 0xc) == 0 && qty(it) >= 2) { int page = *(uint8_t *)(d + 0x55), n = qty(it), done = 0; uint32_t c = code_of(it);
            if (m == 3) { while (qty(it) >= 2 && split_off(G, player, it, 1, page)) done++; }
            else done = split_off(G, player, it, m == 2 ? n / 2 : 1, page) ? 1 : 0;
            LOG("堆叠：拆分 %.3s×%d（档 %d，页 %d）→ 拆出 %d 次，原叠剩 %d", (const char *)&c, n, m, page, done, qty(it));
            if (!done) atomic_store(&g_notice_full, 1); else { int dm_request_save(void); dm_request_save(); } }
        else LOG("堆叠：拆分请求 id %u 已不是一叠，不做", id); } }
    if (atomic_exchange(&g_notice_full, 0)) { void dm_game_notice(uint8_t *, const char *); dm_game_notice(b, dm_str(b, "dmStackNoRoom")); }
    uint32_t tq = *(uint32_t *)(G + 0x170); if (!(tq % 5)) normalize(G, player);
    static int per = -1; if (per < 0) { const char *e = getenv("DM_TEST_STACK_LOG"); per = e ? atoi(e) : 0; }
    // 测试开关 DM_TEST_STACK_CUBE=<拍>：背包第一叠药水走原版包放进方块 (0,0)，之后每 40 拍发一次合成包 0x20（共 3 次），看拆半 / 合并 / 配方
    // Test switch DM_TEST_STACK_CUBE=<tick>: put the first potion stack in the inventory into the cube at (0,0) with the original packets, then send the transmute packet 0x20 every 40 ticks (3 times), to observe split / merge / recipe
    { static int at = -1, st; if (at < 0) { const char *e = getenv("DM_TEST_STACK_CUBE"); at = e ? atoi(e) : 0; }
      if (at && tq >= (uint32_t)at + (uint32_t)st * 40 && st <= 3) { void (*disp)(uint64_t, uint64_t, uint8_t *, uint32_t) = (void (*)(uint64_t, uint64_t, uint8_t *, uint32_t))(b + 0x6e5b40); *(uint32_t *)(G + 0x19050) = 1;
          if (!st) { uint64_t f = 0; for (uint64_t it = inv_first(player), k = 0; it && k < 2048 && !f; k++) { uint64_t d = *(uint64_t *)(it + 0x10); if (!d) break; if (*(uint32_t *)(it + 0xc) == 0 && *(uint8_t *)(d + 0x55) == 0 && is_potion(it) && qty(it) >= 2) f = it; else it = *(uint64_t *)(d + 0xb0); }
              uint64_t pp = f ? *(uint64_t *)(f + 0x38) : 0; if (pp) { uint32_t id = *(uint32_t *)(f + 8), x = *(uint32_t *)(pp + 0x10), y = *(uint32_t *)(pp + 0x14), pg = 0, x0 = 0, y0 = 0, pc = 3; uint8_t a1[17] = { 0x19 }, a2[17] = { 0x18 }; memcpy(a1 + 1, &id, 4); memcpy(a1 + 5, &x, 4); memcpy(a1 + 9, &y, 4); memcpy(a1 + 13, &pg, 4); memcpy(a2 + 1, &id, 4); memcpy(a2 + 5, &x0, 4); memcpy(a2 + 9, &y0, 4); memcpy(a2 + 13, &pc, 4); disp(G, player, a1, 17); disp(G, player, a2, 17); LOG("测试 DM_TEST_STACK_CUBE：放进方块 id %u", id); } }
          else { uint8_t t[7] = { 0x20 }; disp(G, player, t, 7); LOG("测试 DM_TEST_STACK_CUBE：第 %d 次合成", st); }
          *(uint32_t *)(G + 0x19050) = 0; st++; } }
    if (!per || tq % (uint32_t)per) return;
    char s[768]; int k = 0; s[0] = 0;
    for (uint64_t it = inv_first(player), n = 0; it && n < 2048 && k < (int)sizeof s - 40; n++) { uint64_t d = *(uint64_t *)(it + 0x10), pp = *(uint64_t *)(it + 0x38); if (!d) break;
        if (is_potion(it)) { uint32_t c = code_of(it); k += snprintf(s + k, sizeof s - k, " %.3s×%d/%u@%u/%u/%d,%d", (const char *)&c, qty(it), *(uint8_t *)(d + 0x9c), *(uint32_t *)(it + 0xc), *(uint8_t *)(d + 0x55), pp ? *(int32_t *)(pp + 0x10) : -1, pp ? *(int32_t *)(pp + 0x14) : -1); }
        it = *(uint64_t *)(d + 0xb0); }
    LOG("堆叠：拍 %u 药水（码×数量/+9c@方式/页/x,y）:%s", tq, s);
}

// ---- 界面（游戏主线程；只写自己的 TouchQty* 控件）
// ---- UI (game main thread; writes only our own TouchQty* widgets)
#define NDIG 24
#define CELL 98   // 背包网格 (216, 893)、一格 98 布局单位（布局文件） | Inventory grid at (216, 893), 98 layout units per cell (layout file)
#define OFF 99999
static _Atomic uint32_t g_box_id; static int g_box_n;
static uint64_t find_w(uint64_t w, const char *name, int dep) {
    if (!w || dep > 14) return 0; const char *nm = (const char *)*(uint64_t *)(w + 8); if (nm && !strcmp(nm, name)) return w;
    uint64_t ch = *(uint64_t *)(w + 0x58), n = *(uint64_t *)(w + 0x60); if (!ch || n > 256) return 0;
    for (uint64_t i = 0; i < n; i++) { uint64_t r = find_w(*(uint64_t *)(ch + 8 * i), name, dep + 1); if (r) return r; }
    return 0;
}
static int ours(uint64_t w) { const char *nm = w ? (const char *)*(uint64_t *)(w + 8) : NULL; return nm && !strncmp(nm, "TouchQty", 8); }
static void w_move(uint64_t w, int x, int y) { if (!ours(w)) return; int32_t *r = (int32_t *)(w + 0x70); if (r[0] == x && r[1] == y) return; r[0] = x; r[1] = y; ((void (*)(uint64_t))*(uint64_t *)(*(uint64_t *)w + 0x10))(w); }
static void w_text(uint64_t w, const char *t) {   // 同 dm_pouch.c w_text | Same as w_text in dm_pouch.c
    if (!ours(w) || *(uint64_t *)w - (uint64_t)(uintptr_t)B != 0x20ba4c8) return; size_t n = strlen(t);
    if (*(uint64_t *)(w + 0x90) == n && !memcmp(*(const char **)(w + 0x88), t, n)) return;
    ((void (*)(uint64_t, const char *, uint64_t))(B + 0x136e5c))(w + 0x88, t, n); ((void (*)(uint64_t))(B + 0xadc434))(w); }
static uint64_t cli_item(uint32_t id) { return ((uint64_t (*)(uint64_t, uint32_t, uint32_t, uint32_t))(B + 0x45f5f0))((uint64_t)(uintptr_t)(B + 0x2d603a0 + 4 * 0x400), id & 0x7f, id, 4); }
int dm_stack_box_open(void) { return atomic_load(&g_box_id) != 0; }
static void box_close(void) { if (atomic_exchange(&g_box_id, 0)) LOG("堆叠：拆分框收起"); }
static void box_choose(int m) { uint32_t id = atomic_load(&g_box_id); if (!id) return; atomic_store(&g_split_req, (uint64_t)id << 8 | (uint64_t)m); box_close(); }
// 打开拆分框：背包或个人页里叠数 ≥ 2 的药水（ci 取自手柄光标 / 按下点）。返回 1 = 开了
// Open the split box: a potion stack of 2 or more in the inventory or a personal page (ci from the controller cursor / press point). Returns 1 = opened
int dm_stack_open(const dm_cursor_item_info *ci) {
    if (!B || !ci->potion || (ci->container != DM_ITEM_AT_INVENTORY && ci->container != DM_ITEM_AT_STASH) || ci->stack) return 0;
    uint64_t it = cli_item(ci->id); int n = it ? qty(it) : 0; if (n < 2) return 0;
    g_box_n = n; atomic_store(&g_box_id, ci->id); LOG("堆叠：拆分框打开 %s×%d（id %u）", ci->code, n, ci->id); return 1;
}
// 手柄：按住 X 0.4 秒打开；开着时 A 拆 1、Y 拆半、X 全部、B 取消，这几个键不给游戏（dm_cursor.c cursor_apply 每帧调，仅手柄模式）
// Controller: hold X for 0.4 s to open; while open A splits 1, Y half, X all, B cancels, and these keys do not reach the game (called every frame from cursor_apply in dm_cursor.c, controller mode only)
void dm_stack_pad(uint64_t *buttons, uint64_t now_ns) {
    static uint64_t since, prev; uint64_t b = *buttons, edge = b & ~prev; prev = b;
    if (dm_stack_box_open()) { if (edge & DM_BTN_A) box_choose(1); else if (edge & DM_BTN_Y) box_choose(2); else if (edge & DM_BTN_X) box_choose(3); else if (edge & DM_BTN_B) box_close(); *buttons &= ~(uint64_t)(DM_BTN_A | DM_BTN_B | DM_BTN_X | DM_BTN_Y); since = 0; return; }
    if (!(b & DM_BTN_X)) { since = 0; return; }
    if (!since) { since = now_ns; return; }
    if (now_ns - since >= 400000000ull) { since = 0; dm_cursor_item_info ci; if (dm_cursor_item(&ci)) dm_stack_open(&ci); }
}
// 按下点在框开着时：点框里的按钮照常；点外面只收框（同百宝囊清单）
// A press while the box is open: its buttons work as usual; anywhere else only closes the box (same as the pouch list)
int dm_stack_box_gate(int x, int y) {
    if (!dm_stack_box_open() || !B) return 0; uint64_t root = *(uint64_t *)(B + 0x2f05458);
    static const char *const bt[] = { "TouchQtyOne", "TouchQtyHalf", "TouchQtyAll" };
    for (int i = 0; i < 3; i++) { uint64_t w = find_w(root, bt[i], 1); if (w && (((int (*)(uint64_t, uint64_t))*(uint64_t *)(*(uint64_t *)w + 0x48))(w, (uint64_t)(uint32_t)x | ((uint64_t)(uint32_t)y << 32)) & 1)) return 0; }
    box_close(); return 1;
}
void dm_stack_on_uimsg(const char *cat, const char *act, const char *arg) {
    if (!cat || !act || !arg || strcmp(cat, "PanelManager") || strcmp(act, "ClosePanel")) return;
    if (!strcmp(arg, "TouchQtyOne")) box_choose(1); else if (!strcmp(arg, "TouchQtyHalf")) box_choose(2); else if (!strcmp(arg, "TouchQtyAll")) box_choose(3);
}
void dm_stack_ui(uint8_t *b, uint64_t inv_panel, uint64_t pl) {   // 游戏主线程每帧（dm_pouch.c ui_pump；物品栏面板开着才来） | Game main thread every frame (ui_pump in dm_pouch.c; only while the inventory panel is open)
    B = b; char nm[20]; int nd = 0; uint64_t inv = pl ? *(uint64_t *)(pl + 0x90) : 0;
    for (uint64_t it = inv && *(uint32_t *)inv == 0x01020304 ? *(uint64_t *)(inv + 0x10) : 0, k = 0; it && k < 2048 && nd < NDIG; k++) { uint64_t d = *(uint64_t *)(it + 0x10), pp = *(uint64_t *)(it + 0x38); if (!d) break;
        if (*(uint32_t *)(it + 0xc) == 0 && *(uint8_t *)(d + 0x55) == 0 && pp && is_potion(it)) { int q = qty(it);
            if (q >= 2) { snprintf(nm, sizeof nm, "TouchQtyN%02d", nd++); uint64_t w = find_w(inv_panel, nm, 3); char t[8]; snprintf(t, sizeof t, "%d", q); w_text(w, t); w_move(w, 216 + CELL * *(int32_t *)(pp + 0x10), 893 + CELL * *(int32_t *)(pp + 0x14) + 60); } }
        it = *(uint64_t *)(d + 0xb0); }
    for (; nd < NDIG; nd++) { snprintf(nm, sizeof nm, "TouchQtyN%02d", nd); w_move(find_w(inv_panel, nm, 3), OFF, 0); }
    int open = dm_stack_box_open(), bx = open ? 420 : OFF;
    w_move(find_w(inv_panel, "TouchQtyBg", 3), bx, 900);
    w_move(find_w(inv_panel, "TouchQtyTitle", 3), bx + 20, 920);
    w_move(find_w(inv_panel, "TouchQtyOne", 3), bx + 139, 990); w_move(find_w(inv_panel, "TouchQtyHalf", 3), bx + 139, 1070); w_move(find_w(inv_panel, "TouchQtyAll", 3), bx + 139, 1150);
    if (open) { char t[96]; snprintf(t, sizeof t, dm_str(b, "dmStackTitle"), g_box_n); w_text(find_w(inv_panel, "TouchQtyTitle", 3), t); }
}
void dm_stack_panel_closed(void) { box_close(); }
// 测试开关 DM_TEST_STACK_SPLIT=<帧>:<档>：到帧对背包第一叠药水走一次拆分（不经界面）；DM_TEST_STACK_BOX=<帧>：到帧打开拆分框（截图用）
// Test switches DM_TEST_STACK_SPLIT=<frame>:<mode>: at that frame split the first potion stack in the inventory (no UI); DM_TEST_STACK_BOX=<frame>: open the split box at that frame (for screenshots)
void dm_stack_test(uint8_t *b, uint64_t pl, uint64_t fr) {
    static int at = -1, m, bx = -1; if (at < 0) { const char *e = getenv("DM_TEST_STACK_SPLIT"); if (!e || sscanf(e, "%d:%d", &at, &m) != 2) at = 0; e = getenv("DM_TEST_STACK_BOX"); bx = e ? atoi(e) : 0; }
    { static int sip = -1; if (sip < 0) { const char *e = getenv("DM_TEST_STACK_SIP"); sip = e ? atoi(e) : 0; } if (sip && fr >= (uint64_t)sip) { sip = 0; void dm_belt_use(int, int); dm_belt_use(0, 0); LOG("测试 DM_TEST_STACK_SIP：喝第 1 列一瓶"); } }
    if ((!at || fr < (uint64_t)at) && (!bx || fr < (uint64_t)bx)) return; B = b; uint64_t inv = pl ? *(uint64_t *)(pl + 0x90) : 0, f = 0;
    for (uint64_t it = inv ? *(uint64_t *)(inv + 0x10) : 0, k = 0; it && k < 2048 && !f; k++) { uint64_t d = *(uint64_t *)(it + 0x10); if (!d) break; if (*(uint32_t *)(it + 0xc) == 0 && *(uint8_t *)(d + 0x55) == 0 && is_potion(it) && qty(it) >= 2) f = it; else it = *(uint64_t *)(d + 0xb0); }
    if (!f) return;
    if (at && fr >= (uint64_t)at) { at = 0; atomic_store(&g_split_req, (uint64_t)*(uint32_t *)(f + 8) << 8 | (uint64_t)m); LOG("测试 DM_TEST_STACK_SPLIT：拆 id %u 档 %d", *(uint32_t *)(f + 8), m); }
    if (bx && fr >= (uint64_t)bx) { bx = 0; g_box_n = qty(f); atomic_store(&g_box_id, *(uint32_t *)(f + 8)); LOG("测试 DM_TEST_STACK_BOX：打开拆分框"); }
}
