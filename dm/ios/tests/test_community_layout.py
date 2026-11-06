#!/usr/bin/env python3
# 社区提示框布局：渠道卡张数与宿主同一个上限（dm_community.c CO_ROWS），宿主按名字找的 TouchComm* 控件（k_co_names）布局里都有。
# 缺陷来历：3a730854 让宿主收三张渠道卡，布局还只生成两张，第三张（镜像）没有控件；B154 撤网盘行时把这条修复单独留下。
# 只读布局模块与宿主源码，不读游戏包、不写文件。
# Community box layout: the number of channel cards equals the host cap (CO_ROWS in dm_community.c), and every TouchComm* widget the host looks up by name (k_co_names) exists in the layout.
# History: since 3a730854 the host takes three channel cards but the layout made two, so the third (mirror) card had no widgets; B154 kept this fix when removing the drive rows.
# Reads the layout module and host source only; never a game package, never writes.
import os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'dm', 'ios', 'tools'))
import touch_layout
src = open(os.path.join(ROOT, 'dm', 'host', 'dm_community.c'), encoding='utf-8').read()
m = re.search(r'#define CO_ROWS (\d+)', src); assert m, '宿主没有 CO_ROWS'
rows = int(m.group(1)); lay = touch_layout._community()
assert touch_layout.CO_CH_ROWS == rows, '布局渠道卡 %d 张，宿主 %d 张' % (touch_layout.CO_CH_ROWS, rows)
for k in range(rows):
    for nm in ('TouchCommLabel%d' % k, 'TouchCommValue%d' % k, 'TouchCommCopy%d' % k, 'TouchCommCopyText%d' % k) + tuple('TouchCommCard%d%s' % (k, e) for e in 'TBLR'):
        assert '"name": "%s"' % nm in lay, '布局缺 %s' % nm
names = re.search(r'k_co_names\[\] = \{(.*?)\};', src, re.S); assert names, '宿主没有 k_co_names'
miss = [n for n in re.findall(r'"(TouchComm\w+)"', names.group(1)) if '"name": "%s"' % n not in lay]
assert not miss, '宿主要找、布局没有：%s' % ', '.join(miss)
assert 'TouchCommPan' not in lay and 'TouchCommPan' not in src, '网盘行已撤（B154），不许回来'
print('test_community_layout: OK（渠道卡 %d 张，宿主控件名 %d 个全在）' % (rows, len(re.findall(r'"(TouchComm\w+)"', names.group(1)))))
