// B145：设置页「被盖住的暂停菜单按钮」那道闸门按几何认滑块两端的箭头（箭头不是控件，是滑块画在滑槽外、自己收点击的）。合成控件树跑 dm_cursor.c 里的 slider_part_hit。
// B145: the settings-cover gate recognizes a slider's end arrows by geometry (they are not widgets; the slider draws them outside its track and handles their clicks). Runs slider_part_hit from dm_cursor.c on a synthetic tree.
#include "../dm_cursor.c"
#include <assert.h>
typedef struct { int x0, y0, x1, y1; } box;
static _Alignas(8) unsigned char page[160], slider[160], ind[160], other[160];
static uint64_t vt_slider[16], vt_leaf[16], vt_page[16], page_ch[2], slider_ch[1];
// 17 Pro 横屏（vh 1206）实测：「画面加亮」滑槽约 x 1458–1788、左箭头中心 1437、右箭头中心 1806 | 17 Pro landscape (vh 1206) measured: track about x 1458-1788, left arrow centre 1437, right 1806
static box b_slider = { 1458, 495, 1788, 519 }, b_ind = { 1640, 495, 1660, 519 }, b_other = { 300, 495, 700, 519 };
static box *box_of(uint64_t w) { return w == (uint64_t)(uintptr_t)slider ? &b_slider : w == (uint64_t)(uintptr_t)ind ? &b_ind : w == (uint64_t)(uintptr_t)other ? &b_other : 0; }
static int hit_fn(uint64_t w, uint64_t xy) { box *b = box_of(w); int x = (int)(uint32_t)xy, y = (int)(uint32_t)(xy >> 32); return b && x >= b->x0 && x <= b->x1 && y >= b->y0 && y <= b->y1; }
static int none_fn(uint64_t w, uint64_t xy) { (void)w; (void)xy; return 0; }
static uint64_t P(void *p) { return (uint64_t)(uintptr_t)p; }
int main(void) {
    vt_slider[9] = P(hit_fn); vt_leaf[9] = P(hit_fn); vt_page[9] = P(none_fn);
    uint64_t base = P(vt_slider) - 0x20bad70; const int vh = 1206;   // dg_sliders 认 base + 0x20bad70 = 滑块虚表 | dg_sliders takes base + 0x20bad70 as the slider vtable
    *(uint64_t *)page = P(vt_page); *(uint64_t *)slider = P(vt_slider); *(uint64_t *)ind = P(vt_leaf); *(uint64_t *)other = P(vt_leaf);
    page_ch[0] = P(slider); page_ch[1] = P(other); *(uint64_t *)(page + 0x58) = P(page_ch); *(uint64_t *)(page + 0x60) = 2;
    slider_ch[0] = P(ind); *(uint64_t *)(slider + 0x58) = P(slider_ch); *(uint64_t *)(slider + 0x60) = 1;
    page[0x50] = slider[0x50] = ind[0x50] = other[0x50] = 1;
    assert(slider_part_hit(P(page), 1437, 507, base, vh));   // 左箭头中心（B145 主用例）| left arrow centre (the B145 case)
    assert(slider_part_hit(P(page), 1426, 507, base, vh));   // 左箭头左沿 | left edge of the left arrow
    assert(slider_part_hit(P(page), 1806, 507, base, vh));   // 右箭头 | right arrow
    assert(slider_part_hit(P(page), 1600, 507, base, vh));   // 滑槽上 | on the track
    assert(slider_part_hit(P(page), 1437, 490, base, vh));   // 行内稍偏上（手指粗细）| slightly above, within the row
    assert(!slider_part_hit(P(page), 1400, 507, base, vh));  // 离滑槽端 58 像素（> 64 单位 = 35 像素）：不算 | 58 px from the end (> 64 units = 35 px): no
    assert(!slider_part_hit(P(page), 1437, 560, base, vh));  // 下一行 | the next row
    assert(!slider_part_hit(P(page), 500, 507, base, vh));   // 页面上别的控件 | another widget on the page
    assert(!slider_part_hit(P(page), 1437, 507, base, 0));   // 读不到画面高度：不认 | no screen height: no
    slider[0x50] = 0; assert(!slider_part_hit(P(page), 1437, 507, base, vh)); slider[0x50] = 1;   // 滑块不可见 | slider hidden
    page[0x50] = 0; assert(!slider_part_hit(P(page), 1437, 507, base, vh)); page[0x50] = 1;   // 页面不可见 | page hidden
    *(uint64_t *)slider = P(vt_leaf); assert(!slider_part_hit(P(page), 1437, 507, base, vh)); *(uint64_t *)slider = P(vt_slider);   // 不是滑块 | not a slider
    puts("PASS B145 slider arrows: same row within 64 layout units of the track ends counts as the slider; farther, other rows, other widgets, hidden and non-sliders do not");
    return 0;
}
