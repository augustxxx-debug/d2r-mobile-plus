// 依据 dm_uiscreen 的原生比例策略测试前端布局及输入转换。
// Native-aspect frontend policy and UI/input coordinate regression tests.
#include "../dm_uiscreen.c"
#include <math.h>

module_t g_mod[MAX_MOD];
int g_nmod;
static uint32_t render_w = 1600, render_h = 1200;
uint64_t front_game_res(int docked) { (void)docked; return ((uint64_t)render_h << 32) | render_w; }
void dm_oe_res_changed(void) {}
int gate_obs_present(int id) { (void)id; return 0; }
void gate_obs_set(int id, void (*fn)(const uint64_t *)) { (void)id; (void)fn; }

static int checks, failures;
#define CHECK(value) do { checks++; if (!(value)) { failures++; fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #value); } } while (0)
static unsigned output_h(void) { return (unsigned)(dm_game_res(1) >> 32); }

int main(void) {
    dm_uiscreen_enable(DM_UISCREEN_BOTTOM);
    setenv("DM_FRONTEND_FULL", "0", 1);
    CHECK(output_h() == 1200); // 启动时创建原生尺寸窗口。 | Allocate a native-sized window at startup.
    atomic_store(&g_res_live, 1);
    CHECK(output_h() == 900); // 显式传统前端保留黑边。 | Explicit legacy frontend remains letterboxed.
    unsetenv("DM_FRONTEND_FULL");
    CHECK(output_h() == 1200); // 全屏开启时默认原生前端。 | Native frontend is the default when fullscreen is on.
    setenv("DM_FRONTEND_FULL", "1", 1);
    CHECK(output_h() == 1200);
    atomic_store(&g_world, 1);
    CHECK(output_h() == 1200);
    setenv("DM_FRONTEND_FULL", "0", 1);
    CHECK(output_h() == 1200); // 局内不受前端开关影响。 | In-game behavior must not depend on the flag.
    atomic_store(&g_world, 0);
    render_h = 900;
    CHECK(output_h() == 900);
    render_h = 720;
    CHECK(output_h() == 720); // 宽屏手机保持不变。 | No change on wide phones.
    render_h = 1200;
    dm_uiscreen_enable(DM_UISCREEN_OFF);
    CHECK(output_h() == 1200);

    dm_uiscreen_enable(DM_UISCREEN_BOTTOM);
    atomic_store(&g_rw, 1600);
    atomic_store(&g_rh, 1200);
    atomic_store(&g_uh, 900);
    CHECK(dm_ui_class_x(0) == 0); // 传统场景命中位置不变。 | Legacy scene hit positions are unchanged.
    unsetenv("DM_FRONTEND_FULL");
    CHECK(dm_ui_class_x(0) == 200);
    CHECK(dm_ui_class_x(800) == 800);
    CHECK(dm_ui_class_x(1600) == 1400);
    atomic_store(&g_rh, 900);
    CHECK(dm_ui_class_x(0) == 0); // 16:9 保持不变。 | 16:9 remains unchanged.
    atomic_store(&g_rh, 1200);
    int x, y, w, h;
    dm_ui_rect(&x, &y, &w, &h);
    CHECK(x == 0 && y == 150 && w == 1600 && h == 900);
    CHECK(fabsf(dm_ui_ny_back(0) - .125f) < .00001f);
    CHECK(fabsf(dm_ui_ny_back(1) - .875f) < .00001f);
    const float points[] = {0, .1f, .5f, .9f, 1};
    for (unsigned i = 0; i < sizeof points / sizeof *points; i++)
        CHECK(fabsf(dm_ui_ny(dm_ui_ny_back(points[i])) - points[i]) < .00001f);
    CHECK(dm_ui_bottom_offset(142) == 360);
    CHECK(dm_ui_bottom_offset(142) == 360); // 重复布局不能累计偏移。 | Repeated layout must not accumulate offsets.
    dm_ui_bottom_inset(200);
    CHECK(dm_ui_bottom_offset(142) == 302);
    dm_ui_bottom_inset(0);
    atomic_store(&g_rw, 2034); atomic_store(&g_rh, 1398); atomic_store(&g_uh, 1144);
    CHECK(dm_ui_bottom_offset(142) == 239); // 根据当前矩形重算外屏几何。 | Outer-screen geometry, recomputed from the current rect.
    atomic_store(&g_rw, 2532); atomic_store(&g_rh, 1170); atomic_store(&g_uh, 1170);
    CHECK(dm_ui_bottom_offset(142) == 0); // 宽屏手机无居中 UI 间隙。 | Wide phone: no centered-UI gap.
    dm_ui_bottom_inset(200);
    CHECK(dm_ui_bottom_offset(142) == -58); // 宽屏同样遵守更大的底部安全区域。 | Honor a larger bottom safe area even on a wide screen.
    dm_ui_bottom_inset(0);
    atomic_store(&g_rw, 1600); atomic_store(&g_rh, 1200); atomic_store(&g_uh, 900);
    const float extended[] = {-.25f, 0, .5f, 1, 1.125f, 2};
    for (unsigned i = 0; i < sizeof extended / sizeof *extended; i++) {
        uint64_t p = dm_ui_point_pack(.5f, extended[i]);
        CHECK(fabsf(dm_ui_point_x(p) - .5f) < .00002f);
        CHECK(fabsf(dm_ui_point_y(p) - extended[i]) < .00002f);
    }
    CHECK(dm_ui_point_y(dm_ui_point_pack(0, NAN)) == 0);
    CHECK(dm_ui_point_y(dm_ui_point_pack(0, 10)) == 4);
    dm_uiscreen_set_ingame(1);
    dm_ui_rect(&x, &y, &w, &h);
    CHECK(y == 300 && h == 900);
    dm_uiscreen_set_panel(1);
    dm_ui_rect(&x, &y, &w, &h);
    CHECK(y == 150 && h == 900);
    printf("frontend fullscreen: %d/%d checks passed\n", checks - failures, checks);
    return failures != 0;
}
