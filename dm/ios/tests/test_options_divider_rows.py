"""选项页分组分割线与行距（两种模式）。 | Options-page section dividers versus row pitch (both modes).

原版分组标题下的金色分割线（标题控件的子控件 Divider，rect = $OptionsDividerRect）原版在 y 90、配 100 行距；我们的大字号档 BIG_FONT 把它挪到 y 150
（配触屏 180 行距），这个档两种模式共用，于是仍用 100 行距的原版表格分割线落进下一行文字（用户报：安卓手柄模式音频页「分组金黄色分割线乱了」；iOS 手柄模式同样）。
触屏版三页早就改成 180（热区）；手柄版只有「游戏设定」改了，「音频」「游戏」漏了。本测试要求：音频 / 游戏 / 游戏设定三页在两种模式下，
带 Divider 的表格行距一律 180。
The gold divider under a section title (child Divider of the title, rect = $OptionsDividerRect) sits at y 90 with a 100 pitch in the stock game; our large-font
profile BIG_FONT moves it to y 150 (for the touch 180 pitch), and that profile is shared by both modes, so stock tables still at 100 put the divider into the next
row's text (user report: Android controller-mode Audio page; iOS controller mode alike). The touch copies of the three pages were already at 180 (hit areas); of the controller copies only Game Settings was, Audio and
Gameplay were missed. This test requires every Divider-bearing table of the three pages to use row pitch 180 in both modes.
"""
import json
import re
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'ios/tools'))
import touch_layout as layout

PAGES = ('soundoptionspanelhd.json', 'gameplayoptionspanelhd.json', 'onlinegameoptionspanelhd.json')
title = {'type': 'TableRowWidget', 'name': 'Row Volume', 'children': [{'type': 'TextBoxWidget', 'name': 'Volume', 'fields': {'text': '@Volume', 'style': '$StyleSettingsTitle'},
         'children': [{'type': 'ImageWidget', 'name': 'Divider', 'fields': {'rect': '$OptionsDividerRect', 'filename': 'PauseMenu\\Divider'}}]}]}
row = {'type': 'TableRowWidget', 'name': 'Row SoundVolume', 'children': [{'type': 'SliderWidget', 'name': 'SoundVolume', 'fields': {'onUpdateMessage': 'OptionsPanelMessage:Sound'}}]}
# 生成器会删掉的原版行（触屏、手柄两份清单）必须恰好各有一行：从生成器源码里读出全部行名放进合成页 | the stock rows the generator removes must exist exactly once: read every name from its source
src = (root / 'ios/tools/touch_layout.py').read_text(encoding='utf-8')
def drop_calls(text):   # 按括号深度取每个 _drop_rows(...) 的实参，跳过 # 注释 | the arguments of every _drop_rows(...) by paren depth, skipping # comments
    code = '\n'.join(line.split('#', 1)[0] for line in text.splitlines())
    for m in re.finditer(r'_drop_rows\(', code):
        i, depth = m.end(), 1
        while depth: depth += {'(': 1, ')': -1}.get(code[i], 0); i += 1
        yield code[m.end():i - 1]
dropped = sorted({n for call in drop_calls(src) for n in re.findall(r"'([^']+)'", call)})
extra = [{'type': 'TableRowWidget', 'name': 'Row ' + n} for n in dropped]
# 照原版结构：滚动区 ScrollView 里放原版表格 OptionsTable（生成器可能在表格前插我们自己的分节，如游戏页的难度块 TouchDiffRows）
# Stock structure: the native table OptionsTable inside the ScrollView (the generator may insert our own sections before it, e.g. the Gameplay page's TouchDiffRows)
stock = {'type': 'Panel', 'name': 'Panel', 'children': [{'type': 'ScrollViewWidget', 'name': 'ScrollView', 'fields': {'fitToParent': True}, 'children': [
    {'type': 'TableWidget', 'name': 'OptionsTable', 'fields': {'rowHeight': '$OptionsTableRowHeight'}, 'children': [title, row] + extra}]}]}

def walk(node, out):
    if isinstance(node, dict):
        out.append(node)
        for c in node.get('children', []): walk(c, out)
    return out
def has_divider(node): return any(c.get('name') == 'Divider' for c in walk(node, [])[1:])

# 几何不变量（三审 A3-01）：分割线 y 取自大字号档；它要在标题字底（标题在行内垂直居中，40 号字底约 y 110）之下、行距之内
# Geometric invariant (third audit A3-01): the divider y comes from the large-font profile; it must lie below the title text (titles are centred in the row, the
# bottom of 40-pt text is about y 110) and inside the row pitch
m = re.search(r'"OptionsDividerRect": \{ "x": 0, "y": (\d+) \}', ''.join(layout.BIG_FONT))
assert m, 'large-font profile no longer moves OptionsDividerRect'
DIV_Y, TITLE_BOTTOM = int(m.group(1)), 110
assert TITLE_BOTTOM <= DIV_Y < 180, 'divider y %d must lie between the title text bottom %d and the 180 row pitch' % (DIV_Y, TITLE_BOTTOM)

fails = []
for mode, table in (('touch', layout.PRESENT), ('controller', layout.PAD_ROOT)):
    for page in PAGES:
        if page not in table: fails.append('%s %s: page not generated' % (mode, page)); continue
        text, _ = table[page](json.dumps(stock))
        doc = json.loads(re.sub(r',\s*([}\]])', r'\1', text))
        nodes = walk(doc, [])
        native = [n for n in nodes if n.get('name') == 'OptionsTable']   # 按名字认原版表格，前面有我们的分节也照认 | the native table by name, our sections before it are fine
        if len(native) != 1: fails.append('%s %s: %d native OptionsTable' % (mode, page, len(native))); continue
        rh = native[0]['fields'].get('rowHeight')
        if has_divider(native[0]) and (rh != 180 or not isinstance(rh, int) or rh <= DIV_Y):
            fails.append('%s %s: native table rowHeight %r with a section divider at y %d' % (mode, page, rh, DIV_Y))
        # 我们自己插的分节（Touch 开头，如 iOS RC1 游戏页的 TouchDiffRows；本分支三页没有，并线后才有覆盖）若带分割线，标题框高度要大于分割线 y（三审 A3-03）
        # our own Touch* sections with a divider (e.g. iOS RC1's TouchDiffRows on the Gameplay page; none on this branch, covered after the merge) need a title box taller than the divider y (third audit A3-03)
        for n in nodes:
            kids = n.get('children', [])
            if str(n.get('name', '')).startswith('Touch') and any(c.get('name') == 'Divider' for c in kids):
                h = n.get('fields', {}).get('rect', {}).get('height')
                if not isinstance(h, int) or h <= DIV_Y: fails.append('%s %s: our section title %s height %r with a divider at y %d' % (mode, page, n['name'], h, DIV_Y))
for f in fails: print('FAIL', f)
assert not fails, '%d page(s) keep a row pitch that misplaces the section divider' % len(fails)
print('PASS options dividers: divider y %d lies between the title text and the 180 pitch; Audio / Gameplay / Game Settings native tables use 180 in touch and controller mode' % DIV_Y)
