"""Shared notice geometry and art; preserve native modal actions and sizes."""
import json

NOTICE_PRIORITY = 2147483645  # Above native tooltips (2147483644), within signed int.
MODAL_BACKGROUNDS = ('Modal_No_Title_BG', 'Modal_BG', 'Modal_BG_XL')


def notification(width, center_x, controller=False):
    xs = (0, 52, width - 52, width)
    ys = (0, 24, 102, 140, 220, 244)
    children = [{'type': 'ImageWidget',
        'name': 'TouchNoticeTile%d%d' % (row,col), 'fields': {
        'rect': {'x': xs[col], 'y': ys[row], 'width': xs[col+1]-xs[col], 'height': ys[row+1]-ys[row]},
        'filename': 'Ztouch_notification/tile%d%d' % (row,col)}} for row in range(5) for col in range(3)]
    children.append({'type': 'TextBoxWidget', 'name': 'Prompt', 'fields': {
        'rect': {'x': 64, 'y': 12, 'width': width - 128, 'height': 220},
        'fitToText': True,
        'style': '$StyleNotification', 'style/options': {'lineWrap': True, 'newlineHandling': 'standard'}}})
    if controller:
        # Native dismissal is owned by NotificationPanel, not a custom message.
        children.append({'type': 'ButtonWidget', 'name': 'Confirm',
                         'fields': {'fitToParent': True}})
    return {'type': 'NotificationPanel', 'name': 'NotificationPanel', 'fields': {
        'priority': NOTICE_PRIORITY, 'notificationSeconds': 5.0, 'transitionSeconds': 0.8,
        'rect': {'x': center_x, 'y': -790, 'width': width, 'height': 244},
        'anchor': {'x': 0.5, 'y': 1.0}}, 'children': [
        {'type': 'Widget', 'name': 'NotificationBorder', 'fields': {
            'rect': {'x': 0, 'y': 0, 'width': width, 'height': 244},
            'anchor': {'x': -0.5, 'y': 0.0}},
         'children': children}]}


def build_modal_art(romfs, outdir):
    """Change common backgrounds only; dimensions, actions and widgets stay native."""
    from pathlib import Path
    import romfs_art
    import gen_notification_frame as frame
    import gen_widestash_art as codec
    button = romfs_art.frame(romfs, frame.SOURCE)
    replacements = {}
    for name in MODAL_BACKGROUNDS:
        logical = 'hd/global/ui/Panel/Modals/' + name + '.sprite'
        original = romfs_art.read(romfs, logical)
        width, height = romfs_art.frame(romfs, logical).size
        image = frame.stretch_button(button, width, height)
        output = Path(outdir) / (name + '.sprite')
        output.write_bytes(codec.pack(codec.lib(), original, codec.bc3(image), width, height, width))
        replacements['data/' + logical.lower()] = str(output)
    return replacements


def build_notice_slices(romfs, outdir, width):
    from pathlib import Path
    import romfs_art
    import gen_notification_frame as frame
    import gen_widestash_art as codec
    image = frame.stretch_button(romfs_art.frame(romfs, frame.SOURCE), (width + 1) // 2, 122)
    source = romfs_art.read(romfs, frame.SOURCE)
    xs = (0,26,image.width-26,image.width)
    ys = (0,12,51,70,110,122)
    parts = {'tile%d%d' % (row,col): image.crop((xs[col],ys[row],xs[col+1],ys[row+1])) for row in range(5) for col in range(3)}
    replacements = {}
    for name, part in parts.items():
        output = Path(outdir) / ('notice_' + name + '.sprite')
        # Each path is private to one notice slice, with exactly one full frame.
        # The host adjusts only its draw dimensions; UVs and GPU texture stay
        # unchanged. A native NineTile would repeat the gradient and show seams.
        output.write_bytes(codec.pack(codec.lib(), source, codec.bc3(part), part.width, part.height, part.width))
        replacements['data/hd/global/ui/ztouch_notification/' + name + '.sprite'] = str(output)
    return replacements


def modal_priority(text):
    """Only change root stacking; keep every native field/action/child intact."""
    import re
    import touch_layout
    root = re.search(r'"type"\s*:\s*"([^"]+)"\s*,\s*"name"\s*:\s*"([^"]+)"', text)
    if not root or not root[2].endswith('Modal'):
        return text
    children = text.find('"children"')
    prefix = text[:children] if children >= 0 else text
    fields = re.search(r'"fields"\s*:\s*\{', prefix)
    if not fields:
        at = text.index('{') + 1
        return text[:at] + '\n"fields": {"priority": 2147483646},\n' + text[at:]
    end = touch_layout._block_end(text, fields.end() - 1)
    block = text[fields.end():end]
    if re.search(r'"priority"\s*:', block):
        block = re.sub(r'("priority"\s*:)\s*-?\d+', r'\g<1> 2147483646', block)
    else:
        block = '\n"priority": 2147483646,' + block
    return text[:fields.end()] + block + text[end:]


def build_modal_layers(romfs, outdir):
    from pathlib import Path
    import re
    import touch_layout
    replacements = {}
    for line in open(Path(romfs) / 'data/build_manifest.psv', encoding='utf-8'):
        fields = line.rstrip('\n').split('|')
        if len(fields) < 11:
            continue
        logical = fields[9].lower().removeprefix('data:')
        marker = 'data/global/ui/layouts/'
        if not logical.startswith(marker) or not re.search(r'modalhd\.json$', logical):
            continue
        path, source = touch_layout.read_layout(romfs, logical[len(marker):])
        result = modal_priority(source)
        if result == source:
            continue
        output = Path(outdir) / ('modal_layer_' + logical[len(marker):].replace('/', '_'))
        output.write_text(result, encoding='utf-8')
        replacements[path.lower()] = str(output)
    return replacements

# Audited controller modal routes: native controls remain intact.
MODAL_CONTROLLER_PAGES = ('controller/accountlinkingmodalhd.json', 'controller/addfriendmodalhd.json', 'controller/broadcastfriendsmessagemodalhd.json', 'controller/buttonbindingmodalhd.json', 'controller/characterdifficultymodalhd.json', 'controller/cinematicsmodalhd.json', 'controller/confirmationmodalhd.json', 'controller/console/characterdifficultymodalhd.json', 'controller/console/cinematicsmodalhd.json', 'controller/console/onlineplaymodemodalhd.json', 'controller/creditsmodalhd.json', 'controller/deletecharactermodalhd.json', 'controller/dismissablemodalhd.json', 'controller/dropgoldmodalhd.json', 'controller/eulamodalhd.json', 'controller/joingamemodalhd.json', 'controller/renamecharactermodalhd.json', 'controller/textinputmodalhd.json', 'controller/ukcompliancematuredmodalhd.json', 'controller/ukcomplianceunder18modalhd.json')
