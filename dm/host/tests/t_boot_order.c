// 十四审 Q14-01 / Q14-02（同一根因）：iOS 壳第一次问「光追开启动降一档」与「内存档位」是在 SceneDelegate willConnect 建视图那一刻（front_game_res），
// 原先早于 boot_prepare 里的 dm_mem_measure_set / dm_custom_init：设置读不到、上限没量，光追判定被记死为「不降」、档位恒为 2，玩家从设置里开光追 / 低内存机型都不生效。
// 照审计方 harness（evidence/audit14-review/t_audit14_order.c）按真实调用顺序：先问（设置未读）→ 再交上限、给设置目录 → 再问，判定必须按设置来；只用临时目录，不碰真存档
// Review 14 Q14-01 / Q14-02 (one root cause): the iOS shell first asks "RT on drops one level at startup" and "memory tier" when SceneDelegate willConnect builds the view (front_game_res),
// which used to come before dm_mem_measure_set / dm_custom_init in boot_prepare: settings unreadable, limit unmeasured, the RT decision latched to "no drop" and the tier stuck at 2, so neither the RT setting nor a low-memory device took effect.
// Following the reviewer's harness in the real call order: ask first (settings not read) -> hand over the limit and the settings directory -> ask again; the decision must follow the settings. Temporary directories only, never real saves
#define DM_DISPLAY_HZ_TEST_HOOK
#define DM_RT_GATE_TEST_HOOK
#include "../dm_custom.c"
#include <unistd.h>
static int quality;
void dm_3d_quality_mode_set(int value) { quality = value; }
int dm_lang_group(void) { return DM_LG_EN; }
const char *dm_save_commitdir(void) { return NULL; }
int dm_3d_quality_mode_get(void) { return quality; }
float dm_3d_scale_cur(void) { return .75f; }
int dm_display_max_hz(void) { return 120; }
int mtl_rt_shadow_supported(void) { return 1; }   // 机器过光追门槛 | the machine passes the RT gate
static int checks, fails;
#define CHECK(x) do { checks++; if (!(x)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)
int main(void) {
    char dir[] = "/tmp/d2r-bootorder-XXXXXX"; if (!mkdtemp(dir)) return 1; char p[1100]; snprintf(p, sizeof p, "%s/sw_custom.txt", dir);
    unsetenv("DM_RT_SHADOW"); unsetenv("DM_RT_RES_DROP"); unsetenv("DM_MEM_TIER"); unsetenv("DM_SAVE");
    FILE *f = fopen(p, "w"); if (!f) return 1; fputs("rt_shadow=1\nrender_res=3\nq3d_v2=1\n", f); fclose(f);   // 玩家在设置里开了光追、分辨率原生 | the player turned RT on in settings, Native resolution
    // 阶段 A：设置目录还没给、上限还没交（修前 willConnect 就在这一刻）——此时不许把光追判定记死 | stage A: no settings directory, no limit yet (where willConnect used to be) - the RT decision must not latch now
    CHECK(!dm_custom_loaded()); CHECK(dm_mem_tier() == 2);
    int dropA = dm_render_res_rt_drop(); CHECK(dropA == 0);   // 读不到设置：按「不降」答，但不记死 | settings unreadable: answer "no drop" without latching
    // 阶段 B：上限 2900 MB（1 档）、设置目录已给 | stage B: limit 2900 MB (tier 1), settings directory given
    dm_mem_measure_set(2900, 12288, "iPhoneTest,1"); dm_custom_init(dir);
    CHECK(dm_custom_loaded()); CHECK(dm_mem_tier() == 1); CHECK(dm_rt_shadow_setting() == 1); CHECK(dm_render_res_level() == 3);
    CHECK(dm_render_res_rt_drop() == 1);   // 修前这里是 0（已在阶段 A 记死）| before the fix this was 0 (latched in stage A)
    // 读到设置之后才记死：之后改设置不再变（整局不变的语义不变）| it latches once the settings were read: later setting changes no longer move it (the run-long semantics stay)
    atomic_store(&g_val[K_RT], 0); CHECK(dm_render_res_rt_drop() == 1);
    unlink(p); rmdir(dir);
    printf("t_boot_order: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
