# Plus build and verification notes

[简体中文](PLUS.zh-CN.md) · [Project overview](../README.md) · [Changelog](../CHANGELOG.md)

## Scope and architecture

Plus modifies the compatibility layer and build-time UI generators. The original input game files remain unchanged. During a build, generators read the player's own resources and produce an overlay and packaged assets under ignored build directories. The application uses those generated resources alongside the original game data.

Front-end scene coverage and interface coordinates are handled separately. Bottom controls use available screen geometry; in-game coordinate packing is retained. Layout overrides are scoped to touch/controller roots rather than broadly rewriting every shared panel.

The two lower-right indicator families have separate handling: the multi-layer fire logo and the red save indicator. Their placement is initialized while hidden before being revealed. This avoids displaying an unpositioned first frame during loading transitions.

### Quest narration behavior

Runtime messages on the baseline distinguish selecting a quest, playing its narration and finishing the dialogue. A quick second tap emits the finish event. A stationary 0.8-second touch did not produce a click because the mobile touch recognizer rejected releases at or above its ordinary 0.35-second tap deadline.

Plus allows a longer stationary release only on quest icons, the replay control and the close target under the original/expansion quest-log roots. The game still handles narration state. Dragging, cancellation, inventory interactions and unrelated buttons keep their prior gesture rules. This finding comes from baseline and modified device traces, not from a separate retail Switch test.

## Optional font

This feature is experimental and disabled unless `DM_UI_FONT` is set. Supply a static TrueType font that you are entitled to use. No font is included. Public packages use LXGW WenKai Medium at darkening level 0 (SIL OFL 1.1; credits and licence note in the README), the same face and level as the Android package, fetched from <https://github.com/lxgw/LxgwWenKai/releases> at packaging time and subset as described below; the commercial AR ZhongkaiGBK used in the local checks below is for private builds only.

Size limit: the composed main font (the replacement for `blizzardglobal-v5_81.ttf`) must stay under 16 MiB, or the game crashes during start-up before any UI appears (measured 2026-10-29 on the Mac host: 16.35 MB loads, 18.9 MB and above crash, regardless of the font's internal structure). A full CJK face such as LXGW WenKai (26 MB) therefore has to be subset first: public packages use a subset limited to GB 2312 plus every character that actually occurs in the game's Chinese string tables and in Plus's own six text sets (about 7,800 characters, 4 MB; composed result about 10 MB), with OpenType layout and vertical tables dropped. Point `DM_UI_FONT` at the subset file, never at the full font.

Licence in the package (13th review Q13-02): `font_override.prepare` writes `Gen/assets/UIFonts/FONT_LICENSE.txt` next to the generated font, which the App bundles as `UIFonts/FONT_LICENSE.txt`: a provenance note (font name, version, file, SHA-256, and that the embedded font is a subset merged with the game font under the game font's registration name) followed by the unmodified licence file found beside `DM_UI_FONT` (`OFL.txt`, `LICENSE`, `LICENSE.txt`, `COPYING` and so on). Admission rules: a font under a `public` directory must have that licence file beside it or the build fails; a font under a `private` directory is accepted only when the build sets `DM_PRIVATE_FONT=1` and ships no licence (private builds only); any other location ships the licence when one is there and otherwise needs `DM_PRIVATE_FONT=1`. The checks run in `source_path`, so `gen_assets`, `gen_romfs_overlay` and `run.sh build` all stop early. The Android import pack keeps the same file name under `dmgen/font/`. Unit tests cover all three cases. A local AR ZhongkaiGBK Medium font was identified and its Chinese settings appearance inspected on device; this does not validate every string or layout. Font replacement happens at build time, with no in-game font selector.

Install the optional build dependency into the same Python environment used for asset generation:

```sh
python3 -m pip install -r dm/ios/tools/requirements-fonts.txt
export DM_UI_FONT='/path/to/your/font.ttf'
cd dm/ios
./run.sh build
```

Set the variable for the build process, or add it to your private build environment. Do not commit the font or your local configuration. With `DM_UI_FONT` unset, builds use the original font and remove stale optional-font output.

The generator normalizes units per em, retains original vertical metrics and registration names, and fills missing Unicode characters from the original game font. Inputs are not overwritten. Content fingerprints invalidate generated text when the selected font changes. Output is placed in ignored `Gen/assets/UIFonts` and in the generated resource overlay.

Coverage: the shared `BlizzardGlobal-V5_81.ttf` face, generated text that requests that face and the native rift HUD font loader. Other decorative font faces, classic bitmap text and text baked into game artwork are outside this change. Retaining vertical metrics does not guarantee identical glyph widths; line breaks, fallback appearance and clipping must be checked with the real target font in both languages and screen layouts.

Synthetic tests cover Unicode fallback, units-per-em conversion, original registration/vertical metrics, unchanged input files, content fingerprints, disabled cleanup and invalid input paths. Tests also cover custom vertical-table compatibility and preservation of original private-use game symbols, plus the weight levels below (thicker requested glyphs, unchanged advances, line metrics, fallback glyphs and icons; monotonic levels; level 0 identity; fingerprint and invalid values), for nine tests total. Chinese and English Graphics/Custom pages were inspected on device; full panel coverage remains pending.

### Weight

Status: implemented and unit-tested; sample sheets rendered on a Mac; **level 3 accepted on an iPhone Duo** (2026-10-27, by eye at the launch and character screens; full panel coverage still pending).

The requested font can be darkened at build time with `DM_UI_FONT_WEIGHT=0..8` (default 0 = the font's own outlines). The aim is to fill in hairlines that anti-aliasing leaves grey, not to make a bold face: each level widens every stroke by only 5‰ of an em (about 0.1 px per stroke at 20 px) using the point-offset algorithm of FreeType's `FT_Outline_EmboldenXY` applied to the outlines inside `dm/ios/tools/font_override.py`; the result is re-centred so advance widths, side-bearing sums and the original line metrics do not change, and glyphs filled in from the original game font (including private-use icons) are never touched. The composed font is a plain static TrueType file, so the game's runtime rasteriser and the native rift HUD loader both pick up the weight without code changes. Changing the level re-fingerprints the generated font and text.

Calibration on AR ZhongkaiGBK Medium (stems 35–60‰ em, rendered at 20 px): level 0 has a mean ink coverage of 0.45 against 0.58 for the original `BlizzardGlobal`; levels 2–3 (0.50–0.52) darken the hairlines while the glyph shapes stay visibly kai; level 6 (0.59) matches the original's density but already reads as a bold weight, which an earlier 16‰ calibration showed on a Mac run and was rejected as too heavy; level 8 is for very thin faces and clutters dense characters such as 龘. On a Mac run levels 2, 4 and the old 32‰ were compared on the character-selection screen: 2 left the small text grey, 32‰ read as bold, 4 (20‰) looked right in a 1280x720 window. On the iPhone Duo inner screen (about 490 ppi, text rasterised with roughly twice the pixels) 4 was then judged too dark and 3 (15‰) was accepted. Start at 3 for this face on a dense screen; use 2 for a heavier face.

```sh
export DM_UI_FONT='/path/to/your/font.ttf'
export DM_UI_FONT_WEIGHT=2
cd dm/ios && ./run.sh build
```

`python3 dm/ios/tools/font_override.py <blizzardglobal.ttf or RomFS dir> [font.ttf] out.ttf --weight N` composes one font outside a build, and `python3 dm/ios/tools/font_sample.py --weights 0 1 2 3 4 6 --sizes 20 28` renders sample sheets (needs PIL): the levels side by side, plus a simulation of the Graphics **Resolution** cap in which the text is rasterised at the smaller output size and bilinearly upscaled as a whole frame (×1.43 ≈ Auto/Balanced on the iPhone Duo inner screen, ×1.66 ≈ Eco). Output goes to `$DM_ROOT/run/font-weight/`, never into the repository.

Why lower render resolutions soften the text, and what the font can and cannot do about it: the UI layer is rendered at the capped output size, so the game rasterises each glyph at a correspondingly smaller pixel size and the present pass stretches the finished frame back to the screen with a bilinear filter. The font is vector, with no pre-rendered bitmaps or size table to pre-scale, so nothing in the font can give the glyphs more pixels; only an uncapped UI pass could, and that is a render-path change outside this feature. What the weight does is keep thin kai strokes from fading below one output pixel after the upscale. Recommended trade-off: level 3 (the device-accepted level) together with Auto/Balanced on dense screens; drop to 2 only if Native-resolution text looks heavy. The level is fixed at build time; a per-resolution font chosen at launch is a possible follow-up, not part of this change.

Device run: the iOS build with level 4 and then level 3 was installed on an iPhone Duo; 4 was too dark on the dense screen and 3 was accepted. The macOS App was checked earlier at levels 2 and 4 in a 1280x720 window. Still pending on the device: text in the Chinese and English settings pages, quest log, item tooltips and the rift HUD at Native and Auto resolution, with attention to clipping in narrow fields (longer strokes stay inside the same advance, so widths do not change, but ascender/descender pixels grow by half a level) and to dense characters at level 6 and above.

## Verification

Validation record: **2026-10-26**, upstream baseline `195ae11dc70f1ba7735c6063e1ed81102e1dbf52`.

| Check | Result / coverage |
|---|---|
| iOS touch recognizer | 49 cases; 4,487 checks passed, including slow quest releases and cancellation/drag guards |
| Host UI classification | 381 checks passed |
| Front-end geometry | 44 checks passed |
| Notification generator | Automated frame construction checks passed |
| Font composition | 6 font-composition tests passed; target Chinese and English settings inspected on device |
| Graphics settings | 195 migration/persistence/Apply/Discard/global Defaults checks passed |
| Physical device acceptance | Current five-tab audit build: user confirmed a physical Xbox controller and landscape inner/outer-screen folding on iPhone Duo; scope below |
| Device build | Signed iOS build succeeded |
| Quest reproduction | Baseline slow release missed; modified slow and quick releases stopped narration |
| Indicator frame review | 44 captured frames reviewed in the preceding layout build |
| Private native-binding fixtures | Three fixture-dependent tests skipped; fixtures are not distributed |

Run the relevant checks from the repository root with your workspace environment active:

```sh
python3 dm/i18n/scan_literals.py
python3 dm/ios/tools/dm_i18n.py --check
python3 dm/host/tests/native_write_scan.py dm/host --selftest
make -C dm/ios/tests test
python3 dm/ios/tests/test_font_override.py
make -C dm/host/tests build/t_minimap_lift build/t_uiscreen_frontend
./dm/host/tests/build/t_minimap_lift
./dm/host/tests/build/t_uiscreen_frontend
```

The font test explicitly skips when its optional dependency is absent. Some broader tests require game-derived tables or private runtime fixtures; see [contribution requirements](../CONTRIBUTING.md). No full rewrite-equivalence, entire graphics-suite or retail Switch ground-truth acceptance claim is made for this preview. The rewriter is unchanged; MetalFX modifies the final 3D upscale pass in the graphics layer.

### Local audit after the device regression

The following checks used the current local audit build on **2026-10-26**. They do not extend the earlier physical-device acceptance to this build. No phone installation or operation was performed during this audit.

| Area | Evidence and limits |
|---|---|
| Front-end coordinates | 7,884 checks passed, including repeated size changes. These are geometry checks, not physical folding or UIKit event delivery. |
| Notification frame | 7,772 content-size combinations passed. Owned single-frame sprite and widget writes share the same type, filename, frame and atlas guards; rejected inputs leave both objects unchanged. AddressSanitizer and UndefinedBehaviorSanitizer checks passed. |
| Notification delivery | Production queue tests cover copied text, four producers, bounded/reentrant draining and owner-thread delivery. ThreadSanitizer checks passed. The native Mac game also displayed a no-items-to-deposit notice while genuinely paused, without closing the menu. |
| Settings | The native Mac game exercised changed-value confirmation, Discard, Apply and global Defaults. Native offline difficulty reset from 8 to 1, while language and voice remained unchanged. English, Russian, Spanish and Korean screenshots were inspected at four display sizes; five tab destinations were preserved. |
| Auto scaling | Production-entry tests preserve an existing frame-time downgrade across thermal-cap changes, exercise 10,000 rapid cap changes, and check explicit profiles and disabled-controller fallback. The concurrent-reader check passed ThreadSanitizer. A native 1:1 scene can now seed supported MetalFX activation without waiting for frame drops. |
| MetalFX backend | Metal 3 and Metal 4 each passed three sequential frames with 64 color/mask samples per frame and invalid-extent rejection, with Metal API Validation enabled. This verifies the wrapper and sampled output, not phone power consumption. |
| Diagnostics | Foundation tests cover bounded log-tail reads, identifier/known-character-name redaction and invalid UTF-8. Original logs remain unchanged. The iOS share sheet itself still needs device acceptance. |
| Build | iOS Release and native Mac builds succeeded. Three private native-binding fixture tests and two game-derived skill-table hash checks were explicitly skipped in the local iOS test setup. |

The full game could not run in the standard iOS Simulator because its graphics capability checks were not satisfied. Local game screenshots above came from the native Mac host using isolated test-save copies with cloud access disabled. They are not iOS Simulator screenshots and do not verify UIKit touch delivery, physical folding, iPhone thermal behavior or power savings. Broader panel/localization coverage remains subject to the checks below.

### Render resolution cap and measurement build

The Graphics tab's Resolution slider (`dm/host/dm_res_cap.h`, `dm_custom.c` key `render_res`, `front_ios.m` `front_game_res`) is covered by `t_res_cap` (budget, aspect, even sizes, shrink-only, idempotence on Duo/17 Pro Max/iPad sizes), by `t_render_profile` (persistence, Apply/Discard/Defaults, one re-read notification per effective change) and by `test_touch_layout_options.py` (slider row placement under the profile row, hint not overlapped). The Mac host and the iOS Simulator cannot exercise the real device path; device verification of the live resolution switch, the present-thread upscale and the fold/unfold interaction is still pending.

For measurements, build with `DM_DIAG_SAMPLING=1 dm/ios/run.sh build` (or create `Documents/diag_sampling.txt` in any build). At launch the app sets `DM_NVN_PASSGPU`, `DM_THRCPU` (5 s) and `DM_HEAT` (5 s) and writes `Documents/diag/passgpu.tsv`, `thrcpu.tsv` and `heat.tsv`. Per-pass timing makes the submit thread wait for the GPU of the submission eight back, so use it for relative comparison; `passgpu.tsv` grows by one row per render pass and can reach tens of MB in a few minutes. Measure unplugged at a fixed brightness; readings taken while charging cannot be used for power claims.

### Ray-traced shadows (experimental) on a device

Measured on an iPhone 17 Pro (RC1 candidate, ray tracing on, AO off):
- **What triggers the downgrade**: whole-frame GPU time, not memory. The ray-tracing memory budget is about 180 MB against an estimated 82 MB, and no memory-warning downgrade happened. The rule: whole-frame GPU above 92% of the present budget with ray tracing above 12% of it for 1 second steps down one level; below 70% for 8 seconds steps up one level.
- **120 FPS**: the budget is 8.33 ms and the game alone takes about 14 ms; with ray tracing on the frame is 21–24 ms, and within seconds it drops to level 3 (half-resolution hard sun shadows, point lights off, alpha-tested casters on the original shadow map). At 120 FPS there are therefore only hard shadows, plus the render resolution one level down from startup that ray tracing applies; soft shadows do not apply.
- **60 FPS**: light scenes such as the camp stay at level 0 with soft shadows (ray tracing 3.3–4.0 ms, frame 7.5–8.6 ms). In large fights ray tracing itself rises to 10–14 ms and it drops to levels 2–3; a loading hitch can trigger a downgrade too. Once the scene is light again and the frame stays under 70% of the budget for 8 seconds, it steps back up one level at a time.
- Resident memory is about 125–160 MB above ray tracing off (five-act fixed-frame runs). 0 red lines, 0 memory warnings, saves identical before and after.
- **Back faces (RC1 fix)**: back faces count as shadowed, the normal is smoothed over 2 pixels, soft shadows get a 2-pixel minimum radius. Mac, same frame against the original (frame 600 after loading, 3D area, ±25%): camp level 0 brighter than the original 1.65% → 1.27%, darker 2.31% → 4.64%; level 3 1.66% → 1.32% / 2.17% → 4.71%; Blood Moor level 0 1.40% → 0.79% / 0.41% → 0.82%. Characters and monsters are back near the original brightness (Blood Moor bear luminance ratio 1.138 → 1.044, barbarian 1.080 → 0.979); the added darkness sits on the back sides of thin logs and is recorded as a known difference.

### Local debugging: run the iOS build on a Mac (instead of the Simulator)

The iOS Simulator SDK has no MetalFX framework, so the graphics backend does not compile, and the shader library only targets devices; that route is closed. The workable local path launches the iphoneos `DmIOS.app` as "Designed for iPad" on an Apple silicon Mac (the same method as `tools/smoke_mac.sh`): wrap the build product as `<name>.app/Wrapper/DmIOS.app` plus a `WrappedBundle` symlink and start it with `open -n <name>.app --env ... --args -mode boot`; the provisioning profile must include the Mac. The container lives at `~/Library/Containers/<UUID>/Data` (find it through `.com.apple.containermanagerd.metadata.plist` or `Documents/boot.txt`); clone the RomFS `data/` into `Library/Application Support/romfs/` and put a test save in `Documents/save.test`. Useful environment: `DM_TEST_SCREEN=2622x1206` treats the screen as an iPhone 16 Pro (with `DM_TEST_SAFE_L/R` for the safe area), `DM_SAVE_TEST=1`, `DM_TEST_ICLOUD_OFF=1`, `DM_KEYS` to enter a game, `DM_SCREEN_SHOT=<sec>:<file>` for a whole-window screenshot into Documents. It verifies the shell, layout, resolution and aspect logic and entering a game; it cannot verify real touch, thermal state or A-series GPU time.

### Unattended power benchmark

`dm/ios/tools/power_bench.sh <minutes per segment> <level sequence> [MetalFX] [save backup dir]` launches the game once with a test save (`Documents/save.test`, never `Documents/save`), a fixed seed, a key script that enters the game and walks in a loop, a level rotation every 90 s (`DM_TEST_WARP_LV=every<ticks>:<levels>`), timed monster spawns (`DM_TEST_SPAWN` multi-entry form) and a render-resolution schedule (`DM_TEST_RENDER_RES=<level>@<sec>,...`). `heat.tsv` gains battery %, charging state, Low Power Mode, the render level and the MetalFX profile every 5 s; `tools/bench_report.py` splits the run by the render-level column and reports %/hour per segment with ±(battery step ÷ segment length). The battery reading is quantized: 5% steps were observed on the iPhone Duo, so a 10-minute segment carries about ±30 %/hour. The step is taken from `--battery-step` or inferred from unplugged data (the source is printed); segments that were charging, mixed, too short (< 10 min) or have too few samples get no rate, and no ranking is printed when error intervals overlap. Run it unplugged at a fixed brightness; the benchmark holds the device lock from the first device access to the final pull and never terminates an app that is already running.

### Remaining checks

- Render resolution slider: verify on the iPhone Duo inner/outer screen that Apply re-reads the output size without restart, that the picture stays filled, and compare per-pass GPU time against Native.
- Landscape fold/unfold on Graphics is user-confirmed; extend coverage to other pages and menu/loading transitions.
- Graphics controls are confirmed with a physical Xbox controller; extend focus, prompt and action coverage to other panels.
- Reproduce all achievement/notification overlaps, including bottom HUD masking.
- Check all localizations and panel layouts with the actual replacement font.

These require broader device/runtime coverage. A successful focused test does not close every related visual issue.

## Release hygiene

Publish source and documentation only. Keep generated resources, fonts, saves, device captures, private diagnostics, signing configuration and keys outside the published tree. Inspect newly committed file contents and author/committer metadata before pushing. Review local diagnostics before attaching them to an issue.

The `main` branch retains the upstream baseline; `codex/plus` contains the preview changes. To compare behavior, build either branch from your own workspace. Always back up saves first; do not uninstall an app that holds your only copy.

Font build note: keep `DM_UI_FONT` in the untracked `dm/ios/local.env` to retain the choice across builds. Empty mapped placeholders fall back to visible game glyphs; language choices use smaller text without wrapping.

### Settings preservation regression

Settings has five distinct destinations: Audio, Gameplay, Settings, Graphics and Custom. Graphics is generated as a separate layout instead of replacing the native Game Settings layout. New resource paths require an unused leading byte. The generated resource trie is parsed again to verify every existing path and file offset remains intact.

The six touch/controller Audio, Gameplay and Game Settings layouts were compared against the enhanced version before Graphics was added: five are byte-identical, and controller Game Settings changes only table row height to prevent divider overlap. Regression tests also preserve the native Terror Zones widget fields/messages and unknown future native controls, require five unique tab destinations, and extend four-entry native frame arrays safely to five.

Defaults continues to send the native reset message. The enhancement layer resets Graphics and Custom together as pending values, retaining language and voice. Apply saves enhanced values; Discard cancels pending values. The game continues to handle its own options; character progress is outside the reset scope.

This regression used a separate test save on the physical device: all five tabs opened; applying Graphics after disabling native Terror Zones preserved that setting; Defaults → Discard retained 30 FPS / Quality and disabled Terror Zones; Defaults → Apply restored 60 FPS / Auto, enabled Terror Zones and native offline scaling 1 while retaining language and voice. Injected controller input verified the five-tab cycle, frame-rate selection, upscaling switch and profile selection. That automated run did not substitute for physical controller or folding acceptance.

After installing audit build `c7ea42a`, the user confirmed physical Xbox controller acceptance on **2026-10-26**: LT/RT navigation across all five settings tabs, 30/60 FPS selection, the MetalFX switch, profile selection and Apply. The final configuration was restored to 60 FPS + Quality. The user also confirmed landscape folding to the outer screen and unfolding to the inner screen on Graphics, pausing about five seconds on each, without reported clipping or misalignment. This acceptance does not cover folding during loading, every modal or panel combination, or sustained power measurements.
