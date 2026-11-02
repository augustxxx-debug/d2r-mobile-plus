// 药水真堆叠（表见 dm/ios/tools/stack_potions.py）：宿主这边的部分，都在服务端，只调原版函数、只发原版包。
// 数量 = 物品属性 70（0x46，回城书、钥匙同一个）：读 main+0x5b9290(物品, 0x46, 0)，加减 main+0x5bc5a0(物品, 0x46, 差值, 0)，通知客户端 main+0x63cc80(客户端, 物品, 1, 0x46, 值, 0)（同原版 0x64f188–0x64f1bc）。
// 喝药：原版使用物品（命令 0x26，0x6d3xxx）摘下并释放整件，只改表喝一口整叠就没了。背包、腰带键、自动喝药、佣兵喂药都走这一个命令，所以在命令分派入口（diag.c c2s_obs）接住：
//   数量 ≥ 2 → 留一份位流 → 让原版处理这条包（能不能喝、药效、列内补位都是原版的）→ 物品没了就在原位建回数量少 1 的一叠。外层那条包换成原版会忽略的（目标类型非法，0x6d30a0 直接返回）。
//   没喝成（原版拒了）物品还在，什么都不做。
// 散瓶：商人和怪物给的是单瓶，旧存档读进来数量是 0。服务端拍里把背包同种散瓶并进一叠，封顶 99（DMSTK_CAP）。
// 紫药：rvs / rvl 表里开着 AdvancedStashStackable（可进材料页），写档走 8 位那个字段（物品数据 +0x9c，dm_pouch.c），属性 70 不落盘。所以背包和腰带里的紫药把数量同步到 +0x9c，读档后再抄回属性。
// True potion stacking (tables in dm/ios/tools/stack_potions.py): the host side, all on the server, calling only original functions and sending only original packets.
// The count is item stat 70 (0x46, same as tomes and keys): read main+0x5b9290(item, 0x46, 0), adjust main+0x5bc5a0(item, 0x46, delta, 0), notify the client main+0x63cc80(client, item, 1, 0x46, value, 0) (same as the original 0x64f188–0x64f1bc).
// Drinking: the original use-item (command 0x26, 0x6d3xxx) detaches and frees the whole item, so with the tables alone one sip removes the stack. Inventory, belt keys, auto-drink and feeding the merc all go through this one command, so it is caught at the command dispatch entry (c2s_obs in diag.c):
//   count >= 2 -> keep a bitstream copy -> let the original handle this packet (whether it can be drunk, the effect and the column shift are all original) -> if the item is gone, rebuild a stack one smaller in place. The outer packet is replaced with one the original ignores (illegal target type, 0x6d30a0 returns at once).
//   If not drunk (rejected by the original) the item is still there and nothing is done.
// Loose bottles: vendors and monsters give single bottles and old saves load with count 0. In the server tick, loose bottles of a kind in the inventory merge into one stack, capped at 99 (DMSTK_CAP).
// Rejuvenation: rvs / rvl have AdvancedStashStackable on in the table (they can go to the materials page), so saving uses that 8-bit field (item data +0x9c, dm_pouch.c) and stat 70 is not saved. So rejuvenation potions in the inventory and belt mirror the count to +0x9c, and it is copied back to the stat after loading.
// 进腰带：原版整叠搬走。命令 0x53 同样接住：让原版把这一叠移进腰带，再把腰带里那件改成 1 瓶、背包原格建回其余的（背包格刚腾出来，一定放得下）。自动补腰带（dm_beltauto.c）和长按进腰带都走它，所以都是一瓶一瓶补。
// 方块：配方按件数算（qty=3 要 3 件），叠算一件。合成命令 0x20 之前把方块里的叠拆成单瓶；方块里只有一叠 = 拆半；只有同种两叠 = 合并（都不合成）。
// 拆分界面（物品栏格子上的数字与拆 1 / 拆半 / 全部）：控件在 pouch_layout.py，都叫 TouchQty*。触屏长按、手柄按住 X 打开。服务端做，界面只发请求。拆出来的记在名单里不再被自动合并，直到下次进局。
// Test switch DM_TEST_STACK_LOG=<ticks>: every this many server ticks log the potions in the inventory and belt with their counts.
// Into the belt: the original moves the whole stack. Command 0x53 is caught the same way: let the original move the stack to the belt, then set the belt item to 1 bottle and rebuild the rest in the inventory cell it just vacated (always free). Auto-refill (dm_beltauto.c) and long press both use it, so the belt fills one bottle at a time.
// Cube: recipes count items (qty=3 needs 3 items) and a stack counts as one. Before the transmute command 0x20 stacks in the cube are split into single bottles; a lone stack in the cube = split in half; exactly two stacks of a kind = merge (no transmute in either case).
// Split UI (the digits on inventory cells and split 1 / half / all): widgets in pouch_layout.py, all named TouchQty*. Opened by a touch long press or holding X on a controller. The server does the work, the UI only sends requests. Split-off items go on a list and are not auto-merged again until the next run.
#include "dm_bag_layout.h"
#include "dm_stack_rule.h"
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
    if (!others && nl == 1 && dmstk_cube_lone_split(qty(list[0]), is_rejuv(list[0]) && ((const char *)&(uint32_t){ code_of(list[0]) })[2] == 's')) {   /* 3 瓶小紫留给配方 3 小紫 → 大紫 | 3 small rejuvs are left to the recipe 3 rvs -> rvl */ int n = qty(list[0]); uint32_t c = code_of(list[0]); split_off(G, player, list[0], dmstk_split_amount(n, 2), 3); LOG("堆叠：方块里只有一叠 %.3s×%d → 拆半", (const char *)&c, n); return 1; }
    if (!others && nl == 2 && code_of(list[0]) == code_of(list[1]) && dmstk_merge_fits(qty(list[0]), qty(list[1])) && qty(list[0]) >= 2 && qty(list[1]) >= 2) {
        int t = qty(list[0]) + qty(list[1]); uint32_t c = code_of(list[0]); uint64_t pp = *(uint64_t *)(list[1] + 0x38); if (pp) { set_qty(player, list[0], t); remove_item(G, player, list[1], *(int32_t *)(pp + 0x10), *(int32_t *)(pp + 0x14)); LOG("堆叠：方块里两叠 %.3s → 合并成 %d", (const char *)&c, t); return 1; } }
    int split = 0; for (int i = 0; i < nl; i++) while (qty(list[i]) >= 2) { if (!split_off(G, player, list[i], 1, 3)) { LOG("堆叠：方块放不下，%.3s 留 %d 瓶一叠", (const char *)&(uint32_t){ code_of(list[i]) }, qty(list[i])); break; } split = 1; }
    return split ? 2 : 0;
}
// 合成包带着方块里的原料清单（[7] = 件数 − 1，之后每件 {u32 id, 格字节 x | y << 4}），服务端逐项核对（0x6e8938–0x6e8b0c），对不上静默丢。拆过之后按方块现状重写清单
// The transmute packet carries the cube's ingredient list ([7] = count − 1, then per item {u32 id, cell byte x | y << 4}) and the server checks it entry by entry (0x6e8938–0x6e8b0c), silently dropping on a mismatch. After a split, rewrite the list from the cube's current contents
static uint32_t cube_packet(uint64_t player, const uint8_t *pk, uint32_t len, uint8_t *o, uint32_t cap) {
    if (len < 8 || cap < 8) return 0; memcpy(o, pk, 8); uint32_t n = 8; int cnt = 0;
    for (uint64_t it = inv_first(player), k = 0; it && k < 2048; k++) { uint64_t d = *(uint64_t *)(it + 0x10), pp = *(uint64_t *)(it + 0x38); if (!d) break;
        if (*(uint32_t *)(it + 0xc) == 0 && *(uint8_t *)(d + 0x55) == 3 && pp && n + 5 <= cap) { memcpy(o + n, (void *)(it + 8), 4); o[n + 4] = (uint8_t)((*(int32_t *)(pp + 0x10) & 15) | (*(int32_t *)(pp + 0x14) & 15) << 4); n += 5; cnt++; }
        it = *(uint64_t *)(d + 0xb0); }
    if (!cnt) return 0; o[7] = (uint8_t)(cnt - 1); return n;
}

// 命令分派入口（diag.c c2s_obs，regs：x0 游戏、x1 玩家、x2 包、x3 长度，可改）。包字段照 main+0x2cecf0：+6 目标类型，+0xa 物品 id
// Command dispatch entry (c2s_obs in diag.c; regs: x0 game, x1 player, x2 packet, x3 length, writable). Fields as in main+0x2cecf0: +6 target type, +0xa item id
// 商店卖出：一叠 ≥2 瓶的 0x33 先拦下弹数量框（dm_stack_box_gate），选了再由客户端重新发。卖 1 / 卖半 = 服务端先拆出这么多瓶，再卖拆出来的那件
// Selling to a vendor: a 0x33 for a stack of 2+ bottles is held back and the quantity box opens; after the choice the client sends it again. Sell 1 / half = the server first splits that many off, then the split-off item is sold
static _Atomic uint32_t g_box_id; static int g_box_n; static _Atomic int g_box_sell;
static _Atomic uint64_t g_sell_req;   // 物品 id << 8 | 档（1 卖 1、2 卖半） | item id << 8 | mode (1 sell 1, 2 sell half)
static _Atomic uint32_t g_cli_pid; static _Atomic uint32_t g_sell_go, g_sell_pass;   // 这件可以卖了 / 这件的 0x33 放行 | this item may now be sold / let this item's 0x33 through
void dm_stack_c2s(uint64_t *regs) {
    static int inside; uint64_t G = regs[0], player = regs[1]; uint8_t *pk = (uint8_t *)(uintptr_t)regs[2]; uint32_t len = (uint32_t)regs[3];
    { static int lg = -1; if (lg < 0) lg = getenv("DM_TEST_STACK_PKT") != NULL; if (lg && pk && len >= 1 && pk[0] != 0x01 && pk[0] != 0x02 && pk[0] != 0x03 && pk[0] != 0x04 && pk[0] != 0x6d) { char h[160]; int k = 0; for (uint32_t i = 0; i < len && i < 40; i++) k += snprintf(h + k, sizeof h - k, "%02x", pk[i]); LOG("堆叠：包 %s", h); } }   // 测试开关：记命令包 | Test switch: log command packets
    if (inside || !B || !G || !player || !pk || !len) return;
    static uint8_t noop[0x24]; uint32_t id = 0;
    if (pk[0] == 0x20) { int g = cube_gate(G, player); static uint8_t cp[96]; uint32_t cl;
        if (g == 1) { memset(noop, 0, sizeof noop); noop[0] = 0x26; noop[6] = 0xff; regs[2] = (uint64_t)(uintptr_t)noop; regs[3] = sizeof noop; }
        else if (g == 2 && (cl = cube_packet(player, pk, len, cp, sizeof cp))) { regs[2] = (uint64_t)(uintptr_t)cp; regs[3] = cl; LOG("堆叠：方块里的叠拆成单瓶后重写合成清单（%u 字节）", cl); }
        return; }
    // 只给本机玩家弹框（联机客机没有这个框，照原版） | The box is only for the local player (a co-op guest has no such box and keeps the original behaviour)
    if (pk[0] == 0x33 && len >= 9) { memcpy(&id, pk + 5, 4); uint32_t pass = id; if (atomic_compare_exchange_strong(&g_sell_pass, &pass, 0)) return;
        uint64_t it = dm_diag_srv_item(id); if (it && *(uint32_t *)(player + 8) == atomic_load(&g_cli_pid) && is_potion(it) && qty(it) >= 2) { g_box_n = qty(it); atomic_store(&g_box_sell, 1); atomic_store(&g_box_id, id); LOG("堆叠：卖出 %d 瓶的一叠（id %u）先弹数量框", g_box_n, id); static uint8_t rej[64]; if (len <= sizeof rej) { memcpy(rej, pk, len); memset(rej + 5, 0xff, 4); regs[2] = (uint64_t)(uintptr_t)rej; } }   // 物品 id 换成不存在的：服务端走拒绝路径回复客户端，「等待交易确认」才会撤 | Replace the item id with a nonexistent one: the server replies through its reject path, which is what dismisses the "waiting for trade" prompt
        return; }
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
        else { set_qty(player, still, dmstk_belt_keep()); nid = put_back(G, player, buf, bl, dmstk_belt_back(n), mode, page, x, y); if (nid && ex) exempt_add(nid); if (!nid) LOG("【红】堆叠：%.3s 进腰带后放不回背包（格 %d,%d），丢了 %d 瓶", (const char *)&code, x, y, dmstk_belt_back(n)); }
    }
    else if (still && *(uint64_t *)(still + 0x10)) LOG("堆叠：%.3s×%d（id %u）原版没喝成，不动", (const char *)&code, n, id);
    else { nid = put_back(G, player, buf, bl, dmstk_rest_after_one(n), mode, page, x, y); if (nid && ex) exempt_add(nid); if (!nid) LOG("【红】堆叠：%.3s 放不回去（方式 %u，格 %d,%d），丢了 %d 瓶", (const char *)&code, mode, x, y, dmstk_rest_after_one(n)); }
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
            if (mode == 0 && *(uint8_t *)(d + 0x55) == 0 && !merged && dmstk_merge_room(q) > 0 && !exempt(*(uint32_t *)(it + 8))) { uint32_t c = code_of(it); int h = -1; for (int i = 0; i < nh && h < 0; i++) if (code_of(head[i]) == c && dmstk_merge_room(hq[i]) > 0) h = i;
                if (h < 0) { if (nh < 16) { head[nh] = it; hq[nh++] = q; } }
                else { uint64_t pp = *(uint64_t *)(it + 0x38); int take = dmstk_merge_take(hq[h], q);
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
            else { int k = dmstk_split_amount(n, m); if (k < 1) k = 1;   // 档位认不出照老做法拆 1 | an unknown mode splits 1 as before
                done = split_off(G, player, it, k, page) ? 1 : 0; }
            LOG("堆叠：拆分 %.3s×%d（档 %d，页 %d）→ 拆出 %d 次，原叠剩 %d", (const char *)&c, n, m, page, done, qty(it));
            if (!done) atomic_store(&g_notice_full, 1); else { int dm_request_save(void); dm_request_save(); } }
        else LOG("堆叠：拆分请求 id %u 已不是一叠，不做", id); } }
    { uint64_t r = atomic_exchange(&g_sell_req, 0); if (r) { uint32_t id = (uint32_t)(r >> 8); uint64_t it = dm_diag_srv_item(id), d = it ? *(uint64_t *)(it + 0x10) : 0;
        if (d && qty(it) >= 2) { int n = qty(it), m = (int)(r & 0xff), k = dmstk_sell_amount(n, m), page = *(uint8_t *)(d + 0x55) > 4 ? 0 : *(uint8_t *)(d + 0x55); if (k < 1) k = 1;   // 卖全部不拆、整叠那件直接卖；档位认不出照老做法拆 1 | sell all does not split (that whole item is sold); an unknown mode splits 1 as before
            if (k >= n) { LOG("堆叠：卖出整叠 %d 瓶（id %u）", n, id); atomic_store(&g_sell_go, id); }
            else { uint32_t nid = split_off(G, player, it, k, page); LOG("堆叠：卖出先拆：%d 瓶 → 拆出 id %u，原叠剩 %d", n, nid, qty(it)); if (nid) atomic_store(&g_sell_go, nid); else atomic_store(&g_notice_full, 1); } } } }
    if (atomic_exchange(&g_notice_full, 0)) { void dm_game_notice(uint8_t *, const char *); dm_game_notice(b, dm_str(b, "dmStackNoRoom")); }
    uint32_t tq = *(uint32_t *)(G + 0x170); if (!(tq % 5)) normalize(G, player);
    static int per = -1; if (per < 0) { const char *e = getenv("DM_TEST_STACK_LOG"); per = e ? atoi(e) : 0; }
    // 测试开关 DM_TEST_STACK_BELTQ=<拍>：腰带第 1 列底格那瓶改成 5 瓶（造「腰带里有一叠」：手动把一叠拖进腰带的现场）
    // Test switch DM_TEST_STACK_BELTQ=<tick>: set the bottom bottle of belt column 1 to 5 (sets up "a stack in the belt", as when a stack is dragged there by hand)
    { static int at = -1; if (at < 0) { const char *e = getenv("DM_TEST_STACK_BELTQ"); at = e ? atoi(e) : 0; } if (at && tq >= (uint32_t)at) { at = 0; for (uint64_t it = inv_first(player), k = 0; it && k < 2048; k++) { uint64_t d = *(uint64_t *)(it + 0x10), pp = *(uint64_t *)(it + 0x38); if (!d) break; if (*(uint32_t *)(it + 0xc) == 2 && pp && *(int32_t *)(pp + 0x10) == 0) { set_qty(player, it, 5); LOG("测试 DM_TEST_STACK_BELTQ：腰带槽 0 改成 5 瓶"); break; } it = *(uint64_t *)(d + 0xb0); } } }
    // 测试开关 DM_TEST_STACK_TOSTASH=<拍>：背包第一叠药水走原版包放进个人仓库 (3,3)（人要在箱子旁）
    // Test switch DM_TEST_STACK_TOSTASH=<tick>: put the first potion stack in the inventory into the personal stash at (3,3) with the original packets (the player must be next to the stash)
    { static int at = -1; if (at < 0) { const char *e = getenv("DM_TEST_STACK_TOSTASH"); at = e ? atoi(e) : 0; } if (at && tq >= (uint32_t)at) { at = 0; for (uint64_t it = inv_first(player), k = 0; it && k < 2048; k++) { uint64_t d = *(uint64_t *)(it + 0x10), pp = *(uint64_t *)(it + 0x38); if (!d) break; if (*(uint32_t *)(it + 0xc) == 0 && *(uint8_t *)(d + 0x55) == 0 && pp && is_potion(it) && qty(it) >= 2) { void (*disp)(uint64_t, uint64_t, uint8_t *, uint32_t) = (void (*)(uint64_t, uint64_t, uint8_t *, uint32_t))(b + 0x6e5b40); uint32_t id = *(uint32_t *)(it + 8), x = *(uint32_t *)(pp + 0x10), y = *(uint32_t *)(pp + 0x14), pg = 0, tp = 4, cx = 3, cy = 3; uint8_t a1[17] = { 0x19 }, a2[17] = { 0x18 }; memcpy(a1 + 1, &id, 4); memcpy(a1 + 5, &x, 4); memcpy(a1 + 9, &y, 4); memcpy(a1 + 13, &pg, 4); memcpy(a2 + 1, &id, 4); memcpy(a2 + 5, &cx, 4); memcpy(a2 + 9, &cy, 4); memcpy(a2 + 13, &tp, 4); *(uint32_t *)(G + 0x19050) = 1; disp(G, player, a1, 17); disp(G, player, a2, 17); *(uint32_t *)(G + 0x19050) = 0; d = *(uint64_t *)(it + 0x10); LOG("堆叠：id %u 数量 %d 进仓库，现在页 %d", id, qty(it), d ? *(uint8_t *)(d + 0x55) : -1); break; } it = *(uint64_t *)(d + 0xb0); } } }
    // 测试开关 DM_TEST_CHARM_Y=<拍>（护身符袋探针）：背包第一件护身符依次走原版包放到 (0,5)、方块、背包 (4,1) 对照，每步记玩家属性 0–60 的和，看页 0 第 4 行以下是否还生效
    // Test switch DM_TEST_CHARM_Y=<tick> (charm bag probe): move the first charm in the inventory to (0,5), the cube, then the inventory at (4,1) with the original packets, logging the sum of player stats 0-60 at each step, to see whether it still counts below row 4 of page 0
    { static int at = -1, st; if (at < 0) { const char *e = getenv("DM_TEST_CHARM_Y"); at = e ? atoi(e) : 0; }
      if (at && tq >= (uint32_t)at + (uint32_t)st * 40 && st < 4) { uint64_t f = 0; int (*is)(uint64_t, int) = (int (*)(uint64_t, int))(b + 0x574bc0); for (uint64_t it = inv_first(player), k = 0; it && k < 2048 && !f; k++) { uint64_t d = *(uint64_t *)(it + 0x10); if (!d) break; static uint32_t fid; if (!fid) { uint32_t mx = 0; for (uint64_t q = it, n = 0; q && n < 2048; n++) { uint64_t qd = *(uint64_t *)(q + 0x10); if (!qd) break; if (*(uint32_t *)(q + 0xc) == 0 && *(uint8_t *)(qd + 0x55) == 0 && is(q, 13) && *(uint32_t *)(q + 8) > mx) mx = *(uint32_t *)(q + 8); q = *(uint64_t *)(qd + 0xb0); } fid = mx; LOG("护身符探针：挑最新一件 id %u", fid); } if (*(uint32_t *)(it + 8) == fid) f = it; else it = *(uint64_t *)(d + 0xb0); }
          long sum = 0; char v[160]; int vk = 0; for (int i = 0; i <= 60; i++) { int x = ((int (*)(uint64_t, int, int))(b + 0x5b9290))(player, i, 0); sum += x; if ((i == 7 || i == 9 || i == 39 || i == 41 || i == 43 || i == 45 || i == 0 || i == 2) && vk < 140) vk += snprintf(v + vk, sizeof v - vk, " s%d=%d", i, x); }
          uint64_t pp = f ? *(uint64_t *)(f + 0x38) : 0; LOG("护身符探针：步 %d 物品 id %u 格 %d,%d 属性和 %ld%s", st, f ? *(uint32_t *)(f + 8) : 0, pp ? *(int32_t *)(pp + 0x10) : -1, pp ? *(int32_t *)(pp + 0x14) : -1, sum, v);
          if (f && pp && st < 3) { void (*disp)(uint64_t, uint64_t, uint8_t *, uint32_t) = (void (*)(uint64_t, uint64_t, uint8_t *, uint32_t))(b + 0x6e5b40); uint32_t id = *(uint32_t *)(f + 8), x = *(uint32_t *)(pp + 0x10), y = *(uint32_t *)(pp + 0x14), pg = (uint32_t)*(uint8_t *)(*(uint64_t *)(f + 0x10) + 0x55), tp = st == 1 ? 3 : 0, nx = st == 1 ? 0 : st == 2 ? 4 : 0, ny = st == 0 ? 5 : st == 1 ? 0 : 1; uint8_t a1[17] = { 0x19 }, a2[17] = { 0x18 }; memcpy(a1 + 1, &id, 4); memcpy(a1 + 5, &x, 4); memcpy(a1 + 9, &y, 4); memcpy(a1 + 13, &pg, 4); memcpy(a2 + 1, &id, 4); memcpy(a2 + 5, &nx, 4); memcpy(a2 + 9, &ny, 4); memcpy(a2 + 13, &tp, 4); *(uint32_t *)(G + 0x19050) = 1; disp(G, player, a1, 17); disp(G, player, a2, 17); *(uint32_t *)(G + 0x19050) = 0; }
          st++; } }
    { static int at = -1; if (at < 0) { const char *e = getenv("DM_TEST_STACK_THREE"); at = e ? atoi(e) : 0; } if (at && tq >= (uint32_t)at) { at = 0; for (uint64_t it = inv_first(player), k = 0; it && k < 2048; k++) { uint64_t d = *(uint64_t *)(it + 0x10); if (!d) break; if (*(uint32_t *)(it + 0xc) == 0 && *(uint8_t *)(d + 0x55) == 0 && is_rejuv(it) && qty(it) >= 2) { set_qty(player, it, 3); LOG("测试 DM_TEST_STACK_THREE：紫药改成 3 瓶"); break; } it = *(uint64_t *)(d + 0xb0); } } }
    // 测试开关 DM_TEST_STACK_CUBE=<拍>：背包第一叠药水走原版包放进方块 (0,0)，之后每 40 拍发一次合成包 0x20（共 3 次），看拆半 / 合并 / 配方
    // Test switch DM_TEST_STACK_CUBE=<tick>: put the first potion stack in the inventory into the cube at (0,0) with the original packets, then send the transmute packet 0x20 every 40 ticks (3 times), to observe split / merge / recipe
    { static int at = -1, st; if (at < 0) { const char *e = getenv("DM_TEST_STACK_CUBE"); at = e ? atoi(e) : 0; }
      if (at && tq >= (uint32_t)at + (uint32_t)st * 40 && st <= 3) { void (*disp)(uint64_t, uint64_t, uint8_t *, uint32_t) = (void (*)(uint64_t, uint64_t, uint8_t *, uint32_t))(b + 0x6e5b40); *(uint32_t *)(G + 0x19050) = 1;
          if (!st) { uint64_t f = 0; for (uint64_t it = inv_first(player), k = 0; it && k < 2048 && !f; k++) { uint64_t d = *(uint64_t *)(it + 0x10); if (!d) break; if (*(uint32_t *)(it + 0xc) == 0 && *(uint8_t *)(d + 0x55) == 0 && is_potion(it) && qty(it) >= 2) f = it; else it = *(uint64_t *)(d + 0xb0); }
              uint64_t pp = f ? *(uint64_t *)(f + 0x38) : 0; if (pp) { uint32_t id = *(uint32_t *)(f + 8), x = *(uint32_t *)(pp + 0x10), y = *(uint32_t *)(pp + 0x14), pg = 0, x0 = 0, y0 = 0, pc = 3; uint8_t a1[17] = { 0x19 }, a2[17] = { 0x18 }; memcpy(a1 + 1, &id, 4); memcpy(a1 + 5, &x, 4); memcpy(a1 + 9, &y, 4); memcpy(a1 + 13, &pg, 4); memcpy(a2 + 1, &id, 4); memcpy(a2 + 5, &x0, 4); memcpy(a2 + 9, &y0, 4); memcpy(a2 + 13, &pc, 4); disp(G, player, a1, 17); disp(G, player, a2, 17); LOG("测试 DM_TEST_STACK_CUBE：放进方块 id %u", id); } }
          else { uint8_t t0[8] = { 0x20 }, t[96]; for (uint64_t it = inv_first(player), k = 0; it && k < 2048; k++) { uint64_t d = *(uint64_t *)(it + 0x10), pp = *(uint64_t *)(it + 0x38); if (!d) break; uint32_t c = code_of(it); if (!memcmp(&c, "box", 3) && pp) { memcpy(t0 + 1, (void *)(it + 8), 4); t0[5] = *(uint8_t *)(d + 0x55); t0[6] = (uint8_t)((*(int32_t *)(pp + 0x10) & 15) | (*(int32_t *)(pp + 0x14) & 15) << 4); break; } it = *(uint64_t *)(d + 0xb0); } uint32_t tl = cube_packet(player, t0, 8, t, sizeof t); if (tl) disp(G, player, t, tl); LOG("测试 DM_TEST_STACK_CUBE：第 %d 次合成", st); }
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
static void box_choose(int m) { uint32_t id = atomic_load(&g_box_id); if (!id) return; int sl = atomic_load(&g_box_sell) != 0; LOG("堆叠：数量框选 %d（%s，id %u）", m, sl ? "卖出" : "拆分", id); if (!sl) atomic_store(&g_split_req, (uint64_t)id << 8 | (uint64_t)m); else if (m == 3) atomic_store(&g_sell_go, id); else atomic_store(&g_sell_req, (uint64_t)id << 8 | (uint64_t)m); box_close(); }
// 打开拆分框：背包或个人页里叠数 ≥ 2 的药水（ci 取自手柄光标 / 按下点）。返回 1 = 开了
// Open the split box: a potion stack of 2 or more in the inventory or a personal page (ci from the controller cursor / press point). Returns 1 = opened
int dm_stack_open(const dm_cursor_item_info *ci) {
    if (!B || !ci->potion || (ci->container != DM_ITEM_AT_INVENTORY && ci->container != DM_ITEM_AT_STASH) || ci->stack) return 0;
    uint64_t it = cli_item(ci->id); int n = it ? qty(it) : 0; if (n < 2) return 0;
    g_box_n = n; atomic_store(&g_box_sell, 0); atomic_store(&g_box_id, ci->id); LOG("堆叠：拆分框打开 %s×%d（id %u）", ci->code, n, ci->id); return 1;
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
// 按下点在框开着时：点中按钮就在这里选（命中用控件自己的判断）；点外面只收框（同百宝囊清单）。这一下都不再交给游戏
// A press while the box is open: a hit on a button chooses here (with the widget's own hit test); anywhere else only closes the box (same as the pouch list). Either way the press does not reach the game
int dm_stack_box_gate(int x, int y) {
    if (!dm_stack_box_open() || !B) return 0; uint64_t root = *(uint64_t *)(B + 0x2f05458); int sl = atomic_load(&g_box_sell) != 0;
    for (int i = 1; i <= 3; i++) { uint64_t w = find_w(root, dmstk_box_name(sl, i), 1); if (w && (((int (*)(uint64_t, uint64_t))*(uint64_t *)(*(uint64_t *)w + 0x48))(w, (uint64_t)(uint32_t)x | ((uint64_t)(uint32_t)y << 32)) & 1)) { box_choose(i); return 1; } }
    box_close(); return 1;
}
void dm_stack_on_uimsg(const char *cat, const char *act, const char *arg) {
    if (!cat || !act || !arg || strcmp(cat, "PanelManager") || strcmp(act, "ClosePanel")) return;
    int m = dmstk_box_choice(arg); if (m) box_choose(m);   // 六个按钮名 → 档位；别的面板消息不是我们的 | the six button names -> mode; other panel messages are not ours
}
// 个人仓库页的叠数角标：物品在玩家物品栏页 4（别的页签不是这批物品，角标收起）
// Count badges on the personal stash page: the items are on page 4 of the player's container (the other tabs show other items, so the badges are put away)
static void stash_badges(uint8_t *b, uint64_t pl) {
    static uint64_t last; uint64_t root = *(uint64_t *)(b + 0x2f05458), bk = root ? find_w(root, "BankExpansionLayout", 12) : 0, pool = bk ? find_w(bk, "TouchQtyStash", 2) : 0; if (!pool) return;
    uint64_t tabs = find_w(bk, "BankTabs", 3), g = find_w(bk, "grid", 3); int show = *(uint8_t *)(bk + 0x50) && tabs && g && *(int32_t *)(tabs + 0xdc4) == 0 && *(uint8_t *)(g + 0x50);
    if (!show && last == pool) return;   // 收过了：不再每帧扫 | Already put away: no need to scan every frame
    last = show ? 0 : pool; char nm[20]; int nd = 0; uint64_t inv = pl ? *(uint64_t *)(pl + 0x90) : 0; char sum[80]; int sk = 0; sum[0] = 0;
    if (show) for (uint64_t it = inv && *(uint32_t *)inv == 0x01020304 ? *(uint64_t *)(inv + 0x10) : 0, k = 0; it && k < 2048 && nd < NDIG; k++) { uint64_t d = *(uint64_t *)(it + 0x10), pp = *(uint64_t *)(it + 0x38); if (!d) break;
        if (*(uint32_t *)(it + 0xc) == 0 && dmstk_badges_on(DMSTK_PANEL_STASH, *(uint8_t *)(d + 0x55)) && pp && is_potion(it)) { int q = qty(it);
            if (q >= 2) { if (sk < (int)sizeof sum - 8) sk += snprintf(sum + sk, sizeof sum - sk, " %d", q); snprintf(nm, sizeof nm, "TouchQtyS%02d", nd++); uint64_t w = find_w(pool, nm, 1); char t[8]; snprintf(t, sizeof t, "%d", q); w_text(w, t); w_move(w, *(int32_t *)(g + 0x70) + CELL * *(int32_t *)(pp + 0x10), *(int32_t *)(g + 0x74) + CELL * *(int32_t *)(pp + 0x14) + 60); } }
        it = *(uint64_t *)(d + 0xb0); }
    for (; nd < NDIG; nd++) { snprintf(nm, sizeof nm, "TouchQtyS%02d", nd); w_move(find_w(pool, nm, 1), OFF, 0); }
    // 角标一行汇总（只在显示内容变了时记一条，不刷屏）：回归脚本按这一行断言摆了几格、每格几个数
    // One summary line per change of what is shown (no spam): the regression script asserts on this line how many cells were placed and with what counts
    { static char lastsum[80]; if (strcmp(sum, lastsum)) { strlcpy(lastsum, sum, sizeof lastsum); LOG("堆叠：仓库角标 %d 格%s", nd, sum); } }
}
int dm_bag_page(void);
void dm_stack_ui(uint8_t *b, uint64_t inv_panel, uint64_t pl) {   // 游戏主线程每帧（dm_pouch.c ui_pump；物品栏面板开着才来） | Game main thread every frame (ui_pump in dm_pouch.c; only while the inventory panel is open)
    B = b; char nm[20]; int nd = 0; uint64_t inv = pl ? *(uint64_t *)(pl + 0x90) : 0;
    if (pl) atomic_store(&g_cli_pid, *(uint32_t *)(pl + 8));
    for (uint64_t it = inv && *(uint32_t *)inv == 0x01020304 ? *(uint64_t *)(inv + 0x10) : 0, k = 0; it && k < 2048 && nd < NDIG; k++) { uint64_t d = *(uint64_t *)(it + 0x10), pp = *(uint64_t *)(it + 0x38); if (!d) break;
        if (!dm_bag_page() && *(uint32_t *)(it + 0xc) == 0 && dmstk_badges_on(DMSTK_PANEL_INVENTORY, *(uint8_t *)(d + 0x55)) && pp && is_potion(it)) { int q = qty(it);
            if (q >= 2) { snprintf(nm, sizeof nm, "TouchQtyN%02d", nd++); uint64_t w = find_w(inv_panel, nm, 3); char t[8]; snprintf(t, sizeof t, "%d", q); w_text(w, t); w_move(w, DM_BAG_GRID_X + CELL * *(int32_t *)(pp + 0x10), DM_BAG_GRID_Y + CELL * *(int32_t *)(pp + 0x14) + 60); } }
        it = *(uint64_t *)(d + 0xb0); }
    for (; nd < NDIG; nd++) { snprintf(nm, sizeof nm, "TouchQtyN%02d", nd); w_move(find_w(inv_panel, nm, 3), OFF, 0); }
    stash_badges(b, pl);
    { uint32_t id = atomic_load(&g_sell_go); static int tries; if (id) { uint64_t root = *(uint64_t *)(b + 0x2f05458), vp = root ? find_w(root, "VendorPanelLayout", 12) : 0, ci = cli_item(id); int r = 0, gold0 = pl ? ((int (*)(uint64_t, int, int))(b + 0x5b9290))(pl, 14, 0) : 0;
        if (vp && *(uint8_t *)(vp + 0x50) && ci) { atomic_store(&g_sell_pass, id); r = ((int (*)(uint64_t, uint64_t, int, int, int, int))(b + 0x433e50))(pl, ci, 1, 0, 1, 0); if (!r) atomic_store(&g_sell_pass, 0); }   // 距上次交易不足 500 ms 时它会拒，下一帧再试 | it refuses within 500 ms of the last trade, so try again next frame
        if (r || ++tries > 180 || !(vp && *(uint8_t *)(vp + 0x50))) { LOG("堆叠：卖出 id %u → %d（试了 %d 帧），金币 %d → %d", id, r, tries, gold0, pl ? ((int (*)(uint64_t, int, int))(b + 0x5b9290))(pl, 14, 0) : 0); tries = 0; atomic_store(&g_sell_go, 0); } } }   // 金币前后一起记：卖成没成、按几瓶计价都看这一行 | gold before and after: this line tells whether the sale went through and how many bottles it was priced for
    int open = dm_stack_box_open(), bx = open ? 420 : OFF;
    w_move(find_w(inv_panel, "TouchQtyBg", 3), bx, 900);
    w_move(find_w(inv_panel, "TouchQtyTitle", 3), bx + 20, 920);
    { int sl = open && atomic_load(&g_box_sell), ks = sl ? bx : OFF, kb = open && !sl ? bx : OFF; static const char *const n0[] = { "TouchQtyOne", "TouchQtyHalf", "TouchQtyAll" }, *const n1[] = { "TouchQtySellOne", "TouchQtySellHalf", "TouchQtySellAll" };
      for (int i = 0; i < 3; i++) { int y = 990 + 80 * i; w_move(find_w(inv_panel, n0[i], 3), kb == OFF ? OFF : kb + 139, y); w_move(find_w(inv_panel, n1[i], 3), ks == OFF ? OFF : ks + 139, y); } }
    if (open) { char t[96]; snprintf(t, sizeof t, dm_str(b, atomic_load(&g_box_sell) ? "dmStackSellTitle" : "dmStackTitle"), g_box_n); w_text(find_w(inv_panel, "TouchQtyTitle", 3), t); }
}
void dm_stack_panel_closed(void) { box_close(); }
// 测试开关 DM_TEST_STACK_SPLIT=<帧>:<档>：到帧对背包第一叠药水走一次拆分（不经界面）；DM_TEST_STACK_BOX=<帧>：到帧打开拆分框（截图用）
// Test switches DM_TEST_STACK_SPLIT=<frame>:<mode>: at that frame split the first potion stack in the inventory (no UI); DM_TEST_STACK_BOX=<frame>: open the split box at that frame (for screenshots)
void dm_stack_test(uint8_t *b, uint64_t pl, uint64_t fr) {
    // 测试开关 DM_TEST_STACK_SELL=<帧>：把背包第一叠药水卖给当前商人（走原版交易函数），记卖价和金币
    // Test switch DM_TEST_STACK_SELL=<frame>: sell the first potion stack in the inventory to the current vendor (through the original trade function), logging the price and gold
    { static int at = -1, st; static int g0; if (at < 0) { const char *e = getenv("DM_TEST_STACK_SELL"); at = e ? atoi(e) : 0; } int (*gs)(uint64_t, int, int) = (int (*)(uint64_t, int, int))(b + 0x5b9290);
      if (at && st == 1 && fr >= (uint64_t)at + 60) { st = 2; LOG("堆叠：卖出后金币 %d（卖前 %d）", gs(pl, 14, 0), g0); }
      { uint64_t root = *(uint64_t *)(b + 0x2f05458), vp = root ? find_w(root, "VendorPanelLayout", 12) : 0; if (at && !st && fr >= (uint64_t)at && !(vp && *(uint8_t *)(vp + 0x50))) { static int lg; if (!lg++) LOG("堆叠：商人页没开，先不卖"); return; } }   // 没开商人页调交易函数会崩 | Calling the trade function without the vendor page open crashes
      if (at && !st && fr >= (uint64_t)at) { st = 1; g0 = gs(pl, 14, 0); for (uint64_t it = inv_first(pl), k = 0; it && k < 2048; k++) { uint64_t d = *(uint64_t *)(it + 0x10); if (!d) break;
            if (*(uint32_t *)(it + 0xc) == 0 && *(uint8_t *)(d + 0x55) == 0 && is_potion(it) && qty(it) >= 2) { uint64_t npc = ((uint64_t (*)(uint64_t, uint32_t, uint32_t, uint32_t))(b + 0x45f5f0))((uint64_t)(uintptr_t)(b + 0x2d607a0), *(uint32_t *)(b + 0x2d5ebfc) & 0x7f, *(uint32_t *)(b + 0x2d5ebfc), 1); int pr = npc ? ((int (*)(uint64_t, uint64_t, int, uint64_t, int, int))(b + 0x572ec0))(pl, it, *(uint8_t *)(b + 0x2d20b88), *(uint64_t *)(b + 0x2d5ec18), *(int16_t *)(npc + 4), 1) : -1; int r = ((int (*)(uint64_t, uint64_t, int, int, int, int))(b + 0x433e50))(pl, it, 1, 0, 1, 0); LOG("堆叠：卖 id %u 数量 %d：卖价 %d，商人 %#llx，返回 %d，金币 %d", *(uint32_t *)(it + 8), qty(it), pr, (unsigned long long)npc, r, g0); break; } it = *(uint64_t *)(d + 0xb0); } } }
    static int at = -1, m, bx = -1; if (at < 0) { const char *e = getenv("DM_TEST_STACK_SPLIT"); if (!e || sscanf(e, "%d:%d", &at, &m) != 2) at = 0; e = getenv("DM_TEST_STACK_BOX"); bx = e ? atoi(e) : 0; }
    { static int sip = -1; if (sip < 0) { const char *e = getenv("DM_TEST_STACK_SIP"); sip = e ? atoi(e) : 0; } if (sip && fr >= (uint64_t)sip) { sip = 0; void dm_belt_use(int, int); dm_belt_use(0, 0); LOG("测试 DM_TEST_STACK_SIP：喝第 1 列一瓶"); } }
    { static int m = -1; if (m < 0) { const char *e = getenv("DM_TEST_STACK_SIPM"); m = e ? atoi(e) : 0; } if (m && fr >= (uint64_t)m) { m = 0; void dm_belt_use_merc(int, int); dm_belt_use_merc(0, 0); LOG("测试 DM_TEST_STACK_SIPM：第 1 列喂佣兵一瓶"); } }
    if ((!at || fr < (uint64_t)at) && (!bx || fr < (uint64_t)bx)) return; B = b; uint64_t inv = pl ? *(uint64_t *)(pl + 0x90) : 0, f = 0;
    for (uint64_t it = inv ? *(uint64_t *)(inv + 0x10) : 0, k = 0; it && k < 2048 && !f; k++) { uint64_t d = *(uint64_t *)(it + 0x10); if (!d) break; if (*(uint32_t *)(it + 0xc) == 0 && *(uint8_t *)(d + 0x55) == 0 && is_potion(it) && qty(it) >= 2) f = it; else it = *(uint64_t *)(d + 0xb0); }
    if (!f) return;
    if (at && fr >= (uint64_t)at) { at = 0; atomic_store(&g_split_req, (uint64_t)*(uint32_t *)(f + 8) << 8 | (uint64_t)m); LOG("测试 DM_TEST_STACK_SPLIT：拆 id %u 档 %d", *(uint32_t *)(f + 8), m); }
    if (bx && fr >= (uint64_t)bx) { bx = 0; g_box_n = qty(f); atomic_store(&g_box_id, *(uint32_t *)(f + 8)); LOG("测试 DM_TEST_STACK_BOX：打开拆分框"); }
}
