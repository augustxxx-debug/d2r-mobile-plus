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
