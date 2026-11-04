#!/usr/bin/env python3
"""符文名编号七列回归（任务卡 2026-11-04）：annot_strings.rune_numbers() 原来只改 zhCN / esES / esMX，
现在 RUNES_LANGS 七列都带编号（zhCN / zhTW / enUS / esES / esMX / ruRU / koKR），其余注释 / 掉落名仍只做 LANGS 三列。
zhCN / esES / esMX 的 golden 是改代码前跑 annot_strings.build() 写死的（逐条与旧输出一致）；新四列按任务卡格式：
繁中「短名 英文短名 #号」、英 / 俄 / 韩「该列现行全名 #号」，七列的 rNNL 一律「短名#号 」（dm_runeword.c 按 # 截断）。
item_notes 关闭时生成器不跑 annot_strings，游戏读原始字符串表，所以原始数据里七列都该没有编号。
Rune-number regression for all seven columns (task card 2026-11-04): annot_strings.rune_numbers() used to change only
zhCN / esES / esMX; now all RUNES_LANGS columns carry numbers, while the other annotations / drop names stay on the
three LANGS columns. The zhCN / esES / esMX goldens were captured by running annot_strings.build() before the change
(entry-by-entry identical to the old output); the four new columns follow the task card formats. With item_notes off the
generator does not run annot_strings and the game reads the original table, which must have no numbers in any column."""
import json
import os
import re
import sys
import tempfile
import unittest
from pathlib import Path

DM = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(DM / 'ios' / 'tools'))
import annot_strings  # noqa: E402
import touch_layout  # noqa: E402

# 原版 RomFS 目录由环境变量给（test_and_build.sh 导出 DM_ROMFS_SRC）；没给就跳过，不在源码里写本机路径
# The original RomFS directory comes from the environment (test_and_build.sh exports DM_ROMFS_SRC); skip when unset, no local path in the source
ROMFS = os.environ.get('DM_ROMFS_SRC') or os.environ.get('DM_ROMFS') or ''

if not os.path.isdir(ROMFS):
    print('SKIP')
    sys.exit(0)

COLS = annot_strings.RUNES_LANGS
OLD_COLS = annot_strings.LANGS

# 改代码前（基线 16042589）跑 build 得到的旧三列输出，以及本次要产出的新四列；比较前都去掉 ann_drop 后加的颜色码 ÿc.
# The old three columns' output from build at baseline 16042589, plus the four new columns to produce; both are compared after stripping ann_drop's ÿc. color codes
GOLDEN = {
    'r01': {'zhCN': '艾尔 El#1', 'zhTW': '艾爾 El #1', 'enUS': 'El Rune #1', 'esES': 'Runa El#1', 'esMX': 'Runa El#1', 'ruRU': 'Руна Эль #1', 'koKR': '엘 룬 #1'},
    'r01L': {'zhCN': '艾尔#1 ', 'zhTW': '艾爾#1 ', 'enUS': 'El#1 ', 'esES': 'El#1 ', 'esMX': 'El#1 ', 'ruRU': 'Эль#1 ', 'koKR': '엘#1 '},
    'r08': {'zhCN': '拉尔 Ral#8', 'zhTW': '拉爾 Ral #8', 'enUS': 'Ral Rune #8', 'esES': 'Runa Ral#8', 'esMX': 'Runa Ral#8', 'ruRU': 'Руна Рал #8', 'koKR': '랄 룬 #8'},
    'r08L': {'zhCN': '拉尔#8 ', 'zhTW': '拉爾#8 ', 'enUS': 'Ral#8 ', 'esES': 'Ral#8 ', 'esMX': 'Ral#8 ', 'ruRU': 'Рал#8 ', 'koKR': '랄#8 '},
    'r24': {'zhCN': '—————————\n\n伊司特 Ist#24\n\n—————————', 'zhTW': '伊司特 Ist #24', 'enUS': 'Ist Rune #24',
            'esES': '—————————\n\nRuna Ist#24\n\n—————————', 'esMX': '—————————\n\nRuna Ist#24\n\n—————————', 'ruRU': 'Руна Ист #24', 'koKR': '이스트 룬 #24'},
    'r24L': {'zhCN': '伊司特#24 ', 'zhTW': '伊司特#24 ', 'enUS': 'Ist#24 ', 'esES': 'Ist#24 ', 'esMX': 'Ist#24 ', 'ruRU': 'Ист#24 ', 'koKR': '이스트#24 '},
    'r30': {'zhCN': '—————————————\n\n\n\n贝 Ber#30\n\n\n\n—————————————', 'zhTW': '貝 Ber #30', 'enUS': 'Ber Rune #30',
            'esES': '—————————————\n\n\n\nRuna Ber#30\n\n\n\n—————————————', 'esMX': '—————————————\n\n\n\nRuna Ber#30\n\n\n\n—————————————', 'ruRU': 'Руна Бер #30', 'koKR': '베르 룬 #30'},
    'r30L': {'zhCN': '贝#30 ', 'zhTW': '貝#30 ', 'enUS': 'Ber#30 ', 'esES': 'Ber#30 ', 'esMX': 'Ber#30 ', 'ruRU': 'Бер#30 ', 'koKR': '베르#30 '},
    'r33': {'zhCN': '—————————————\n\n\n\n佐德 Zod#33\n\n\n\n—————————————', 'zhTW': '薩德 Zod #33', 'enUS': 'Zod Rune #33',
            'esES': '—————————————\n\n\n\nRuna Zod#33\n\n\n\n—————————————', 'esMX': '—————————————\n\n\n\nRuna Zod#33\n\n\n\n—————————————', 'ruRU': 'Руна Зод #33', 'koKR': '조드 룬 #33'},
    'r33L': {'zhCN': '佐德#33 ', 'zhTW': '薩德#33 ', 'enUS': 'Zod#33 ', 'esES': 'Zod#33 ', 'esMX': 'Zod#33 ', 'ruRU': 'Зод#33 ', 'koKR': '조드#33 '},
}

strip_color = lambda t: re.sub('ÿc.', '', t)   # 去掉 ann_drop 加的颜色码（ÿc8 等） | strip ann_drop's color codes (ÿc8 etc.)


def entries(text):
    """字符串表里的条目（每条形如 { "Key": ..., ... }）。 | Entries of the string table (each shaped like { "Key": ..., ... })."""
    return {d['Key']: d for d in (json.loads(m) for m in re.findall(r'\{[^{}]*\}', text)) if 'Key' in d}


class RuneNumbers(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()   # 缺省落在 $TMPDIR | defaults to $TMPDIR
        cls.addClassCleanup(cls.tmp.cleanup)
        out = os.path.join(cls.tmp.name, 'overlay')
        os.makedirs(out, exist_ok=True)
        repl, cls.stat = annot_strings.build(ROMFS, out, {})
        path = next(v for k, v in repl.items() if k.endswith('item-runes.json'))
        cls.rune_path = path
        with open(path, encoding='utf-8-sig') as f:
            cls.built = entries(f.read())
        cls.table = annot_strings._last_table

    def test_seven_columns_golden(self):
        # zhCN / esES / esMX 与改前逐条一致；zhTW / enUS / ruRU / koKR 按任务卡格式带编号
        # zhCN / esES / esMX match the pre-change output entry by entry; the four new columns carry numbers in the task card's formats
        for key, cols in GOLDEN.items():
            for col in COLS:
                self.assertEqual(strip_color(self.built[key][col]), cols[col], '%s / %s' % (key, col))

    def test_old_three_columns_unchanged(self):
        # golden 里旧三列就是改前跑出来的值：再显式确认这三次列只可能是基线内容 | the old three columns' goldens are the pre-change values; assert explicitly that they are the baseline content
        for key, cols in GOLDEN.items():
            for col in OLD_COLS:
                self.assertEqual(strip_color(self.built[key][col]), cols[col], '%s / %s' % (key, col))

    def test_short_names_truncate_at_hash(self):
        # dm_runeword.c 在 '#' 处截断 rNNL（清单自己拼 #号）：七列每条都要有 #号，且 # 前没有空白 / 第二个 # | dm_runeword.c truncates rNNL at '#' (the list assembles the number itself): every column and rune must have #N, with no whitespace or second # before it
        for i in range(1, 34):
            for col in COLS:
                v = self.built['r%02dL' % i][col]
                self.assertRegex(v, r'^\S+#%d $' % i, 'r%02dL / %s' % (i, col))

    def test_textbox_plain_still_generated(self):
        # 文字框对照表照常生成（新四列不加颜色码，不新增映射；条目数用 LANGS 三列算） | the text-box table is still generated (the new four columns add no colors, so no new mappings; entries come from the three LANGS columns)
        dst = os.path.join(self.tmp.name, 'textbox')
        os.makedirs(dst, exist_ok=True)
        n = annot_strings.write_textbox_table(dst)
        self.assertEqual(n, self.stat['文字框对照'])
        with open(os.path.join(dst, annot_strings.TEXTBOX_TABLE), encoding='utf-8') as f:
            lines = f.read().splitlines()
        self.assertTrue(lines[0].startswith('#'))
        self.assertEqual(len(lines), n + 1)

    def test_item_notes_off_has_no_numbers(self):
        # item_notes 关闭 = 不跑 annot_strings = 游戏读原始表：七列 rNN / rNNL 都不带编号 | item_notes off = annot_strings not run = the game reads the original table: no numbers in rNN / rNNL in any column
        _, text = touch_layout.read_layout(ROMFS, None, suffix='/lng/strings/item-runes.json')
        orig = entries(text)
        for i in range(1, 34):
            for key in ('r%02d' % i, 'r%02dL' % i):
                for col in COLS:
                    self.assertNotRegex(orig[key][col], r'#\d', '%s / %s' % (key, col))


if __name__ == '__main__':
    unittest.main(verbosity=2)
