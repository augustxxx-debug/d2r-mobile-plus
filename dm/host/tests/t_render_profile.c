// 画质配置迁移、持久化与旧布尔键兼容：只使用临时设置文件，不读游戏存档。
// Test profile migration, persistence and legacy boolean compatibility using temporary settings only, never game saves.
#define DM_DISPLAY_HZ_TEST_HOOK
#include "../dm_custom.c"
#include <unistd.h>
#include <sys/stat.h>
static int quality;
void dm_3d_quality_mode_set(int value) { quality=value; }
int dm_lang_group(void) { return DM_LG_EN; }
const char *dm_save_commitdir(void) { return NULL; }
int dm_3d_quality_mode_get(void) { return quality; }
float dm_3d_scale_cur(void) { return .75f; }
static int display_hz;   // 屏幕最高刷新率替身（0 = 还没窗口） | stand-in for the screen's maximum refresh rate (0 = no window yet)
int dm_display_max_hz(void) { return display_hz; }
static int checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while (0)
int main(void) {
    char directory[]="/tmp/d2r-profile-XXXXXX"; if (!mkdtemp(directory)) return 1;
    char path[1024]; snprintf(path,sizeof path,"%s/sw_custom.txt",directory);
    const char *settings[]={"", "q3d=0\n", "q3d=1\n", "q3d=0\nq3d_v2=1\n", "q3d=1\nq3d_v2=1\n", "q3d=2\nq3d_v2=1\n", "q3d=3\nq3d_v2=1\n", "q3d=4\nq3d_v2=1\n", "q3d=5\nq3d_v2=1\n", "q3d=-1\nq3d_v2=1\n", "q3d=1oops\nq3d_v2=1\n"};
    const int expected[]={0,0,3,0,1,2,3,4,0,0,0};
    dm_custom_init(directory);
    for (unsigned i=0;i<sizeof settings/sizeof *settings;i++) {
        FILE *file=fopen(path,"w"); CHECK(file); fputs(settings[i],file); fclose(file);
        atomic_store(&g_loaded,0); CHECK(dm_3d_profile_get()==expected[i]); CHECK(quality==(expected[i]==3));
        save(); atomic_store(&g_loaded,0); CHECK(dm_3d_profile_get()==expected[i]);
        file=fopen(path,"r"); CHECK(file); char text[4096]={0}; fread(text,1,sizeof text-1,file); fclose(file); CHECK(strstr(text,"q3d_v2=1\n"));
    }
    // 应用才更新生效值；取消丢弃待选值，默认值也必须再应用。
    // Only Apply changes the live value; Discard drops pending values and Defaults also requires Apply.
    for (int mode=0;mode<5;mode++) {
        int previous=dm_3d_profile_get(); atomic_store(&g_pend[K_Q3D],mode);
        CHECK(dm_3d_profile_get()==previous);
        dm_custom_on_uimsg("OptionsPanelMessage","AcceptChanges",NULL);
        CHECK(dm_3d_profile_get()==mode); CHECK(quality==(mode==3));
    }
    atomic_store(&g_pend[K_Q3D],1);
    dm_custom_on_uimsg("OptionsPanelMessage","CancelChanges",NULL);
    CHECK(dm_3d_profile_get()==4); CHECK(atomic_load(&g_pend[K_Q3D])==-1);
    dm_custom_on_uimsg("OptionsPanelMessage","ResetToDefaults",NULL);
    CHECK(dm_3d_profile_get()==4); CHECK(atomic_load(&g_pend[K_Q3D])==0);
    dm_custom_on_uimsg("OptionsPanelMessage","AcceptChanges",NULL);
    CHECK(dm_3d_profile_get()==0);
    graphics_select_profile(2);
    CHECK(atomic_load(&g_pend[K_Q3D])==2); CHECK(dm_3d_profile_get()==0);
    dm_custom_on_uimsg("OptionsPanelMessage","AcceptChanges",NULL); CHECK(dm_3d_profile_get()==2);
    atomic_store(&g_pend[K_UPSCALE],0); dm_custom_on_uimsg("OptionsPanelMessage","AcceptChanges",NULL);
    CHECK(dm_3d_profile_get()==4); CHECK(atomic_load(&g_val[K_Q3D])==2);
    atomic_store(&g_pend[K_UPSCALE],1); dm_custom_on_uimsg("OptionsPanelMessage","AcceptChanges",NULL);
    CHECK(dm_3d_profile_get()==2);
    graphics_select_profile(-1); graphics_select_profile(4);
    CHECK(atomic_load(&g_pend[K_Q3D])==-1);
    for (int row=0;row<NK-N_GFX;row++) CHECK(!graphics_key(custom_key_at(row)));
    // 渲染分辨率档位 render_res：读盘范围 0–3、非法回 0；应用才生效并通知游戏重读；放弃不通知；默认回 0 | Render resolution level: parsed 0–3, invalid falls back to 0; only Apply takes effect and notifies the game; Discard does not notify; Defaults return to 0
    { const char *rs[]={"render_res=2\n","render_res=3\n","render_res=4\n","render_res=-1\n","render_res=1x\n","render_res=0\n",""}; const int re[]={2,3,0,0,0,0,0};
      for (unsigned i=0;i<sizeof rs/sizeof *rs;i++) { FILE *file=fopen(path,"w"); CHECK(file); fputs("q3d=0\nq3d_v2=1\n",file); fputs(rs[i],file); fclose(file);
          atomic_store(&g_loaded,0); CHECK(dm_render_res_level()==re[i]); }
      unsigned n0=dm_render_res_notify_count();
      graphics_select_res(2); CHECK(atomic_load(&g_pend[K_RES])==2); CHECK(dm_render_res_level()==0); CHECK(dm_render_res_notify_count()==n0);
      dm_custom_on_uimsg("OptionsPanelMessage","CancelChanges",NULL); CHECK(atomic_load(&g_pend[K_RES])==-1); CHECK(dm_render_res_level()==0); CHECK(dm_render_res_notify_count()==n0);
      graphics_select_res(2); dm_custom_on_uimsg("OptionsPanelMessage","AcceptChanges",NULL); CHECK(dm_render_res_level()==2); CHECK(dm_render_res_notify_count()==n0+1);
      graphics_select_res(2); dm_custom_on_uimsg("OptionsPanelMessage","AcceptChanges",NULL); CHECK(dm_render_res_notify_count()==n0+1);   // 没变不通知 | unchanged: no notification
      graphics_select_res(-1); graphics_select_res(DM_RES_LEVELS); CHECK(atomic_load(&g_pend[K_RES])==-1);
      atomic_store(&g_loaded,0); CHECK(dm_render_res_level()==2);   // 已存盘 | persisted
      { FILE *file=fopen(path,"r"); CHECK(file); char text[4096]={0}; fread(text,1,sizeof text-1,file); fclose(file); CHECK(strstr(text,"render_res=2\n")); }
      dm_custom_on_uimsg("OptionsPanelMessage","ResetToDefaults",NULL); CHECK(atomic_load(&g_pend[K_RES])==0); CHECK(dm_render_res_level()==2);
      dm_custom_on_uimsg("OptionsPanelMessage","AcceptChanges",NULL); CHECK(dm_render_res_level()==0); CHECK(dm_render_res_notify_count()==n0+2); }
    // DM_TEST_RENDER_RES 时间表（纯函数）：单档、带时刻的多档、非法串 | DM_TEST_RENDER_RES schedule (pure function): single level, timed levels, invalid strings
    CHECK(res_schedule_level("2",0)==2); CHECK(res_schedule_level("2",999)==2);
    CHECK(res_schedule_level("1@0,3@720,0@1440,2@2160",0)==1); CHECK(res_schedule_level("1@0,3@720,0@1440,2@2160",719.9)==1);
    CHECK(res_schedule_level("1@0,3@720,0@1440,2@2160",720)==3); CHECK(res_schedule_level("1@0,3@720,0@1440,2@2160",1500)==0); CHECK(res_schedule_level("1@0,3@720,0@1440,2@2160",5000)==2);
    CHECK(res_schedule_level("3@60",0)==-1 || res_schedule_level("3@60",0)==3);   // 首项晚于 0 秒：0 秒前无档，实现按首项补 | first entry later than 0 s: implementation falls back to the first entry
    CHECK(res_schedule_level("",0)==-1); CHECK(res_schedule_level("x",0)==-1); CHECK(res_schedule_level("4",0)==-1); CHECK(res_schedule_level("1@-5",0)==-1); CHECK(res_schedule_level("1@0;2@5",0)==-1);
    // Upstream defaults reset all enhanced preferences together, retaining language/voice.
    int auto_mana=key_index("auto_mana"); atomic_store(&g_val[auto_mana],0);
    atomic_store(&g_val[K_Q3D],3); atomic_store(&g_val[K_UPSCALE],0);
    int saved_lang=atomic_load(&g_val[0]);
    dm_custom_on_uimsg("OptionsPanelMessage","ResetToDefaults",NULL);
    CHECK(atomic_load(&g_pend[auto_mana])==1); CHECK(atomic_load(&g_pend[K_Q3D])==0);
    CHECK(atomic_load(&g_pend[K_UPSCALE])==1); CHECK(atomic_load(&g_pend[0])==-1);
#if VOICE_ROW
    CHECK(atomic_load(&g_pend[K_VOICE])==-1);
#endif
    CHECK(dm_3d_profile_get()==4); CHECK(enhanced_settings_dirty()); // Reset remains pending until Apply.
    dm_custom_on_uimsg("OptionsPanelMessage","CancelChanges",NULL);
    CHECK(atomic_load(&g_val[auto_mana])==0); CHECK(dm_3d_profile_get()==4);
    dm_custom_on_uimsg("OptionsPanelMessage","ResetToDefaults",NULL);
    dm_custom_on_uimsg("OptionsPanelMessage","AcceptChanges",NULL);
    CHECK(atomic_load(&g_val[auto_mana])==1); CHECK(dm_3d_profile_get()==0);
    CHECK(atomic_load(&g_val[0])==saved_lang); CHECK(!enhanced_settings_dirty());
    atomic_store(&g_pend[auto_mana],0); CHECK(enhanced_settings_dirty()); // Custom-only changes still enable Graphics Apply.
    dm_custom_on_uimsg("OptionsPanelMessage","CancelChanges",NULL); CHECK(!enhanced_settings_dirty());
    // Every enhanced preference participates in global defaults, and Reset
    // itself must neither change live values nor write the preferences file.
    for (int i=1+VOICE_ROW;i<NK;i++) atomic_store(&g_val[i], k_def[i] ? 0 : 1);
    save();
    char before_reset[4096]={0}, after_reset[4096]={0};
    FILE *reset_file=fopen(path,"r"); CHECK(reset_file);
    fread(before_reset,1,sizeof before_reset-1,reset_file); fclose(reset_file);
    dm_custom_on_uimsg("OptionsPanelMessage","ResetToDefaults",NULL);
    for (int i=1+VOICE_ROW;i<NK;i++) {
        CHECK(pending_value(i)==k_def[i]); CHECK(atomic_load(&g_val[i])!=k_def[i]);
    }
    reset_file=fopen(path,"r"); CHECK(reset_file);
    fread(after_reset,1,sizeof after_reset-1,reset_file); fclose(reset_file);
    CHECK(!strcmp(before_reset,after_reset));
    dm_custom_on_uimsg("OptionsPanelMessage","CancelChanges",NULL);
    for (int i=1+VOICE_ROW;i<NK;i++) CHECK(atomic_load(&g_val[i])!=k_def[i]);
    dm_custom_on_uimsg("OptionsPanelMessage","ResetToDefaults",NULL);
    dm_custom_on_uimsg("OptionsPanelMessage","AcceptChanges",NULL);
    for (int i=1+VOICE_ROW;i<NK;i++) CHECK(atomic_load(&g_val[i])==k_def[i]);
    CHECK(atomic_load(&g_val[0])==saved_lang);
    graphics_select_fps(0); CHECK(dm_fps_setting()==60); CHECK(pending_value(K_FPS)==1);
    dm_custom_on_uimsg("OptionsPanelMessage","AcceptChanges",NULL); CHECK(dm_fps_setting()==30);
    graphics_select_fps(1); dm_custom_on_uimsg("OptionsPanelMessage","CancelChanges",NULL); CHECK(dm_fps_setting()==30);
    graphics_select_fps(1); dm_custom_on_uimsg("OptionsPanelMessage","AcceptChanges",NULL); CHECK(dm_fps_setting()==60);
    graphics_select_fps(-1); graphics_select_fps(2); CHECK(atomic_load(&g_pend[K_FPS])==-1);
    // 120 帧档（eco_fps=2）：只在屏幕最高刷新率 ≥ 120 时开放第三格并生效；60 Hz 屏上存着的 120 按 60 走、滑块停在 60 那格且不算改动；旧值 0 / 1 照旧
    // 120 fps mode (eco_fps=2): the third stop is offered and takes effect only when the screen's maximum refresh rate is >= 120; a saved 120 on a 60 Hz screen runs at 60, the slider sits on the 60 stop and it is not a change; old values 0 / 1 unchanged
    { const char *fs[]={"eco_fps=2\n","eco_fps=1\n","eco_fps=0\n","","eco_fps=2x\n","eco_fps=3\n"}; const int f120[]={120,30,60,60,30,30}, f60[]={60,30,60,60,30,30};
      for (unsigned i=0;i<sizeof fs/sizeof *fs;i++) { FILE *file=fopen(path,"w"); CHECK(file); fputs("q3d=0\nq3d_v2=1\n",file); fputs(fs[i],file); fclose(file);
          atomic_store(&g_loaded,0); display_hz=120; CHECK(dm_fps_setting()==f120[i]); display_hz=60; CHECK(dm_fps_setting()==f60[i]); display_hz=0; CHECK(dm_fps_setting()==f60[i]); }
      FILE *file=fopen(path,"w"); CHECK(file); fputs("q3d=0\nq3d_v2=1\neco_fps=2\n",file); fclose(file); atomic_store(&g_loaded,0); load_once();
      display_hz=60; CHECK(fps_max_pos()==1); CHECK(fps_pos(pending_value(K_FPS))==1);
      graphics_select_fps(1); CHECK(atomic_load(&g_pend[K_FPS])==-1); CHECK(!enhanced_settings_dirty());   // 滑块读回 60 那格：不是改动 | slider reads back the 60 stop: not a change
      graphics_select_fps(2); CHECK(atomic_load(&g_pend[K_FPS])==-1);                                       // 60 Hz 屏没有第三格 | no third stop on a 60 Hz screen
      display_hz=120; CHECK(fps_max_pos()==2); CHECK(fps_pos(pending_value(K_FPS))==2); CHECK(dm_fps_setting()==120);
      graphics_select_fps(1); CHECK(pending_value(K_FPS)==0); CHECK(dm_fps_setting()==120); CHECK(enhanced_settings_dirty());   // 应用前不生效 | not in effect before Apply
      dm_custom_on_uimsg("OptionsPanelMessage","AcceptChanges",NULL); CHECK(dm_fps_setting()==60);
      graphics_select_fps(2); CHECK(pending_value(K_FPS)==FPS_120); dm_custom_on_uimsg("OptionsPanelMessage","CancelChanges",NULL); CHECK(dm_fps_setting()==60);
      graphics_select_fps(2); dm_custom_on_uimsg("OptionsPanelMessage","AcceptChanges",NULL); CHECK(dm_fps_setting()==120);
      file=fopen(path,"r"); CHECK(file); char text[4096]={0}; fread(text,1,sizeof text-1,file); fclose(file); CHECK(strstr(text,"eco_fps=2\n"));   // 存盘 | persisted
      atomic_store(&g_loaded,0); CHECK(dm_fps_setting()==120);
      graphics_select_fps(0); dm_custom_on_uimsg("OptionsPanelMessage","AcceptChanges",NULL); CHECK(dm_fps_setting()==30);
      graphics_select_fps(2); dm_custom_on_uimsg("OptionsPanelMessage","AcceptChanges",NULL); CHECK(dm_fps_setting()==120);
      dm_custom_on_uimsg("OptionsPanelMessage","ResetToDefaults",NULL); dm_custom_on_uimsg("OptionsPanelMessage","AcceptChanges",NULL); CHECK(dm_fps_setting()==60);   // 默认回 60 | defaults back to 60
      display_hz=0; }
    // DM_TEST_FPS 时间表（纯函数）：帧率只认 30 / 60 / 120 | DM_TEST_FPS schedule (pure function): only 30 / 60 / 120 are accepted
    CHECK(fps_schedule("120",0)==120); CHECK(fps_schedule("30@0,60@40,120@70",39.9)==30); CHECK(fps_schedule("30@0,60@40,120@70",40)==60); CHECK(fps_schedule("30@0,60@40,120@70",500)==120);
    CHECK(fps_schedule("120@20",0)==120); CHECK(fps_schedule("",0)==-1); CHECK(fps_schedule("45",0)==-1); CHECK(fps_schedule("120@-1",0)==-1); CHECK(fps_schedule("60;120@5",0)==-1);
    // 光追开时启动即降一档的判定（纯函数）：环境钉住优先且要过门槛；没钉住按设置；DM_RT_RES_DROP=0 关掉；其它值不关 | RT-on startup tier drop decision (pure): the env pin wins and needs the gate; otherwise the setting; DM_RT_RES_DROP=0 turns it off; other values do not
    { int on;
      CHECK(rt_res_drop_decide(NULL,NULL,1,1,&on)==1 && on==1); CHECK(rt_res_drop_decide(NULL,NULL,1,0,&on)==0 && on==0);
      CHECK(rt_res_drop_decide(NULL,"1",1,0,&on)==1 && on==1); CHECK(rt_res_drop_decide(NULL,"1",0,1,&on)==0 && on==0);   // 钉开但机器不可用：不降 | pinned on but the machine fails the gate: no drop
      CHECK(rt_res_drop_decide(NULL,"0",1,1,&on)==0 && on==0);   // 钉关压过设置 | pinned off overrides the setting
      CHECK(rt_res_drop_decide("0",NULL,1,1,&on)==0 && on==1); CHECK(rt_res_drop_decide("1",NULL,1,1,&on)==1);
      // 整局只定一次：第一次之后改设置不影响（游戏渲染目标按启动尺寸建） | decided once per run: later setting changes do not matter (the game's render targets are built at the startup size)
      FILE *file=fopen(path,"w"); CHECK(file); fputs("rt_shadow=1\n",file); fclose(file); atomic_store(&g_loaded,0);
      int first=dm_render_res_rt_drop(); CHECK(first==0);   // 单测不链图形层：弱定义的机器门槛 = 不可用 → 不降 | unit tests do not link the graphics layer: the weak machine gate = unsupported → no drop
      file=fopen(path,"w"); CHECK(file); fputs("rt_shadow=0\n",file); fclose(file); atomic_store(&g_loaded,0); CHECK(dm_render_res_rt_drop()==first); }
    // 120 帧档不给超分「高画质」（用户 2026-10-29，RC1）：存着的高画质在 120 帧生效时按均衡走、文件里仍是高画质；60 / 30 帧下高画质照旧（0 副作用）；
    // 还没窗口（刷新率 0）时按 60，建窗后到 120 再落到均衡；图形页在 120 帧下档位滑块只到均衡、读回均衡不算改动，回到 60 恢复可选
    // 120 fps gets no MetalFX Quality (user 2026-10-29, RC1): a saved Quality runs as Balanced while 120 fps is in effect and the file keeps Quality; Quality at 60 / 30 fps is untouched (no side effect);
    // with no window yet (refresh 0) it runs as 60 and drops to Balanced once 120 takes effect; at 120 the Graphics profile slider stops at Balanced, reading Balanced back is not a change, and 60 makes Quality selectable again
    CHECK(q3d_for_fps(3,120)==2); CHECK(q3d_for_fps(3,60)==3); CHECK(q3d_for_fps(3,30)==3);
    for (int q=0;q<5;q++) if (q!=3) { CHECK(q3d_for_fps(q,120)==q); CHECK(q3d_for_fps(q,60)==q); CHECK(q3d_for_fps(q,30)==q); }
    { const char *cs[]={"eco_fps=2\n","eco_fps=0\n","eco_fps=1\n"}; const int at120[]={2,3,3};
      for (unsigned i=0;i<sizeof cs/sizeof *cs;i++) { FILE *file=fopen(path,"w"); CHECK(file); fputs("q3d=3\nq3d_v2=1\nupscale=1\n",file); fputs(cs[i],file); fclose(file);
          display_hz=120; atomic_store(&g_loaded,0); load_once(); CHECK(quality==(at120[i]==3)); CHECK(dm_3d_profile_get()==at120[i]); CHECK(quality==(at120[i]==3));
          display_hz=60; CHECK(dm_3d_profile_get()==3); CHECK(quality==1);            // 60 Hz 屏：120 档按 60，高画质回来 | 60 Hz screen: 120 runs as 60, Quality is back
          display_hz=0; atomic_store(&g_loaded,0); load_once(); CHECK(quality==1); CHECK(dm_3d_profile_get()==3);   // 还没窗口 | no window yet
          display_hz=120; CHECK(dm_3d_profile_get()==at120[i]); CHECK(quality==(at120[i]==3));   // 建窗后 | once the window exists
          save(); file=fopen(path,"r"); CHECK(file); char text[4096]={0}; fread(text,1,sizeof text-1,file); fclose(file); CHECK(strstr(text,"q3d=3\n")); }   // 文件仍存高画质 | the file keeps Quality
      // 其余档在 120 帧下不变 | the other profiles are unchanged at 120
      for (int q=0;q<3;q++) { FILE *file=fopen(path,"w"); CHECK(file); fprintf(file,"q3d=%d\nq3d_v2=1\nupscale=1\neco_fps=2\n",q); fclose(file);
          display_hz=120; atomic_store(&g_loaded,0); CHECK(dm_3d_profile_get()==q); CHECK(quality==0); }
      // 超分关着：仍是原版档，与帧率无关 | upscaling off: still Original, whatever the frame rate
      { FILE *file=fopen(path,"w"); CHECK(file); fputs("q3d=3\nq3d_v2=1\nupscale=0\neco_fps=2\n",file); fclose(file); display_hz=120; atomic_store(&g_loaded,0); CHECK(dm_3d_profile_get()==4); CHECK(quality==0); }
      // 日志：「超分档位：」行写出帧率、设置档与生效档（分享诊断日志摘要取最后一条）| log: the "超分档位：" line carries the frame rate, the saved and the effective profile (the share summary takes the last one)
      { char logp[1100]; snprintf(logp,sizeof logp,"%s/log.txt",directory);
        FILE *file=fopen(path,"w"); CHECK(file); fputs("q3d=3\nq3d_v2=1\nupscale=1\neco_fps=2\n",file); fclose(file); display_hz=120; atomic_store(&g_loaded,0);
        int saved_err=dup(2); CHECK(saved_err>=0); CHECK(freopen(logp,"w",stderr)); int p=dm_3d_profile_get(); fflush(stderr); dup2(saved_err,2); close(saved_err); CHECK(p==2);
        file=fopen(logp,"r"); CHECK(file); char text[8192]={0}; fread(text,1,sizeof text-1,file); fclose(file); unlink(logp);
        char *line=strstr(text,"超分档位："); CHECK(line); char *e=strchr(line,'\n'); if (e) *e=0;
        CHECK(strstr(line,"120")); CHECK(strstr(line,"高画质")); CHECK(strstr(line,"均衡")); }
      // 图形页 | Graphics tab
      FILE *file=fopen(path,"w"); CHECK(file); fputs("q3d=3\nq3d_v2=1\nupscale=1\neco_fps=0\n",file); fclose(file); display_hz=120; atomic_store(&g_loaded,0); load_once();
      CHECK(profile_max()==3); CHECK(profile_shown()==3);
      graphics_select_profile(3); CHECK(atomic_load(&g_pend[K_Q3D])==-1);
      graphics_select_fps(2); CHECK(profile_max()==2); CHECK(profile_shown()==2); CHECK(atomic_load(&g_pend[K_Q3D])==-1);   // 选到 120：显示落到均衡 | 120 selected: the display drops to Balanced
      graphics_select_profile(3); CHECK(atomic_load(&g_pend[K_Q3D])==-1);   // 120 帧下选不到高画质 | Quality cannot be picked at 120
      graphics_select_profile(2); CHECK(atomic_load(&g_pend[K_Q3D])==-1);   // 滑块读回均衡：不是改动 | the slider reads back Balanced: not a change
      dm_custom_on_uimsg("OptionsPanelMessage","AcceptChanges",NULL); CHECK(dm_fps_setting()==120); CHECK(dm_3d_profile_get()==2); CHECK(quality==0); CHECK(!enhanced_settings_dirty());
      { file=fopen(path,"r"); CHECK(file); char text[4096]={0}; fread(text,1,sizeof text-1,file); fclose(file); CHECK(strstr(text,"q3d=3\n")); CHECK(strstr(text,"eco_fps=2\n")); }
      graphics_select_fps(1); CHECK(profile_max()==3); CHECK(profile_shown()==3);   // 回到 60：高画质恢复可选 | back to 60: Quality selectable again
      dm_custom_on_uimsg("OptionsPanelMessage","AcceptChanges",NULL); CHECK(dm_fps_setting()==60); CHECK(dm_3d_profile_get()==3); CHECK(quality==1);
      graphics_select_fps(0); CHECK(profile_max()==3); CHECK(profile_shown()==3);   // 30 帧同 60 | 30 fps same as 60
      dm_custom_on_uimsg("OptionsPanelMessage","CancelChanges",NULL);
      graphics_select_profile(1); CHECK(pending_value(K_Q3D)==1); graphics_select_profile(3); CHECK(pending_value(K_Q3D)==3);   // 60 帧下高画质照常可选 | at 60 Quality is selectable as before
      dm_custom_on_uimsg("OptionsPanelMessage","AcceptChanges",NULL); CHECK(dm_3d_profile_get()==3); CHECK(quality==1);
      graphics_select_fps(2); graphics_select_profile(1); CHECK(pending_value(K_Q3D)==1); CHECK(profile_shown()==1);   // 120 帧下其余档照常 | the other profiles work at 120
      dm_custom_on_uimsg("OptionsPanelMessage","AcceptChanges",NULL); CHECK(dm_fps_setting()==120); CHECK(dm_3d_profile_get()==1); CHECK(quality==0);
      graphics_select_profile(3); CHECK(atomic_load(&g_pend[K_Q3D])==-1);
      display_hz=60; CHECK(profile_max()==3); CHECK(dm_fps_setting()==60);   // 60 Hz 屏：没有 120 档，也就没有上限 | 60 Hz screen: no 120 stop, so no cap
      display_hz=0; }
    unlink(path); rmdir(directory); printf("PASS render profiles: %d migration/persistence checks\n",checks); return 0;
}
