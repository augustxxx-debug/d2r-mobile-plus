"""Notice styles share geometry; modal transforms preserve every native action."""
import json, re, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import notification_layout as n

for width in (1680, 1800):
    touch, pad = n.notification(width, 0), n.notification(width, 0, True)
    assert touch['fields'] == pad['fields']
    assert touch['fields']['priority'] > 2147483644
    frame = touch['children'][0]
    assert frame['type'] == 'Widget', 'ImageWidget would replace the frame dimensions with its texture size'
    nodes = {x['name']: x for x in frame['children']}
    assert 'anchor' not in nodes['Prompt']['fields']
    assert nodes['Prompt']['fields']['rect']['x'] == 64
    assert nodes['Prompt']['fields']['fitToText']
    assert nodes['Prompt']['fields']['style/options']['lineWrap']
    assert nodes['Prompt']['fields']['rect']['width'] == width - 128
    # Prompt 一字不差照原版（审计 r2 P1-4）：只有 $StyleNotification，不改字号 / 间距 / 颜色，所有通知（原版系统提示、别的 Plus 通知）观感不变
    # Prompt exactly as native (audit r2 P1-4): $StyleNotification only, no size / spacing / colour override, so every notice keeps its native look
    assert nodes['Prompt'] == {'type': 'TextBoxWidget', 'name': 'Prompt', 'fields': {
        'rect': {'x': 64, 'y': 12, 'width': width - 128, 'height': 220}, 'fitToText': True,
        'style': '$StyleNotification', 'style/options': {'lineWrap': True, 'newlineHandling': 'standard'}}}, nodes['Prompt']
    # B035 两级提示：自己的两个控件，正文 36 号主色、副文 28 号浅灰（CLAUDE.md §2.6，原版色票不手写 RGB）| B035 two-level tip: two widgets of our own, 36 pt primary body and 28 pt light-grey subtext (original colour tokens, no hand-written RGB)
    body, sub = nodes['TouchNoticeBody'], nodes['TouchNoticeSub']
    for w, size in ((body, 36), (sub, 28)):
        assert w['type'] == 'TextBoxWidget' and w['fields']['fitToText'] and w['fields']['style/options']['lineWrap']
        assert w['fields']['style'] == '$StyleNotification'   # 保留原版样式种类，原生文字度量才认（dm_cursor.c +0xb0 == 14） | keep the original style kind so native text measurement recognises it (dm_cursor.c +0xb0 == 14)
        assert w['fields']['style/pointSize'] == size and w['fields']['style/spacing'] == '$ReducedSpacing'
        assert w['fields']['rect']['x'] == 99999 and w['fields']['rect']['y'] == 99999 and w['fields']['rect']['width'] == width - 128   # 不用时在屏外，宿主只在两级提示时摆进来 | off screen while unused; the host places them only for a two-level tip
        assert 'text' not in w['fields']   # 字只由宿主在两级提示时写 | text is written by the host only for a two-level tip
    assert sub['fields']['style/fontColor'] == '$FontColorLightGray' and 'style/fontColor' not in body['fields']   # 正文主色 = $StyleNotification 的白 | body primary colour = $StyleNotification white
    names = [x['name'] for x in frame['children']]
    assert names.index('Prompt') < names.index('TouchNoticeBody') < names.index('TouchNoticeSub')   # 画在 Prompt 之后（同层后画的在上） | drawn after Prompt
    assert nodes['TouchNoticeTile00']['fields']['rect']['width'] == nodes['TouchNoticeTile02']['fields']['rect']['width'] == 52
    assert len([name for name in nodes if name.startswith('TouchNoticeTile')]) == 15
    assert all(x['type']=='ImageWidget' and 'tileScale' not in x['fields']
               for name,x in nodes.items() if name.startswith('TouchNoticeTile'))
    assert pad['children'][0]['children'][:-1] == touch['children'][0]['children']
    assert pad['children'][0]['children'][-1]['name'] == 'Confirm'

for fields in ({'priority': 200, 'isDismissable': True}, {'anchor': {'x': .5}}, None):
    original = {'basedOn': 'ConfirmationModalHD.json', 'type': 'ConfirmationModal', 'name': 'ExampleModal',
                'children': [{'type': 'ButtonWidget', 'name': 'Confirm', 'fields': {'onClickMessage': 'ModalMessage:Confirm', 'text': '@LocalizedConfirm'}},
                             {'type': 'ButtonWidget', 'name': 'Dismiss', 'fields': {'onClickMessage': 'ModalMessage:Dismiss'}}]}
    if fields is not None: original['fields'] = fields
    # Native files put root fields before children, even though dictionary order need not.
    original = {k:original[k] for k in ('basedOn','type','name','fields','children') if k in original}
    result = json.loads(n.modal_priority(json.dumps(original)))
    expected = dict(original); expected['fields'] = dict(fields or {}, priority=2147483646)
    assert result == expected
    assert n.modal_priority(json.dumps(result)) == json.dumps(result)
assert n.modal_priority('{"type":"FatalErrorPanel","name":"FatalErrorPanel"}') == '{"type":"FatalErrorPanel","name":"FatalErrorPanel"}'
print('PASS shared notification geometry/native dismissal, fitted multilingual text, intact caps, modal layering and callback preservation')

# 四审 Q4-05：原版 NotificationPanel 每帧按 y = 进度 × 目标 插值（main+0x3d7fbc，起点写死 0 = 屏幕底边以下），
# 滑入 / 滑出途中整框都在底部 UIKit 底栏后面，镂空的药水格里露出通知。过渡时间缩到一帧之内后，第一帧之后就只有「底边以下」和「停靠位」两种位置。
# Fourth review Q4-05: native NotificationPanel interpolates y = progress * target each frame (main+0x3d7fbc, start fixed at 0 = below the bottom edge);
# during slide-in / slide-out the box passes behind the bottom UIKit bar and shows through the belt cut-outs. With the transition shorter than a frame only "below the edge" and "docked" remain.
def native_y(target, elapsed, seconds, showing):
    t = elapsed / seconds
    y = int(t * target) if showing else int((1 - t) * target)
    return max(target, min(y, 0))

for controller in (False, True):
    fields = n.notification(1680, 0, controller)['fields']
    target, seconds = fields['rect']['y'], fields['transitionSeconds']
    assert 'e' not in json.dumps(seconds), 'layout parser gets a plain decimal'
    for showing in (True, False):
        for frame in range(1, 61):   # 60 / 120 帧下第 1 帧起（最早一帧离开始至少 8 ms） | from the first frame at 60 / 120 fps (at least 8 ms after the start)
            for hz in (60, 120):
                y = native_y(target, frame / hz, seconds, showing)
                assert y in (0, target), ('notice crosses the bottom bar', controller, showing, frame, hz, y)
print('PASS notice never slides through the bottom bar (Q4-05)')

# 真包核对（DM_ROMFS 指到 3.3.94012 RomFS 时）：$StyleNotification 原值，Prompt 不改它 | real package check (when DM_ROMFS points at the 3.3.94012 RomFS): native $StyleNotification values that Prompt keeps
import os
romfs = os.environ.get('DM_ROMFS')
if romfs and os.path.isdir(romfs):
    import touch_layout
    _, profile = touch_layout.read_layout(romfs, '_profilehd.json')
    style = re.search(r'"StyleNotification"\s*:\s*\{(.*?)\}\s*,\s*"', profile, re.S)[1]
    assert '"pointSize": "$MediumFontSize"' in style and '"fontColor": "$FontColorWhite"' in style and 'spacing' not in style, style
    assert re.search(r'"MediumFontSize"\s*:\s*26\b', profile)
    print('PASS native $StyleNotification (94012): Exocet $MediumFontSize 26, white, default spacing; Prompt does not override it')
else:
    print('SKIP native $StyleNotification check: DM_ROMFS not set')
