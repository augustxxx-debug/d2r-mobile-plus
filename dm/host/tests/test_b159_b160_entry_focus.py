#!/usr/bin/env python3
"""B159 / B160 回归：手柄从别的页按 R / L 进任务日志、技能树时没有金框（焦点还停在上一页已藏起的控件上）。
实测（ui_regress 9c3c1dff panels_pad）：物品栏 → R → 技能树 FOCUSLOST（按一下方向键才出）；选项 → R → 任务日志 FOCUSLOST，十字键也走不回来。
从角色页按 L 进任务日志时原版自己把焦点放在幕页签条当前那一幕上（p2_fix2 FOCUS 1014,246）。
修：ui_pad_entry_focus_pump —— 手柄模式、两页之一刚成为最上一页（无模态）、焦点（[界面根+0xd0]+0x190）不在本页看得见的控件上时，
光标放到本页起始控件中心（布局 focusOnMouseOver：悬停即得焦点）：任务日志 Tabs 的 Image<选中号> → 当前幕第一个任务格；技能树 Tab<当前页> → 第一个技能格。
放下后隔几帧核焦点进没进页，没进再试下一个；只在进页 60 帧内；焦点已在本页（原版自己给了）就不动。
B159 / B160 regression: entering the quest log / skill tree with R / L from another page in controller mode left the focus on a hidden widget of the previous page (no gold frame).
Now the host hovers the page's starting widget (quest log: the current act tab, then the first quest; skill tree: the current skill tab, then the first skill) within 60 frames of entry,
checks the focus landed on the page before stopping, and leaves entry paths where the game already focused the page alone.
"""
import os
import re
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
DM = HERE.parents[1]
CUR = (DM / 'host' / 'dm_cursor.c').read_text(encoding='utf-8')


def code(s):
    return re.sub(r'//[^\n]*', '', re.sub(r'/\*.*?\*/', '', s, flags=re.S))


def body(src, name):
    m = re.search(r'\n(?:static\s+)?(?:inline\s+)?(?:void|int|uint64_t)\s+%s\([^;{]*\)\s*\{' % re.escape(name), src)
    if not m: return ''
    i, d = m.end() - 1, 0
    for j in range(i, len(src)):
        d += src[j] == '{'; d -= src[j] == '}'
        if d == 0: return code(src[i:j + 1])
    return ''


class EntryFocus(unittest.TestCase):
    def setUp(self):
        self.b = body(CUR, 'ui_pad_entry_focus_pump')
        self.c = body(CUR, 'ef_candidate')
        self.assertTrue(self.b, '没有 ui_pad_entry_focus_pump')

    def test_called_every_frame(self):
        self.assertIn('ui_pad_entry_focus_pump(main_module(), root);', body(CUR, 'ui_panel_state_pump'))

    def test_both_pages(self):
        self.assertIn('"QuestLogPanelExpansion", "SkillsTreePanel"', self.b)
        self.assertIn('root_panel_shown(root, pn[i])', self.b)

    def test_controller_only_and_modal(self):
        self.assertRegex(self.b, r'if \(!m \|\| !root \|\| !dm_padmode_current\(\)\) \{ last = -1; return; \}')   # 触屏清零 | touch mode clears the state
        self.assertIn('modal_any(root, 1, NULL)', self.b)

    def test_focus_check_is_on_page(self):
        self.assertIn('*(uint64_t *)(root + 0xd0)', self.b)
        self.assertIn('*(uint64_t *)(fm + 0x190)', self.b)
        self.assertRegex(self.b, r'if \(p && EF_VIS\(f\) && ui_has\(p, f, 0\)\) \{ done = 1;')   # 原版已给焦点就不动 | game already focused: untouched

    def test_entry_window_and_retry(self):
        self.assertIn('if (page != last) { last = page; since = fr; k = 0; done = 0; tried_at = 0; }', self.b)
        self.assertIn('if (fr - since > 60) { done = 1; return; }', self.b)
        self.assertIn('if (tried_at && fr - tried_at < 4) return;', self.b)
        self.assertIn('k > 1', self.b)
        self.assertRegex(self.b, r'if \(!c \|\| !ui_hit_center\(m, c, &x, &y\)\)')       # 落点核过才放 | hit-checked before hovering
        self.assertIn('if (!dm_ui_hover_now(x, y)) return;', self.b)

    def test_candidates(self):
        self.assertIn('ui_find(p, "Tabs", 0)', self.c)
        self.assertIn('TAB_SEL(tabs)', self.c)
        self.assertIn('"Image%d", sel', self.c)
        self.assertIn('ef_first(tw, "Quest", 0)', self.c)
        self.assertIn('bd_tab_cur(p)', self.c)
        self.assertIn('ef_first(tw, "SkillButton", 0)', self.c)

    def test_no_keys_taken(self):
        self.assertNotIn('page_keys', self.b)   # 不收任何键：方向键照旧归原版 | takes no keys
        self.assertNotIn('stick', self.b)


class Layout(unittest.TestCase):
    """布局里起始控件悬停即得焦点（focusOnMouseOver），有幕页签条 Tabs。缺 romfs 解包就跳过。"""
    def test_layout(self):
        root = Path(os.environ.get('DM_LAYOUTS_DIR', '')) / 'controller'   # 解包的布局目录由环境变量给，不在源码里写本机路径 | extracted layouts dir from the environment, no local path in the source
        q, s = root / 'questlogpanelexpansionhd.json', root / 'skillstreepanelhd.json'
        if not (q.exists() and s.exists()): self.skipTest('no extracted layouts')
        qt, st = q.read_text(encoding='utf-8-sig'), s.read_text(encoding='utf-8-sig')
        self.assertRegex(qt, r'"name"\s*:\s*"Tabs"'); self.assertIn('"tabCount": 5', re.sub(r'\s+', ' ', qt).replace('"tabCount" :', '"tabCount":'))
        self.assertGreaterEqual(st.count('"focusOnMouseOver": true'), 5)


if __name__ == '__main__':
    unittest.main()
