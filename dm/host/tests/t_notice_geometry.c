#include "../dm_notice_geometry.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    int tiles[15][4],checks=0;
    for (int width=40;width<=2000;width+=17) for (int height=40;height<=900;height+=13) {
        int h=dm_notice_geometry(width,height,1680,tiles); assert(h>=244 && h<=800);
        assert(tiles[0][2]==52 && tiles[2][2]==52 && tiles[0][3]==24 && tiles[12][3]==24 && tiles[6][3]==38);
        int w=tiles[0][2]+tiles[1][2]+tiles[2][2]; assert(w>=600 && w<=1680);
        assert(tiles[0][0]*2+w==1680 || tiles[0][0]*2+w==1679);
        for (int row=0;row<5;row++) for (int col=0;col<3;col++) {
            int *r=tiles[row*3+col]; assert(r[2]>0 && r[3]>0);
            if (col<2) assert(r[0]+r[2]==tiles[row*3+col+1][0]);
            if (row<4) assert(r[1]+r[3]==tiles[(row+1)*3+col][1]);
        }
        checks++;
    }
    assert(!dm_notice_geometry(0,100,1680,tiles)); assert(!dm_notice_geometry(100,100,0,tiles));
    printf("PASS multilingual notice geometry: %d content sizes, intact caps/medallions, bounded height, centered coverage without gaps\n",checks);
}
