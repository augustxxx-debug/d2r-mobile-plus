// 选项页（图形页、自定义页）手柄选中框与行文字的横向摆放（纯函数，dm_custom.c 用；单测 tests/t_sel_frame.c）。
// 用户报（奥丁与 iPhone 都有，2026-10-28）：手柄选中框的左边线在每行文字那一行的上半有一段黑色缺口。奥丁实测（`audit1-fix-20261028/gap-exp/`）：
//   框左缘放 x = −20 / −60（行内容坐标）都有缺口，放 +10 没有；运行时把行文字右移 60，字跟着走、缺口不动；空字的标签与灰色小字副文不出缺口。
//   即：用 $StyleSettingsText 的文字，在它载入时的位置左边约 12 单位以外、这一行文字的高度上，选中框的线和外发光画不出来（引擎侧成因未定）。
// 修法：选中框左缘不进 x < DM_SEL_FRAME_MIN_X；行里载入时横坐标为 0 的叶子控件（文字、空标签）在运行时右移 DM_ROW_INDENT，框按「标签左缘 − DM_SEL_FRAME_PAD」摆，
//   正好落在 +10。只挪运行时位置、不改布局里的坐标：实测缺口跟着载入时的位置走，改布局坐标会把缺口一起挪进来。
// Horizontal placement of the controller selection frame and the row text on the options pages (Graphics, Custom) (pure functions, used by dm_custom.c;
// unit test tests/t_sel_frame.c). User report (Thor and iPhone, 2026-10-28): the frame's left line has a black gap at the upper half of each text line.
// Measured on the Thor: with the frame's left edge at x = -20 / -60 (row content coordinates) the gap is there, at +10 it is not; moving the row text right by 60 at
// run time moves the glyphs but not the gap; empty labels and the grey small hint text cause no gap. So left of about 12 units before where a $StyleSettingsText text
// sat when loaded, at that text line's height, the frame's line and glow are not drawn (engine-side cause not determined).
// Fix: the frame's left edge never goes below DM_SEL_FRAME_MIN_X; leaf widgets of a row (text, empty labels) loaded at x = 0 move right by DM_ROW_INDENT at run time and the
// frame sits at "label left edge - DM_SEL_FRAME_PAD", which is exactly +10. Only the run-time position moves, not the layout coordinate: the gap follows the loaded position,
// so moving the layout coordinate would move the gap in with it.
#pragma once
#define DM_ROW_INDENT 40        // 行文字运行时右移量（行内容坐标）| run-time right shift of row text (row content coordinates)
#define DM_SEL_FRAME_PAD 30     // 框左缘在标签左缘左边多少 | how far left of the label's left edge the frame starts
#define DM_SEL_FRAME_MIN_X 10   // 框左缘最小横坐标（实测 +10 无缺口）| smallest frame left edge (measured: +10 has no gap)
// 叶子控件（没有子控件）载入时横坐标为 0 → 运行时该在哪；别的不动 | where a leaf widget loaded at x = 0 belongs at run time; anything else stays
static inline int dm_row_indent_x(int x, int is_leaf) { return is_leaf && x == 0 ? DM_ROW_INDENT : x; }
// 框左缘：标签左缘 − 留白，且不小于 DM_SEL_FRAME_MIN_X | frame left edge: label left edge minus the margin, never below DM_SEL_FRAME_MIN_X
static inline int dm_sel_frame_x(int label_x) { int x = label_x - DM_SEL_FRAME_PAD; return x < DM_SEL_FRAME_MIN_X ? DM_SEL_FRAME_MIN_X : x; }
