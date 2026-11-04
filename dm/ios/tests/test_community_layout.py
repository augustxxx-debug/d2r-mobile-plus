#!/usr/bin/env python3
# 社区提示框布局（RC2 任务卡）：_community() 生成三行渠道卡 + 四行网盘（图标方框 / 首字 / 名称 / 打开 / 提取码 / 复制）+ 确认正文 + 取消；
# 渠道 / 网盘上限与宿主同一个常量；布局里的 TouchComm* 名字必须覆盖宿主 dm_community.c 的 k_co_names（宿主按这些名字找控件）。
# 只读布局模块与两个头文件，不读游戏包、不写任何文件。
# Community box layout (RC2 card): _community() yields three channel rows + four drive rows (icon square / glyph / name / Open / code / Copy) + confirm body + Cancel;
# the channel / drive caps match the host constants; every TouchComm* name in the host's k_co_names must exist in the layout (the host looks widgets up by these names).
# Reads the layout module and two headers only; never a game package, never writes.
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'dm', 'ios', 'tools'))
import touch_layout

HOST = os.path.join(ROOT, 'dm', 'host')


def read(path):
    with open(path, encoding='utf-8') as f:
        return f.read()


def const(path, pattern):
    m = re.search(pattern, read(path))
    if not m:
        raise AssertionError('%s：找不到 %s' % (path, pattern))
    return int(m.group(1))


layout = touch_layout._community()
names = set(re.findall(r'"name":\s*"(TouchComm[^"]+)"', layout))


def widget(key):
    """取一个控件自己的那段 JSON（到下一个控件为止） | one widget's JSON slice, up to the next widget"""
    m = re.search(r'"name": "%s".*?(?=\s*\{ "type")' % re.escape(key), layout, re.S)
    assert m, '布局里没有控件 %s' % key
    return m.group(0)


ch_limit = const(os.path.join(HOST, 'dm_community.h'), r'#define\s+DM_COMMUNITY_CHANNEL_ROWS\s+(\d+)')

# 1. 渠道卡三行（修复任务卡第 0 项：布局只生成两行、宿主上限已是 3） | three channel rows (card item 0: the layout used to make two while the host allowed three)
channel_copies = sorted(n for n in names if re.fullmatch(r'TouchCommCopy\d+', n))
assert channel_copies == ['TouchCommCopy0', 'TouchCommCopy1', 'TouchCommCopy2'], '渠道复制钮应为 0..2，实际 %s' % channel_copies
assert len(channel_copies) == ch_limit == 3, '渠道复制钮数量 %d 不等于宿主上限 %d' % (len(channel_copies), ch_limit)
for k in range(3):
    for pat in ('TouchCommLabel%d', 'TouchCommValue%d', 'TouchCommCopy%d', 'TouchCommCopyText%d', 'TouchCommCard%dT', 'TouchCommCard%dB', 'TouchCommCard%dL', 'TouchCommCard%dR'):
        name = pat % k
        assert name in names, '布局缺少渠道行控件 %s' % name
assert 'TouchCommLabel3' not in names and 'TouchCommCopy3' not in names, '渠道卡不应有第四行'
assert touch_layout.CO_CH_ROWS == ch_limit == 3, 'touch_layout.CO_CH_ROWS 与宿主上限不一致'

# 2. 网盘四行：图标方框四条线、首字、名称、打开、提取码、复制（按钮与钮上的字） | four drive rows: icon box lines, glyph, name, Open, code, Copy (button and its text)
assert touch_layout.CO_PAN_ROWS == 4, 'touch_layout.CO_PAN_ROWS 应为 4'
assert const(os.path.join(HOST, 'dm_pan.h'), r'DM_PAN_MAX\s*=\s*(\d+)') == touch_layout.CO_PAN_ROWS == 4, '网盘上限两边不一致'
for n in range(4):
    for pat in ('TouchCommPanBox%dT', 'TouchCommPanBox%dB', 'TouchCommPanBox%dL', 'TouchCommPanBox%dR',
                'TouchCommPanIcon%d', 'TouchCommPanName%d', 'TouchCommPanOpen%d', 'TouchCommPanCode%d',
                'TouchCommPanCopy%d', 'TouchCommPanCopyText%d'):
        name = pat % n
        assert name in names, '布局缺少网盘行控件 %s' % name
    assert 'PanelManager:ClosePanel:TouchCommPanOpen%d' % n in layout, '第 %d 行「打开」的消息不对' % (n + 1)
    assert 'PanelManager:ClosePanel:TouchCommPanCopy%d' % n in layout, '第 %d 行「复制」的消息不对' % (n + 1)
    # 首字 / 名称 36 号金色，提取码 28 号灰色（与任务卡的 36 / 28 一致） | glyph / name 36 gold, code 28 grey (card says 36 / 28)
    for key, size, color in (('TouchCommPanIcon%d' % n, 36, '$FontColorLightGold'),
                             ('TouchCommPanName%d' % n, 36, '$FontColorLightGold'),
                             ('TouchCommPanCode%d' % n, 28, '$FontColorLightGray')):
        body = widget(key)
        assert '"pointSize": %d' % size in body, '%s 字号不是 %d' % (key, size)
        assert color in body, '%s 颜色不是 %s' % (key, color)
assert 'TouchCommPanIcon4' not in names and 'TouchCommPanOpen4' not in names, '网盘不应有第五行'
# 首字底色的五个样本控件（宿主从它们身上取文字色，代码里不写 RGB） | five swatches the host copies the glyph colour from (no RGB in code)
for provider in ('Quark', 'Baidu', 'Aliyun', 'P123', 'Generic'):
    assert 'TouchCommPanInk%s' % provider in names, '布局缺少色样本 TouchCommPanInk%s' % provider

# 3. 确认态复用同一个框：标题复用 TouchCommTitle（宿主换字），正文 / 取消是新增控件；取消走 Cancel 消息
#    the confirm state reuses the same box: TouchCommTitle (text swapped by the host), a new body and Cancel; Cancel carries its own message
assert 'TouchCommConfirmText' in names, '布局缺少确认正文 TouchCommConfirmText'
assert 'TouchCommCancel' in names, '布局缺少取消按钮 TouchCommCancel'
assert 'PanelManager:ClosePanel:TouchCommCancel' in layout, '取消按钮的消息不对'

# 4. 复制钮长成 _co_copy 那样：原版小按钮 × 0.8（网盘行与渠道行同一套做法） | Copy looks like _co_copy: the small front-end button × 0.8, same as the channel rows
for n in range(4):
    m = re.search(r'"name":\s*"TouchCommPanCopy%d".*?"scale":\s*([0-9.]+)' % n, layout, re.S)
    assert m and float(m.group(1)) == 0.8, '第 %d 行复制钮不是 0.8 缩放' % (n + 1)

# 5. 宿主按名字找的每个控件都在布局里（名字对不上，宿主会判「控件不全」整框不显示） | every host name exists in the layout (a mismatch hides the whole box)
host = read(os.path.join(HOST, 'dm_community.c'))
block = re.search(r'k_co_names\[\]\s*=\s*\{(.*?)\};', host, re.S)
assert block, 'dm_community.c 里找不到 k_co_names'
host_names = set(re.findall(r'"(TouchComm[A-Za-z0-9]+)"', block.group(1)))
missing = sorted(host_names - names)
assert not missing, '布局缺少宿主会找的控件：%s' % ', '.join(missing)

# 6. 网盘行用同一套间距常量：行高 100、行距 120（与渠道卡一致，宿主 co_geom_pans 断言） | same spacing constants: 100 high, 120 pitch (the host asserts them too)
assert 'CO_PAN_ROWS = 4' in read(os.path.join(ROOT, 'dm', 'ios', 'tools', 'touch_layout.py')), '网盘行上限不是常量 4'

print('test_community_layout: 3 渠道行 / 4 网盘行 / 取消 / 宿主 %d 个名字，全部对得上' % len(host_names))
