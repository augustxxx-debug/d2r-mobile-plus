#!/usr/bin/env python3
"""B189 / B235 / B239 接线检查（行为用例在 t_bag_nav.c，按 dm_bag_nav.h 的表逐格断言；Mac 整机按真实按键走一遍见 ui_regress bag_nav_judge.py）。
这里只查宿主把表接上了：pad_nav 每个方向都问 dm_bag_nav_step、按表落点放光标；焦点换元素时记忆照 dm_bag_nav_moved 更新；物品栏开 / 关时清空记忆（B189 ①）；
网格按方向键按下沿推算的逻辑格认格子（B189 ③）；只接管表里不交原版的方向（网格只管第 0 行 ↑、底排钮只管 ↑、「存放」/ 商店里的百宝囊归 dm_pouch.c）；切页键走 dm_bag_nav_page_key；B222 的 A 兜底还在。
Wiring check for B189 / B235 / B239 (the behaviour cases are in t_bag_nav.c against the table in dm_bag_nav.h; the Mac whole-machine walk is ui_regress bag_nav_judge.py).
Only checks the host is wired to the table: pad_nav asks dm_bag_nav_step for every direction and puts the cursor on the table's target; the memory follows dm_bag_nav_moved when the focus
changes element; it is cleared when the inventory opens / closes (B189 item 1); the grid cell is the logical cell advanced on direction press edges (B189 item 3); only directions the table does not leave to
the original are taken (grid: row 0 Up only; bottom buttons: Up only; Deposit / the pouch in a shop belong to dm_pouch.c); page keys go through dm_bag_nav_page_key; the B222 A fallback stays.
"""
import re
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
BAG = (HERE.parent / 'dm_bag.c').read_text(encoding='utf-8')


def code(s):
    return re.sub(r'//[^\n]*', '', re.sub(r'/\*.*?\*/', '', s, flags=re.S))


def fn(c, name, ret='static void '):
    return re.search(ret + name + r'\(.*?\n}\n', c, re.S).group(0)


class Wiring(unittest.TestCase):
    def setUp(self):
        self.c = code(BAG); self.f = fn(self.c, 'pad_nav')

    def test_table_drives_every_direction(self):
        f = self.f
        self.assertIn('to[d] = dm_bag_nav_step(&g_nav, cur, d); if (to[d] != BN_ORIG && nav_owned(cur, d, shop)) want |= BTN[d];', f)
        self.assertIn('dm_bag_nav_rect r = dm_bag_nav_rect_of(to[d]);', f)
        self.assertIn('int ok = dm_ui_hover_now(x, y);', f)
        self.assertNotIn('TouchBagTab', f)                    # 页签不再按名字特判（B235） | tabs are no longer special-cased by name (B235)

    def test_memory_follows_moves_and_resets(self):
        self.assertIn('if (cur != g_nav_prev) { if (g_nav_prev >= 0 && cur >= 0) dm_bag_nav_moved(&g_nav, g_nav_prev, cur);', self.f)
        self.assertIn('if (open && !was_up) { LOG("大背包：物品栏打开，页 %d", g_page); nav_reset(1); }', self.c)
        self.assertIn('if (!open && was_up) { set_page(0, "closed"); nav_reset(0); }', self.c)
        self.assertIn('dm_bag_nav_panel(&g_nav, open);', fn(self.c, 'nav_reset'))

    def test_logical_cell(self):   # B189 ③：网格格子按方向键按下沿推算，不读动画途中的光标 | B189 item 3: the grid cell follows direction press edges, not the animating cursor
        e = fn(self.c, 'nav_elem', 'static int ')
        self.assertIn('if (g_logic >= 0) return g_logic;', e)
        self.assertIn('for (int d = 0; d < 4; d++) if (edge & B4[d]) g_logic = dm_bag_nav_track(g_logic, d);', self.f)
        self.assertIn('fr - last_edge > 20 && fr - still > 3', self.f)
        self.assertNotIn('0x544', self.c)                  # 那个字段是光标下的格，不是逻辑格（Mac 实测） | that field is the cell under the cursor, not the logical one (measured)

    def test_owned_directions(self):
        o = fn(self.c, 'nav_owned', 'static int ')
        self.assertIn('if (e < BN_CELLS) return d == BN_UP && e / COLS == 0;', o)
        self.assertIn('return d == BN_UP && (e == BN_GOLD || (e == BN_POUCH && !shop));', o)
        self.assertIn('if (dm_bag_nav_is_beltu(e)) return d == BN_DOWN;', o)

    def test_entry_cell_exported(self):   # 「存放」/ 商店里的百宝囊按上也回进来那一格（dm_pouch.c 读 dm_bag_pad_last） | Deposit / the pouch in a shop also return to the entry cell
        x = fn(self.c, 'nav_export')
        self.assertIn('g_nav.entry_bot', x)
        P = code((HERE.parent / 'dm_pouch.c').read_text(encoding='utf-8'))
        self.assertEqual(P.count('dm_bag_pad_last(&lx, &ly) && dm_store_in_rect(&g_sgrid.r, lx, ly) && dm_store_row(&g_sgrid, ly) == g_sgrid.rows - 1'), 2)

    def test_page_keys(self):
        self.assertIn('dm_bag_nav_page_key(BN_KEY_ZL, 0, 0, held, 1)', self.c)
        self.assertIn('dm_bag_nav_page_key(BN_KEY_ZR, 0, 0, held, 1)', self.c)
        self.assertIn('dm_bag_nav_page_key(i ? BN_KEY_R : BN_KEY_L, 1, 1, held, fr - down_fr[i] < 18)', self.c)
        self.assertNotRegex(self.c, r'dm_padmode_page_keys\([^)]*DM_BTN_[LR]\b')   # L / R 只旁听（不开商店是原版页组换页，按住 R 是排序） | L / R are only listened to

    def test_held_tab_a_swallowed(self):
        self.assertIn('uint64_t a_mask = held && dm_bag_nav_is_tab(cur) ? DM_BTN_A : 0;', self.f)
        self.assertIn('int fx = dm_bag_nav_held_fix(&g_nav, g_nav_prev, cur);', self.f)   # 拿着物品被原版放到页签上 → 改放几何落点 | moved off a tab with an item in hand

    def test_b222_belt_covers_slot(self):   # B222：拿着非腰带物品、光标在被腰带盖住的装备槽里 → A 直接放进槽 | held non-belt item over a belt-covered slot -> A equips directly
        b = re.search(r'if \(held && !strcmp\(hn, "Belt"\)\) \{.*?return; \} \} \}', self.f, re.S).group(0)
        self.assertIn('!is(it, 9) && !is(it, 0x16)', b)
        self.assertIn('dm_held_equip(loc);', b)
        self.assertLess(self.f.index('if (held && !strcmp(hn, "Belt"))'), self.f.index('if (cur < 0 || g_gx0 < 0) return;'))


if __name__ == '__main__':
    unittest.main()
