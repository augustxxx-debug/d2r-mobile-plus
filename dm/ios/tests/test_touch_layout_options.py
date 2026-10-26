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
    assert [key for key in keys if key not in ('eco_fps', 'q3d', 'upscale', 'render_res')] == [key for key, _ in layout._CUSTOM_ALL], (voice, keys)   # 图形页四键不在自定义页 | the four Graphics-tab keys are not Custom-page rows
    assert keys[-2:] == ['upscale', 'render_res']
    assert ('voice' in keys) == voice
    defaults = [int(x) for x in re.search(r'k_def\[NK\].*?=\s*\{(.*?)\}', host, re.S)[1].split(',')]
    assert len(defaults) == len(keys) and defaults[keys.index('touch_swap_above')] == 0
os.environ.pop('MOVIE_FFMPEG'); layout = importlib.reload(layout)
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
assert g_nodes['TouchGraphicsUpscale']['type'] == 'ToggleButtonWidget'
assert g_nodes['TouchGraphicsApplyHint']['fields']['style/pointSize'] < 40
assert isinstance(g_nodes['TouchGraphicsApplyHint']['fields']['style/fontColor'], str)
assert g_nodes['TouchGraphicsApplyHint']['fields']['style/fontColor'].startswith('$FontColor')
assert g_nodes['TouchGraphicsProfile']['fields']['onUpdateMessage'].endswith('SetD2RUpscaleProfile')
assert '@dmGraphicsFrameRate60' in graphics_out and '@dmGraphicsUpscaling' in graphics_out
assert 'Row Offline' not in g_nodes and 'Row Online' not in g_nodes
assert not g_nodes['OptionsTable'].get('children')
assert g_nodes['TouchGraphicsRows']['type'] == 'Widget'
# 渲染分辨率行：四档滑块与标签在档位行之后、说明之前，不与说明重叠 | Render resolution row: four-position slider and label after the profile row and before the hint, without overlapping it
assert g_nodes['TouchGraphicsRes']['type'] == 'SettingsSliderWidget'
assert g_nodes['TouchGraphicsRes']['fields']['onUpdateMessage'].endswith('SetD2RRenderResolution')
assert g_nodes['TouchGraphicsResText']['fields']['text'] == '@dmGraphicsResAuto'
_res_y = g_nodes['TouchGraphicsResRow']['fields']['rect']['y']; _hint = g_nodes['TouchGraphicsApplyHint']['fields']['rect']
assert _res_y == g_nodes['TouchGraphicsProfileRow']['fields']['rect']['y'] + 180 and _hint['y'] >= _res_y + 180
assert g_nodes['TouchGraphicsRows']['fields']['rect']['height'] >= _hint['y'] + _hint['height']
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
