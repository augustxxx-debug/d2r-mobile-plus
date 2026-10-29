// 免费回城（回城书也作废；直接复用原版回城技能）。
// 数据版本：3.x 存档（单位 +0x1bd = 3）有回城技能 411；2.x 时代的存档（= 2，如标准存档里的 88 级法师）那套原版数据里没有（411 行是空的），退回直接建门 + 连门、没有施法动作。
// 点回城钮 = 玩家施放原版回城技能 411「Townportal O Skill」：施法动作 SC、施法音效、出门都是原版自己的流程，只是不要回城书 / 卷轴、不扣东西。
//   · 411 的服务端执行函数（技能表 srvdofunc 109，main+0x786560）= 建门 main+0x64c8e0（城里返回 0、建物件 59、旧门先收，一人一扇）+ 连门 main+0x64ce50（接对应营地）。
//     卷轴 / 书是另两个物品技能（219 / 220），扣数在使用物品那层；411 不是卷轴类，原版就不扣东西。
//   · 玩家身上本来没有 411：第一次点时用原版不扣技能点的「加技能」main+0x7bf010(玩家, 技能号) 加进技能链（玩家 +0x100，节点 +0 = 技能表行、+8 = 下一个；
//     加点学技能 0x7bed00 在它外面先扣技能点，这里不走那层）。411 不是本职技能，存档只存本职技能，所以不进存档，每局第一次点时再加。
//   · 施放 = 右键对地施放包 0x0c 的服务端处理（0x6d4470）里那一步 main+0x6e77f0(游戏, 玩家, x, y, 1 = 右手, 技能节点)；它要求传入的就是右键技能节点，
//     所以调用期间把技能链 +0x10（右键技能）临时指向 411、调完放回，玩家自己选的右键技能不变。目标点 = 脚下。
//   · 城里（城镇站立模式 5）、死亡、正在施法时原版拒绝施放（施法中再点返回 1、不出第二扇门）。
// Free town portal (the Tome of Town Portal is also made obsolete; the original game's town portal skill is reused directly).
// Data version: 3.x saves (unit +0x1bd = 3) have town portal skill 411; 2.x-era saves (= 2, e.g. the level 88 Sorceress in the standard saves) do not have it in that set of original-game data (row 411 is empty), so fall back to creating the portal + linking the portal directly, with no cast animation.
// Tapping the town portal button = the player casts the original game's town portal skill 411 'Townportal O Skill': the cast animation SC, the cast sound and the portal appearing are all the original game's own flow, only no Tome of Town Portal / scroll is required and nothing is consumed.
//   - The server execute function of 411 (skill table srvdofunc 109, main+0x786560) = create the portal main+0x64c8e0 (returns 0 in town, creates object 59, removes the old portal first, one per player) + link the portal main+0x64ce50 (connects to the corresponding camp).
//     The scroll / tome are two other item skills (219 / 220), and the count is deducted at the use-item layer; 411 is not a scroll-type skill, and the original game simply consumes nothing.
//   - The player does not have 411 to begin with: on the first tap it is added to the skill chain with the original game's 'add skill' that spends no skill point, main+0x7bf010(player, skill number) (player +0x100, node +0 = skill table row, +8 = next;
//     learning a skill by spending a point, 0x7bed00, deducts the skill point first outside of it, and that layer is not used here). 411 is not a class skill, and the save stores only class skills, so it does not go into the save and is added again on the first tap of each run.
//   - Casting = the step main+0x6e77f0(game, player, x, y, 1 = right hand, skill node) inside the server handler (0x6d4470) of the right-click cast-on-ground packet 0x0c; it requires the node passed in to be the right-click skill node,
//     so during the call skill chain +0x10 (right-click skill) is temporarily pointed at 411 and put back after the call, leaving the right-click skill the player chose unchanged. Target point = at the feet.
//   - In town (town standing mode 5), when dead, or while casting, the original game refuses the cast (tapping again while casting returns 1 and no second portal appears).
// 开关：自定义页「免费回城」（键 free_tp，缺省关，用户 2026-10-30 定：按 RT 不扣东西随手开门不该是缺省）。测试可用 DM_TEST_FREETP_ON=0|1 盖过设置（不写设置文件）。
//   开着：上面说的全部照旧（挂 411、ZR 空格 = 免费回城、回城钮 = 免费回城）。
//   关着：不挂 411（技能树里没有它）；ZR 空格整颗交还原版（dm_freetp_zr_free 恒 0）；回城钮 = 原版「使用」身上的回城书（有次数的优先）→ 背包里的回城卷轴 → 腰带里的回城卷轴，
//   走 dm_item_use 那条原版使用路径（扣数、城里不让用等全由原版判）；一样都没有就弹一句系统提示、不开门。
//   开着时挂上的 411 只在本局技能链里（不进存档）：局中关掉，这一局里它还在（技能树 / 绑定键照常能放），下一局进局起就没有了；RT 与回城钮立即按关处理。
// Switch: the Custom page's "Free Town Portal" (key free_tp, off by default; user 2026-10-30: free portals on RT that consume nothing should not be the default). Tests can override the setting with DM_TEST_FREETP_ON=0|1 (the settings file is not written).
//   On: everything above as before (411 attached, empty ZR = free town portal, the portal button = free town portal).
//   Off: 411 is not attached (it does not appear in the skill tree); an empty ZR goes back to the original game entirely (dm_freetp_zr_free is always 0); the portal button = the original game "uses" the Tome of Town Portal (one with charges first) -> a Scroll of Town Portal in the inventory -> one in the belt,
//   through the original use path of dm_item_use (charges deducted, refused in town, etc., all judged by the original game); with none of them, one system notice and no portal.
//   A 411 attached while on lives only in this run's skill chain (it is not saved): turned off mid-run it stays for the rest of this run (the skill tree / a bound key still cast it) and is gone from the next run on; RT and the portal button follow "off" immediately.
#include "hle.h"
#include "dm_custom.h"
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { SK_TP_V3 = 411, SRVDO_TP = 109, ZR_EMPTY = 0xffffffffu };   // 3.x 数据里的原版回城技能 Townportal O Skill；技能表行 +0x4e = 服务端执行函数号（宿主读表核对：219 / 220 = 113，411 = 109） | The original game's town portal skill Townportal O Skill in the 3.x data; skill table row +0x4e = server execute function number (verified by the host reading the table: 219 / 220 = 113, 411 = 109)
static _Atomic int g_req;
void dm_tp_request(void) { atomic_store(&g_req, 1); }   // 前端（任意线程）：点了回城钮 | Front end (any thread): the town portal button was tapped
int dm_freetp_enabled(void) {   // 任意线程 | Any thread
    static int env = -2; if (env == -2) { const char *e = getenv("DM_TEST_FREETP_ON"); env = e && *e ? atoi(e) != 0 : -1; }
    return env >= 0 ? env : dm_custom_get("free_tp");
}

// 玩家这一版数据（单位 +0x1bd：2 = 2.x 时代存档、3 = 3.x）的技能表里找「服务端执行 = 109 且本版可用（+0x25 第 2 位）」的那一行；没有返回 -1
// In the skill table of the player's data version (unit +0x1bd: 2 = 2.x-era save, 3 = 3.x), find the row with 'server execute = 109 and available in this version (bit 2 of +0x25)'; returns -1 if there is none
static int tp_skill_id(uint8_t *mb, uint64_t player) {
    uint8_t ver = *(uint8_t *)(player + 0x1bd); if (ver < 1 || ver > 3) return -1;
    uint64_t T = *(uint64_t *)(mb + 0x2d64a18 + (uint64_t)ver * 16); if (!T) return -1;
    uint64_t arr = *(uint64_t *)(T + 0x11e8), cnt = *(uint64_t *)(T + 0x11f0);
    if (arr && cnt > SK_TP_V3 && *(uint16_t *)(arr + SK_TP_V3 * 0x2ec + 0x4e) == SRVDO_TP && (*(uint8_t *)(arr + SK_TP_V3 * 0x2ec + 0x25) & 4)) return SK_TP_V3;
    for (uint64_t i = 0; arr && i < cnt; i++) if (*(uint16_t *)(arr + i * 0x2ec + 0x4e) == SRVDO_TP && (*(uint8_t *)(arr + i * 0x2ec + 0x25) & 4)) return (int)i;
    return -1;
}

// 手柄玩家照原版手柄方案：免费回城不再占住 ZR 整颗键，改成一个原版技能——进局后把 411 挂上玩家的技能链
// （服务端 + 客户端各挂一次），它就出现在原版自己的选技能界面「综合技能 → 普通技能」里，玩家用原版的绑定操作绑到任意一格，按那一格 = 原版自己施放，
// 不查卷轴、不扣东西；绑定照原版存进存档，下一局挂上后照常可用（Mac 上 iOS 包实跑：出现在列表 / 绑到 B / 野外按 B 开门 / 存档重进仍在）。
// ZR 这一格空着时（没绑任何技能）仍照旧当免费回城用（dm_gamepad.m）：老玩家「ZR = 回城」的习惯不变，也不占掉什么；绑了技能就整颗还给原版。
// 2.x 时代的存档那套数据里没有 411：列表里不会出现，只有「ZR 空着 = 免费回城」这一个手柄入口。
// Controller players follow the original controller scheme: the free town portal no longer occupies the whole ZR button and becomes an
// original skill instead -- after joining a run, 411 is attached to the player's skill chain
// (once on the server side and once on the client side), so it shows up in the original game's own skill selection screen under
// "General Skills -> Normal Skills"; the player binds it to any slot with the original binding operation, and pressing that slot = the original
// game casts it itself,
// without checking for scrolls or deducting anything; the binding is stored in the save as in the original game and works as usual once the
// skill is attached in the next run (real run of the iOS app on Mac: appears in the list / bound to B / pressing B in the wilderness opens a
// portal / still there after saving and re-entering).
// While the ZR slot is empty (no skill bound) it still serves as the free town portal as before (dm_gamepad.m): the existing players' habit of
// "ZR = town portal" stays unchanged and nothing is taken up; once a skill is bound the whole button goes back to the original game.
// The data set of 2.x era saves has no 411: it will not appear in the list, and "ZR empty = free town portal" is the only controller entry.
static void grant(uint8_t *mb, uint64_t unit, int srv) {   // 这个单位的技能链上没有 411 就挂上（原版不扣技能点的加技能 main+0x7bf010） | If this unit's skill chain has no 411, attach it (the original add-skill routine that does not deduct skill points, main+0x7bf010)
    int sk = tp_skill_id(mb, unit); if (sk < 0) return;
    uint64_t sl = *(uint64_t *)(unit + 0x100); for (uint64_t n = sl ? *(uint64_t *)sl : 0, k = 0; n && k < 512; n = *(uint64_t *)(n + 8), k++) { uint64_t rec = *(uint64_t *)n; if (rec && *(int16_t *)rec == sk) return; }
    uint64_t nd = ((uint64_t (*)(uint64_t, int))(mb + 0x7bf010))(unit, sk); static int lg; if (lg++ < 8) LOG("免费回城：%s玩家技能链挂上回城技能 %d → %s", srv ? "服务端" : "客户端", sk, nd ? "成" : "【红】失败");
}
// ZR 这一格现在是不是空的：面 → 格表 main+0x2d13cbc（6 × {逻辑键, 普通格, 替代格}，ZR 的逻辑键 0x800）、格 → 技能表 main+0x2d5f0c0（每格 28 字节，首 4 字节技能号；武器第二组在 +16 格），
// 读法同前端的技能格快照（dm/ios/Sources/dm_touch_game_bridge.c）。[0] = 没按 ZL 时 ZR 用的那格，[1] = 按着 ZL 时的那格
// Whether the ZR slot is empty right now: face button -> slot table main+0x2d13cbc (6 x {logical key, normal slot, alternate slot}, the logical key
// of ZR is 0x800), slot -> skill table main+0x2d5f0c0 (28 bytes per slot, the first 4 bytes are the skill number; the second weapon set is at
// +16 slots),
// read the same way as the front end's skill slot snapshot (dm/ios/Sources/dm_touch_game_bridge.c). [0] = the slot ZR uses when ZL is not held,
// [1] = the slot used while ZL is held
static _Atomic int g_zr_free[2];
int dm_freetp_zr_free(int zl) { return atomic_load(&g_zr_free[zl != 0]); }
// 关着时的回城钮：照原版用身上的回城书 / 卷轴。物品链读法同 diag.c hud_tick（玩家 +0x90 物品栏 → +0x10 首件；物品 +4 类号、+8 id、+0xc 模式（0 物品栏里、2 腰带）、
// +0x10 数据块（+0x55 页：0 = 背包）、下一件 [数据块 +0xb0]；书的次数 = 属性 70（属性表 [+0x88] +0x30 起 {statId<<16 | 层, 值}，+0x38 个数））。
// 用：交给 dm_item_use，本帧稍后同一个主线程拍里由 dm_actions.c 走原版使用（背包 0x3c0098、腰带 0x2cecf0 分支），原版判不能用（城里等）就不发
// The portal button while off: use the Tome / Scroll of Town Portal on the player as the original game does. The item chain is read as in diag.c hud_tick (player +0x90 item store -> +0x10 first item; item +4 class, +8 id, +0xc mode (0 in a store, 2 belt),
// +0x10 data block (+0x55 page: 0 = inventory), next item [data block +0xb0]; tome charges = stat 70 (stat list [+0x88]: {statId<<16 | layer, value} from +0x30, count at +0x38)).
// Use: hand it to dm_item_use; later in this same main-thread tick dm_actions.c goes through the original use (inventory 0x3c0098, belt 0x2cecf0 branch), and nothing is sent if the original game says it cannot be used (in town, etc.)
enum { CLS_TBK = 533, CLS_TSC = 544 };   // 回城书 / 回城卷轴（同 diag.c） | Tome / Scroll of Town Portal (as diag.c)
static uint32_t item_stat(uint64_t it, uint32_t want) {
    uint64_t sl = *(uint64_t *)(it + 0x88); if (!sl) return 0; uint64_t sb = *(uint64_t *)(sl + 0x30); uint32_t sc = *(uint32_t *)(sl + 0x38); if (!sb) return 0; if (sc > 512) sc = 512;
    for (uint32_t i = 0; i < sc; i++) if (*(uint32_t *)(sb + 8 * i) == want << 16) return *(uint32_t *)(sb + 8 * i + 4);
    return 0;
}
static void use_scroll(uint8_t *mb, uint64_t pl) {
    uint64_t inv = *(uint64_t *)(pl + 0x90), it = inv ? *(uint64_t *)(inv + 0x10) : 0; uint32_t tome = 0, tome0 = 0, scroll = 0, belt = 0, q = 0, nsc = 0;
    for (int k = 0; it && k < 1024; k++) { uint64_t d = *(uint64_t *)(it + 0x10); if (!d) break;
        uint32_t cls = *(uint32_t *)(it + 4), id = *(uint32_t *)(it + 8), md = *(uint32_t *)(it + 0xc); int pack = md == 0 && *(uint8_t *)(d + 0x55) == 0;
        if (cls == CLS_TBK && pack) { uint32_t n = item_stat(it, 70); if (n && !tome) { tome = id; q = n; } else if (!n) tome0 = id; }
        if (cls == CLS_TSC && (pack || md == 2)) nsc++;
        if (cls == CLS_TSC && pack && !scroll) scroll = id;
        if (cls == CLS_TSC && md == 2 && !belt) belt = id;
        it = *(uint64_t *)(d + 0xb0); }
    uint32_t use = tome ? tome : scroll ? scroll : belt;
    if (use) { void dm_item_use(uint32_t); dm_item_use(use); LOG("回城（免费回城关）：用%s（id %u；书里次数 %u，卷轴 %u 张）→ 交原版使用", tome ? "回城书" : scroll ? "背包里的回城卷轴" : "腰带里的回城卷轴", use, q, nsc); return; }
    static uint64_t last; struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); uint64_t now = (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;   // 半秒内不重复（同 dm_rift.c 的系统提示） | not repeated within half a second (as the dm_rift.c notice)
    if (now - last < 500) return; last = now;
    void dm_game_notice_ui(uint8_t *, const char *); const char *dm_str(uint8_t *, const char *); dm_game_notice_ui(mb, dm_str(mb, "dmFreeTpNoScroll"));
    LOG("回城（免费回城关）：身上没有可用的回城书 / 回城卷轴%s → 提示「%s」，不开门", tome0 ? "（回城书次数是 0）" : "", dm_str(mb, "dmFreeTpNoScroll"));
}
// 关着时的老绑定：开着时玩家可能把回城技能 411 绑在了某个键上（绑定照原版进存档）。关着进局后玩家身上没有 411，原版把那一格画成红色的「技能不可用」图标、按了什么也不做，
// 图标一直留在技能栏上（Mac 宿主实测 dm-work/run/freetp/runs/off_oldbind_pad）。所以关着且玩家技能链里没有它时，把绑着它的格（手柄技能绑定表 main+0x2d5f0c0，32 格 × 28 字节，
// 首 4 字节技能号，空格 = 0xffffffff，同 DM_TEST_BINDTBL）清成空格：那个键回到「没绑技能」，ZR 空格交还原版、触屏技能钮显示空槽。局中刚关掉（这一局里 411 还在链上）不清，照常能放到退出。
// Stale bindings while off: while on, the player may have bound town portal skill 411 to a key (the binding is saved as in the original game). Entering a run while off, the player has no 411, so the original
// game draws that slot as a red "skill unavailable" icon that does nothing when pressed and stays on the skill bar (measured on the Mac host, dm-work/run/freetp/runs/off_oldbind_pad). So while off and the player's
// skill chain does not have it, the slots bound to it (controller skill binding table main+0x2d5f0c0, 32 slots x 28 bytes, first 4 bytes the skill id, empty = 0xffffffff, as DM_TEST_BINDTBL) are cleared to empty:
// that key goes back to "no skill bound", an empty ZR goes to the original game, the touch skill button shows an empty slot. Just turned off mid-run (411 still on the chain this run): not cleared, it works until leaving.
static int has_skill(uint64_t unit, int sk) {
    uint64_t sl = *(uint64_t *)(unit + 0x100); for (uint64_t n = sl ? *(uint64_t *)sl : 0, k = 0; n && k < 512; n = *(uint64_t *)(n + 8), k++) { uint64_t rec = *(uint64_t *)n; if (rec && *(int16_t *)rec == sk) return 1; }
    return 0;
}
static void unbind_stale(uint8_t *mb, uint64_t pl) {
    if (has_skill(pl, SK_TP_V3)) return;
    int n = 0; for (int i = 0; i < 32; i++) { uint8_t *e = mb + 0x2d5f0c0 + (uint64_t)i * 28; uint32_t id; memcpy(&id, e, 4); if (id == SK_TP_V3) { id = ZR_EMPTY; memcpy(e, &id, 4); n++; } }
    if (n) LOG("免费回城（关）：%d 个技能格还绑着回城技能 %d（开着时绑的），玩家身上没有它 → 清成空格", n, SK_TP_V3);
}
void dm_freetp_client_step(uint8_t *mb, uint64_t pl) {   // 游戏主线程每帧（dm_actions.c），pl = 客户端玩家；不在局里传 0 | Every frame on the game main thread (dm_actions.c), pl = the client player; pass 0 when not in a run
    if (!mb || !pl) { atomic_store(&g_zr_free[0], 0); atomic_store(&g_zr_free[1], 0); if (!dm_freetp_enabled()) atomic_store(&g_req, 0); return; }   // 关着时不在局里点的回城钮不留到进局后才生效 | while off, a portal tap outside a run is not carried into the next run
    int on = dm_freetp_enabled(); static int was = -1; if (on != was) { was = on; LOG("免费回城：%s", on ? "开（自定义页 free_tp）" : "关（缺省）：ZR 交还原版、不挂回城技能、回城钮用回城书 / 卷轴"); }
    if (!on) { atomic_store(&g_zr_free[0], 0); atomic_store(&g_zr_free[1], 0); unbind_stale(mb, pl); if (atomic_exchange(&g_req, 0)) use_scroll(mb, pl); return; }
    grant(mb, pl, 0);
    int dm_swapskill_wset(void); int bank = dm_swapskill_wset() ? 16 : 0, fr[2] = { 0, 0 }; uint32_t ids[2] = { 0, 0 };
    for (int i = 0; i < 6; i++) { const uint8_t *e = mb + 0x2d13cbc + 12 * i; uint32_t lg, sl[2]; memcpy(&lg, e, 4); memcpy(sl, e + 4, 8); if (lg != 0x800) continue;
        for (int k = 0; k < 2; k++) if (sl[k] < 12) { memcpy(&ids[k], mb + 0x2d5f0c0 + (uint64_t)(bank + sl[k]) * 28, 4); fr[k] = ids[k] == ZR_EMPTY; } }
    static uint32_t last[2] = { 1, 1 }; if (ids[0] != last[0] || ids[1] != last[1]) { last[0] = ids[0]; last[1] = ids[1]; LOG("免费回城：ZR 这一格绑的技能 %u（按着 ZL：%u）→ %s", ids[0], ids[1], fr[0] ? "空着，ZR 仍当免费回城" : "已绑技能，ZR 交还原版"); }
    atomic_store(&g_zr_free[0], fr[0]); atomic_store(&g_zr_free[1], fr[1]);
}
void dm_freetp_server_step(uint8_t *mb, uint64_t G, uint64_t player) {   // 服务端拍里调（diag.c） | Called in the server tick (diag.c)
    int on = dm_freetp_enabled();
    if (on && mb && player) grant(mb, player, 1);
    // 测试开关 DM_TEST_FREETP=<拍>[,<拍>…]：到拍当作点了一次回城钮（裸宿主没有前端；复现公开仓库 「回城后点自己的蓝门第一次仍落城内」用）
    // Test switch DM_TEST_FREETP=<tick>[,<tick>…]: at each tick, act as if the Town Portal button was tapped (the bare host has no front end; used to reproduce the public repo report "after returning to town, the first click on your own blue portal still lands in town")
    { static char q[128]; static int init; if (!init) { init = 1; const char *e = getenv("DM_TEST_FREETP"); if (e) strlcpy(q, e, sizeof q); }
      if (q[0] && G) { uint32_t t = *(uint32_t *)(uintptr_t)(G + 0x170), at = (uint32_t)atoi(q); if (t >= at) { const char *c = strchr(q, ','); memmove(q, c ? c + 1 : q + strlen(q), strlen(c ? c + 1 : "") + 1); atomic_store(&g_req, 1); LOG("DM_TEST_FREETP：拍 %u 当作点了回城钮", t); } } }
    if (!on) return;   // 关着：请求留给客户端那一步（dm_freetp_client_step 用回城书 / 卷轴） | Off: the request is left to the client step (dm_freetp_client_step uses the tome / scroll)
    if (!atomic_exchange(&g_req, 0) || !mb || !G || !player) return;
    uint32_t pm = *(uint32_t *)(player + 0xc); if (pm == 0 || pm == 17) { LOG("免费回城：玩家死了，不开"); return; }
    int sk = tp_skill_id(mb, player);
    if (sk < 0) {   // 2.x 时代存档（数据版本 2）：那套原版数据里没有不要书的回城技能（411 行是空的、全表没有执行函数 109）——直接调这个技能的两步，门与规则照原版，但没有施法动作 | 2.x-era saves (data version 2): that set of original-game data has no town portal skill that needs no tome (row 411 is empty, and no row in the table has execute function 109) -- call the two steps of this skill directly; the portal and the rules follow the original game, but there is no cast animation
        int ok = ((int (*)(uint64_t, uint64_t, int))(mb + 0x64c8e0))(G, player, SK_TP_V3) & 1;
        if (ok) ((void (*)(uint64_t, uint64_t))(mb + 0x64ce50))(G, player);
        LOG("免费回城：数据版本 %u 没有回城技能，直接建门 + 连门 → %s", *(uint8_t *)(player + 0x1bd), ok ? "已开门" : "原版不让开（城里等）");
        return;
    }
    uint64_t sl = *(uint64_t *)(player + 0x100), node = 0;
    for (uint64_t n = sl ? *(uint64_t *)sl : 0, k = 0; n && k < 512 && !node; n = *(uint64_t *)(n + 8), k++) { uint64_t rec = *(uint64_t *)n; if (rec && *(int16_t *)rec == sk) node = n; }
    int added = 0; if (!node) { node = ((uint64_t (*)(uint64_t, int))(mb + 0x7bf010))(player, sk); added = 1; }
    if (!node) { LOG("【红】免费回城：给玩家加回城技能 %d 失败", sk); return; }
    uint64_t pp = *(uint64_t *)(player + 0x38); int x = pp ? *(uint16_t *)(pp + 2) : 0, y = pp ? *(uint16_t *)(pp + 6) : 0;
    uint64_t old_r = sl ? *(uint64_t *)(sl + 0x10) : 0; if (sl) *(uint64_t *)(sl + 0x10) = node;
    int r = ((int (*)(uint64_t, uint64_t, int, int, int, uint64_t))(mb + 0x6e77f0))(G, player, x, y, 1, node);
    if (sl) *(uint64_t *)(sl + 0x10) = old_r;
    uint32_t pm2 = *(uint32_t *)(player + 0xc);   // 开始施法 = 模式变成 10（施法）；返回值在城里也是 0，不能当判据 | Cast started = mode becomes 10 (casting); the return value is also 0 in town, so it cannot be used as the criterion
    LOG("免费回城：%s回城技能 %d，脚下 %d,%d → %s（返回 %d，模式 %u → %u）", added ? "加上" : "已有", sk, x, y, pm2 == 10 && pm != 10 ? "开始施法" : "原版没放（城里 / 正在施法等）", r, pm, pm2);
}
