"""B014（RC1.1）：手柄模式下物品类页面提示条 X 那条原版写「比较」，可比较实际要按住 X（原版只在比较键按下 / 松开时写「对比键按着」，dm_actions.c）。
宿主换串小表（dm_padmode.c g_hint，与「捡起（按住鉴定）」「存入」同一套）把原版键 ControllerPromptItemCompare 的串在手柄模式下常换成 dmPadLegCompare，触屏模式写回原文；
只在有我们译文的语言（dm_lang_ours）下换，德法意波日葡保留原版自己的译文（「捡起」那条同样处理，「存入」照旧各语言都换）。
这里核：换串表有这一条且下标对得上 HINT_CMP；hint_pump 对它不看页声明、只看手柄模式与语言；无译文语言不换（比较 / 捡起）；六语都是「按住」语义；宽度不超过已在同一条提示条上的「捡起（按住鉴定）」；生成头文件带上了；
有 RomFS 时再核原版 ui-controller.json 里确有这个键（没有就跳过这一项）。
B014 (RC1.1): in controller mode the original legend's X entry on the item pages says "Compare", but comparing needs X held (the original writes "compare button held"
only on the compare button's press / release, dm_actions.c). The host's string swap table (dm_padmode.c g_hint, the same one as "Pick Up (Hold to Identify)" and "Deposit")
points the original key ControllerPromptItemCompare at dmPadLegCompare all the time in controller mode and writes the original back in touch mode;
only in languages with our translation (dm_lang_ours) -- de / fr / it / pl / ja / pt keep the original's own text (same for "Pick Up"; "Deposit" is still swapped everywhere).
Checked here: the swap table has the entry at index HINT_CMP; hint_pump ignores page declarations for it and looks at controller mode and language; untranslated languages are not swapped; all six languages say "hold";
no wider than "Pick Up (Hold to Identify)", which already sits on the same legend; the generated header carries it; with a RomFS, the original ui-controller.json has the key (skipped otherwise)."""
import os
import re
import sys
import unittest
from pathlib import Path

DM = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(DM / 'ios' / 'tools'))
import dm_i18n  # noqa: E402
def est_width(line, pt):   # 同 test_community_entry.est_width（安卓分支没有那个 iOS 社区框测试）| as test_community_entry.est_width (the Android branch lacks that iOS community-box test)
    return sum(pt if re.match(r'[ᄀ-ᇿ　-鿿가-힯＀-￯]', ch) else pt * 0.7 for ch in line)

PADMODE = (DM / 'host' / 'dm_padmode.c').read_text(encoding='utf-8')
LANGS = ('中文', 'English', 'Español', 'Español MX', 'Русский', '한국어')
HOLD = (r'按住', r'\bhold\b', r'\bmantén\b', r'\bmantén\b', r'держ', r'길게')   # 各语「按住」的词 | the word for "hold" per language


class CompareHint(unittest.TestCase):
    def test_swap_table_entry(self):
        enum = re.search(r'enum \{ (HINT_\w+(?:, HINT_\w+)*), NHINT \};', PADMODE)
        self.assertIsNotNone(enum)
        names = enum.group(1).split(', ')
        self.assertIn('HINT_CMP', names)
        table = re.search(r'g_hint\[NHINT\] = \{(.*?)\};', PADMODE, re.S)
        self.assertIsNotNone(table)
        pairs = re.findall(r'\{ "(\w+)", "(\w+)" \}', table.group(1))
        self.assertEqual(len(pairs), len(names))   # 表与枚举一一对应 | table matches the enum one to one
        self.assertEqual(pairs[names.index('HINT_CMP')], ('ControllerPromptItemCompare', 'dmPadLegCompare'))

    def test_always_on_in_controller_mode(self):
        pump = re.search(r'static void hint_pump\(void\) \{(.*?)\n\}', PADMODE, re.S)
        self.assertIsNotNone(pump)
        # 只看手柄模式（g_cur），不看页声明的倒数 | gated only by controller mode (g_cur), not by the page-declaration countdown
        self.assertRegex(pump.group(1), r'int on = g_cur && \(h == HINT_VAULT \|\| dm_lang_ours\(dm_lang_index\(\)\)\) && \(h == HINT_CMP \|\| atomic_load\(&g_hint\[h\]\.on\) > 0\);')
        self.assertNotIn('HINT_CMP].on', PADMODE)   # 没有谁去声明它 | nobody declares it

    def test_untranslated_languages_not_swapped(self):
        # 无译文语言不换串：dm_lang_ours 的语言号集合 = 我们表里有列的语言（enUS / esES / koKR / ruRU / zhCN / zhTW / esMX），德法意波日葡不在里面；
        # 比较 / 捡起两条都受它管（上面的 hint_pump 断言），存入不受（编年史页是我们的页）
        # untranslated languages are not swapped: dm_lang_ours = the languages that have a column of ours; de / fr / it / pl / ja / pt are not in it;
        # both Compare and Pick Up are gated by it (hint_pump assertion above), Deposit is not (the Chronicle page is ours)
        src = (DM / 'host' / 'dm_i18n.c').read_text(encoding='utf-8')
        body = re.search(r'int dm_lang_ours\(int l\) \{ return ([^;]*); \}', src)
        self.assertIsNotNone(body)
        ours = {int(n) for n in re.findall(r'l == (\d+)', body.group(1))}
        order = ('enUS', 'deDE', 'esES', 'frFR', 'itIT', 'koKR', 'plPL', 'ruRU', 'zhCN', 'zhTW', 'esMX', 'jaJP', 'ptBR')   # 原版语言号（dm_i18n.h g_dm_lang） | original language numbers
        self.assertEqual({order[n] for n in ours}, {'enUS', 'esES', 'koKR', 'ruRU', 'zhCN', 'zhTW', 'esMX'})
        self.assertEqual(set(order) - {order[n] for n in ours}, {'deDE', 'frFR', 'itIT', 'plPL', 'jaJP', 'ptBR'})

    def test_text_six_languages(self):
        rows = {r[0]: r for r in dm_i18n.rows()}
        self.assertIn('dmPadLegCompare', rows)
        cmp_row, ident = rows['dmPadLegCompare'], rows['dmPadLegIdentify']
        self.assertEqual(cmp_row[1], '比较（按住）')
        self.assertEqual(cmp_row[2], 'Compare (hold)')
        limit = max(est_width(t, 36) for t in ident[1:])   # 同一条提示条上已有的最宽一条（36 只是比例，§2.6 估宽） | the widest entry already on the same legend
        for lang, text, hold in zip(LANGS, cmp_row[1:], HOLD):
            self.assertRegex(text.lower(), hold, lang)
            self.assertLessEqual(est_width(text, 36), limit, '%s「%s」比「捡起（按住鉴定）」还宽' % (lang, text))
        self.assertIn('{ "dmPadLegCompare", "比较（按住）", "Compare (hold)"', (DM / 'host' / 'dm_i18n_tab.h').read_text(encoding='utf-8'))

    def test_original_key_exists(self):
        root = os.environ.get('DM_ROMFS_SRC') or os.path.join(os.environ.get('DM_ROOT', os.path.expanduser('~/dm-work')), 'data/switch/3.3.94012/Update/RomFS')
        if not os.path.exists(os.path.join(root, 'data', 'build_manifest.psv')): self.skipTest('没有 RomFS（%s）' % root)
        import touch_layout
        _, text = touch_layout.read_layout(root, None, suffix='/lng/strings/ui-controller.json')
        self.assertRegex(text, r'"Key":\s*"ControllerPromptItemCompare"')
        import json
        e = next(x for x in json.loads(text) if x.get('Key') == 'ControllerPromptItemCompare')
        for lang in ('deDE', 'frFR', 'itIT', 'plPL', 'jaJP', 'ptBR'):   # 不换的语言原版自己有译文（不是英文） | the languages left alone have their own original translation (not English)
            self.assertTrue(e.get(lang) and e[lang] != e['enUS'], lang)


if __name__ == '__main__':
    unittest.main()
