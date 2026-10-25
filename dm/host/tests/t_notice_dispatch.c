// Execute the production producer/consumer queue, with a delivery spy instead
// of calling the game. No native UI call may run on a producer thread.
#include "../dm_bossrush.c"
#include <assert.h>
static uint8_t image_byte;
static pthread_t owner;
static int delivered,requeue,recursive;
static unsigned seen[4][12];
static void delivery(uint8_t *base,const char *text) {
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
    puts("PASS notice dispatch: owned copy, four producers, owner-only delivery, bounded/reentrant drain, overflow and invalid requests");
    return 0;
}
