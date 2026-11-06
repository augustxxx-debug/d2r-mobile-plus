// DM_RT_POINT / DM_RT_POINT_N 的启动读取（第八轮外审 R8-01：初始化与钳制曾被写进行尾注释，点光开关打开后盏数恒为 0）
// Start-up reading of DM_RT_POINT / DM_RT_POINT_N (round-eight external audit R8-01: the initialisation and clamps once sat in a trailing comment, so the count stayed 0 with point lights on)
#define main rts_gate_main
#include "t_rts_gate.m"
#undef main
#if DM_NVN_M3   // 旧接口版没有光追（模块为空）：用例跳过（tests_m3，B211）；桩照样从 t_rts_gate.m 带进来 | the older-API build has no ray tracing (empty module): the case is skipped; the stubs still come from t_rts_gate.m
int main(void) { printf("SKIP：旧接口版没有光追阴影\n"); return 77; }
#else

// 走一次 rts_on 的一次性读配置：on 回到 -1 就会重读 | one pass through rts_on's one-time configuration read: on back at -1 reads it again
static void reread(const char *point, const char *n) {
    if (point) setenv("DM_RT_POINT", point, 1); else unsetenv("DM_RT_POINT");
    if (n) setenv("DM_RT_POINT_N", n, 1); else unsetenv("DM_RT_POINT_N");
    g_rts.on = -1; rts_on();
}
int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0);
    unsetenv("DM_RT_SHADOW"); unsetenv("DM_RT_LEVEL"); unsetenv("DM_RT_AUTO");
    mtl_init();
    if (!mtl_rt_shadow_supported()) { printf("SKIP：本机不支持光追阴影\n"); return 77; }
    reread(NULL, NULL);
    EXPECT("没设 DM_RT_POINT：点光影缺省关", 0, g_rts.point_on); EXPECT("没设 DM_RT_POINT：第 0 档也不开点光", 0, rts_lvl_of(0).point_on);
    reread("1", NULL);
    EXPECT("DM_RT_POINT=1：打开", 1, g_rts.point_on); EXPECT("DM_RT_POINT=1 没设盏数：缺省 2 盏", 2, g_rts.point_n); EXPECT("第 0 档生效 2 盏", 2, rts_lvl_of(0).point_n); EXPECT("第 0 档点光开", 1, rts_lvl_of(0).point_on);
    reread("1", "3");
    EXPECT("DM_RT_POINT_N=3：照给", 3, g_rts.point_n); EXPECT("DM_RT_POINT_N=3：点光开", 1, g_rts.point_on);
    reread("1", "9");
    EXPECT("DM_RT_POINT_N>4：钳到 4", 4, g_rts.point_n); EXPECT("DM_RT_POINT_N>4：点光开", 1, g_rts.point_on);
    reread("1", "0");
    EXPECT("DM_RT_POINT_N=0：关掉点光", 0, g_rts.point_on);
    reread("1", "-1");
    EXPECT("DM_RT_POINT_N 为负：关掉点光", 0, g_rts.point_on);
    reread("0", "3");
    EXPECT("DM_RT_POINT=0：关着，盏数不打开它", 0, g_rts.point_on);
    printf("== %d 项，失败 %d ==\n", count, fail);
    return fail ? 1 : 0;
} }
#endif
