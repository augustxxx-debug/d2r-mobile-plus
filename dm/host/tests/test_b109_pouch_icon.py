#!/usr/bin/env python3
# B109：背包左下角百宝囊入口的符文图不许超出框内凹槽，两种输入模式同一套布局数据。
#   尺寸来源（2026-11-04 在 PouchBox.sprite 上逐像素量）：
#     · 按钮格 TouchPouch = 贴图原尺寸 140×64 像素 = 280×128 布局单位（1 像素 = 2 单位）。
#     · 框内凹槽：PouchBox.sprite 里从外框金边内沿量，左/右各内缩 18 像素、上 10 像素、下 22 像素
#       → 104×32 像素 = 208×64 单位。原版物品栏小件（符文）是 49×49 像素帧、占一格 49 像素（≈一格边长的 90%
#       可见像素，四边各留约 5% 透明边），所以这里同样让图标在凹槽内留边，不允许压到金边。
#     · 原实现 scale 0.79 → 49×2×0.79 = 77.42 单位见方，比凹槽高 13.42 单位，上下都压到金边（用户反馈「过大」）。
#   触屏命中区仍是整块 TouchPouch（280×128），不随图标缩小。
# B109: the rune icon at the inventory's bottom-left pouch entry must stay inside the frame's inner well; both input modes
# share the same layout data.
#   Measurements (taken pixel by pixel on PouchBox.sprite, 2026-11-04):
#     - TouchPouch button cell = the texture's natural size 140x64 px = 280x128 layout units (1 px = 2 units).
#     - Inner well: measured from the outer gold frame's inner edge in PouchBox.sprite, inset 18 px left/right, 10 px top,
#       22 px bottom -> 104x32 px = 208x64 units. An original inventory small item (a rune) is a 49x49 px frame in a 49 px
#       cell (about 90% visible pixels, ~5% transparent margin per side), so the icon likewise keeps a margin inside the well
#       and must not ride on the gold border.
#     - Before: scale 0.79 -> 49x2x0.79 = 77.42 units square, 13.42 units taller than the well, overlapping the gold border
#       top and bottom (the user's "too large" report).
#   The touch hit area stays the whole TouchPouch button (280x128) and does not shrink with the icon.
import ast
import json
import os
from pathlib import Path
import re
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]                      # dm/host/tests -> repo root
TOOLS = ROOT / 'dm' / 'ios' / 'tools'
sys.path.insert(0, str(TOOLS))
import pouch_layout

PX = pouch_layout.PX
BOX = pouch_layout.BOX                      # (x, y, w, h), layout units
WELL = pouch_layout.well_rect()             # (x, y, w, h), inner well, layout units
MARGIN = pouch_layout.MIN_MARGIN            # minimum margin per side, fraction of that side
EPS = 1e-9

# 原版物品栏里 bag_layout.rects 要改名的控件：用它拼两份最小物品栏布局，分别代表触屏版和手柄版（两种模式的输入不同、
# 输出走同一个 pouch_layout.inv_layout）。 | Widgets bag_layout.rects renames in the original inventory: two minimal
# inventory layouts stand in for the touch and controller sources (different inputs, both go through pouch_layout.inv_layout).
NAMES = ('background', 'grid', 'Belt', 'slot_right_arm', 'slot_left_arm', 'slot_head', 'slot_gloves', 'slot_torso',
         'slot_belt', 'slot_neck', 'slot_right_hand', 'slot_left_hand', 'slot_feet', 'background_right_arm_selected',
         'background_right_arm', 'background_left_arm_selected', 'background_left_arm', 'text_i_left', 'text_i_right',
         'text_ii_left', 'text_ii_right', 'WeaponSwapRightLegend', 'WeaponSwapLeftLegend', 'weaponswap_right_arm',
         'weaponswap_left_arm')


def source(mode):
    return '{"mode": "%s", "children": [%s,]}' % (
        mode, ','.join('{"type": "Widget", "name": "%s", "fields": {}}' % name for name in NAMES))


def nodes_of(layout_text):
    root = json.loads(re.sub(r',\s*([}\]])', r'\1', layout_text))
    found = {}

    def walk(node):
        if isinstance(node, dict):
            if 'name' in node:
                found[node['name']] = node
            for value in node.values():
                walk(value)
        elif isinstance(node, list):
            for value in node:
                walk(value)

    walk(root)
    return found


def main():
    # 两种输入模式各生成一份：触屏版叠在改过的触屏布局上，手柄版叠在原版手柄布局上（gen_romfs_overlay.py 两处 build）。
    # Generate one layout per input mode: the touch copy stacks on the patched touch layout and the controller copy on the
    # original controller layout (two pouch_layout.build calls in gen_romfs_overlay.py).
    modes = {mode: nodes_of(pouch_layout.inv_layout(source(mode))) for mode in ('touch', 'controller')}
    icons = {}
    for mode, nodes in modes.items():
        icon = nodes['TouchPouchIcon']['fields']['rect']
        hit = nodes['TouchPouch']['fields']['rect']
        count = nodes['TouchPouchCount']['fields']['rect']
        side = pouch_layout.ICON_PX * PX * icon['scale']
        wx, wy, ww, wh = WELL

        # 图标矩形必须完整落在凹槽内，并且每边 ≥ 凹槽该边尺寸的 8%。
        # The icon rect must lie wholly inside the well, with >= 8% of that well side as margin on every side.
        left, top = icon['x'] - wx, icon['y'] - wy
        right, bottom = (wx + ww) - (icon['x'] + side), (wy + wh) - (icon['y'] + side)
        assert min(left, right, top, bottom) >= 0, (mode, icon, WELL)
        assert left + EPS >= ww * MARGIN, ('left margin', mode, left, ww * MARGIN)
        assert right + EPS >= ww * MARGIN, ('right margin', mode, right, ww * MARGIN)
        assert top + EPS >= wh * MARGIN, ('top margin', mode, top, wh * MARGIN)
        assert bottom + EPS >= wh * MARGIN, ('bottom margin', mode, bottom, wh * MARGIN)

        # 命中区保持原值（整块按钮），不小于凹槽，也不随图标缩小；图标不压到右边的件数。
        # The hit area keeps its original size (the whole button), is not smaller than the well, and does not shrink with
        # the icon; the icon does not run into the count text on its right.
        assert (hit['x'], hit['y'], hit['width'], hit['height']) == BOX, (mode, hit)
        assert hit['width'] >= ww and hit['height'] >= wh, (mode, hit, WELL)
        assert icon['x'] + side <= count['x'], (mode, icon, count)

        assert abs(side - pouch_layout.icon_rect()[2]) < 1e-6, (mode, side, pouch_layout.icon_rect())
        icons[mode] = (icon['x'], icon['y'], icon['scale'])

    # 两种模式必须得到同一套图标几何：pouch_layout.build 对两种输入都调用同一个 inv_layout/widgets。
    # Both modes must end up with the same icon geometry: pouch_layout.build runs the same inv_layout/widgets on both.
    assert icons['touch'] == icons['controller'], icons

    # 宿主 dm_pouch.c 每帧会把百宝囊控件挪回布局里的坐标（自定义页开关），位置必须和布局一致。
    # The host dm_pouch.c moves the pouch widgets back to the layout coordinates every frame (Custom-page switch), so its
    # positions must match the layout.
    host = (ROOT / 'dm' / 'host' / 'dm_pouch.c').read_text(encoding='utf-8')
    m = re.search(r'w_move\(find_w\(boxw, "TouchPouchIcon", 1\), gp \? (-?\d+) : 99999, (-?\d+)\)', host)
    assert m, 'dm_pouch.c：找不到 TouchPouchIcon 的挪位调用'
    assert (int(m.group(1)), int(m.group(2))) == icons['touch'][:2], (m.groups(), icons['touch'])
    m = re.search(r'w_move\(find_w\(boxw, "TouchPouch", 1\), gp \? (-?\d+) : 99999, (-?\d+)\)', host)
    assert m, 'dm_pouch.c：找不到 TouchPouch 的挪位调用'
    assert (int(m.group(1)), int(m.group(2))) == BOX[:2], (m.groups(), BOX)

    # 触屏 / 手柄两条产出路径都真的接到了 pouch_layout.build（注释掉一处也不会全绿）。
    # Both production paths (touch and controller) really call pouch_layout.build (commenting one out cannot stay green).
    tree = ast.parse((TOOLS / 'gen_romfs_overlay.py').read_text(encoding='utf-8'))
    calls = [n for n in ast.walk(tree) if isinstance(n, ast.Call) and isinstance(n.func, ast.Attribute)
             and n.func.attr == 'build' and isinstance(n.func.value, ast.Name) and n.func.value.id == 'pouch_layout']
    assert len(calls) == 2, 'gen_romfs_overlay.py：pouch_layout.build 调用数 %d（触屏 / 手柄各一次）' % len(calls)

    wx, wy, ww, wh = WELL
    ix, iy, scale = icons['touch']
    side = pouch_layout.ICON_PX * PX * scale
    print('PASS B109 pouch icon: well %dx%d at (%d,%d), icon %.2f units at (%d,%d) scale %g; margins L/R/T/B %.2f/%.2f/%.2f/%.2f units; hit %dx%d; touch+controller'
          % (ww, wh, wx, wy, side, ix, iy, scale, ix - wx, wx + ww - ix - side, iy - wy, wy + wh - iy - side,
             BOX[2], BOX[3]))
    return 0


if __name__ == '__main__':
    sys.exit(main())
