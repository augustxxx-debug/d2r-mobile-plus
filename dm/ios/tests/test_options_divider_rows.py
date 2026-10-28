"""选项页分组分割线与行距（两种模式）。 | Options-page section dividers versus row pitch (both modes).

原版分组标题下的金色分割线（标题控件的子控件 Divider，rect = $OptionsDividerRect）在本宿主里要配 180 单位的行距才落在「标题行与下一行之间」；
原版 100 单位行距时它落在下一行文字中间（用户报：安卓手柄模式音频页「分组金黄色分割线乱了」；iOS 手柄模式同样，Mac 触屏 180 行距正常）。
触屏版三页早就改成 180（热区）；手柄版只有「游戏设定」改了，「音频」「游戏」漏了。本测试要求：音频 / 游戏 / 游戏设定三页在两种模式下，
带 Divider 的表格行距一律 180。
The gold divider under a section title (child Divider of the title, rect = $OptionsDividerRect) only lands between the title row and the next row with a
180-unit row pitch in this host; with the stock 100 it crosses the next row's text (user report: Android controller-mode Audio page; iOS controller mode is the
same, Mac touch at 180 is fine). The touch copies of the three pages were already at 180 (hit areas); of the controller copies only Game Settings was, Audio and
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

fails = []
for mode, table in (('touch', layout.PRESENT), ('controller', layout.PAD_ROOT)):
    for page in PAGES:
        if page not in table: fails.append('%s %s: page not generated' % (mode, page)); continue
        text, _ = table[page](json.dumps(stock))
        doc = json.loads(re.sub(r',\s*([}\]])', r'\1', text))
        nodes = walk(doc, [])
        native = [n for n in nodes if n.get('name') == 'OptionsTable']   # 按名字认原版表格，前面有我们的分节也照认 | the native table by name, our sections before it are fine
        if len(native) != 1: fails.append('%s %s: %d native OptionsTable' % (mode, page, len(native))); continue
        if has_divider(native[0]) and native[0]['fields'].get('rowHeight') != 180:
            fails.append('%s %s: native table rowHeight %r with a section divider' % (mode, page, native[0]['fields'].get('rowHeight')))
        # 我们自己插的分节（Touch 开头）若带分割线，它的标题框高度也得是 180，分割线才落在标题与下一行之间 | our own Touch* sections with a divider need a 180-tall title box too
        for n in nodes:
            kids = n.get('children', [])
            if str(n.get('name', '')).startswith('Touch') and any(c.get('name') == 'Divider' for c in kids):
                h = n.get('fields', {}).get('rect', {}).get('height')
                if h != 180: fails.append('%s %s: our section title %s height %r with a divider' % (mode, page, n['name'], h))
for f in fails: print('FAIL', f)
assert not fails, '%d page(s) keep a row pitch that misplaces the section divider' % len(fails)
print('PASS options dividers: Audio / Gameplay / Game Settings native tables use row pitch 180 in touch and controller mode; our own divider sections are 180 tall')
