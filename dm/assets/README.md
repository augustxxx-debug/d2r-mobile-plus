# 加载页素材（loadpage/，字体在 loadpage_fonts/，本文件不进包）| Loading page assets (loadpage/; fonts in loadpage_fonts/; this file is not shipped)

进安装包的「导入 / 准备游戏内容」加载页素材（iOS 包内 `loadpage/`，安卓 APK `assets/` 根）。**全部自制、程序化生成，不含任何原版游戏素材**（不从 romfs / 原版资源里抽图或音乐）。
Assets of the "import / prepare game content" loading page shipped inside the install package (`loadpage/` in the iOS bundle, the `assets/` root of the Android APK). **All self-made and procedurally generated; no original game assets** (nothing is taken from romfs / the original resources).

| 文件 File | 内容 Content | 来源 Source | 授权 Licence | 体积 Size |
|---|---|---|---|---|
| `loadpage_bg_land.jpg` | 横屏背景 2048×1152：深色石纹、哥特尖拱、中下方火光、火星、暗角、金线花饰 | `dm/tools/gen_loadpage_art.py`（numpy + Pillow，固定种子，重跑同图） | CC0 1.0（本项目贡献者放弃著作权） | ≈ 180 KB |
| `loadpage_bg_port.jpg` | 竖屏背景 1152×2048，同上 | 同上 | CC0 1.0 | ≈ 170 KB |
| `loadpage_logo.png` | 字标图 1600×600（透明 PNG）：「D2R」+「MOBILE PLUS」金属金色、浮雕、描边、外发光；放在页面中部居中 | `dm/tools/gen_loadpage_logo.py`（Pillow + numpy；字体 `loadpage_fonts/CinzelDecorative-Black.ttf`，Natanael Gama，SIL OFL 1.1，许可证 `loadpage_fonts/OFL.txt`；字体只用于生成，不进安装包） | 图 CC0 1.0；字体 OFL 1.1（允许把字形渲染成图任意使用） | ≈ 320 KB |
| `loadpage_loop.m4a` | 循环音乐 48 秒：低音持续音、慢速小调和声垫、稀疏钟声、风声，首尾交叉淡化 | `dm/tools/gen_loadpage_music.py`（numpy 合成 → macOS `afconvert` AAC 96 kbps） | CC0 1.0 | ≈ 640 KB |

- 焦点（火光中心）在图内 (0.5, 0.58)，与 `dm/host/dm_loadpage.c` 的 `LP_FOCUS_X/Y` 一致：cover 裁切以它为中心，短边对齐、长边裁掉。横 / 竖两张按屏幕方向选（`dm_loadpage_bg_name`）。
- 重新生成：`source env.sh && $DM_ROOT/build/cache/dm/venv/bin/python dm/tools/gen_loadpage_art.py && … gen_loadpage_music.py`。改了图的尺寸要同步改 `dm_loadpage_bg_name` 里的尺寸并重跑 `dm/host/tests/t_loadpage`。
- 体积上限：背景每张 ≤ 1 MB（生成脚本超限即报错），音乐 ≤ 2 MB。
- The focus (glow centre) is at (0.5, 0.58) in the image, matching `LP_FOCUS_X/Y` in `dm/host/dm_loadpage.c`: the cover crop is centred on it, short side fitted, long side cropped. Landscape / portrait is picked by screen orientation (`dm_loadpage_bg_name`).
- Regenerate with the two scripts above (project venv). Changing an image size requires updating `dm_loadpage_bg_name` and rerunning `dm/host/tests/t_loadpage`.
- Size caps: each background ≤ 1 MB (the generator fails above that), music ≤ 2 MB.
