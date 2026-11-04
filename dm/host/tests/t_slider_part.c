// B145：设置页「被盖住的暂停菜单按钮」那道闸门认滑块两端的箭头（滑块的可见子控件）为设置页自己的控件。合成控件树跑 dm_cursor.c 里的 slider_part_hit。
// B145: the settings-cover gate accepts the arrows at a slider's ends (visible children of the slider) as the settings page's own widgets. Runs slider_part_hit from dm_cursor.c on a synthetic widget tree.
#include "../dm_cursor.c"
#include <assert.h>
typedef struct { int x0, y0, x1, y1; } box;
static _Alignas(8) unsigned char page[160], slider[160], left[160], right[160], other[160];
static uint64_t vt_slider[16], vt_leaf[16], vt_page[16];
static uint64_t page_ch[2], slider_ch[2];
static box b_slider = { 917, 490, 1148, 525 }, b_left = { 890, 495, 912, 520 }, b_right = { 1153, 495, 1175, 520 }, b_other = { 300, 490, 500, 525 };
static box *box_of(uint64_t w) { return w == (uint64_t)(uintptr_t)slider ? &b_slider : w == (uint64_t)(uintptr_t)left ? &b_left : w == (uint64_t)(uintptr_t)right ? &b_right : w == (uint64_t)(uintptr_t)other ? &b_other : 0; }
static int hit_fn(uint64_t w, uint64_t xy) { box *b = box_of(w); int x = (int)(uint32_t)xy, y = (int)(uint32_t)(xy >> 32); return b && x >= b->x0 && x <= b->x1 && y >= b->y0 && y <= b->y1; }
static int none_fn(uint64_t w, uint64_t xy) { (void)w; (void)xy; return 0; }
static uint64_t P(void *p) { return (uint64_t)(uintptr_t)p; }
static uint64_t XY(int x, int y) { return (uint64_t)(uint32_t)x | ((uint64_t)(uint32_t)y << 32); }
int main(void) {
    vt_slider[9] = P(hit_fn); vt_leaf[9] = P(hit_fn); vt_page[9] = P(none_fn);
    uint64_t base = P(vt_slider) - 0x20bad70;   // dg_sliders 认 base + 0x20bad70 = 滑块虚表 | dg_sliders recognizes base + 0x20bad70 as the slider vtable
    *(uint64_t *)page = P(vt_page); *(uint64_t *)slider = P(vt_slider); *(uint64_t *)left = P(vt_leaf); *(uint64_t *)right = P(vt_leaf); *(uint64_t *)other = P(vt_leaf);
    page_ch[0] = P(slider); page_ch[1] = P(other); *(uint64_t *)(page + 0x58) = P(page_ch); *(uint64_t *)(page + 0x60) = 2;
    slider_ch[0] = P(left); slider_ch[1] = P(right); *(uint64_t *)(slider + 0x58) = P(slider_ch); *(uint64_t *)(slider + 0x60) = 2;
    page[0x50] = slider[0x50] = left[0x50] = right[0x50] = other[0x50] = 1;
    assert(slider_part_hit(P(page), XY(900, 507), base));    // 左箭头（B145 主用例）| left arrow (the B145 case)
    assert(slider_part_hit(P(page), XY(1165, 507), base));   // 右箭头 | right arrow
    assert(!slider_part_hit(P(page), XY(1000, 507), base));  // 滑槽本身不算这一条（它走 click_walk / 拖动）| the track itself is not this rule's job
    assert(!slider_part_hit(P(page), XY(400, 507), base));   // 页面上别的控件不算 | another widget on the page does not count
    assert(!slider_part_hit(P(page), XY(900, 600), base));   // 箭头外 | outside the arrow
    left[0x50] = 0; assert(!slider_part_hit(P(page), XY(900, 507), base)); left[0x50] = 1;   // 箭头不可见 | arrow hidden
    slider[0x50] = 0; assert(!slider_part_hit(P(page), XY(900, 507), base)); slider[0x50] = 1;   // 整条滑块不可见 | slider hidden
    page[0x50] = 0; assert(!slider_part_hit(P(page), XY(900, 507), base)); page[0x50] = 1;   // 页面不可见 | page hidden
    *(uint64_t *)slider = P(vt_leaf); assert(!slider_part_hit(P(page), XY(900, 507), base)); *(uint64_t *)slider = P(vt_slider);   // 不是滑块的控件的子控件不算 | children of a non-slider do not count
    puts("PASS B145 slider parts: visible arrows of a visible slider count as the settings page's own widgets; track, other widgets, hidden parts and non-sliders do not");
    return 0;
}
