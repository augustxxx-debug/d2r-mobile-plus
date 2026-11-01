#!/usr/bin/env python3
# 大背包物品栏（用户 11-01 定稿方案 ④）：背包 12×7，两枚原版外观页签（背包 / 护身符）坐在腰带行左侧、压网格上框，右侧读数；装备带五列不缩放，腰带栏在其下居中，金币行原位。
# 全部是面板内单位（1 像素 = 2 单位，1 格 = 98）。底图 gen_bag_art.py 按这里的表拼；控件矩形写进手柄 Switch 版物品栏布局（两种模式都从它出）。
# 宿主 dm/host/dm_bag.c 管页签、键帽、读数。
# Large-bag inventory (user-approved option 4, 11-01): bag 12×7, two original-style tabs (bag / charms) on the left of the belt row sitting on the grid's top frame, a readout on the right; five unscaled equipment columns, belt row centred beneath, gold row unchanged.
# All values are panel units (1 pixel = 2 units, 1 cell = 98). gen_bag_art.py assembles the base image from these tables; the widget rects go into the controller Switch inventory layout (both modes derive from it).
# The host dm/host/dm_bag.c handles the tabs, keycaps and readout.
import os, re, shutil

# 页签形态的参数和宿主共用一份（dm/host/dm_bag_layout.h），换方案只改那里 | Tab form parameters are shared with the host (dm/host/dm_bag_layout.h); a new design only changes that file
P = {m.group(1): int(m.group(2)) for m in re.finditer(r'#define DM_BAG_(\w+) (-?\d+)', open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'host', 'dm_bag_layout.h'), encoding='utf-8').read())}
COLS, ROWS = P['COLS'], P['ROWS']
GRID = (P['GRID_X'], P['GRID_Y'])
TAB_W, TAB_H = P['TAB_W'], P['TAB_H']
OFF = 99999
BG_U, BG_V = 56, 19                # 底图左上角的面板坐标 | Panel coordinates of the base image's top-left corner
BG_SIZE = (644, 712)
PANEL_C = (704, 743)               # 面板中心（底图 anchor 0.5）| Panel centre (the base image anchors at 0.5)
# 五列外框的左沿 | Left edge of the five outer frame columns
C0, C1, C2, C3, C4 = 70, 330, 590, 850, 1110
BELT_DX, BELT_DY = 0, -263
# 原图框 (左 上 右 下，像素)、新位置（面板单位）、缩放后尺寸（像素，None = 原样） | Original frame box (pixels), new position (panel units), resized size (pixels, None = unchanged)
FRAMES = [
    ((6, 66, 119, 272), (C0, 70), None),            # 右手武器 | right weapon
    ((197, 2, 310, 110), (C1, 33), None),           # 头 | head
    ((6, 272, 119, 385), (C1, 249), None),          # 手套 | gloves
    ((197, 110, 310, 272), (C2, 33), None),         # 甲 | torso
    ((197, 272, 310, 336), (C2, 357), None),        # 腰带 | belt
    ((319, 82, 383, 147), (903, 33), (59, 59)),     # 项链 | amulet
    ((124, 273, 188, 337), (842, 151), (59, 58)),   # 戒指 | ring
    ((321, 273, 385, 337), (964, 151), (59, 58)),   # 戒指 | ring
    ((391, 272, 503, 385), (C3, 267), (112, 104)),  # 鞋 | boots
    ((391, 66, 503, 272), (C4, 70), None),          # 左手武器 | left weapon
    ((126, 351, 381, 410), (450 + BELT_DX, 745 + BELT_DY), None),   # 腰带四格 | four belt cells
    ((152, 625, 353, 693), (500, 1293), None),      # 金币框 | gold frame
]
# 控件矩形 | Widget rects
RECTS = {
    'slot_right_arm': (C0 + 16, 81, 196, 392), 'slot_left_arm': (C4 + 16, 81, 196, 392),
    'slot_head': (C1 + 17, 45, 196, 196), 'slot_gloves': (C1 + 16, 265, 196, 196),
    'slot_torso': (C2 + 17, 51, 196, 294), 'slot_belt': (C2 + 16, 371, 196, 98),
    'slot_neck': (913, 43, 98, 98), 'slot_right_hand': (852, 160, 98, 98), 'slot_left_hand': (974, 160, 98, 98),
    'slot_feet': (C3 + 14, 273, 196, 196),
}
TAB_L, TAB_R = C0 + 4, C4 + 4     # 武器切换页签 | Weapon swap tabs
TAB_Y = 29

def _fields(s, name, frag):
    """给 name 这个控件加字段：已有 fields 就插到最前（后面同名的 rect 先删掉），没有就新建。 | Add fields to the named widget: prepended into an existing fields block (dropping its rect), or created."""
    m = re.search(r'"name"\s*:\s*"%s"\s*,?' % re.escape(name), s)
    if not m: raise SystemExit('bag_layout：物品栏布局里没有 %s（原版布局变了？）' % name)
    tail = s[m.end():]; f = re.match(r'\s*"fields"\s*:\s*\{', tail)
    if f:
        rest = tail[f.end():]
        if '"rect"' in frag: rest = re.sub(r'^(\s*)"rect"\s*:\s*\{[^}]*\}\s*,', r'\1', rest, count=1)
        return s[:m.end()] + tail[:f.end()] + ' ' + frag + ', ' + rest
    return s[:m.end()].rstrip(',') + ', "fields": { ' + frag + ' },' + tail

def rect(x, y, w=None, h=None):
    return '"rect": { "x": %d, "y": %d%s }' % (x, y, '' if w is None else ', "width": %d, "height": %d' % (w, h))

def rects(s):
    bx, by = BG_U - PANEL_C[0], BG_V - PANEL_C[1]
    s = _fields(s, 'background', '"anchor": { "x": 0.5, "y": 0.5 }, "filename": "Controller/Panel/zsbag/BagBG", ' + rect(bx, by))
    s = _fields(s, 'grid', '"cellCount": { "x": %d, "y": %d }, ' % (COLS, ROWS) + rect(*GRID))   # 不写 cellCount 的话命中矩形还是原版 10×4，下面三行、右边两列点不到 | Without cellCount the hit rect stays the original 10×4, so the lower three rows and right two columns cannot be reached
    s = _fields(s, 'Belt', rect(452 + BELT_DX, 750 + BELT_DY))
    for n, r in RECTS.items(): s = _fields(s, n, rect(*r))
    for n, x in (('background_right_arm_selected', TAB_L), ('background_right_arm', TAB_L), ('background_left_arm_selected', TAB_R), ('background_left_arm', TAB_R)): s = _fields(s, n, rect(x, TAB_Y))
    for n, x in (('text_i_left', TAB_L + 12), ('text_i_right', TAB_R + 12)): s = _fields(s, n, rect(x, TAB_Y - 3, 84, 48))
    for n, x in (('text_ii_left', TAB_L + 117), ('text_ii_right', TAB_R + 117)): s = _fields(s, n, rect(x, TAB_Y - 3, 84, 48))
    s = _fields(s, 'WeaponSwapRightLegend', rect(TAB_L + 5, TAB_Y - 65, 197, 48))
    s = _fields(s, 'WeaponSwapLeftLegend', rect(TAB_R + 11, TAB_Y - 65, 197, 48))
    s = _fields(s, 'weaponswap_right_arm', rect(TAB_L, TAB_Y, 216, 59))
    s = _fields(s, 'weaponswap_left_arm', rect(TAB_R, TAB_Y, 216, 59))
    return s

def widgets():
    t = ''
    for k in range(2):
        t += ('            { "type": "ButtonWidget", "name": "TouchBagTab%d", "fields": { "rect": { "x": %d, "y": %d, "width": %d, "height": %d, "scale": 0.5 }, "filename": "Controller/Panel/zsbag/BagTab%d", '
              '"onClickMessage": "PanelManager:ClosePanel:TouchBagTab%d" } },\n') % (k, P['TAB_X'] + k * P['TAB_DX'], P['TAB_Y'] + k * P['TAB_DY'], 2 * TAB_W, 2 * TAB_H, k, k)
    for k in range(2):   # 选中帧盖在未选帧上，宿主只显示当前页那一枚 | The selected frame covers the unselected one; the host shows only the current page's
        t += '            { "type": "ImageWidget", "name": "TouchBagHi%d", "fields": { "rect": { "x": %d, "y": %d, "scale": 0.5 }, "filename": "Controller/Panel/zsbag/BagTabHi%d" } },\n' % (k, P['TAB_X'] if k == 0 else OFF, P['TAB_Y'], k)
    t += '            { "type": "ImageWidget", "name": "TouchBagFocus", "fields": { "rect": { "x": %d, "y": 0, "scale": 0.5 }, "filename": "Controller/Panel/zsbag/BagFocus" } },\n' % OFF
    t += ('            { "type": "TextBoxWidget", "name": "TouchBagCount0", "fields": { "rect": { "x": %d, "y": %d, "width": %d, "height": 40 }, "text": "", '
          '"style": { "fontColor": "$FontColorLightGray", "pointSize": %d, "spacing": "$ReducedSpacing", "alignment": { "h": "right", "v": "center" } } } },\n') % (P['COUNT_X'], P['COUNT_Y'], P['COUNT_W'], P['COUNT_PT'])
    t += '            { "type": "ImageWidget", "name": "TouchBagDot", "fields": { "rect": { "x": %d, "y": %d, "scale": 0.5 }, "filename": "Controller/Panel/zsbag/BagDot" } },\n' % (OFF, 0)
    for nm, act in (('TouchBagKeyL', 'tabLeft'), ('TouchBagKeyR', 'tabRight')):
        t += '            { "type": "ButtonLegendWidget", "name": "%s", "fields": { "rect": { "x": %d, "y": 0, "width": 0, "height": 162 }, "text": { "style": "$StyleButtonLegend" }, "presets": [ { "action": "%s" } ] } },\n' % (nm, OFF, act)
    return t

def art(outdir):
    out = {}
    src = __import__('gen_paths').gen_dir('NativeHUD', 'bag')
    for n in ('BagBG', 'BagTab0', 'BagTab1', 'BagTabHi0', 'BagTabHi1', 'BagFocus', 'BagDot'):
        p = os.path.join(outdir, 'bag_' + n + '.sprite'); shutil.copyfile(os.path.join(src, n + '.sprite'), p); out['+data/hd/global/ui/controller/panel/zsbag/%s.sprite' % n.lower()] = p
    return out
