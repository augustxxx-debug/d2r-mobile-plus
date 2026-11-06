#!/usr/bin/env python3
"""B189 回归（RC2.1，Duo 用户反馈「手柄圆圈焦点无法直接移动到背包和护身符包上，好像有很多盲区」）。
实测（Mac 4f3ae742，不开商店，十字键）：背包第 0 行只有第 0–2 列按上进页签；第 3–8 列进腰带、第 9 列进鞋格、第 10–11 列进副手格，页签是盲区；
  腰带第 0 格按右跳回网格（腰带四格之间走不了；修后第 3 格按右原版也跳回网格第 0 行中间）；页签 / 腰带按下回到页签正下方那列，不是原来那列。
  底排：原版金币钮 ↑ 固定回第 6 列、百宝囊图标 ↑ 回第 2 列（不开商店）。
修：第 0 行任意列按上进当前页页签；页签 · 腰带四格一行左右走（第 3 格按右下到网格第 0 行腰带右边那列，B222 / G11）；按下回网格上次离开的那列；页签 / 腰带按上照旧交原版进装备栏；金币钮、百宝囊图标（不开商店）按上回进来那一列。不开商店时 L / R 照原版切上方面板，不改。
"""
import re
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
BAG = (HERE.parent / 'dm_bag.c').read_text(encoding='utf-8')


def code(s):
    return re.sub(r'//[^\n]*', '', re.sub(r'/\*.*?\*/', '', s, flags=re.S))


def fn(c, name):
    return re.search(r'static void ' + name + r'\(.*?\n}\n', c, re.S).group(0)


class InvFocus(unittest.TestCase):
    def setUp(self):
        self.f = fn(code(BAG), 'pad_nav')

    def test_any_column_enters_tab(self):
        f = self.f
        self.assertIn('if (!held && (vert ? cx < x0 + cell : cy < y0 + cell)) {', f)   # 手上有物品交原版（复核） | with an item in hand, the original (recheck)
        self.assertNotIn('3 * cell', f)                       # 不再只认前三列 | no longer only the first three columns
        row0 = f[f.index('if (hov == grid'):f.index('return; }')]
        self.assertNotIn('shop', row0)                         # 开不开商店一样 | same with or without a shop
        self.assertIn('dm_ui_hover_now(TAB_SX(g_page), TAB_SY(g_page))', f)

    def test_return_to_column(self):
        f = self.f
        self.assertIn('if (cx == px && cy == py) { col_x = x0 + (int)(((int)((cx - x0) / cell) + 0.5) * cell);', f)   # 停稳才记、记格子中心 | settled only, cell centre
        self.assertIn('dm_ui_hover_now(col_x >= 0 ? col_x : cx, gy0 + (int)(49 * k))', f)
        self.assertEqual(f.count('BACK_TO_GRID();'), 2)        # 页签、腰带两处 | tabs and belt

    def test_belt_row(self):
        f = self.f
        b = re.search(r'!strcmp\(hn, "Belt"\) && cy > BELT_SY - \(int\)\(56 \* k\)\) \{.*?return; \}', f, re.S).group(0)   # 只管最下一排 | bottom row only
        self.assertIn('dm_padmode_page_keys(PREV | NEXT | EXIT)', b)   # 腰带的上交原版；最后一格的右也接管（下到腰带右边那列，不困在腰带上） | up stays the game's; right on the last cell is taken too (stays put, no jump back to the grid)
        self.assertNotIn('ENTER', b)
        self.assertIn('if (i) dm_ui_hover_now(BELT_CX(i - 1), BELT_SY); else dm_ui_hover_now(TAB_SX(1), TAB_SY(1));', b)
        self.assertIn('else if ((ev & NEXT) && i < BELT_N - 1) dm_ui_hover_now(BELT_CX(i + 1), BELT_SY);', b)

        self.assertIn('if ((ev & NEXT) && tab == 1 && !vert) dm_ui_hover_now(BELT_CX(0), BELT_SY);', f)
        self.assertRegex(code(BAG), r'BELT_N = 4 \}')

    def test_g11_belt_last_cell_right_leaves_belt(self):   # B222 / G11：腰带第 3 格按右不困住，下到网格第 0 行腰带右边 → 焦点离开腰带、腰带收起 | right on belt cell 3 drops to grid row 0 right of the belt so focus leaves and the belt collapses
        b = self.f[self.f.index('cy > BELT_SY - (int)(56 * k)'):]; b = b[:b.index('return; }')]
        self.assertIn('else if (ev & NEXT) {', b)
        self.assertIn('DM_BAG_BELT_X + BELT_N * DM_BAG_BELT_CELL', b)
        self.assertIn('if (c > COLS - 1) c = COLS - 1;', b)
        self.assertIn('gy0 + (int)(49 * k)', b)

    def test_bottom_buttons_return(self):
        f = self.f
        self.assertIn('g_last_x = col_x; g_last_y = row_y;', f)
        P = code((HERE.parent / 'dm_pouch.c').read_text(encoding='utf-8'))
        self.assertEqual(P.count('dm_bag_pad_last(&lx, &ly) && dm_store_in_rect(&g_sgrid.r, lx, ly) && dm_store_row(&g_sgrid, ly) == g_sgrid.rows - 1'), 2)   # 「存放」钮、商店里的百宝囊图标 | Deposit, pouch icon in a shop
        self.assertIn('row_y > gy0 + (int)((ROWS - 1) * 98 * k) && (!strcmp(hn, "gold_button") || (!shop && !strcmp(hn, "TouchPouch")))', f)   # 商店里图标归 B170 那条 | in a shop the icon is B170's
        self.assertIn('if (dm_padmode_page_keys(DM_BTN_UP) & DM_BTN_UP) dm_ui_hover_now(col_x, row_y);', f)
        self.assertNotIn('TouchStore', f)                      # 「存放」归 store_pad_nav | Deposit is store_pad_nav's
        self.assertIn('pad_nav(root, inv_panel, grid, held, shop);', code(BAG))

    def test_b222_belt_covers_slot(self):   # B222：拿着非腰带物品、光标在被腰带盖住的装备槽里 → A 直接放进槽 | held non-belt item over a belt-covered slot -> A equips directly
        f = self.f
        b = re.search(r'if \(held && !strcmp\(hn, "Belt"\)\) \{.*?return; \} \} \}', f, re.S).group(0)
        self.assertIn('!is(it, 9) && !is(it, 0x16)', b)               # 药水 / 卷轴照原版进腰带 | potions / scrolls still go to the belt
        self.assertIn('!strncmp(nm, "slot_", 5) && *(uint8_t *)(c + 0x50) && HIT(c, cx, cy)', b)
        self.assertIn('dm_held_equip(loc);', b)
        self.assertIn('dm_padmode_page_keys(DM_BTN_A) & DM_BTN_A', b)
        self.assertLess(f.index('if (held && !strcmp(hn, "Belt"))'), f.index('!strcmp(hn, "Belt") && cy > BELT_SY'))   # 先于腰带最下排的处理 | before the bottom-row belt handling

    def test_tab_up_stays_original(self):
        t = self.f[self.f.index('int tab ='):]
        m = re.search(r'dm_padmode_page_keys\(([^;]*)\);', t).group(1)
        self.assertNotIn('ENTER', m); self.assertNotIn('DM_BTN_UP', m)

    def test_lr_without_shop_untouched(self):
        c = code(BAG)
        self.assertNotRegex(c, r'dm_padmode_page_keys\([^)]*DM_BTN_[LR]\b')
        self.assertIn('uint64_t bt = shop && pad &&', c)       # L / R 点按切页只在商店里（B170） | L / R taps page only in a shop


if __name__ == '__main__':
    unittest.main()
