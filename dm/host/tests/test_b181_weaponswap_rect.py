#!/usr/bin/env python3
# B181：大背包物品栏布局（bag_layout.rects）挪控件时，原有的 rect 不管排在 fields 第几个都要换掉，不许新旧两个并存。
#   原版 weaponswap_right_arm / weaponswap_left_arm（可点的换武器按钮）的 rect 排在 focusOnMouseOver、focusIndicatorVisible 之后；
#   原先只删排第一个的 rect，旧值留在后面生效，按钮留在原位、不在 I / II 页签上，触屏点页签不换武器。
#   这里用同样字段顺序的最小布局喂 rects()：每个被挪的控件 fields 里只能有一个 rect，值是 bag_layout 定的那个；
#   换武器按钮与页签底图同一位置；fields 里嵌套对象里的 rect 不动。
# B181: when the large-bag inventory layout (bag_layout.rects) moves a widget, its existing rect must be replaced wherever it sits in fields;
#   the old and new rects must never coexist. The original weaponswap_right_arm / weaponswap_left_arm (the clickable weapon-swap buttons) list
#   rect after focusOnMouseOver and focusIndicatorVisible; previously only a leading rect was removed, the old value stayed later and won, the buttons
#   stayed off the I / II tabs, and tapping the tabs in touch mode did not swap weapons. A minimal layout with the same field order is fed to rects():
#   each moved widget must have exactly one rect, equal to bag_layout's value; the weapon-swap buttons sit where the tab backgrounds are; a rect in a
#   nested object inside fields is left alone.
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'dm' / 'ios' / 'tools'))
import bag_layout

FILE_JSON, TEXT_JSON = r'PANEL\\HUD_02\\x', r'a \" b { \\ c'   # JSON 原文；单个转义引号，扫描若不认转义会从此奇偶颠倒、漏删后面真的 rect | raw JSON text; a single escaped quote: a scan that ignores escapes flips parity here and misses the real rect after it
NAMES = ('background', 'grid', 'Belt', 'slot_right_arm', 'slot_left_arm', 'slot_head', 'slot_gloves', 'slot_torso',
         'slot_belt', 'slot_neck', 'slot_right_hand', 'slot_left_hand', 'slot_feet', 'background_right_arm_selected',
         'background_right_arm', 'background_left_arm_selected', 'background_left_arm', 'text_i_left', 'text_i_right',
         'text_ii_left', 'text_ii_right', 'WeaponSwapRightLegend', 'WeaponSwapLeftLegend', 'weaponswap_right_arm',
         'weaponswap_left_arm')
fails = 0


def check(ok, what):
    global fails
    if not ok:
        fails += 1
        print('FAIL', what)


def source(order):
    # order 0：rect 排第一；1：rect 排在两个别的字段后面（原版换武器按钮的写法）；2：前面还有个带 rect 的嵌套对象；
    # 3：字符串里带转义引号与反斜杠（原版布局的贴图路径写成 PANEL\\HUD_02\\…）；4：fields 里没有 rect，紧跟着一个不挪的兄弟控件带 rect（它的 rect 不许被当成前一个的删掉）
    # order 0: rect first; 1: rect after two other fields (as in the original weapon-swap buttons); 2: a nested object holding its own rect comes first;
    # 3: escaped quotes and backslashes inside strings (original texture paths read PANEL\\HUD_02\\...); 4: no rect in fields, followed by an unmoved sibling with a rect (which must not be removed as if it were the previous one's)
    def fields(i):
        r = '"rect": { "x": %d, "y": %d, "width": 7, "height": 9 },' % (50000 + i, 50000 + i)
        if order == 0: return '{ %s "focusOnMouseOver": true, }' % r
        if order == 1: return '{ "focusOnMouseOver": true, "focusIndicatorVisible": false, %s }' % r
        if order == 2: return '{ "style": { "rect": { "x": 1, "y": 2 } }, "focusOnMouseOver": true, %s }' % r
        if order == 3: return '{ "filename": "%s", "text": "%s", "focusOnMouseOver": true, %s }' % (FILE_JSON, TEXT_JSON, r)
        return '{ "focusOnMouseOver": true, }'
    kid = lambda i, n: '{ "type": "Widget", "name": "%s", "fields": %s }' % (n, fields(i))
    keep = lambda n: ',\n{ "type": "Widget", "name": "keep_%s", "fields": { "focusOnMouseOver": true, "rect": { "x": 777, "y": 777 }, } }' % n if order == 4 else ''
    kids = ',\n'.join(kid(i, n) + keep(n) for i, n in enumerate(NAMES))
    return '{ "type": "Widget", "name": "Root", "children": [\n%s,\n] }' % kids


def parse(text):
    dup = []

    def hook(pairs):
        if [k for k, _ in pairs].count('rect') > 1: dup.append(pairs)
        return dict(pairs)
    root = json.loads(re.sub(r',\s*([}\]])', r'\1', text), object_pairs_hook=hook)
    return {c['name']: c['fields'] for c in root['children']}, dup


for order in (0, 1, 2, 3, 4):
    f, dup = parse(bag_layout.rects(source(order)))
    check(not dup, 'order %d：fields 里有两个 rect（%d 处）' % (order, len(dup)))
    for n in NAMES:
        r = f[n].get('rect', {})
        check(r and r.get('x', 0) < 50000, 'order %d：%s 的旧 rect 还在（%s）' % (order, n, r))
        check(f[n].get('focusOnMouseOver') is True, 'order %d：%s 的别的字段丢了' % (order, n))
    for btn, tab in (('weaponswap_right_arm', 'background_right_arm'), ('weaponswap_left_arm', 'background_left_arm')):
        b, t = f[btn]['rect'], f[tab]['rect']
        check((b['x'], b['y'], b['width'], b['height']) == (t['x'], t['y'], 216, 59), 'order %d：%s %s 不在页签 %s 上' % (order, btn, b, t))
    if order == 3:
        w = f['weaponswap_right_arm']; check((w['filename'], w['text']) == (json.loads('"%s"' % FILE_JSON), json.loads('"%s"' % TEXT_JSON)), 'order 3：字符串被改动了（%s）' % w)
    if order == 4:
        check(all(f['keep_' + n].get('rect') == {'x': 777, 'y': 777} for n in NAMES), 'order 4：不挪的兄弟控件的 rect 被删了')
    if order == 2:
        check(f['weaponswap_right_arm']['style'] == {'rect': {'x': 1, 'y': 2}}, 'order 2：嵌套对象里的 rect 被动了')

print('test_b181_weaponswap_rect: %d fail' % fails)
sys.exit(1 if fails else 0)
