// Execute the production producer/consumer queue, with a delivery spy instead
// of calling the game. No native UI call may run on a producer thread.
#include "../dm_bossrush.c"
#include "../dm_padmode_pure.h"
#include <assert.h>
static uint8_t image_byte;
static pthread_t owner;
static int delivered,requeue,recursive;
static unsigned seen[4][12];
static char last_text[NOTICE_UI_BYTES],last_sub[NOTICE_UI_BYTES];
static char posted[4][NOTICE_UI_BYTES]; static int posts;
static void post_spy(uint8_t *base,const char *text) { assert(base==&image_byte && *text && posts<4); strcpy(posted[posts++],text); }
static void delivery(uint8_t *base,const char *text,const char *sub) {
    assert(sub);strcpy(last_text,text);strcpy(last_sub,sub);
    assert(base==&image_byte && pthread_equal(pthread_self(),owner));
    delivered++;
    if (recursive) { recursive=0;notice_ui_drain(delivery); }
    if (requeue) { requeue=0;dm_game_notice_ui(base,"next poll"); }
    if (!strncmp(text,"producer",8)) {
        int a,b;char tail;assert(sscanf(text,"producer%d-%d%c",&a,&b,&tail)==2);
        assert(a>=0 && a<4 && b>=0 && b<12 && !seen[a][b]++);
    }
}
static void *producer(void *arg) {
    for (int i=0;i<12;i++) {
        char text[80];snprintf(text,sizeof text,"producer%ld-%d",(long)arg,i);
        dm_game_notice_ui(&image_byte,text);memset(text,0,sizeof text);
    }
    return NULL;
}
int main(void) {
    owner=pthread_self();
    char text[]="caller storage";dm_game_notice_ui(&image_byte,text);memset(text,0,sizeof text);
    assert(!delivered && notice_ui_count==1);
    assert(!strcmp(notice_ui_queue[notice_ui_read].text,"caller storage"));
    requeue=1;notice_ui_drain(delivery);assert(delivered==1 && notice_ui_count==1);
    notice_ui_drain(delivery);assert(delivered==2 && !notice_ui_count);
    pthread_t workers[4];
    for (long i=0;i<4;i++) assert(!pthread_create(&workers[i],NULL,producer,(void *)i));
    for (int i=0;i<4;i++) assert(!pthread_join(workers[i],NULL));
    assert(delivered==2 && notice_ui_count==48);notice_ui_drain(delivery);
    assert(delivered==50 && !notice_ui_count);
    for (int a=0;a<4;a++) for (int b=0;b<12;b++) assert(seen[a][b]==1);
    for (int i=0;i<NOTICE_UI_CAPACITY+1;i++) dm_game_notice_ui(&image_byte,"bounded");
    assert(notice_ui_count==NOTICE_UI_CAPACITY);notice_ui_drain(delivery);
    char long_text[NOTICE_UI_BYTES+1];memset(long_text,'x',sizeof long_text);long_text[sizeof long_text-1]=0;
    dm_game_notice_ui(&image_byte,long_text);dm_game_notice_ui(NULL,"ignored");dm_game_notice_ui(&image_byte,"");
    assert(!notice_ui_count && delivered==50+NOTICE_UI_CAPACITY);
    dm_game_notice_ui(&image_byte,"recursive 1");dm_game_notice_ui(&image_byte,"recursive 2");
    recursive=1;notice_ui_drain(delivery);assert(!notice_ui_count && delivered==52+NOTICE_UI_CAPACITY);
    // 两级提示（B035）：难度说明经 dm_diff_tip_send → dm_game_notice_ui_pair 排队，正文 / 副文原样送到 deliver（审计 r2 P1-3）
    // Two-level tip (B035): the difficulty tip goes dm_diff_tip_send -> dm_game_notice_ui_pair, and body / subtext reach deliver unchanged (audit r2 P1-3)
    const char *fmt="Game Difficulty %d: same as %d players",*sub="More players: tougher monsters";
    dm_diff_tip_send(&image_byte,3,fmt,sub,dm_game_notice_ui_pair);assert(notice_ui_count==1);notice_ui_drain(delivery);
    assert(!strcmp(last_text,"Game Difficulty 3: same as 3 players") && !strcmp(last_sub,sub));
    dm_diff_tip_send(&image_byte,0,fmt,sub,dm_game_notice_ui_pair);notice_ui_drain(delivery);   // 选择条刚展开：只有副文 | selector just opened: subtext only
    assert(!strcmp(last_text,"") && !strcmp(last_sub,sub));
    dm_diff_tip_send(&image_byte,5,"%s %d",sub,dm_game_notice_ui_pair);notice_ui_drain(delivery);   // 坏格式串（翻译错）：不当格式串用 | broken format (bad translation): not used as a format
    assert(!strcmp(last_text,"") && !strcmp(last_sub,sub));
    assert(dm_fmt_two_d("%d / %d") && dm_fmt_two_d("100%% %d %d") && !dm_fmt_two_d("%d") && !dm_fmt_two_d("%d %d %d") && !dm_fmt_two_d("%d %s") && !dm_fmt_two_d(NULL));
    dm_game_notice_ui(&image_byte,"plain");notice_ui_drain(delivery);assert(!strcmp(last_text,"plain") && !strcmp(last_sub,""));   // 普通通知没有副文 | plain notices carry no subtext
    dm_game_notice_ui_pair(&image_byte,"","");assert(!notice_ui_count);   // 两段都空：不排 | both empty: not queued
    // 绑定表：只记带副文的；按原版 Prompt 显示的那串（正文，正文空时用副文）找；新的先查；长度不同 / 别的字找不到 | binding table: only entries with a subtext; looked up by the native text (body, or subtext when empty); newest first; other text / length misses
    dm_notice_pairs pr; memset(&pr,0,sizeof pr);
    assert(!dm_notice_pair_add(&pr,"plain","") && !dm_notice_pair_find(&pr,"plain",5));
    assert(dm_notice_pair_add(&pr,"Body A",sub) && dm_notice_pair_add(&pr,"",sub));
    assert(!strcmp(dm_notice_pair_key("",sub),sub) && !strcmp(dm_notice_pair_key("Body A",sub),"Body A"));
    const dm_notice_pair *e=dm_notice_pair_find(&pr,"Body A",6); assert(e && !strcmp(e->sub,sub) && !strcmp(e->body,"Body A"));
    e=dm_notice_pair_find(&pr,sub,strlen(sub)); assert(e && !*e->body);
    assert(!dm_notice_pair_find(&pr,"Body A",5) && !dm_notice_pair_find(&pr,"Your inventory is full",22) && !dm_notice_pair_find(&pr,"",0));
    for (int i=0;i<DM_NOTICE_PAIR_N;i++) { char t[16]; snprintf(t,sizeof t,"tip %d",i); dm_notice_pair_add(&pr,t,sub); }
    assert(!dm_notice_pair_find(&pr,"Body A",6) && dm_notice_pair_find(&pr,"tip 0",5));   // 只留最近 4 条 | only the latest 4 are kept
    // 投递（游戏主线程）：交给原版通知的是正文，正文空时是副文——原版从不收到空串；带副文的记进绑定表，提示框排版按 Prompt 的字查得到；普通通知不进表
    // Delivery (game main thread): the original notice gets the body, or the subtext when the body is empty -- never an empty string; entries with a subtext enter the
    // binding table, so the notice layout finds them by Prompt's text; plain notices stay out
    notice_ui_post=post_spy;
    dm_diff_tip_send(&image_byte,4,fmt,sub,dm_game_notice_ui_pair);dm_diff_tip_send(&image_byte,0,fmt,sub,dm_game_notice_ui_pair);dm_game_notice_ui(&image_byte,"Your inventory is full");
    dm_game_notice_ui_pump();assert(posts==3 && !notice_ui_count);
    assert(!strcmp(posted[0],"Game Difficulty 4: same as 4 players") && !strcmp(posted[1],sub) && !strcmp(posted[2],"Your inventory is full"));
    e=dm_game_notice_pair_for(posted[0],strlen(posted[0])); assert(e && !strcmp(e->sub,sub) && !strcmp(e->body,posted[0]));
    e=dm_game_notice_pair_for(posted[1],strlen(posted[1])); assert(e && !*e->body && !strcmp(e->sub,sub));
    assert(!dm_game_notice_pair_for(posted[2],strlen(posted[2])));
    puts("PASS notice dispatch: owned copy, four producers, owner-only delivery, bounded/reentrant drain, overflow and invalid requests; two-level tip body/subtext reach delivery; binding table");
    return 0;
}
