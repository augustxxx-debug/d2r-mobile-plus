// Content-fitted frame: fixed corners/medallions, flexible connecting strips.
#pragma once
static inline int dm_notice_geometry(int text_width, int text_height, int limit, int tiles[15][4]) {
    if (limit < 600 || limit > 4000 || text_width <= 0 || text_width > 10000 || text_height <= 0 || text_height > 10000) return 0;
    int width = text_width + 128;
    if (width < 600) width = 600; if (width > limit) width = limit;
    int height = text_height + 24;
    if (height < 244) height = 244; if (height > 800) height = 800;
    int left=(limit-width)/2, top=(244-height)/2, upper=(height-86)/2;
    int xs[]={left,left+52,left+width-52,left+width};
    int ys[]={top,top+24,top+24+upper,top+62+upper,top+height-24,top+height};
    for (int row=0;row<5;row++) for (int col=0;col<3;col++) {
        int *r=tiles[row*3+col]; r[0]=xs[col]; r[1]=ys[row]; r[2]=xs[col+1]-xs[col]; r[3]=ys[row+1]-ys[row];
    }
    return height;
}
