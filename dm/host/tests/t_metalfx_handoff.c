// Drive the production present-interval entry, not a copied controller.
#ifndef HLE_OE_SOURCE
#define HLE_OE_SOURCE "../hle_oe.c"
#endif
#include HLE_OE_SOURCE
#include <assert.h>
#include <math.h>
#include <signal.h>
float test_fx_hint=.80f;
int test_fx_auto=1;
static uint64_t test_clock=1000000000ull;
uint64_t now_ns(void) { return test_clock; }
module_t g_mod[MAX_MOD]; int g_nmod;
pthread_key_t g_keyres[2];
uint64_t dm_game_res(int docked) { (void)docked;return 0; }
int dm_nvn_ruler_truth(void) { return 0; }
void dm_ui_gen_bump(int why) { (void)why; }
void dm_uiscreen_res_reread(void) {}
uint64_t dm_nvn_frame_count(void) { return 0; }
int dm_ingame_now(void) { return 1; }
int mtl_fps_target(void) { return 60; }
double mtl_display_refresh(void) { return 60; }
double dm_pace_hz(double screen,double desired) { (void)screen;return desired; }
static void sample(double ms) {
    test_clock+=(uint64_t)(ms*1000000);dm_3d_scale_feed(ms);
    float cfg=s3_effective_cfg();assert(fabsf(s3_target_scale(cfg)-s3_level_scale(cfg,atomic_load(&g_scale3d_level)))<1e-4f);
}
static float actual(void) { return s3_target_scale(s3_effective_cfg()); }
static void assertion_exit(int sig) { (void)sig;_Exit(86); }
static _Atomic int stress_done;
static float stress_scale;
static void *flap(void *unused) {
    (void)unused;
    for (int i=0;i<10000;i++) { test_fx_hint=i%2 ? .65f : .80f;sample(16.666667); }
    atomic_store(&stress_done,1);return NULL;
}
int main(int argc,char **argv) {
    signal(SIGABRT,assertion_exit);
    unsetenv("DM_3D_SCALE_DYN");unsetenv("DM_THERMAL_NOFALLBACK");unsetenv("DM_3D_SCALE_DYN_INJECT");unsetenv("DM_3D_SCALE_GPU");
    setenv("DM_3D_SCALE_FLOOR","0.5",1);
    if (argc>1 && !strcmp(argv[1],"--disabled")) {
        setenv("DM_3D_SCALE_DYN","0",1);test_fx_hint=.60f;sample(16.666667);assert(fabsf(actual()-.60f)<1e-4f);
        test_fx_hint=.85f;sample(16.666667);assert(fabsf(actual()-.85f)<1e-4f);
        test_fx_hint=0;sample(16.666667);assert(fabsf(actual()-s3_cfg())<1e-4f);
        puts("PASS disabled dynamic controller: explicit profiles and original fallback still publish");return 0;
    }
    for (int i=0;i<10000 && actual()>.60f+1e-4f;i++) sample(33.333333);
    float before=actual();assert(before<=.60f+1e-4f);
    test_fx_hint=.65f;assert(fabsf(actual()-before)<1e-4f);sample(16.666667);
    fprintf(stderr,"Auto thermal cap: prior actual %.2f, after cap change %.2f\n",before,actual());
    assert(actual()<=before+1e-4f);
    test_fx_hint=.80f;assert(fabsf(actual()-before)<1e-4f);sample(16.666667);assert(actual()<=before+1e-4f);
    for (int i=0;i<120;i++) sample(16.666667);
    assert(actual()<=before+1e-4f); // Recovery must not reset the prior downgrade.
    for (int i=0;i<2100;i++) sample(16.666667);
    assert(actual()>before && actual()<=before+.051f); // One gradual recovery step.
    stress_scale=actual();pthread_t writer;assert(!pthread_create(&writer,NULL,flap,NULL));
    do { assert(fabsf(s3_target_scale(.80f)-stress_scale)<1e-4f); } while (!atomic_load(&stress_done));
    assert(!pthread_join(writer,NULL));
    test_fx_auto=0;test_fx_hint=.75f;sample(16.666667);assert(fabsf(actual()-.75f)<1e-4f);
    test_fx_hint=.85f;dm_3d_quality_mode_set(1);sample(16.666667);assert(fabsf(actual()-.85f)<1e-4f);
    test_fx_auto=1;test_fx_hint=.80f;dm_3d_quality_mode_set(0);sample(16.666667);assert(fabsf(actual()-.80f)<1e-4f);
    puts("PASS production scale handoff: thermal/recovery retain actual downgrade; recovery steps slowly; explicit profile changes reset");
    return 0;
}
