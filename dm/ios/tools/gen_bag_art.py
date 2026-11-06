#!/usr/bin/env python3
# 大背包物品栏的底图与页签图（构建时从玩家的游戏文件生成到 Gen/assets/NativeHUD/bag，不进仓库；bag_layout.py 只拷）：
#   BagBG = 原版格子图 InventoryBG_Classic（透明底）里的装备框、腰带四格、背包格、金币框按大背包布局重摆：装备带五列、腰带栏在其下居中、背包 12×7。
#   BagTab0 / BagTab1 = 未选页签（原版 AdditionalStash_tabs 第 0 帧 + 压暗的无框图形：皮囊 / 三枚护身符，自绘）；BagTabHi0 / 1 = 选中页签（第 1 帧，带下沿蓝菱形）；BagFocus = 原版手柄焦点框 StashTab_Hover；BagDot = 原版红旗徽 TabNotificationIcon。
# 坐标见 bag_layout.py；1 像素 = 2 单位。用法：gen_bag_art.py <原版 RomFS（Update）>
# Base image and tab images of the large-bag inventory (generated at build time from the player's game files into Gen/assets/NativeHUD/bag, not in the repo; bag_layout.py only copies them):
#   BagBG = the equipment frames, four belt cells, bag cells and gold frame of the original grid image InventoryBG_Classic (transparent background) rearranged for the large bag: five equipment columns, belt row centred beneath, bag 12×7.
#   BagTab0 / BagTab1 = unselected tabs (frame 0 of the original AdditionalStash_tabs + dimmed frameless graphic: pouch / three charms, self-drawn); BagTabHi0 / 1 = selected tabs (frame 1, with the blue diamond on the lower edge); BagFocus = the original controller focus frame StashTab_Hover; BagDot = the original red flag TabNotificationIcon.
# Coordinates are in bag_layout.py; 1 pixel = 2 units. Usage: gen_bag_art.py <original game RomFS (Update)>
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, '..', '..', 'tools'))
import gen_widestash_art as G, dmx, bag_layout as B, bag_icons
from PIL import Image

CELL = 49   # 背包格在原图里的边长（像素） | Bag cell side in the original image (pixels)
GX, GY, GM = 10, 425, 5   # 原图背包格左上角、外框多留的边 | Top-left of the bag cells in the original image, extra margin kept for the outer frame

def px(u, v): return (u - B.BG_U) // 2, (v - B.BG_V) // 2

def grid(src, out):
    # 按格拼：首尾列 / 行取原图的边格（连外框），中间循环取中段，格线和明暗都是原版的 | Assembled cell by cell: first and last columns / rows take the original edge cells (with the outer frame), the middle cycles through the original middle cells
    ox, oy = px(B.GRID[0], B.GRID[1])
    for r in range(B.ROWS):
        sr = 0 if r == 0 else 3 if r == B.ROWS - 1 else 1 + (r - 1) % 2
        for c in range(B.COLS):
            sc = 0 if c == 0 else 9 if c == B.COLS - 1 else 1 + (c - 1) % 8
            l, t, rr, b = GM if c == 0 else 0, GM if r == 0 else 0, GM if c == B.COLS - 1 else 0, GM if r == B.ROWS - 1 else 0
            fr = src.crop((GX + sc * CELL - l, GY + sr * CELL - t, GX + (sc + 1) * CELL + rr, GY + (sr + 1) * CELL + b))
            out.alpha_composite(fr, (ox + c * CELL - l, oy + r * CELL - t))

def main():
    dmx.ROOT = os.path.join(sys.argv[1], 'data'); L = G.lib()
    raw = dmx.get('data/hd/global/ui/controller/panel/inventorypanel/v2/inventorybg_classic.sprite'); src = G.decode(L, raw); assert src.size == (507, 700), '经典版格子图不是 507×700'
    out = Image.new('RGBA', B.BG_SIZE, (0, 0, 0, 0))
    for box, (u, v), size in B.FRAMES:
        fr = src.crop(box)
        if size: fr = fr.resize(size, Image.LANCZOS)
        out.alpha_composite(fr, px(u, v))
    grid(src, out)
    D = __import__('gen_paths').gen_dir('NativeHUD', 'bag')
    def put(name, im): open(os.path.join(D, name + '.sprite'), 'wb').write(G.pack(L, raw, G.bc3(im), im.width, im.height, im.width)); print('%s：%d×%d' % (name, im.width, im.height))
    put('BagBG', out)
    ui = lambda n: G.decode(L, dmx.get('data/hd/global/ui/controller/%s.sprite' % n))
    sheet = ui('panel/stash/v2/additionalstash/additionalstash_tabs'); fh = sheet.height
    frames = [sheet.crop((k * 90, 0, (k + 1) * 90, fh)).resize((B.TAB_W, B.TAB_H), Image.LANCZOS) for k in range(2)]
    btn = G.decode(L, dmx.get('data/hd/global/ui/panel/vendors/repairall_button.sprite')).crop((0, 0, 58, 58))
    side = B.TAB_H - 28   # 图形留出下沿蓝菱形的位置 | The graphic leaves room for the blue diamond on the lower edge
    for k, ic in enumerate(bag_icons.bare(btn)):
        ic = ic.resize((side, side), Image.LANCZOS); pos = ((B.TAB_W - side) // 2, 8)
        dim = Image.eval(ic.convert('RGB'), lambda v: int(v * .62)).convert('RGBA'); dim.putalpha(ic.getchannel('A'))
        off = frames[0].copy(); off.alpha_composite(dim, pos); put('BagTab%d' % k, off)
        on = frames[1].copy(); on.alpha_composite(ic, pos); put('BagTabHi%d' % k, on)
    pad = 2 * B.P['FOCUS_PAD']; put('BagFocus', ui('hoverimages/stashtab_hover').resize((B.TAB_W + pad, B.TAB_H + pad), Image.LANCZOS))
    put('BagDot', ui('panel/tabnotificationicon').resize((43, 55), Image.LANCZOS))

if __name__ == '__main__':
    main()
