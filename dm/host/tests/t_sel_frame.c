// 选项页手柄选中框左缘不进缺口带（dm_sel_frame.h）：图形页与自定义页的行文字载入在 x = 0，运行时右移后框左缘 ≥ +10，且框在字的左边留白。
// 旧摆法（图形页框 x = −20、自定义页框 = 标签 − 30 = −30）在这里失败。
// Controller selection frame on the options pages stays out of the gap band (dm_sel_frame.h): row text is loaded at x = 0, after the run-time shift the frame's left
// edge is >= +10 and the frame leaves a margin left of the text. The old placement (Graphics frame x = -20, Custom frame = label - 30 = -30) fails here.
#include "dm_sel_frame.h"
#include <stdio.h>
static int fails, checks;
#define CHECK(c, what) do { checks++; if (!(c)) { fails++; printf("FAIL %s\n", what); } } while (0)
#ifdef DM_SEL_FRAME_OLD   // 旧摆法 | the old placement
static int graphics_frame_x(void) { return -20; }
static int custom_frame_x(int label_loaded_x) { return label_loaded_x - 30; }
static int row_x(int x, int leaf) { (void)leaf; return x; }
#else
static int graphics_frame_x(void) { return dm_sel_frame_x(dm_row_indent_x(0, 1)); }
static int custom_frame_x(int label_loaded_x) { return dm_sel_frame_x(dm_row_indent_x(label_loaded_x, 1)); }
static int row_x(int x, int leaf) { return dm_row_indent_x(x, leaf); }
#endif
int main(void) {
    CHECK(graphics_frame_x() >= DM_SEL_FRAME_MIN_X, "graphics frame left edge >= +10");
    CHECK(custom_frame_x(0) >= DM_SEL_FRAME_MIN_X, "custom frame left edge >= +10 for a label loaded at 0");
    CHECK(row_x(0, 1) - graphics_frame_x() >= 20, "graphics: at least 20 units between the frame and the text");
    CHECK(row_x(0, 1) - custom_frame_x(0) >= 20, "custom: at least 20 units between the frame and the text");
    CHECK(row_x(0, 0) == 0, "containers (title row with its divider, row widgets) stay put");
    CHECK(row_x(1110, 1) == 1110 && row_x(1315, 1) == 1315, "sliders / checkboxes not at x = 0 stay put");
    CHECK(row_x(99999, 1) == 99999, "off-screen widgets stay off screen");
    CHECK(dm_sel_frame_x(-100) == DM_SEL_FRAME_MIN_X, "frame never goes left of the bound");
    printf("%s t_sel_frame: %d/%d\n", fails ? "FAIL" : "PASS", checks - fails, checks);
    return fails != 0;
}
