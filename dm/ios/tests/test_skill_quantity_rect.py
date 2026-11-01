"""技能格数量字的位置（B011，两种模式）。 | Where the count on a skill cell sits (B011, both modes).

原版键鼠档 $SkillQuantityTextRect 在图标下部（6, 85），手柄版底栏用 $SkillQuantityTextRectController（6, 6）。手柄版技能树「物品技能」与技能绑定框的格子
引用的是前者，我们的大字号档 BIG_FONT 把 $MediumFontSize 放到 40 后，回城书次数这类数字整个掉到图标下方、压住按键提示（真机截图：回城书 18）。
本测试要求：BIG_FONT（两份字号档、两种模式共用）把 $SkillQuantityTextRect 改到图标左上，与底栏同一处，字身（40 号约 46 单位高）落在 128 单位的图标格里。
The stock keyboard profile puts $SkillQuantityTextRect at the lower part of the icon (6, 85); the controller bar uses $SkillQuantityTextRectController (6, 6).
The controller skill tree's item skills and the binding box cells refer to the former, and once BIG_FONT sets $MediumFontSize to 40 a tome count drops below the icon
onto the button prompt (device capture: tome 18). This test requires BIG_FONT (shared by both profiles and both modes) to move $SkillQuantityTextRect to the
icon's top left, the bar's spot, with the glyph body (about 46 units at size 40) inside the 128-unit icon cell.
"""
import re
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'ios/tools'))
import touch_layout as layout

prof = ''.join(layout.BIG_FONT[1:])
m = re.search(r'"SkillQuantityTextRect": \{ "x": (-?\d+), "y": (-?\d+) \}', prof)
assert m, 'BIG_FONT 没有改 $SkillQuantityTextRect（B011） | BIG_FONT does not override $SkillQuantityTextRect (B011)'
x, y = int(m.group(1)), int(m.group(2))
size = int(re.search(r'"MediumFontSize": (\d+)', prof).group(1))
assert (x, y) == (6, 6), ('数量字应与手柄底栏同一处（6, 6） | the count belongs at the controller bar spot (6, 6)', x, y)
assert y + size * 1.15 <= 128, ('数量字掉出图标格 | the count falls out of the icon cell', y, size)
assert prof.count('SkillQuantityTextRect') == 1
# 两份字号档都用这一组值（build_profile 对 consolehd / handheldhd 都写 BIG_FONT[1]） | both profiles take this set (build_profile writes BIG_FONT[1] into consolehd and handheldhd)
src = (root / 'ios/tools/touch_layout.py').read_text(encoding='utf-8')
body = src[src.index('def build_profile'):]; body = body[:body.index('\n    return repl')]
assert body.count('BIG_FONT[1]') == 2, '两份字号档都要写 BIG_FONT[1] | both profiles must take BIG_FONT[1]'
print('PASS test_skill_quantity_rect')
