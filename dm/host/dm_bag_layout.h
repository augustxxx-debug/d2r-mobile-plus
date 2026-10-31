// 物品栏页签形态的布局参数（面板内单位，1 格 = 98）。bag_layout.py 从这里读，宿主也用这一份；换页签方案只改这里和贴图。
// 页签条：第 0 个页签在 (TAB_X, TAB_Y)，下一个加 (TAB_DX, TAB_DY)；TAB_DX = 0 是纵向（第 0 列按 ← 进条），否则横向（第 0 行按 ↑ 进条）。
// Layout parameters of the inventory tab form (panel units, 1 cell = 98). bag_layout.py reads this file and so does the host; changing the tab design only touches this and the art.
// Tab bar: tab 0 at (TAB_X, TAB_Y), each next one offset by (TAB_DX, TAB_DY); TAB_DX = 0 is vertical (left from column 0 enters), otherwise horizontal (up from row 0 enters).
#pragma once
#define DM_BAG_COLS 12
#define DM_BAG_ROWS 7
#define DM_BAG_GRID_X 158
#define DM_BAG_GRID_Y 599
#define DM_BAG_TAB_X 78
#define DM_BAG_TAB_Y 599
#define DM_BAG_TAB_W 76
#define DM_BAG_TAB_H 117
#define DM_BAG_TAB_DX 0
#define DM_BAG_TAB_DY 124
#define DM_BAG_KEYL_X 116
#define DM_BAG_KEYL_Y 493
#define DM_BAG_KEYR_X 116
#define DM_BAG_KEYR_Y 777
#define DM_BAG_COUNT_DX -8
#define DM_BAG_COUNT_DY 77
#define DM_BAG_COUNT_W 92
#define DM_BAG_DOT_DX 56
#define DM_BAG_DOT_DY -6
