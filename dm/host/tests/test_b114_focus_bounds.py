#!/usr/bin/env python3
"""B114 回归：手柄模式任务日志、角色页金框离开屏幕（FOCUSLOST）。

实测（Mac 宿主 p2_base，dm-work/run/bugs/padfix-opus-20261104）：
- 任务日志：十字键在任务格（Quest1–6）间走、金框在；左摇杆是原版自由光标，推出任务格焦点落到 ClickCatcher，金框没了。
  修：同 B071，任务日志是最上一页（无模态）时四个方向都当十字键交给原版（dm_stick_nav_as_dpad want = 2）。
- 角色页：从别的页按 L 切过来时页里没有起始焦点，十字键下什么也不做（四下都没焦点）。原版布局里四个「+」有自己的焦点链。
  修：焦点不在「+」上、至少一个「+」看得见时收下「下」，光标放到第一个看得见的「+」中心（命中函数扫出来、核过才放；找不到就把「下」还给原版）。
  左摇杆照旧是自由光标（「+」与属性说明都靠它指，原版设计）。
核：t_stick_nav 里 want = 2 四向都交、want = 1 照旧只交上下；dm_padmode 有 dm_padmode_stick_as_dpad4 且把 want 提到 2；
ui_panel_state_pump 在任务日志最上一页（无模态）时调它；ui_pad_charstats_pump 只认四个「+」、已在链上不接、看得见才接、落点核过才放、失败把键还回去；
角色页不碰摇杆。回退任一处这里就红。
B114 regression: in controller mode the quest log's left stick is a free cursor that loses the gold frame (now passed to the game as a four-way D-pad while the quest log is on top, like B071), and the
character page has no starting focus so D-pad down does nothing (now Down puts the cursor on the first visible "+", after which the original chain works; the stick stays a free cursor by design).
"""
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
DM = HERE.parents[1]
CUR = (DM / 'host' / 'dm_cursor.c').read_text(encoding='utf-8')
PAD = (DM / 'host' / 'dm_padmode.c').read_text(encoding='utf-8')
NAV = (DM / 'host' / 'dm_stick_nav.h').read_text(encoding='utf-8')


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


class QuestLog(unittest.TestCase):
    def test_four_way(self):
        b = body(NAV, 'dm_stick_nav_as_dpad')
        self.assertIn('want == 2 ? DM_BTN_UP | DM_BTN_DOWN | DM_BTN_LEFT | DM_BTN_RIGHT : DM_BTN_UP | DM_BTN_DOWN', b)
        self.assertRegex(PAD, r'void dm_padmode_stick_as_dpad4\(void\) \{[^}]*g_nav4_frame[^}]*g_nav_frame')
        self.assertIn('if (want && n4 != UINT64_MAX && dm_guest_frame() - n4 <= 3) want = 2;', code(PAD))

    def test_called_for_quest_log(self):
        st = body(CUR, 'ui_panel_state_pump')
        self.assertRegex(st, r'if \(root_panel_shown\(root, "QuestLogPanelExpansion"\) && !modal_any\(root, 1, NULL\)\) \{[^}]*dm_padmode_stick_as_dpad4\(\);')

    def test_stick_unit(self):
        exe = Path(tempfile.gettempdir()) / 't_stick_nav_b114'
        subprocess.run(['clang', '-O1', '-Wall', '-Werror', '-I', str(DM / 'host'), '-o', str(exe), str(HERE / 't_stick_nav.c')], check=True)
        out = subprocess.run([str(exe)], capture_output=True, text=True)
        self.assertEqual(out.returncode, 0, out.stdout)
        self.assertIn('want = 2', (HERE / 't_stick_nav.c').read_text(encoding='utf-8'))


class CharacterPage(unittest.TestCase):
    def test_pump(self):
        b = body(CUR, 'ui_pad_charstats_pump')
        self.assertTrue(b, '没有 ui_pad_charstats_pump')
        self.assertIn('dm_padmode_current()', b)
        self.assertIn('root_panel_shown(root, "CharacterStatsPanel")', b)
        self.assertIn('modal_any(root, 1, NULL)', b)
        for n in ('StrengthPointButton', 'DexPointButton', 'VitalityPointButton', 'EnergyPointButton'): self.assertIn('"%s"' % n, b)
        self.assertRegex(b, r'if \(!strcmp\(hn, btn\[i\]\)\) return;')   # 已在链上：不接 | already on the chain: not taken
        self.assertIn('ui_find(cp, "LevelUpInfo", 0)', b); self.assertRegex(b, r'PM_VIS\(lu\) && !first')   # 「+」所在那块看得见才接 | only when the "+" block is visible
        self.assertIn('if (PM_VIS(b)) first = b;', b)
        self.assertRegex(b, r'if \(!first \|\| !\(dm_padmode_page_keys\(DM_BTN_DOWN\) & DM_BTN_DOWN\)\) return;')
        self.assertRegex(b, r'if \(!ui_hit_center\(m, first, &x, &y\) \|\| !dm_ui_hover_now\(x, y\)\) \{ dm_padmode_page_pulse\(DM_BTN_DOWN\);')
        self.assertNotIn('stick', b)                                       # 角色页不碰摇杆 | the stick is left alone here
        self.assertIn('ui_pad_charstats_pump(main_module(), root);', body(CUR, 'ui_panel_state_pump'))

    def test_hit_center(self):
        b = body(CUR, 'ui_hit_center')
        self.assertIn('st = H / 60', b)
        self.assertIn('return PM_HIT(w, *cx, *cy);', b)   # 中心点自己也要命中 | the centre itself must hit


if __name__ == '__main__':
    unittest.main()
