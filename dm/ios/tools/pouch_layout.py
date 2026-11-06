#!/usr/bin/env python3
# 百宝囊框与「存放」按钮（宿主 dm/host/dm_pouch.c）：物品栏面板（手柄 Switch 版布局）底部金币框左右各加一个我们自己的控件。
#   左：百宝囊框 = 原版金币框那张底（gen_pouch_art.py 从物品栏底图裁、压窄）+ 原版符文物品图 + 件数；点它宿主出清单（原版说明框样式）。
#   右：「存放」= 原版前端小按钮图 FrontEnd_ButtonSmall + 原版字符串 @strGoldDeposit（存放）。
# 两个按钮都发 PanelManager:ClosePanel:TouchPouch / TouchStore（空操作，宿主当信号收，同编年史页子页签）。全包在一个容器 TouchPouchBox 里（面板直接子控件数要 ≤ 256，见 touch_layout._RW）。
# 坐标：物品栏底图摆在面板 (38 + …) 处、1 像素 = 2 单位，金币框外圈在单位 x 512–906、y 1294–1422；网格 x 216–1196。框与按钮各占金币框两侧的空当。
# 宿主按「自定义」页开关把整个容器挪到屏外（关）或挪回（开）
# Pouch box and 「存放」(Deposit) button (host dm/host/dm_pouch.c): one widget of our own is added on each side of the gold box at the bottom of the inventory panel (controller Switch layout).
#   Left: pouch box = the original game's gold box backing (cropped from the inventory base image and narrowed by gen_pouch_art.py) + the original rune item image + piece count; tapping it makes the host show a list (in the original game's tooltip box style).
#   Right: 「存放」(Deposit) = the original front-end small button image FrontEnd_ButtonSmall + the original string @strGoldDeposit (Deposit).
# Both buttons send PanelManager:ClosePanel:TouchPouch / TouchStore (a no-op; the host receives it as a signal, same as the chronicle page's sub-tabs). Everything is wrapped in one container TouchPouchBox (the panel's direct child widget count must be ≤ 256, see touch_layout._RW).
# Coordinates: the inventory base image is placed at (38 + ...) in the panel, 1 pixel = 2 units, the gold box's outer ring is at units x 512–906, y 1294–1422; grid x 216–1196. The box and the button each occupy the gap on either side of the gold box.
# The host moves the whole container off screen (off) or back (on) according to the switch on the 「自定义」(Custom) page
import math, os, shutil
import touch_layout, bag_layout

HERE = os.path.dirname(os.path.abspath(__file__))
INV = 'controller/console/switch/playerinventoryexpansionlayouthd.json'
TIP = 99999   # 说明框缺省在屏外，宿主按字的大小摆 | The tooltip box is off screen by default; the host positions it according to the size of the text

# 百宝囊框（B109）：按钮格 = 贴图原尺寸 140×64 像素 = 280×128 布局单位。框内凹槽在 PouchBox.sprite 里逐行量得：
# 左右各内缩 18 像素、上内缩 10 像素、下内缩 22 像素（凹槽内沿到外框金边），即 104×32 像素 = 208×64 单位。
# 符文图 el_rune 原版帧 49×49 像素（同 touch_layout.RF_MS 量到的 zod_rune），1 像素 = 2 单位、scale 0.53 → 51.94 单位见方；
# 距凹槽每边 ≥ 8%（左 17 / 上 6 / 下 6.06 单位），不再压到金边。触屏命中区仍是整块 280×128 的 TouchPouch，不随图缩小。
# Pouch box (B109): the button cell is the texture's natural size 140x64 px = 280x128 layout units. Measured row by row on PouchBox.sprite,
# the inner well is inset 18 px left/right, 10 px top and 22 px bottom from the outer gold edge, i.e. 104x32 px = 208x64 units.
# The rune image el_rune has a 49x49 px original frame (same as the zod_rune measured in touch_layout.RF_MS); 1 px = 2 units, scale 0.53 -> 51.94 units square,
# leaving >= 8% of the well on every side (left 17 / top 6 / bottom 6.06 units) instead of riding on the gold border. The touch hit area stays the whole 280x128
# TouchPouch button and does not shrink with the icon.
PX = 2                       # 1 像素 = 2 布局单位 | 1 pixel = 2 layout units
BOX = (222, 1294, 280, 128)  # TouchPouch 命中区 / 格子 | TouchPouch hit area / cell
WELL_INSET_PX = (18, 10, 18, 22)   # 贴图内凹槽相对外框的内缩（左 上 右 下，像素）| inner-well insets from the outer frame (left top right bottom, px)
ICON_PX = 49                 # el_rune 原版帧边长（像素）| el_rune original frame side (px)
ICON_SCALE = 0.53            # 49×2×0.53 = 51.94 单位，见上 | 49x2x0.53 = 51.94 units, see above
MIN_MARGIN = 0.08            # 图标到凹槽每边的最小边距（占该边尺寸）| minimum margin per side (fraction of that side)

def well_rect():
    """框内凹槽（图标可占的格子），布局单位。 | Inner well of the frame (the cell the icon may occupy), layout units."""
    x, y, w, h = BOX; l, t, r, b = WELL_INSET_PX
    return (x + l * PX, y + t * PX, w - (l + r) * PX, h - (t + b) * PX)

def icon_rect():
    """符文图矩形 (x, y, side, side)，布局单位；左上角贴凹槽左边/上边留最小边距。 | Rune rect (x, y, side, side) in layout units; top-left keeps the minimum margin from the well's left/top."""
    wx, wy, ww, wh = well_rect()
    side = ICON_PX * PX * ICON_SCALE
    return (wx + math.ceil(ww * MIN_MARGIN), wy + math.ceil(wh * MIN_MARGIN), side, side)

# 药水叠数（宿主 dm_stack.c 按格摆，平时在屏外）与拆分框：副文 28 号、金色，右下对齐
# Potion counts (placed per cell by the host dm_stack.c, off screen otherwise) and the split box: 28 pt secondary text, gold, bottom right
def _qty_widgets():
    t = ''
    for i in range(24):
        t += ('            { "type": "TextBoxWidget", "name": "TouchQtyN%02d", "fields": { "rect": { "x": %d, "y": 0, "width": 90, "height": 44 }, "text": "", '
              '"style": { "fontColor": "$FontColorGoldYellow", "pointSize": "$MediumFontSize", "spacing": "$ReducedSpacing", "dropShadow": "$DefaultDropShadow", "alignment": { "h": "right", "v": "bottom" } } } },\n') % (i, TIP)
    t += ('            { "type": "NineTileImageWidget", "name": "TouchQtyBg", "fields": { "rect": { "x": %d, "y": 0, "width": 560, "height": 340 }, "filename": "PANEL/TooltipBorder/TooltipBorder", "backgroundColor": [0.0, 0.0, 0.0, 0.92], "tileScale": 1.0 } },\n'
          '            { "type": "TextBoxWidget", "name": "TouchQtyTitle", "fields": { "rect": { "x": %d, "y": 0, "width": 520, "height": 60 }, "text": "", '
          '"style": { "fontColor": "$FontColorLightGold", "pointSize": 36, "spacing": "$ReducedSpacing", "alignment": { "h": "center", "v": "center" } } } },\n') % (TIP, TIP)   # 框标题 §2.6 正文 36 | box title §2.6 body 36
    for n, key in (('TouchQtyOne', '@dmStackOne'), ('TouchQtyHalf', '@dmStackHalf'), ('TouchQtyAll', '@dmStackAll'), ('TouchQtySellOne', '@dmStackSellOne'), ('TouchQtySellHalf', '@dmStackSellHalf'), ('TouchQtySellAll', '@dmStackSellAll'), ('TouchQtyBuyOne', '@dmStackBuyOne'), ('TouchQtyBuyTen', '@dmStackBuyTen'), ('TouchQtyBuyFull', '@dmStackBuyFull')):
        t += ('            { "type": "ButtonWidget", "name": "%s", "fields": { "rect": { "x": %d, "y": 0, "width": 700, "scale": 0.74 }, "filename": "FrontEnd/HD/Final/FrontEnd_ButtonSmall", "hoveredFrame": 1, '
              '"textString": "%s", "textColor": "$LightButtonTextColor", "text/style": %s, "onClickMessage": "PanelManager:ClosePanel:%s" } },\n') % (n, TIP, key, QTY_BTN_STYLE, n)
    # 手柄键帽：原版提示条控件 ButtonLegendWidget 自己按当前手柄出图（Xbox / PlayStation / Switch 键帽同原版其它面板，按动作取键、不按字母），只要键帽不要字；
    # 宿主 dm_stack.c 在手柄模式把它们摆到三个按钮左边，触屏模式收到屏外（同大背包页签的 ZL / ZR 键帽 TouchBagKeyL/R）
    # Controller keycaps: the original legend widget draws the glyph for the current controller itself (Xbox / PlayStation / Switch, like the game's other
    # panels, by action rather than by letter), glyph only; the host moves them left of the three buttons in controller mode and off screen in touch mode
    for n, act in QTY_KEYS:
        t += '            { "type": "ButtonLegendWidget", "name": "%s", "fields": { "rect": { "x": %d, "y": 0, "width": 0, "height": %d }, "text": { "style": "$StyleButtonLegend" }, "presets": [ { "action": "%s" } ] } },\n' % (n, TIP, QTY_KEY_H, act)
    return t
# 数量框按钮字：§2.6 副文 28 号（下限）。36 号时西语「Separar mitad / Vender todo」、俄语「Разделить все」压到按钮两端的框（六语截图 *_L2 / *_L3 断言），
# 布局不分语言，所以统一取 28；按钮上不再写 (A)/(Y)/(X)，键帽由 QTY_KEYS 出
# Quantity box button text: §2.6 secondary 28 pt (the floor). At 36 the Spanish / Russian labels ran into the button's end frame (six-language screenshot assertions),
# and the layout is language independent, so 28 for all; no more (A)/(Y)/(X) in the text, the keycaps come from QTY_KEYS
QTY_BTN_STYLE = '{ "fontColor": "$LightButtonTextColor", "pointSize": 28, "dropShadow": "$DefaultDropShadow", "spacing": "$ReducedSpacing" }'
# 三档对应的原版动作：第 1 档 A（select）、第 2 档 Y、第 3 档 X（option1 / option2 哪个是 X 以实机出图为准，见 dm_stack.c 键位）
# The original actions for the three modes: mode 1 A (select), mode 2 Y, mode 3 X
# 键帽高度（布局单位）：暂定，等 ui-pad-spec 规范；位置在 dm_stack_rule.h DMSTK_KEY_DX / DY | keycap height (layout units): provisional until the ui-pad-spec spec; position in dm_stack_rule.h
QTY_KEY_H = 72
QTY_KEYS = (('TouchQtyKeyA', 'select'), ('TouchQtyKeyY', 'option1'), ('TouchQtyKeyX', 'option2'))

def widgets():
    bx, by, bw, bh = BOX; ix, iy = icon_rect()[:2]
    return ('        { "type": "Widget", "name": "TouchPouchBox", "children": [\n'
            '            { "type": "ButtonWidget", "name": "TouchPouch", "fields": { "rect": { "x": %d, "y": %d, "width": %d, "height": %d }, "filename": "Controller/Panel/zspouch/PouchBox", '
            '"onClickMessage": "PanelManager:ClosePanel:TouchPouch" } },\n'
            '            { "type": "ImageWidget", "name": "TouchPouchIcon", "fields": { "rect": { "x": %d, "y": %d, "scale": %s }, "filename": "Items/misc/rune/el_rune" } },\n' % (bx, by, bw, bh, ix, iy, '%g' % ICON_SCALE) +
            '            { "type": "TextBoxWidget", "name": "TouchPouchCount", "fields": { "rect": { "x": 350, "y": 1294, "width": 140, "height": 128 }, "text": "0", '
            '"style": { "fontColor": "$FontColorGoldYellow", "pointSize": "$MediumLargeFontSize", "alignment": { "h": "left", "v": "center" } } } },\n'
            '            { "type": "ButtonWidget", "name": "TouchStore", "fields": { "rect": { "x": 916, "y": 1323, "width": 366, "scale": 0.74 }, "filename": "FrontEnd/HD/Final/FrontEnd_ButtonSmall", "hoveredFrame": 1, '
            '"textString": "@strGoldDeposit", "textColor": "$LightButtonTextColor", "text/style": "$StyleFEButtonText", "onClickMessage": "PanelManager:ClosePanel:TouchStore" } },\n'
            '            { "type": "NineTileImageWidget", "name": "TouchPouchTipBg", "fields": { "rect": { "x": %d, "y": 0, "width": 700, "height": 400 }, "filename": "PANEL/TooltipBorder/TooltipBorder", "backgroundColor": [0.0, 0.0, 0.0, 0.85], "tileScale": 1.0 } },\n'
            '            { "type": "TextBoxWidget", "name": "TouchPouchTip", "fields": { "rect": { "x": %d, "y": 0, "width": 900, "height": 600 }, "text": "", "fitToText": true, '
            '"style": { "fontColor": "$FontColorWhite", "pointSize": "$TooltipFontSize", "alignment": { "h": "center", "v": "top" }, "options": { "newlineHandling": "standard" } } } },\n' % (TIP, TIP)
            + _qty_widgets() + bag_layout.widgets() +
            '        ] },\n')

def inv_layout(s):
    if '"TouchPouchBox"' in s: raise SystemExit('pouch_layout：物品栏布局里已经有 TouchPouchBox')
    i = s.rstrip().rfind(']'); assert s.rstrip().endswith('}') and i > 0
    return bag_layout.rects(s[:i].rstrip(' ') + widgets() + '    ' + s[i:])

def build(romfs, outdir, repl):
    out = {}
    p = os.path.join(outdir, 'ws_pouchbox.sprite'); shutil.copyfile(os.path.join(__import__('gen_paths').gen_dir('NativeHUD', 'pouch'), 'PouchBox.sprite'), p); out['+data/hd/global/ui/controller/panel/zspouch/pouchbox.sprite'] = p
    out.update(bag_layout.art(outdir))
    logical = touch_layout.read_layout(romfs, INV)[0].lower(); src = repl.get(logical)
    text = open(src, encoding='utf-8').read() if src else touch_layout.read_layout(romfs, INV)[1]   # 叠在触摸布局已改的那份上（符文之语清单等） | Stacked on the copy already modified by the touch layout (runeword list etc.)
    p = os.path.join(outdir, 'pouch_' + os.path.basename(INV)); open(p, 'w', encoding='utf-8').write(inv_layout(text)); out[logical] = p
    return out, {'files': len(out)}
