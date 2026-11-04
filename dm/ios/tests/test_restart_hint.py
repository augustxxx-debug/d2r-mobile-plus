#!/usr/bin/env python3
# B151「重启后生效」提示：配音 / 物品注释 / iCloud / 渲染分辨率四处提示用原版橙色色票、28 号；宿主按「当前值 ≠ 启动值」显隐（dm_custom.c restart_pending / rt_drop_restart）。
# 其余行的说明副文仍是说明灰（不许被一起改成橙色）；配音两项说明里不再写「下次启动生效」。只读布局模块、宿主源码和词表。
# B151 restart hints: voice / item notes / iCloud / render resolution use the original orange token at 28 pt; the host shows them by "current != launch value" (restart_pending / rt_drop_restart in dm_custom.c).
# Other rows' notes stay description grey; the two voice hints no longer say "applies after restart". Reads the layout module, host source and the text table only.
import os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'dm', 'ios', 'tools')); import touch_layout as t
def widget(text, name):
    m = re.search(r'\{ "type": "TextBoxWidget", "name": "%s", "fields": \{(.*?)\} \},\n' % re.escape(name), text); assert m, '布局缺 %s' % name; return m.group(1)
v = widget(t._custom_q3d_row(1, 'voice', 0), 'TouchCustom_voice_Restart')
assert '"@dmCustomRestartHint"' in v and '$FontColorOrange' in v and '"pointSize": 28' in v, v
for k in ('item_notes', 'icloud'):
    w = widget(t._custom_toggle_row(1, k, 'x', 0), 'TouchCustom_%s_Note' % k); assert '$FontColorOrange' in w and '"pointSize": 28' in w, (k, w)
g = widget(t._custom_toggle_row(1, 'auto_mana', 'x', 0), 'TouchCustom_auto_mana_Note'); assert '$FontColorLightGray' in g and 'Orange' not in g, g
host = open(os.path.join(ROOT, 'dm', 'host', 'dm_custom.c'), encoding='utf-8').read()
for s in ('restart_pending(K_VOICE)', 'restart_pending(K_ICLOUD)', 'restart_pending(k_in)', 'rt_drop_restart()', '"dmGraphicsRtResRestart"', 'memcpy(g_boot_v, v, sizeof g_boot_v)'): assert s in host, s
rows = {l.split('\t')[0]: l.split('\t') for l in open(os.path.join(ROOT, 'dm', 'i18n', 'ui_text.tsv'), encoding='utf-8') if '\t' in l}
for k in ('dmCustomVoiceZhHint', 'dmCustomVoiceEnHint'):
    assert not any(w in ' '.join(rows[k][1:7]).lower() for w in ('启动', 'restart', 'reinici', 'перезапуск', '재시작')), (k, rows[k][1:7])
for k in ('dmCustomRestartHint', 'dmGraphicsRtResRestart'): assert all(rows[k][c] for c in (1, 2, 3, 5, 6)), k
print('test_restart_hint: OK')
