"""选项页「键 → 页 / 组 / 平台」表的跨语言契约：布局 _OPT_TABLE 与宿主 k_opts 逐行一致，每个设置键都有归宿，两端出行互不混。 | Cross-language contract of the options "key -> page / group / platform" table: _OPT_TABLE in the layout and k_opts in the host agree line by line, every setting key has a home, and the rows of the two platforms stay apart."""
import importlib
import os
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'ios/tools'))
os.environ.pop('MOVIE_FFMPEG', None); os.environ.pop('DM_TARGET', None)
import touch_layout as layout

host = (root / 'host/dm_custom.c').read_text(encoding='utf-8')
PAGE = {'PG_GFX': 'gfx', 'PG_GAME': 'game', 'PG_SYS': 'sys'}
PLAT = {'PF_ALL': 'all', 'PF_APPLE': 'apple', 'PF_ANDROID': 'android'}
block = re.search(r'k_opts\[\] = \{(.*?)\n\};', host, re.S)[1]
c_table = tuple((k, PAGE[p], g, PLAT[f]) for k, p, g, f in re.findall(r'\{ "(\w+)", (PG_\w+), "(\w+)", (PF_\w+) \}', block))
assert c_table == layout._OPT_TABLE, (c_table, layout._OPT_TABLE)
assert len({r[0] for r in c_table}) == len(c_table)

# 每个现有设置键都在表里；待并入的键确实还不在 k_keys 里（并入时要从 _OPT_PENDING 删掉）| every setting key is in the table; pending keys are really absent from k_keys (remove them from _OPT_PENDING on merge)
keys = re.findall(r'"(\w+)"', re.search(r'k_keys\[\] = \{(.*?)\};', host, re.S)[1])
assert set(keys) <= {r[0] for r in c_table}, set(keys) - {r[0] for r in c_table}
assert not (layout._OPT_PENDING & set(keys)), layout._OPT_PENDING & set(keys)
assert layout._OPT_PENDING <= {r[0] for r in c_table}

# 语言是系统页第一行（自定义页的行号 0，宿主 lang 单算）| language is the System page's first row (Custom row 0; the host treats lang apart)
assert [r[0] for r in c_table if r[1] != 'gfx'][0] == 'lang'

# 分组标题文案键都在，且 iOS 专属组 / 项的键归入 IOS_ONLY | heading keys exist later; iOS-only rows are marked so they stay out of Android packages
import dm_i18n
assert dm_i18n.platform_of('dmCustomDropHaptic') == 'ios' and dm_i18n.platform_of('dmCustomIcloud') == 'ios'

apple = [k for k, _ in layout._CUSTOM_ALL] + list(layout._GAME_KEYS)
os.environ['DM_TARGET'] = 'android'; layout = importlib.reload(layout)
android = [k for k, _ in layout._CUSTOM_ALL] + list(layout._GAME_KEYS)
os.environ.pop('DM_TARGET'); layout = importlib.reload(layout)
assert set(apple) - set(android) == {'icloud', 'drop_haptic'}, (apple, android)
assert not set(android) - set(apple)
print('options table ok')
