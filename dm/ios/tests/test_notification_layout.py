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
