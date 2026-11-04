"""现有自定义页中两位置换组开关的跨语言契约。 | Cross-language contract for the two-position switch in the existing Custom page. | Cross-language contract for the two-position switch in the existing Custom page."""
import importlib
import json
import os
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'ios/tools'))
os.environ.pop('MOVIE_FFMPEG', None)   # 缺省包的自定义页（「配音」行现在缺省包也有，与过场解码开关无关）
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
# 换组键位置搬到游戏页的 Plus 块（方案 C）：两项单选，两个勾选框，各自发消息 | the swap-button position moved to the Gameplay page's Plus block (plan C): a two-option radio, two checkboxes with their own messages
game_rows = layout._game_rows()
for name in ('TouchCustom_touch_swap_above', 'TouchCustom_touch_swap_above_near'):
    assert re.search(r'"type": "ToggleButtonWidget", "name": "%s", "fields"' % name, game_rows)
    assert '"onClickMessage": "PanelManager:ClosePanel:%s"' % name in game_rows
assert re.search(r'"name": "TouchCustom_touch_swap_above_Text", "fields": \{ "rect": \{ "x": 0, "y": \d+, "width": 1000, "height": 360 \}', game_rows)
for suffix in ('Near', 'Far'):
    assert '"text": "@dmCustomTouchSwap%s"' % suffix in game_rows
host_src = (root / 'host/dm_custom.c').read_text()
# K_ICLOUD_KEY（e1c459bd，§2.8）：安卓用占位名，这里测的是 Apple 一侧的布局，按非安卓那条宏展开 | K_ICLOUD_KEY (e1c459bd): Android uses a placeholder; this test covers the Apple layout, so expand the non-Android definition
_ik = re.search(r'#else\n#define K_ICLOUD_KEY ("[a-z_]+")\n#endif', host_src); assert _ik and _ik[1] == '"icloud"', 'K_ICLOUD_KEY 的 Apple 定义找不到'
host_src = host_src.replace('K_ICLOUD_KEY,', _ik[1] + ',')
def host_variant():   # VOICE_ROW 恒为 1（voice 行不再跟 MOVIE_FFMPEG 走）：保留 #if VOICE_ROW 块，只去掉预处理行 | VOICE_ROW is always 1: keep the #if VOICE_ROW blocks and strip only the directives
    keep = re.sub(r'#if VOICE_ROW\n(.*?)#endif\n', r'\1', host_src, flags=re.S)
    return re.sub(r'(?m)^#.*\n', '', keep)
host = host_variant()
keys = re.findall(r'"([a-z_0-9]+)"', re.search(r'k_keys\[\].*?=\s*\{(.*?)\}', host, re.S)[1])
assert sorted(key for key in keys if key not in ('eco_fps', 'q3d', 'bright_level', 'fps_hud', 'upscale', 'render_res', 'rt_level')) == sorted([key for key, _ in layout._CUSTOM_ALL] + list(layout._GAME_KEYS)), keys   # 图形页七键（含显示帧数、画面加亮、光追档位）不在系统 / 游戏页 | the seven Graphics-tab keys are not System / Gameplay rows
assert keys[-5:] == ['bright_level', 'fps_hud', 'upscale', 'render_res', 'rt_level'] and keys[-8:-5] == ['belt_refill', 'tel_stats', 'auto_crash']   # 末五键 = 图形页 K_BRIGHT / K_FPSHUD / K_UPSCALE / K_RES / K_RTLVL，之前是 K_TELSTATS / K_AUTOCRASH | last five keys = Graphics-tab K_BRIGHT .. K_RTLVL, preceded by K_TELSTATS / K_AUTOCRASH
assert keys[0] == 'lang' and keys[1] == 'voice'   # 配音行在语言行之后（缺省包也有）| voice follows language on every build
for name in ('TouchCustom_voice', 'TouchCustom_voice_zh', 'TouchCustom_voice_Text'): assert name in nodes, name   # 触屏系统页真的摆出了配音单选（两个勾选框）| the touch System page really lays out the voice radio (both checkboxes)
defaults = [int(x) for x in re.search(r'k_def\[NK\].*?=\s*\{(.*?)\}', host, re.S)[1].split(',')]
assert len(defaults) == len(keys) and defaults[keys.index('touch_swap_above')] == 0
assert defaults[keys.index('free_tp')] == 1   # 免费回城缺省开（用户 2026-10-30，RC2 起） | free town portal on by default (user 2026-10-30, from RC2)
assert defaults[keys.index('bright_level')] == 2   # 画面加亮缺省「中」（用户方案 C，B098 中 = 上限 2；审计 r1 P2-6） | brightening defaults to mid (plan C, B098 mid = cap 2; audit r1 P2-6)
# 平台限制：DM_TARGET=android 的自定义页没有 icloud 行，其余行序不变（宿主 dm_custom.c custom_row 在没有 iCloud 的平台同样跳过）| per-platform: under DM_TARGET=android the Custom page has no icloud row, the other rows keep their order (the host's custom_row skips it likewise)
os.environ['DM_TARGET'] = 'android'; layout = importlib.reload(layout)
assert 'icloud' not in [k for k, _ in layout._CUSTOM_ALL] and 'drop_haptic' not in layout._GAME_KEYS, layout._GAME_KEYS
assert 'i == K_ICLOUD && !dm_icloud_supported()' in host_src
os.environ.pop('DM_TARGET'); layout = importlib.reload(layout)
assert 'icloud' in [k for k, _ in layout._CUSTOM_ALL]
host = host_src
assert 'TouchCustom_touch_swap_above_near' in host
print('PASS touch layout options: generated radio pair, labels, defaults, host order (voice row on every build) and scrolling row extent')

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
assert g_nodes['TouchGraphicsFpsHud']['type'] == 'ToggleButtonWidget' and g_nodes['TouchGraphicsFpsHudRow']['fields']['rect']['y'] == 720   # 显示帧数在帧率组第一行（显示组：分辨率 / 加亮）| Show frame rate leads the frame group (display group: resolution / brightening)
assert not any(name == 'TouchCustom_fps_hud' for name in nodes)
assert g_nodes['TouchGraphicsUpscale']['type'] == 'ToggleButtonWidget'
assert g_nodes['TouchGraphicsApplyHint']['fields']['style/pointSize'] < 40
assert isinstance(g_nodes['TouchGraphicsApplyHint']['fields']['style/fontColor'], str)
assert g_nodes['TouchGraphicsApplyHint']['fields']['style/fontColor'].startswith('$FontColor')
assert g_nodes['TouchGraphicsProfile']['fields']['onUpdateMessage'].endswith('SetD2RUpscaleProfile')
assert '@dmGraphicsFrameRate60' in graphics_out and '@dmGraphicsUpscaling' in graphics_out
# 图形页说明按平台分：iOS 原文案，安卓中性文案、整段说明不提 MetalFX（B079）| Graphics page hint per platform: iOS keeps its text, Android is neutral and does not mention MetalFX (B079)
_and_row = [l for l in open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../i18n/ui_text.tsv'), encoding='utf-8') if l.startswith('dmAndGraphicsApplyHint\t')]
assert len(_and_row) == 1 and 'MetalFX' not in _and_row[0].rsplit('\t', 1)[0]
assert g_nodes['TouchGraphicsApplyHint']['fields']['text'] == '@dmGraphicsApplyHint'
os.environ['DM_TARGET'] = 'android'; layout = importlib.reload(layout)
_and_out, _ = layout._graphics_page(graphics_src)
assert '@dmAndGraphicsApplyHint' in _and_out and '@dmGraphicsApplyHint' not in _and_out
os.environ.pop('DM_TARGET'); layout = importlib.reload(layout)
assert 'Row Offline' not in g_nodes and 'Row Online' not in g_nodes
assert not g_nodes['OptionsTable'].get('children')
assert g_nodes['TouchGraphicsRows']['type'] == 'Widget'
# 行序（用户 2026-10-27）：渲染分辨率 / 帧率 / MetalFX 开关 / 超分档位，每行 180 单位，档位行在最后；说明在档位行之后、不重叠
# Row order (user, 2026-10-27): render resolution / frame rate / MetalFX switch / upscale profile, 180 units each, profile row last; the hint follows it without overlapping
assert g_nodes['TouchGraphicsRes']['type'] == 'SettingsSliderWidget'
assert g_nodes['TouchGraphicsRes']['fields']['onUpdateMessage'].endswith('SetD2RRenderResolution')
assert g_nodes['TouchGraphicsResText']['fields']['text'] == '@dmGraphicsResAuto'
_res_y = g_nodes['TouchGraphicsResRow']['fields']['rect']['y']; _hint = g_nodes['TouchGraphicsApplyHint']['fields']['rect']
_rows_y = [_res_y, g_nodes['TouchGraphicsBrightRow']['fields']['rect']['y'], g_nodes['TouchGraphicsFpsHudRow']['fields']['rect']['y'], g_nodes['TouchGraphicsFpsText']['fields']['rect']['y'],
           g_nodes['TouchGraphicsUpscaleText']['fields']['rect']['y'], g_nodes['TouchGraphicsProfileRow']['fields']['rect']['y'], g_nodes['TouchGraphicsRtRow']['fields']['rect']['y']]
# 布局初始 y 与宿主全部可见（Apple）时的排法逐行一致（审计 r1 P2-5 补回）：这张表 t_graphics_gate 用 graphics_row_y 核宿主那一侧，两边任一侧漂了都会红
# The layout's initial y matches the host's all-visible (Apple) packing row by row (audit r1 P2-5, restored): t_graphics_gate checks the host side of the same table with graphics_row_y, so drift on either side fails
GRAPHICS_ALL_VISIBLE_Y = [180, 360, 720, 900, 1260, 1440, 1800]   # 分辨率 / 加亮 | 显示帧数 / 帧率 | 超分开关 / 档位 | 光追档位（每换一组多一行标题）| resolution / brightening | fps hud / fps | upscale / profile | rt level (one heading row per group)
assert _rows_y == GRAPHICS_ALL_VISIBLE_Y, _rows_y
_gate = (root / 'host/tests/t_graphics_gate.c').read_text(encoding='utf-8')
assert 'static const int want[7] = { %s };' % ', '.join(map(str, GRAPHICS_ALL_VISIBLE_Y)) in _gate   # 同一张表 | the same table
assert [g_nodes[h]['fields']['rect']['y'] for h in ('TouchOptHeadFrame', 'TouchOptHeadUpscale', 'TouchOptHeadRt')] == [GRAPHICS_ALL_VISIBLE_Y[2] - 180, GRAPHICS_ALL_VISIBLE_Y[4] - 180, GRAPHICS_ALL_VISIBLE_Y[6] - 180]   # 组标题在组首行上一行（宿主 graphics_row_y - 180）| headings one row above the group's first row
assert g_nodes['TouchGraphicsFps']['fields']['rect']['y'] == _rows_y[3] + 55 and g_nodes['TouchGraphicsUpscale']['fields']['rect']['y'] == _rows_y[4] + 55
assert g_nodes['TouchGraphicsFpsNote']['fields']['rect']['y'] == _rows_y[3] + 116 and g_nodes['TouchGraphicsUpscaleNoHw']['fields']['rect']['y'] == _rows_y[4] + 116   # 宿主 y + 116 | host y + 116
assert _hint['y'] == _rows_y[-1] + 180 and 'graphics_row_y(all_vis, nv - 1) + 180' in (root / 'host/dm_custom.c').read_text(encoding='utf-8')
assert g_nodes['TouchGraphicsRows']['fields']['rect']['height'] >= _hint['y'] + _hint['height']
# 光追档位（实验性）：一行四档滑块，方案 C「光线追踪」组里唯一一行，缺省摆在超分档位行之后（宿主按可见行数挪），说明再往下；三行副文同款 28 号说明灰、在行内、不压滑块，宿主三选一显示；灰色文字给没有硬件光追的机器 | ray-tracing level (experimental): one four-position slider row, after the profile row by default (moved by the host per visible rows), the hint below; three subtexts in the same 28 pt grey, inside the row, clear of the slider, the host shows one; the grey caption is for machines without hardware ray tracing
_rt = g_nodes['TouchGraphicsRtRow']['fields']['rect']; assert _rt['y'] == _rows_y[-1] and _hint['y'] == _rt['y'] + 180, (_rt, _hint)
assert not any(n.startswith('TouchGraphicsRtAo') or n == 'TouchGraphicsRtShadow' for n in g_nodes)   # 光追组只有档位一行：旧的阴影 / AO 两个开关不进布局 | the ray-tracing group is the level row only: no old shadow / AO switches
assert g_nodes['TouchGraphicsRtLevel']['type'] == 'SettingsSliderWidget' and g_nodes['TouchGraphicsRtLevel']['fields']['onUpdateMessage'].endswith('SetD2RRtLevel')
assert g_nodes['TouchGraphicsRtText']['fields']['text'] == '@dmGraphicsRtLevelOff' and g_nodes['TouchGraphicsRtTextOff']['fields']['text'] == '@dmGraphicsRtLevelNone' and g_nodes['TouchGraphicsRtTextOff']['fields']['style/fontColor'] == '$FontColorLightGray'
for _n, _k in (('TouchGraphicsRtNote', '@dmGraphicsRtLevelNote'), ('TouchGraphicsRtHighNote', '@dmGraphicsRtLevelHighNote'), ('TouchGraphicsRtNoHw', '@dmGraphicsRtLevelNoHw')):
    _f = g_nodes[_n]['fields']; assert _f['text'] == _k and _f['style/pointSize'] == 28 and _f['style/fontColor'] == '$FontColorLightGray' and _f['style/spacing'] == '$ReducedSpacing', _f
    assert _f['rect']['y'] >= 80 and _f['rect']['y'] + _f['rect']['height'] <= 150 and _f['rect']['x'] + _f['rect']['width'] <= g_nodes['TouchGraphicsRtLevel']['fields']['rect']['x'], _f['rect']
# 安卓没有 Metal 光追：布局整行不出现（§2.8）| Android has no Metal ray tracing: the row is absent from its layout (§2.8)
assert 'TouchGraphicsRt' not in _and_out and 'dmGraphicsRtLevel' not in _and_out and 'TouchOptHeadRt' not in _and_out and 'dmOptGrpRt' not in _and_out   # 光追组标题也不进（§2.8）| nor the ray-tracing group heading
# 120 帧档不给高画质（用户 2026-10-29）：档位行里一行副文（28 号、说明灰、$ReducedSpacing，CLAUDE.md §2.6），宿主只在 120 帧格选着时显示；在档位文字下方、行内，不压下一行
# 120 fps gets no Quality (user 2026-10-29): one subtext line inside the profile row (28 pt, description grey, $ReducedSpacing, CLAUDE.md §2.6), shown by the host only while the 120 fps stop is selected; below the profile caption, inside the row, never over the next row
_note = g_nodes['TouchGraphicsProfileNote']['fields']
assert _note['text'] == '@dmGraphicsQuality120Note' and _note['style/pointSize'] == 28 and _note['style/fontColor'] == '$FontColorLightGray', _note
assert _note['style/spacing'] == '$ReducedSpacing', _note
assert _note['rect']['x'] == 0 and _note['rect']['y'] >= 100 and _note['rect']['y'] + _note['rect']['height'] <= 180, _note['rect']
assert 'TouchGraphicsProfileNote' in [c['name'] for c in g_nodes['TouchGraphicsProfileRow']['children']]
# 内存档位（用户 2026-10-29）：帧率行下同款副文，紧挨帧率文字、在下一行之上 | memory tier (user 2026-10-29): same subtext under the frame-rate caption, above the next row
_fn = g_nodes['TouchGraphicsFpsNote']['fields']
assert _fn['text'] == '@dmGraphicsFps120MemNote' and _fn['style/pointSize'] == 28 and _fn['style/fontColor'] == '$FontColorLightGray' and _fn['style/spacing'] == '$ReducedSpacing', _fn
assert _fn['rect']['y'] == _rows_y[3] + 116 and _fn['rect']['y'] + _fn['rect']['height'] <= _rows_y[3] + 180, _fn['rect']
_rn = g_nodes['TouchGraphicsResNote']['fields']
assert _rn['text'] == '@dmGraphicsResRestartNote' and _rn['style/pointSize'] == 28 and _rn['style/fontColor'] == '$FontColorLightGray', _rn
assert _rn['rect']['y'] + _rn['rect']['height'] <= 180 and 'TouchGraphicsResNote' in [c['name'] for c in g_nodes['TouchGraphicsResRow']['children']], _rn['rect']
print('PASS Graphics page: native FPS/profile sliders, independent upscale toggle, localization hooks')

# Regression: adding Graphics must preserve all original destinations and native settings.
settings_src = '{"name":"SettingsPanel","children":[{"type":"TabBarWidget","name":"OptionsTabs","fields":{"textStyle":{"pointSize":"$MediumFontSize","options":{"lineWrap":true},"alignment":{"h":"center","v":"center"},"spacing":{"leading":0.9,"kerning":0.95}},"tabCount":5,"inactiveFrames":[0,2,4,6,8],"activeFrames":[1,3,5,7,9],"textStrings":["@OptionsMenuVideo","@Audio","@Gameplay","@GameSettingsTab","@Controls"],"tabMessages":["SettingsPanelMessage:CheckChanges:VideoOptionsPanel","SettingsPanelMessage:CheckChanges:SoundOptionsPanel","SettingsPanelMessage:CheckChanges:GameplayOptionsPanel","SettingsPanelMessage:CheckChanges:OnlineGameOptionsPanel","SettingsPanelMessage:CheckChanges:KeyBindingMenu"]}}]}'
for transform in (lambda s: layout._settings_tabs(s)[0], layout.pad_settings):
    tab=json.loads(transform(settings_src))['children'][0]['fields']
    assert tab['tabCount']==len(tab['textStrings'])==len(tab['tabMessages'])==len(tab['inactiveFrames'])==len(tab['activeFrames'])==4
    destinations=[m.rsplit(':',1)[1] for m in tab['tabMessages']]
    assert len(set(destinations))==4
    assert set(destinations)=={'SoundOptionsPanel','GameplayOptionsPanel','VideoOptionsPanel',layout.GRAPHICS_PANEL}
    assert '@dmTabSystem' in tab['textStrings'] and '@dmTabGraphics' in tab['textStrings']
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

# Actual console assets can have only four frame IDs for the four tabs.
four_frames=settings_src.replace('[0,2,4,6,8]','[0,2,4,6]').replace('[1,3,5,7,9]','[1,3,5,7]')
for transform in (lambda s: layout._settings_tabs(s)[0],layout.pad_settings):
    fields=json.loads(transform(four_frames))['children'][0]['fields']
    assert fields['inactiveFrames']==[0,2,4,6]
    assert fields['activeFrames']==[1,3,5,7]
print('PASS native four-frame artwork fits the four tabs')

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

# 页签顺序（用户 2026-10-27）：音频 / 图形 / 游戏 / 系统（方案 C），文字与目标面板一一对应
# Tab order (user, 2026-10-27): Audio / Graphics / Gameplay / System (plan C), with labels and destination panels paired
assert layout._SETTINGS_LABELS == ('@Audio', '@dmTabGraphics', '@Gameplay', '@dmTabSystem')
assert layout._SETTINGS_PANELS == ('SoundOptionsPanel', layout.GRAPHICS_PANEL, 'GameplayOptionsPanel', 'VideoOptionsPanel')
print('PASS settings tabs: Graphics right after Audio')

# 宿主手柄选行与布局同序：行号 0–3 = 渲染分辨率 / 帧率 / 超分开关 / 超分档位，选中框 y = 170 + 180 × 可见序号（与上面 _rows_y 对应）
# The host's controller row selection matches the layout: rows 0–3 = render resolution / frame rate / upscale switch / profile, selection frame y = 170 + 180 × visible index (matching _rows_y above)
_custom = (root / 'host' / 'dm_custom.c').read_text(encoding='utf-8')
assert 'y = pad ? graphics_row_y(vis, selected) - 10 : 0' in _custom   # 行 y 由宿主按可见行与分组标题排（t_graphics_gate 验）| row y comes from the host's packing with group headings (checked in t_graphics_gate)
assert 'row_id == ROW_FPS' in _custom and 'row_id == ROW_FPSHUD' in _custom and 'row_id == ROW_UPSCALE' in _custom and 'row_id == ROW_PROFILE' in _custom and 'row_id == ROW_RES' in _custom and 'row_id == ROW_RT' in _custom
# 光追行（实验性）接在最后：手柄行号 4；可见行紧挨着排，选中框按可见序号放；档位行藏着时光追行与说明各上移一格（与布局缺省 900 / 1080 对应）
# Ray-tracing row (experimental) comes last: controller row 4; visible rows are packed and the selection frame follows the visible index; with the profile row hidden the ray-tracing row and the hint each move up one slot (matching the layout defaults 900 / 1080)
# 可见行由 graphics_rows_pack 紧挨着排（f705573 起：安卓藏分辨率 / 超分行时后面的行上移），行号取 vis[selected]；说明行接在最后一个可见行下面；全部可见时说明在 180 × 7 = 1260
# Visible rows are packed by graphics_rows_pack (since f705573: with the resolution / upscale rows hidden on Android the later rows move up), row_id = vis[selected]; the hint follows the last visible row; with everything visible the hint sits at 180 × 7 = 1260
assert 'enum { ROW_RES, ROW_BRIGHT, ROW_FPSHUD, ROW_FPS, ROW_UPSCALE, ROW_PROFILE, ROW_RT }' in _custom
assert 'int row_id = vis[selected];' in _custom and 'graphics_rows_pack(res_ok, up_show, prof_on, rt_show, vis)' in _custom
assert 'case ROW_RT: if (rtrow) move_ours_y(rtrow, y); break;' in _custom and 'case ROW_FPSHUD: move_ours_y(find_w(sp, "TouchGraphicsFpsHudRow", 0), y); break;' in _custom
assert 'move_ours_y(find_w(sp, "TouchGraphicsApplyHint", 0), graphics_row_y(all_vis, nv - 1) + 180)' in _custom
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
assert json.loads(re.sub(r',(\s*[}\]])', r'\1', touch_console))['children'][-1]['fields']['tabCount'] == 4   # 页签变换照常 | the tab transform still applies
front, _ = layout.PRESENT['controller/settingspanelhd.json']('{"children":[{"type": "ButtonLegendWidget", "name": "Legend", "fields": {"rect": {"x": 0, "y": 1661, "width": 1950, "height": 103}}}]}')
assert legend_of(front)['fields']['rect']['y'] >= 99999   # 前端那份照旧挪走 | the front-end copy is still moved away
print('PASS options legend: touch in-game and front-end copies move the controller legend off screen; controller copy keeps it')
# B067：手柄这份在提示条下垫一条暗底（挂在面板底图下，先于标题 / 提示条画），盖住底栏技能格；重复套不再加；没有面板底图声明的夹具不动
# B067: the controller copy puts a dark strip under the legend (a child of the panel image, drawn before the title / legend) over the bottom-bar skill cells; applying twice adds nothing; fixtures without the panel image declaration are untouched
pad_console = json.loads(re.sub(r',(\s*[}\]])', r'\1', layout.pad_settings(console_src)))
anchor = pad_console['children'][1]['children'][0]['children']
assert [c['name'] for c in anchor] == ['Background', 'Title', 'Legend'], anchor
band = anchor[0]['children'][0]
assert band['type'] == 'RectangleWidget' and band['name'] == 'TouchPadLegendBand' and band['fields']['color'] == [0, 0, 0, 0.8], band
r = band['fields']['rect']; assert r['x'] == 0 and r['width'] == 1950 and r['y'] <= 1661 and r['y'] + r['height'] >= 1661 + 103, r   # 盖住原版提示条（y 1661、高 103、宽 1950） | covers the original legend (y 1661, height 103, width 1950)
assert layout.pad_settings(layout.pad_settings(console_src)) == layout.pad_settings(console_src)
assert 'TouchPadLegendBand' not in layout.pad_settings(settings_src) and 'TouchPadLegendBand' not in touch_console
print('PASS options legend (B067): controller copy puts a dark strip under the legend, once')
# 游戏页难度块高：宿主 GAME_ROWS_Y 与布局 DIFF_H 同一个数（审计 r1 P2-9） | Gameplay difficulty block height: the host's GAME_ROWS_Y equals the layout's DIFF_H (audit r1 P2-9)
assert int(re.search(r'#define GAME_ROWS_Y (\d+)', (root / 'host/dm_custom.c').read_text(encoding='utf-8'))[1]) == layout.DIFF_H, layout.DIFF_H
print('PASS Gameplay difficulty block: host GAME_ROWS_Y == layout DIFF_H')

# RC2 并入的四个键（方案 C × rt_level 移植）：光追档位 → 图形页「光线追踪」组、只有 Apple；统计 / 自动上报崩溃 → 系统页「隐私」组、两端都有；
# 战斗中补腰带 → 游戏页「拾取与掉落」组（与药水拾取同组）、两端都有。布局一侧；宿主一侧由 t_graphics_gate / t_options_android 核
# The four keys RC2 brought in (plan C x rt_level port): ray-tracing level -> Graphics page Rt group, Apple only; statistics / automatic crash reports -> System page
# Privacy group, both platforms; belt refill -> Gameplay page Loot group (with potion pick-up), both platforms. Layout side; the host side is checked by t_graphics_gate / t_options_android
for _target in (None, 'android'):
    if _target: os.environ['DM_TARGET'] = _target
    else: os.environ.pop('DM_TARGET', None)
    layout = importlib.reload(layout)
    _tab = {k: (page, g, plat) for k, page, g, plat in layout._OPT_TABLE}
    assert _tab['rt_level'] == ('gfx', 'Rt', 'apple') and [k for k, (_, g, _p) in _tab.items() if g == 'Rt'] == ['rt_level'], _tab
    assert _tab['tel_stats'] == ('sys', 'Privacy', 'all') and _tab['auto_crash'] == ('sys', 'Privacy', 'all')
    assert _tab['belt_refill'] == ('game', 'Loot', 'all') and _tab['pot_pick'][1] == 'Loot'
    assert not {'rt_level', 'tel_stats', 'auto_crash', 'belt_refill'} & layout._OPT_PENDING and 'rt_shadow' not in _tab and 'rt_ao' not in _tab
    assert layout._CUSTOM_KEYS[-2:] == ('tel_stats', 'auto_crash') and 'belt_refill' in layout._GAME_KEYS, (layout._CUSTOM_KEYS, layout._GAME_KEYS)
    assert ('Privacy', layout._CUSTOM_Y[[k for k, _ in layout._CUSTOM_ALL].index('tel_stats')] - layout._CUSTOM_LINE_H) in layout._CUSTOM_HEADS, layout._CUSTOM_HEADS   # 隐私组标题在统计行上一行 | the Privacy heading sits one row above the statistics row
os.environ.pop('DM_TARGET', None); layout = importlib.reload(layout)
assert [l for l in open(root / 'i18n/ui_text.tsv', encoding='utf-8') if l.startswith('dmOptGrpPrivacy\t')]   # 隐私组标题六语 | Privacy heading in the six languages
print('PASS RC2 keys: rt_level Graphics/Rt (Apple), tel_stats / auto_crash System/Privacy, belt_refill Gameplay/Loot')
