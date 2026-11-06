// Content-fitted frame: fixed corners/medallions, flexible connecting strips.
#pragma once
// max_height：框最高多少（原版提示框与我们多数框照旧 800；社区框三行渠道 + 页脚要 922，B185 起给它放宽——原先被截到 800，「确定」压在下边框上、一半出框）
// max_height: the tallest frame (800 as before for the original notice and most of our boxes; the community box needs 922 with three channel rows + footer and gets more from B185 -- it used to be clipped to 800, leaving OK half outside the bottom edge)
static inline int dm_notice_geometry_h(int text_width, int text_height, int limit, int max_height, int tiles[15][4]) {
    if (max_height < 244 || max_height > 2000) return 0;
    if (limit < 600 || limit > 4000 || text_width <= 0 || text_width > 10000 || text_height <= 0 || text_height > 10000) return 0;
    int width = text_width + 128;
    if (width < 600) width = 600; if (width > limit) width = limit;
    int height = text_height + 24;
    if (height < 244) height = 244; if (height > max_height) height = max_height;
    int left=(limit-width)/2, top=(244-height)/2, upper=(height-86)/2;
    int xs[]={left,left+52,left+width-52,left+width};
    int ys[]={top,top+24,top+24+upper,top+62+upper,top+height-24,top+height};
    for (int row=0;row<5;row++) for (int col=0;col<3;col++) {
        int *r=tiles[row*3+col]; r[0]=xs[col]; r[1]=ys[row]; r[2]=xs[col+1]-xs[col]; r[3]=ys[row+1]-ys[row];
    }
    return height;
}
static inline int dm_notice_geometry(int text_width, int text_height, int limit, int tiles[15][4]) { return dm_notice_geometry_h(text_width, text_height, limit, 800, tiles); }
