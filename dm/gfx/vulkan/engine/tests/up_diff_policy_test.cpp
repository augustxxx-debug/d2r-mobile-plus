// B150：差分重传只收「BC 解成像素」的图；BC3→ASTC 的图（无原生 BC 的 GPU）不收。CPU 单测，不碰 Vulkan。
// B150: differential re-upload only takes images decoded to pixels; BC3 -> ASTC images (GPUs without native BC) are excluded. CPU-only.
// g++ -std=c++17 -Wall -Wextra -I<dir holding vulkan/> -DSRC_DIR=\"dm/gfx/vulkan/engine\" dm/gfx/vulkan/engine/tests/up_diff_policy_test.cpp -o up_diff_policy_test && ./up_diff_policy_test
#include "../xbg_bc_policy.h"
#include <stdio.h>
static int fails;
#define CHECK(c) do{if(!(c)){printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c);++fails;}}while(0)
int main(){
    const uint64_t budget=64ull*1024*1024;
    CHECK(xbg_up_diff_eligible(true,false,1,64*1024,budget,3));    // 像素解码的 BC 图，循环 3 次：进差分 | pixel-decoded BC, cycled 3 times: eligible
    CHECK(!xbg_up_diff_eligible(true,true,1,64*1024,budget,3));    // BC3→ASTC（Mali 真机闪退那张）：不进 | BC3 -> ASTC (the Mali crash): not eligible
    CHECK(!xbg_up_diff_eligible(false,false,1,64*1024,budget,3));  // 原生格式：不进 | native format: not eligible
    CHECK(!xbg_up_diff_eligible(true,false,2,64*1024,budget,3));   // 体纹理：不进 | volume: not eligible
    CHECK(!xbg_up_diff_eligible(true,false,1,8*1024,budget,3));    // 太小 | too small
    CHECK(!xbg_up_diff_eligible(true,false,1,budget/16,budget,3)); // 超单张上限 | over the per-image cap
    CHECK(!xbg_up_diff_eligible(true,false,1,64*1024,budget,2));   // 还没真正在循环 | not cycling yet
    // 源码：上传循环的差分入口走这个函数 | source: the upload loop's differential gate goes through it
    FILE *f=fopen(SRC_DIR "/xbg_game.cpp","rb");CHECK(f);
    if(f){static char buf[1<<23];size_t n=fread(buf,1,sizeof buf-1,f);buf[n]=0;fclose(f);
        CHECK(strstr(buf,"if(xbg_up_diff_eligible(im.decoded,im.astc,im.depth,t->L.size,up_diff_budget,seen))")!=nullptr);
        CHECK(strstr(buf,"if(im.decoded && im.depth==1 && t->L.size>=16ull*1024")==nullptr);}
    printf(fails?"up_diff_policy_test: %d FAIL\n":"up_diff_policy_test: OK\n",fails);return fails!=0;
}
