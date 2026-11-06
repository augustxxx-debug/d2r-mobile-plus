#!/usr/bin/env python3
"""B170 / B171 回归（RC2.1，Duo 用户反馈）。
B170：手柄模式商店开着时光圈到不了背包页签 / 百宝囊图标。实测（Mac 11510365 b170 / b170b / b170c）：只有背包第 0 行前三列按上能进页签，别的列按上进装备栏；
  ZL / ZR 归商店页签条；L / R 在商店里点按原版无反应。修：商店开着时第 0 行任意列按上进页签条；L / R 点按（< 0.3 秒，只旁听不接管，按住 R 仍是原版排序）切背包 / 护身符页；
  最后一行在百宝囊图标下方按下进图标、图标上按上回格子。不开商店行为不变。
B171：手柄丢弃符文丢不掉。实测（b171b）：符文其实丢到了地上，自动收符文宝石下一拍就捡回收进囊。修：在物品栏见过的符文宝石 id 不自动捡，离开 12 格或 5 秒哪儿都不见后恢复。
"""
import re
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
HOST = HERE.parent
BAG = (HOST / 'dm_bag.c').read_text(encoding='utf-8')
POUCH = (HOST / 'dm_pouch.c').read_text(encoding='utf-8')
PAD = (HOST / 'dm_padmode.c').read_text(encoding='utf-8')


def code(s):
    return re.sub(r'//[^\n]*', '', re.sub(r'/\*.*?\*/', '', s, flags=re.S))


class Shop(unittest.TestCase):
    def test_any_column_in_shop(self):   # B189 起不开商店也是任意列（test_b189_inv_focus.py） | since B189 any column without a shop too
        c = code(BAG)
        self.assertIn('if (!held && (vert ? cx < x0 + cell : cy < y0 + cell)) {', c)
        self.assertIn('int shop = open && up(dm_ui_top(root, "VendorPanelLayout"));', c)

    def test_lr_tap_listen_only(self):
        c = code(BAG)
        self.assertIn('dm_padmode_raw_buttons()', c)
        self.assertIn('fr - down_fr[i] < 18 && !held', c)       # 点按才切、手上有物品不切 | tap only, not with an item in hand
        self.assertIn('shop && !(bt & K[i]) && (prev & K[i])', c)  # 松开时还在商店 | still in the shop on release
        self.assertNotRegex(c, r'dm_padmode_page_keys\([^)]*DM_BTN_[LR]\b')   # 不接管 L / R（按住 R 排序归原版） | L / R are never taken
        self.assertIn('atomic_store(&g_raw_bt, bt);', code(PAD))

    def test_pouch_chain_shop_only(self):
        c = code(POUCH)
        self.assertIn('if (padn && gp && panel_up(dm_ui_top(root, "VendorPanelLayout"))) pouch_pad_nav(root, inv, boxw);', c)
        self.assertIn('find_w(boxw, "TouchPouch", 1)', c)
        self.assertIn('dm_store_row(&g_sgrid, cy) != g_sgrid.rows - 1', c)
        self.assertIn('dm_padmode_page_keys(DM_BTN_DOWN) & DM_BTN_DOWN', c)
        self.assertIn('dm_padmode_page_keys(DM_BTN_UP) & DM_BTN_UP', c)
        self.assertNotIn('DM_BTN_A', re.search(r'static void pouch_pad_nav.*?\n}\n', c, re.S).group(0))   # A 归原版（发 onClick） | A stays the game's


class DroppedRune(unittest.TestCase):
    def test_own_list(self):
        c = code(POUCH)
        self.assertIn('if (is_rune_gem(it)) dm_pouch_own_add(&g_own, *(uint32_t *)(it + 8), fr);', c)
        self.assertRegex(c, r'if \(dm_pouch_own_has\(&g_own, id\)\) \{ static uint32_t lg_id;.*?continue; \}')
        self.assertIn('if (dm_pouch_own_far(x - px, y - py)) { dm_pouch_own_del(&g_own, id);', c)
        self.assertIn('dm_pouch_own_sweep(&g_own, fr);', c)
        # 跳过在 PICK_R 判之后、want 选中之前 | the skip sits after the PICK_R check and before choosing want
        i, j, k = c.index('abs(x - px) > PICK_R'), c.index('static uint32_t lg_id'), c.index('if (!seen) { want = id;')
        self.assertLess(i, j); self.assertLess(j, k)


if __name__ == '__main__':
    unittest.main()
