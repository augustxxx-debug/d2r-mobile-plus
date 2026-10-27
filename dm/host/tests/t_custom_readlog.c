// 「自定义设置：读盘」日志行要写全每个键（分享诊断日志靠它看出玩家开了什么，例如显示帧数 fps_hud）：只使用临时设置文件，不读游戏存档。
// The "custom settings: load" log line must list every key (the diagnostic share relies on it to show what the player turned on, e.g. fps_hud); temporary settings only, never game saves.
#include "../dm_custom.c"
#include <unistd.h>
static int quality;
void dm_3d_quality_mode_set(int value) { quality=value; }
int dm_lang_group(void) { return DM_LG_EN; }
const char *dm_save_commitdir(void) { return NULL; }
int dm_3d_quality_mode_get(void) { return quality; }
float dm_3d_scale_cur(void) { return .75f; }
static int checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while (0)
int main(void) {
    char directory[]="/tmp/d2r-readlog-XXXXXX"; if (!mkdtemp(directory)) return 1;
    char path[1024], logp[1024]; snprintf(path,sizeof path,"%s/sw_custom.txt",directory); snprintf(logp,sizeof logp,"%s/log.txt",directory);
    // 每个键都写成与缺省不同的值，日志里看到的必须是文件里的值 | every key differs from its default, so the log must show the file's value
    FILE *file=fopen(path,"w"); CHECK(file);
    fputs("auto_mana=0\nmerc_pot=0\nmerc_pot_v2=0\ndrop_light=0\nmap_mark=0\nitem_notes=0\nicloud=0\npot_pick=0\ndrop_haptic=0\ngem_pick=0\nstore_btn=0\neco_fps=1\nq3d=2\nq3d_v2=1\nloot_filter=0\nauto_buff=0\ntouch_swap_above=1\nfps_hud=1\nupscale=1\nrender_res=2\n",file); fclose(file);
    dm_custom_init(directory);
    int saved=dup(2); CHECK(saved>=0); CHECK(freopen(logp,"w",stderr));
    int fps=dm_custom_get("fps_hud");
    fflush(stderr); dup2(saved,2); close(saved);
    CHECK(fps==1);
    file=fopen(logp,"r"); CHECK(file); char text[8192]={0}; fread(text,1,sizeof text-1,file); fclose(file);
    char *line=strstr(text,"自定义设置：读盘"); CHECK(line); char *end=strchr(line,'\n'); if (end) *end=0;
    for (int i=1+VOICE_ROW;i<NK;i++) {
        char want[64]; snprintf(want,sizeof want," %s=%d",k_keys[i],atomic_load(&g_val[i]));
        if (!strstr(line,want)) { fprintf(stderr,"FAIL: 读盘行缺「%s」：%s\n",want+1,line); return 1; }
        checks++;
    }
    CHECK(strstr(line," fps_hud=1")); CHECK(strstr(line," render_res=2")); CHECK(strstr(line," q3d=2")); CHECK(strstr(line," upscale=1")); CHECK(strstr(line," touch_swap_above=1"));
    unlink(path); unlink(logp); rmdir(directory);
    printf("t_custom_readlog: %d checks, 0 failures\n",checks);
    return 0;
}
