"""现有自定义页中两位置换组开关的跨语言契约。 | Cross-language contract for the two-position switch in the existing Custom page. | Cross-language contract for the two-position switch in the existing Custom page."""
import importlib
import json
import os
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'ios/tools'))
os.environ.pop('MOVIE_FFMPEG', None)   # 缺省包的自定义页（「配音」行只在 MOVIE_FFMPEG=1 时有，下面另核）
import touch_layout as layout

out, _ = layout._custom_page('"name": "VideoOptionsPanel", "name": "OptionsTable"')
page = json.loads(re.sub(r',\s*([}\]])', r'\1', out))
nodes = {}
def walk(node):
    if isinstance(node, dict):
        if 'name' in node:
            assert node['name'] not in nodes, node['name']
            nodes[node['name']] = node
        for child in node.values():
            walk(child)
    elif isinstance(node, list):
        for child in node:
            walk(child)
walk(page)
for name in ('TouchCustom_touch_swap_above', 'TouchCustom_touch_swap_above_near'):
    assert nodes[name]['type'] == 'ToggleButtonWidget'
    assert nodes[name]['fields']['onClickMessage'].endswith(name)
assert nodes['TouchCustom_touch_swap_above_Text']['fields']['rect']['height'] == 360
for suffix in ('Near', 'Far'):
    assert any(n.get('fields', {}).get('text') == '@dmCustomTouchSwap' + suffix for n in nodes.values())
host_src = (root / 'host/dm_custom.c').read_text()
def host_variant(voice):   # 宿主按 VOICE_ROW（DM_MOVIE_FFMPEG）条件编译的两份键表：缺省去掉 #if VOICE_ROW 块，开关包只去掉预处理行
    keep = re.sub(r'#if VOICE_ROW\n(.*?)#endif\n', (lambda m: m[1]) if voice else '', host_src, flags=re.S)
    return re.sub(r'(?m)^#.*\n', '', keep)
for voice in (False, True):
    if voice:
        os.environ['MOVIE_FFMPEG'] = '1'; layout = importlib.reload(layout)
    host = host_variant(voice)
    keys = re.findall(r'"([a-z_0-9]+)"', re.search(r'k_keys\[\].*?=\s*\{(.*?)\}', host, re.S)[1])
    assert [key for key in keys if key not in ('eco_fps', 'q3d', 'fps_hud', 'upscale', 'render_res', 'rt_shadow')] == [key for key, _ in layout._CUSTOM_ALL], (voice, keys)   # 图形页六键（含显示帧数）不在自定义页 | the five Graphics-tab keys are not Custom-page rows
    assert keys[-3:] == ['upscale', 'render_res', 'rt_shadow']   # 末三键 = 图形页 K_UPSCALE / K_RES / K_RT | last three keys = Graphics-tab K_UPSCALE / K_RES / K_RT
    assert ('voice' in keys) == voice
    defaults = [int(x) for x in re.search(r'k_def\[NK\].*?=\s*\{(.*?)\}', host, re.S)[1].split(',')]
    assert len(defaults) == len(keys) and defaults[keys.index('touch_swap_above')] == 0
os.environ.pop('MOVIE_FFMPEG'); layout = importlib.reload(layout)
# 平台限制：DM_TARGET=android 的自定义页没有 icloud 行，其余行序不变（宿主 dm_custom.c custom_row 在没有 iCloud 的平台同样跳过）| per-platform: under DM_TARGET=android the Custom page has no icloud row, the other rows keep their order (the host's custom_row skips it likewise)
os.environ['DM_TARGET'] = 'android'; layout = importlib.reload(layout)
assert 'icloud' not in [k for k, _ in layout._CUSTOM_ALL] and [k for k, _ in layout._CUSTOM_ALL][5:7] == ['item_notes', 'pot_pick'], layout._CUSTOM_ALL
assert 'i == K_ICLOUD && !dm_icloud_supported()' in host_src
os.environ.pop('DM_TARGET'); layout = importlib.reload(layout)
assert 'icloud' in [k for k, _ in layout._CUSTOM_ALL]
host = host_src
assert 'TouchCustom_touch_swap_above_near' in host
print('PASS touch layout options: generated radio pair, labels, defaults, host order (default and MOVIE_FFMPEG=1) and scrolling row extent')

# Graphics controls live in the dedicated Graphics tab, not duplicated in Custom.
assert not any(name.startswith('TouchCustom_q3d') for name in nodes)
assert not any(name.startswith('TouchCustom_eco_fps') for name in nodes)
print('PASS Custom page excludes moved graphics controls')

# Graphics tab uses the game's native slider and toggle widgets in a separate panel without replacing native settings.
empty_rows = ''.join('{"type":"TableRowWidget","name":"Row %s"},' % name for name in (
    'Shared', 'WideItemDropEnabled', 'Online', 'MaxPlayers', 'LevelDifferenceEnabled', 'MaxLevelDifference',
    'AllowBnetFriendsJoin', 'PrivateGameSecurity', 'DesecratedZonesEnabled', 'OnlineSettingsEffectNote', 'Offline',
    'OfflineDesecratedZonesEnabled', 'OfflineNewGameSettingsEffectNote', 'OfflineDifficultyScaling', 'OfflineSettingsEffectNote'))
graphics_src = '{"type": "OnlineGameOptionsPanel", "name": "OnlineGameOptionsPanel", "children": [{"type": "ScrollViewWidget", "name": "ScrollView", "children": [{"type": "TableWidget", "name": "OptionsTable", "children": [' + empty_rows + ']}]}]}'
graphics_out, _ = layout._graphics_page(graphics_src)
graphics = json.loads(re.sub(r',\s*([}\]])', r'\1', graphics_out))
g_nodes = {}
def g_walk(node):
    if isinstance(node, dict):
        if 'name' in node: g_nodes[node['name']] = node
        for value in node.values(): g_walk(value)
    elif isinstance(node, list):
        for value in node: g_walk(value)
g_walk(graphics)
assert g_nodes['TouchGraphicsProfile']['type'] == 'SettingsSliderWidget'
assert g_nodes['TouchGraphicsFps']['type'] == 'SettingsSliderWidget'
assert g_nodes['TouchGraphicsFpsHud']['type'] == 'ToggleButtonWidget' and g_nodes['TouchGraphicsFpsHudRow']['fields']['rect']['y'] == 360   # 显示帧数在图形页第二行 | Show frame rate is Graphics row 2
assert not any(name == 'TouchCustom_fps_hud' for name in nodes)
assert g_nodes['TouchGraphicsUpscale']['type'] == 'ToggleButtonWidget'
assert g_nodes['TouchGraphicsApplyHint']['fields']['style/pointSize'] < 40
assert isinstance(g_nodes['TouchGraphicsApplyHint']['fields']['style/fontColor'], str)
assert g_nodes['TouchGraphicsApplyHint']['fields']['style/fontColor'].startswith('$FontColor')
assert g_nodes['TouchGraphicsProfile']['fields']['onUpdateMessage'].endswith('SetD2RUpscaleProfile')
assert '@dmGraphicsFrameRate60' in graphics_out and '@dmGraphicsUpscaling' in graphics_out
assert 'Row Offline' not in g_nodes and 'Row Online' not in g_nodes
assert not g_nodes['OptionsTable'].get('children')
assert g_nodes['TouchGraphicsRows']['type'] == 'Widget'
# 行序（用户 2026-10-27）：渲染分辨率 / 帧率 / MetalFX 开关 / 超分档位，每行 180 单位，档位行在最后；说明在档位行之后、不重叠
# Row order (user, 2026-10-27): render resolution / frame rate / MetalFX switch / upscale profile, 180 units each, profile row last; the hint follows it without overlapping
assert g_nodes['TouchGraphicsRes']['type'] == 'SettingsSliderWidget'
assert g_nodes['TouchGraphicsRes']['fields']['onUpdateMessage'].endswith('SetD2RRenderResolution')
assert g_nodes['TouchGraphicsResText']['fields']['text'] == '@dmGraphicsResAuto'
_res_y = g_nodes['TouchGraphicsResRow']['fields']['rect']['y']; _hint = g_nodes['TouchGraphicsApplyHint']['fields']['rect']
_rows_y = [_res_y, g_nodes['TouchGraphicsFpsHudRow']['fields']['rect']['y'], g_nodes['TouchGraphicsFpsText']['fields']['rect']['y'], g_nodes['TouchGraphicsUpscaleText']['fields']['rect']['y'], g_nodes['TouchGraphicsProfileRow']['fields']['rect']['y']]
assert _rows_y == [180, 360, 540, 720, 900], _rows_y   # 2026-10-28：第二行是显示帧数 | 2026-10-28: row 2 is Show frame rate
assert g_nodes['TouchGraphicsFps']['fields']['rect']['y'] == 540 + 55 and _hint['y'] >= _rows_y[-1] + 180
assert g_nodes['TouchGraphicsRows']['fields']['rect']['height'] >= _hint['y'] + _hint['height']
# 光追阴影（实验性）：自成一行，缺省摆在档位行之后（宿主按可见行数挪：档位行藏着时上移到 720），说明再往下；勾选框 + 标签都在行容器里，宿主按 mtl_rt_shadow_supported 藏整行 | Ray-traced shadows (experimental): its own row, laid out after the profile row (the host moves it by the visible row count: up to 720 while the profile row is hidden), the hint below it; checkbox + label inside the row container, the host hides the whole row via mtl_rt_shadow_supported
_rt = g_nodes['TouchGraphicsRtRow']['fields']['rect']; assert _rt['y'] == _rows_y[-1] + 180 and _hint['y'] == _rt['y'] + 180, (_rt, _hint)
assert g_nodes['TouchGraphicsRtShadow']['type'] == 'ToggleButtonWidget' and g_nodes['TouchGraphicsRtText']['fields']['text'] == '@dmGraphicsRtShadow'
print('PASS Graphics page: native FPS/profile sliders, independent upscale toggle, localization hooks')

# Regression: adding Graphics must preserve all original destinations and native settings.
settings_src = '{"name":"SettingsPanel","children":[{"type":"TabBarWidget","name":"OptionsTabs","fields":{"textStyle":{"pointSize":"$MediumFontSize","options":{"lineWrap":true},"alignment":{"h":"center","v":"center"},"spacing":{"leading":0.9,"kerning":0.95}},"tabCount":5,"inactiveFrames":[0,2,4,6,8],"activeFrames":[1,3,5,7,9],"textStrings":["@OptionsMenuVideo","@Audio","@Gameplay","@GameSettingsTab","@Controls"],"tabMessages":["SettingsPanelMessage:CheckChanges:VideoOptionsPanel","SettingsPanelMessage:CheckChanges:SoundOptionsPanel","SettingsPanelMessage:CheckChanges:GameplayOptionsPanel","SettingsPanelMessage:CheckChanges:OnlineGameOptionsPanel","SettingsPanelMessage:CheckChanges:KeyBindingMenu"]}}]}'
for transform in (lambda s: layout._settings_tabs(s)[0], layout.pad_settings):
    tab=json.loads(transform(settings_src))['children'][0]['fields']
    assert tab['tabCount']==len(tab['textStrings'])==len(tab['tabMessages'])==len(tab['inactiveFrames'])==len(tab['activeFrames'])==5
    destinations=[m.rsplit(':',1)[1] for m in tab['tabMessages']]
    assert len(set(destinations))==5
    assert set(destinations)=={'SoundOptionsPanel','GameplayOptionsPanel','OnlineGameOptionsPanel','VideoOptionsPanel',layout.GRAPHICS_PANEL}
    assert '@dmTabGameSettings' in tab['textStrings'] and '@dmTabGraphics' in tab['textStrings']
    assert tab['textStyle']['pointSize']==40 and tab['textStyle']['options']['lineWrap'] is False
    assert tab['textStyle']['spacing']=={'leading':0.9,'kerning':0.95}
    assert tab['textStyle']['alignment']=={'h':'center','v':'center'}
    twice=transform(transform(settings_src))
    assert transform(settings_src)==twice
    assert 'textStyle/' not in twice

native_row={'type':'TableRowWidget','name':'Row OfflineDesecratedZonesEnabled','children':[{'type':'ToggleButtonWidget','name':'OfflineDesecratedZonesEnabled','fields':{'onClickMessage':'OptionsPanelMessage:NativeTerrorZoneToggle','text':'@TerrorZonesEnabled','tooltipString':'@TerrorZonesEnabledTooltip','sentinel':{'default':True,'condition':'native'}}}]}
future_row={'type':'TableRowWidget','name':'Row FutureNativeSetting','children':[{'name':'FutureControl','fields':{'onUpdateMessage':'NativeFuture:Update','maximum':123}}]}
stock={'type':'OnlineGameOptionsPanel','name':'OnlineGameOptionsPanel','children':[{'type':'TableWidget','name':'OptionsTable','fields':{'rowHeight':'$OptionsTableRowHeight'},'children':[native_row,future_row,{'type':'TableRowWidget','name':'Row OfflineDifficultyScaling'},{'type':'TableRowWidget','name':'Row OfflineSettingsEffectNote'}]}]}
for transform in (layout.PRESENT['onlinegameoptionspanelhd.json'],layout.PAD_ROOT['onlinegameoptionspanelhd.json']):
    restored,_=transform(json.dumps(stock));restored=json.loads(re.sub(r',\s*([}\]])',r'\1',restored))
    assert restored['children'][0]['fields']['rowHeight']==180
    assert restored['children'][0]['children']==[native_row,future_row], 'Native controls, future controls and their original fields must survive'
    assert 'TouchGraphics' not in json.dumps(restored), 'Graphics must not occupy the native settings page'
print('PASS settings preservation: five unique destinations in both modes; native Terror Zone/future controls retain fields and callbacks')

# Actual console assets can have only four frame IDs despite five declared tabs.
four_frames=settings_src.replace('[0,2,4,6,8]','[0,2,4,6]').replace('[1,3,5,7,9]','[1,3,5,7]')
for transform in (lambda s: layout._settings_tabs(s)[0],layout.pad_settings):
    fields=json.loads(transform(four_frames))['children'][0]['fields']
    assert fields['inactiveFrames']==[0,2,4,6,6]
    assert fields['activeFrames']==[1,3,5,7,7]
print('PASS native four-frame artwork extends safely to five tabs')

# Console override lacks a local textStyle and inherits the base style.
no_style = re.sub(r'"textStyle":\{.*?"tabCount"', '"tabCount"', settings_src)
for fixture in (settings_src,no_style):
    out,_=layout._settings_tabs(fixture)
    tab=json.loads(out)['children'][0]['fields']
    assert tab['textStyle']['pointSize']==40
    assert tab['textStyle']['options']['lineWrap'] is False
    assert layout._settings_tabs(out)[0]==out
print('PASS inherited console style receives a complete nested tab style')

# 实验性功能要在设置页标出来（用户 2026-10-27）：MetalFX 开关名六套文本都带「实验性」标记，且不比原文长太多（标签宽 1000 单位）
# Experimental features are marked on the settings page (user, 2026-10-27): the MetalFX switch name carries the experimental marker in all six languages, without growing much past the original (label width 1000 units)
EXPERIMENTAL = ('实验性', 'experimental', 'experimental', 'experimental', 'эксперимент', '실험적')
tsv = {l.split('\t')[0]: l.split('\t') for l in (root / 'i18n' / 'ui_text.tsv').read_text(encoding='utf-8').splitlines() if '\t' in l}
for cell, mark in zip(tsv['dmGraphicsUpscaling'][1:7], EXPERIMENTAL):
    assert mark in cell, (cell, mark)
    assert len(cell) <= 40, cell
print('PASS MetalFX switch is marked experimental in every language')

# 页签顺序（用户 2026-10-27）：音频 / 图形 / 游戏 / 游戏设定 / 自定义，文字与目标面板一一对应
# Tab order (user, 2026-10-27): Audio / Graphics / Gameplay / Game Settings / Custom, with labels and destination panels paired
assert layout._SETTINGS_LABELS == ('@Audio', '@dmTabGraphics', '@Gameplay', '@dmTabGameSettings', '@dmTabCustom')
assert layout._SETTINGS_PANELS == ('SoundOptionsPanel', layout.GRAPHICS_PANEL, 'GameplayOptionsPanel', 'OnlineGameOptionsPanel', 'VideoOptionsPanel')
print('PASS settings tabs: Graphics right after Audio')

# 宿主手柄选行与布局同序：行号 0–3 = 渲染分辨率 / 帧率 / 超分开关 / 超分档位，选中框 y = 170 + 180 × 可见序号（与上面 _rows_y 对应）
# The host's controller row selection matches the layout: rows 0–3 = render resolution / frame rate / upscale switch / profile, selection frame y = 170 + 180 × visible index (matching _rows_y above)
_custom = (root / 'host' / 'dm_custom.c').read_text(encoding='utf-8')
assert 'y = pad ? 170 + 180 * selected : 0' in _custom
for _row, _name in enumerate(('ROW_RES', 'ROW_FPSHUD', 'ROW_FPS', 'ROW_UPSCALE', 'ROW_PROFILE')):
    assert 170 + 180 * _row + 10 == _rows_y[_row], (_name, _rows_y)
assert 'row_id == ROW_FPS' in _custom and 'row_id == ROW_FPSHUD' in _custom and 'row_id == ROW_UPSCALE' in _custom and 'row_id == ROW_PROFILE' in _custom and 'row_id == ROW_RES' in _custom and 'row_id == ROW_RT' in _custom
# 光追行（实验性）接在最后：手柄行号 4；可见行紧挨着排，选中框按可见序号放；档位行藏着时光追行与说明各上移一格（与布局缺省 900 / 1080 对应）
# Ray-tracing row (experimental) comes last: controller row 4; visible rows are packed and the selection frame follows the visible index; with the profile row hidden the ray-tracing row and the hint each move up one slot (matching the layout defaults 900 / 1080)
# 可见行由 graphics_rows_pack 紧挨着排（f705573 起：安卓藏分辨率 / 超分行时后面的行上移），行号取 vis[selected]；说明行接在最后一个可见行下面；全部可见时说明在 180 × 7 = 1260
# Visible rows are packed by graphics_rows_pack (since f705573: with the resolution / upscale rows hidden on Android the later rows move up), row_id = vis[selected]; the hint follows the last visible row; with everything visible the hint sits at 180 × 7 = 1260
assert 'enum { ROW_RES, ROW_FPSHUD, ROW_FPS, ROW_UPSCALE, ROW_PROFILE, ROW_RT }' in _custom
assert 'int row_id = vis[selected];' in _custom and 'graphics_rows_pack(res_ok, up_ok, prof_on, rt != 0, vis)' in _custom
assert 'case ROW_RT: if (rtrow) move_ours_y(rtrow, y); break;' in _custom and 'case ROW_FPSHUD: move_ours_y(find_w(sp, "TouchGraphicsFpsHudRow", 0), y); break;' in _custom
assert 'move_ours_y(find_w(sp, "TouchGraphicsApplyHint", 0), 180 + 180 * (nv + 1))' in _custom and 180 + 180 * 6 == _hint['y']
print('PASS Graphics rows: render resolution first, host controller rows in the same order')
# 局内选项页的手柄提示条（2026-10-27：触屏下「全部接受 / 默认 / 返回」露在「应用 / 默认 / 放弃」下面）：Console 这份 basedOn 原版手柄基页（字面路径，宿主改写前缀管不到），
# 所以触屏这份自己把 Legend 挪出屏；手柄这份（pad_settings）原样留着提示条。夹具照原版 Console 那份的形状：Legend 没有 fields、带尾逗号
# In-game options page controller legend (2026-10-27: under touch "Accept All / Default / Back" showed below 「应用 / 默认 / 放弃」): the Console copy is basedOn the original controller base page (a literal path the host prefix rewrite does not reach),
# so the touch copy moves the Legend off screen itself; the controller copy (pad_settings) keeps the legend. The fixture follows the shape of the original Console copy: Legend without fields, trailing commas
console_src = ('{\n    "basedOn": "Controller/SettingsPanelHD.json",\n    "type": "SettingsPanel", "name": "SettingsPanel",\n    "children": [\n'
               '        { "type": "ClickCatcherWidget", "name": "catcher", },\n'
               '        { "type": "RectangleWidget", "name": "Background", "children": [ { "type": "Widget", "name": "Anchor", "children": [\n'
               '            { "type": "ImageWidget", "name": "Background", },\n            { "type": "TextBoxWidget", "name": "Title", },\n'
               '            { "type": "ButtonLegendWidget", "name": "Legend", },\n        ] }, ] },\n'
               '        { "type": "ImageWidget", "name": "SettingsBackground", "children": [ { "type": "ControllerCursorBoundsWidget", "name": "ControllerCursorBounds", }, ] },\n'
               '        ' + json.dumps(json.loads(settings_src)['children'][0]) + ',\n    ]\n}\n')
def legend_of(text):
    found = []
    def walk_legend(node):
        if isinstance(node, dict):
            if node.get('type') == 'ButtonLegendWidget': found.append(node)
            for value in node.values(): walk_legend(value)
        elif isinstance(node, list):
            for value in node: walk_legend(value)
    walk_legend(json.loads(re.sub(r',(\s*[}\]])', r'\1', text)))
    assert len(found) == 1, found
    return found[0]
touch_console, _ = layout.PRESENT['controller/console/settingspanelhd.json'](console_src)
rect = legend_of(touch_console)['fields']['rect']
assert rect['x'] >= 99999 and rect['y'] >= 99999, rect   # 触屏：挪出屏 | touch: off screen
assert 'fields' not in legend_of(layout.pad_settings(console_src))   # 手柄：不动，照旧继承原版提示条 | controller: untouched, still inherits the native legend
assert json.loads(re.sub(r',(\s*[}\]])', r'\1', touch_console))['children'][-1]['fields']['tabCount'] == 5   # 页签变换照常 | the tab transform still applies
front, _ = layout.PRESENT['controller/settingspanelhd.json']('{"children":[{"type": "ButtonLegendWidget", "name": "Legend", "fields": {"rect": {"x": 0, "y": 1661, "width": 1950, "height": 103}}}]}')
assert legend_of(front)['fields']['rect']['y'] >= 99999   # 前端那份照旧挪走 | the front-end copy is still moved away
print('PASS options legend: touch in-game and front-end copies move the controller legend off screen; controller copy keeps it')
