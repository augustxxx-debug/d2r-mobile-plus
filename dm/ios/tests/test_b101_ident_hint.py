"""B101 回归：手柄模式光标停在未鉴定物品上时，提示条 A 那条换成的「捡起（按住鉴定）」被截成「捡起（按住」、后面留空。

实测（Mac 宿主，dm-work/run/bugs/padfix-opus-20261104，DM_TEST_LEGEND_DUMP 读提示条排版槽）：原版提示条每个排版槽的字 = 键帽字形 3 字节 + 条目文字。
同一张六条的提示条（佣兵页背包物品）上，原版自己的「捡起(按住装备)」（20 字节）槽里 23 字节、完整；我们的「捡起（按住鉴定）」（24 字节、全角括号）槽里只有 18 字节 = 截在第 15 字节；
照原版格式串 ControllerPromptHoldFormatString「(按住%s)」改成半角括号的「捡起(按住鉴定)」后槽里 23 字节、完整（截图对照 b101_cmp.png）。
根因：提示条每条有固定槽宽（ControllerOverlay Legend1–8 各 360 单位），超宽的条目在槽里滚动，截图上就是「截断 + 空白」（英西俄的同一张提示条上原版「比较」「排序」「高级属性」也在滚）。\n规则：原版同位置自己的「捡起 + (按住装备)」排得下的语言（中文），我们这条也必须排得下；原版本来就超宽的语言照原版格式写。
核：原版排得下的语言我们也排得下（有 RomFS 时按原版串现拼；没有就跳过这一项）；装串缓冲放得下六语（俄文原先被 48 字节缓冲截掉末尾「)」）；中文照原版格式串用半角括号；英文照原版「 (Hold to %s)」；六语都还是「捡起 + 按住鉴定」两层意思；生成头文件带上了。
B101 regression: in controller mode, with the cursor on an unidentified item, the legend's A entry we swap in ("捡起（按住鉴定）") was cut to "捡起（按住" with a blank after it.
Measured on the Mac host (DM_TEST_LEGEND_DUMP reads the legend's layout slots): each slot holds the keycap glyph (3 bytes) + the entry text. On the same six-entry legend the original's own
"捡起(按住装备)" (20 bytes) gets 23 bytes in its slot, complete; ours with full-width parentheses (24 bytes) got 18 bytes = cut at byte 15; after switching to the half-width parentheses of the original
format string ControllerPromptHoldFormatString "(按住%s)" the slot holds 23 bytes, complete. Rule: in every language ours is no wider than the original's own "Pick Up + (Hold to Equip)" at the
same position, which is measured to fit on that legend. Checked: six languages no wider than that (built from the original strings when a RomFS is present, skipped otherwise); Chinese uses the
original format's half-width parentheses; English follows the original " (Hold to %s)"; all six still say "pick up" + "hold to identify"; the generated header carries it."""
import json
import os
import re
import sys
import unittest
from pathlib import Path

DM = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(DM / 'ios' / 'tools'))
import dm_i18n  # noqa: E402
from test_community_entry import est_width  # noqa: E402

COLS = ('zhCN', 'enUS', 'esES', 'esMX', 'ruRU', 'koKR')   # ui_text.tsv 第 2–7 列 | columns 2–7 of ui_text.tsv
PICK = (r'^捡起', r'^Pick Up\b', r'^Recoger\b', r'^Recolectar\b', r'^Взять\b', r'^')   # 原版「捡起」各语；韩文这格原本只写「감정」（窄、不截断，B101 不动它） | the original "Pick Up" per language; Korean has only 「감정」 (narrow, not cut; left alone)
IDENT = (r'按住鉴定', r'Hold to Identify', r'identificar', r'identificar', r'опознать', r'감정')


def row():
    r = {x[0]: x for x in dm_i18n.rows()}['dmPadLegIdentify']
    return [t or r[3] for t in r[1:7]]   # 拉美西空着 = 用西语 | empty Latin American Spanish = Spanish


class IdentHint(unittest.TestCase):
    def test_format_like_original(self):
        zh, en = row()[0], row()[1]
        self.assertEqual(zh, '捡起(按住鉴定)')   # 原版「捡起」+ 原版格式串「(按住%s)」（半角括号） | the original's "捡起" + its format "(按住%s)" (half-width parentheses)
        self.assertNotRegex(zh, r'[（）]', '全角括号在同一张提示条上排不下（B101 实测截在第 15 字节）')
        self.assertEqual(en, 'Pick Up (Hold to Identify)')   # = 原版「Pick Up」+ 原版「 (Hold to %s)」 | the original's own format
        for col, text, pick, ident in zip(COLS, row(), PICK, IDENT):
            self.assertRegex(text, pick, col)
            self.assertRegex(text, ident, col)
        self.assertIn('{ "dmPadLegIdentify", "捡起(按住鉴定)", "Pick Up (Hold to Identify)"', (DM / 'host' / 'dm_i18n_tab.h').read_text(encoding='utf-8'))

    def test_fits_where_the_original_fits(self):
        # 槽宽（est_width 36 的单位）按中文实测标定：「捡起(按住鉴定)」266 排得下（槽里 23 字节、截图完整），「捡起（按住鉴定）」288 滚动（槽里 18 字节）。
        # 原版同位置自己的「捡起 + (按住装备)」排得下的语言，我们这条也必须排得下；原版那条本来就超宽的语言（英西俄：同一张提示条上原版「比较」「排序」「高级属性」都在滚），照原版格式写、滚动同原版
        # Slot width (est_width 36 units) calibrated on Chinese: 266 fits (23 bytes in the slot, screenshot complete), 288 scrolls (18 bytes). Where the original's own "Pick Up + (Hold to Equip)" fits,
        # ours must fit too; where the original is already too wide (en / es / ru: on the same legend the original "Compare", "Sort", "Advanced Stats" scroll as well), ours follows the original format and scrolls like it
        SLOT = 270
        self.assertLessEqual(est_width('捡起(按住鉴定)', 36), SLOT); self.assertGreater(est_width('捡起（按住鉴定）', 36), SLOT)
        root = os.environ.get('DM_ROMFS_SRC') or os.path.join(os.environ.get('DM_ROOT', os.path.expanduser('~/dm-work')), 'data/switch/3.3.94012/Update/RomFS')
        if not os.path.exists(os.path.join(root, 'data', 'build_manifest.psv')): self.skipTest('没有 RomFS（%s）' % root)
        import touch_layout
        _, text = touch_layout.read_layout(root, None, suffix='/lng/strings/ui-controller.json')
        d = {e['Key']: e for e in json.loads(text)}
        fit = 0
        for col, ours in zip(COLS, row()):
            orig = d['ControllerPromptItemPickUpFromInventory'][col] + d['ControllerPromptHoldFormatString'][col].replace('%s', d['ControllerPromptItemEquipOnSelf'][col])
            if est_width(orig, 36) <= SLOT:
                fit += 1; self.assertLessEqual(est_width(ours, 36), SLOT, '%s：原版「%s」排得下，我们的「%s」超宽会滚动' % (col, orig, ours))
        self.assertGreaterEqual(fit, 1)   # 至少中文这一格真在核 | at least Chinese is really checked

    def test_buffer_holds_every_language(self):   # 换进串表的是 g_hint[].ours 这个缓冲：放不下就被 strlcpy 截掉（俄文原先丢了末尾「)」） | the swapped-in text lives in g_hint[].ours; strlcpy cuts what does not fit (Russian lost its final ")")
        src = (DM / 'host' / 'dm_padmode.c').read_text(encoding='utf-8')
        cap = int(re.search(r'char ours\[(\d+)\]; \} g_hint\[NHINT\]', src).group(1))
        keys = re.findall(r'\{ "\w+", "(dm\w+)" \}', src[src.index('g_hint[NHINT] = {'):src.index('void dm_padmode_vault_hint')])
        self.assertEqual(set(keys), {'dmPadHintDeposit', 'dmPadLegIdentify', 'dmPadLegCompare'})
        rows = {x[0]: x for x in dm_i18n.rows()}
        for k in keys:
            for t in rows[k][1:7]: self.assertLess(len(t.encode('utf-8')), cap, '%s「%s」放不进 ours[%d]' % (k, t, cap))


if __name__ == '__main__':
    unittest.main()
