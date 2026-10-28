# D2R Mobile Plus

**An iOS interface enhancement fork of [D2R Mobile](https://github.com/jhaizhou-ops/d2r-mobile).**

[简体中文](README.zh-CN.md) · [Changelog](CHANGELOG.md) · [Build & verification](docs/PLUS.md) · [Upstream documentation](https://github.com/jhaizhou-ops/d2r-mobile#readme)

D2R Mobile provides the native compatibility layer, Metal renderer, touch controls and controller support for running your own Nintendo Switch edition of Diablo II: Resurrected. **Plus focuses on extensive display optimization for the iPhone Duo inner screen, addressing UI issues in front-end screens, character menus, tab focus, notification frames and loading indicators, with touch and controller refinements.** The upstream project remains the foundation of this work.

This is a development preview. Improvements have been tested locally on an iOS device and in automated tests; comprehensive device, controller and folding-transition coverage is still in progress. “Full screen” describes scene coverage and adaptive layout, not a guarantee that every original game panel has been redesigned.

## What changes from upstream

| Area | Plus changes | Status |
|---|---|---|
| Front end | Full-screen scene rendering with a centered interface; bottom controls use the available screen area | Implemented; geometry tests and device checks |
| Character screens | Class creation buttons, character-list alignment and bottom actions | Implemented; broader aspect-ratio review pending |
| Panel navigation | Synchronizes selected tabs and refreshes native hover/focus state | Implemented; device checked |
| Notifications | Builds a single frame from your own game artwork, retaining corner details and an opaque center | Implemented; generator tests and device checks |
| Loading indicators | Anchors both the fire-logo animation and red save/loading symbol; prepares placement before revealing the widget | Implemented; captured frame sequences reviewed |
| Quest log touch | Accepts stationary releases longer than the normal tap deadline on quest-specific controls | Implemented; automated tests and device reproduction |
| Optional font | Build-time main-font override with missing-character fallback and `DM_UI_FONT_WEIGHT` stroke-darkening levels | Experimental; ARKai_T inspected locally on device, darkening level 3 accepted on an iPhone Duo, broader layout coverage pending |

These are changes on top of upstream commit [`195ae11`](https://github.com/jhaizhou-ops/d2r-mobile/commit/195ae11dc70f1ba7735c6063e1ed81102e1dbf52). The branch adds experimental MetalFX spatial upscaling; it does not introduce multiplayer or frame generation. Upstream performance measurements and platform support claims are not new benchmarks for this fork.

## MetalFX spatial upscaling (experimental)

Settings retains five separate tabs: **Audio / Graphics / Gameplay / Settings / Custom** (Graphics right after Audio). The original offline Terror Zones control remains in Settings; Graphics uses a separate destination.

Open **Options → Graphics** in game. Enable **MetalFX upscaling (experimental)** to reveal the four-position Auto / Eco / Balanced / Quality slider. Turning the switch off restores the original upscale pass and remembers the selected profile. Frame rate has a separate left/right 30 / 60 FPS selector, plus an experimental 120 FPS stop on 120 Hz screens (see below). Press **Apply** to activate changes without restarting; switching may briefly pause rendering. **Discard** restores the active settings. **Defaults** preserves the native global reset, including game options, Graphics and Custom, while retaining language and voice; press Apply to save. Native Terror Zones and offline difficulty scaling also return to defaults; Discard cancels a pending reset.

![English Graphics settings on device](docs/images/graphics-settings-five-tabs-en.jpg)

*Five-tab revision on device (taken before the tab reorder, when Graphics was fourth): Audio / Gameplay / Settings / Graphics / Custom remain separate; Graphics includes frame-rate selection, the MetalFX switch and four upscaling profiles. The Chinese documentation example uses an optional local font; default builds retain the game font, and no font file is included.*

| Profile | Initial 3D width/height ratio | Behavior |
|---|---|---|
| Auto | 70% or 80% | Chooses by output pixel count; uses 65% in Low Power Mode or serious/critical thermal states, with 60 seconds of stable recovery before raising the ratio; also adapts to frame timing |
| Eco | 60% | Targets lower 3D rendering load; measured energy savings are not guaranteed |
| Balanced | 75% | Balances 3D clarity and rendering load |
| Quality | 85% | Prioritizes a higher 3D ratio |

Auto samples system state at most once per second. Reductions take effect at the next sample; increases require an unchanged healthy target for 60 continuous seconds. Renewed thermal pressure, Low Power Mode or a changed target restarts recovery. Gaps longer than 3 seconds (such as backgrounding or loading) do not count toward recovery. Manual profiles bypass this recovery delay.

The frame-timing controller retains its 3-second post-change hold, approximately 30-second clean recovery, and approximately 90-second recovery after a failed upward step. Auto cap changes retain any further reduction imposed by frame timing; recovering the cap does not reset actual resolution. The game thread receives one final absolute target, avoiding transient cap/level mismatches. Low Power Mode refers to the system switch, not battery percentage. Local state-sequence and production-controller tests cover these timing rules; device stutter and power measurements for this change remain pending.

Ratios apply separately to width and height, not energy savings. Only 3D color is upscaled, retaining the original mask; subsequent UI composition retains its resolution.

### 120 FPS (experimental)

On screens whose maximum refresh rate is at least 120 Hz (ProMotion iPhone / iPad), the Graphics frame-rate selector gets a third stop, **Frame rate: 120 FPS (experimental)**. On other screens the stop does not appear, and a saved 120 setting runs at 60.

- Game logic still runs at 25 ticks per second; every one of the 120 frames is fully rendered by the game (unit and camera positions are interpolated by time), with no inserted or repeated frames.
- On iOS, 120 FPS presents from the present thread instead of the display link: with the in-run touch / HUD overlay on screen, the display-link path was composited at 60 Hz and every other frame was dropped.
- Cost: roughly twice the GPU frames of 60 FPS, more heat. On the iPhone Duo it needs the **Eco** render resolution to hold 120; at higher resolutions the GPU cannot keep up and the 3D ratio controller lowers 3D resolution instead.
- Status: implemented; device-tested on an iPhone Duo (A20 Pro) inner screen with full effects for about 3 minutes (0 frames dropped by the system, 0.17% late; heavy spawn waves dip to about 100 FPS). Power draw not measured (tested while charging). Not tested on older ProMotion devices, regular iPhones, the Duo outer screen or iPad.
- The diagnostic log records the effective frame rate and present path on lines starting with 「帧率档：」, which the exported diagnostics summary includes.

### Render resolution (whole-frame output cap)

The 3D ratio above never touches the UI layer, the final composite, the upscale pass or the present blit, which all run at the full output size. On very dense screens that part dominates GPU time: the iPhone Duo inner screen renders the game at 2852x2006 (about 5.7 MP, 2.8x the Switch's 1080p), and on-device logs showed the same ~15 ms GPU frame with MetalFX Eco and with the original upscaler once the 3D ratio had reached its 0.60 floor. The Graphics tab therefore has a fourth row, **Resolution**, a four-position slider that caps the whole-frame output size by pixel budget; the present thread's existing aspect-preserving bilinear upscale fills the screen again, UI rectangles and touch coordinates are relative and unchanged, and the change applies live with Apply (the game re-reads its output size through the same notification the foldable screen switch uses).

| Level | Pixel budget | Effect |
|---|---|---|
| Auto (default) | By GPU generation: A16 and earlier 2.07 MP, A17 Pro / A18 2.8 MP, A18 Pro / A19 3.8 MP, A19 Pro / A20 and later 4.0 MP, M series 4.5 MP | 17 Pro Max (A19 Pro) and the Duo outer screen unchanged; screens above 4.5 MP (the Duo inner screen) take the Balanced 2.8 MP budget instead and shrink to about 1994x1402, because at native size the GPU sat at ~15 ms per frame under full-screen spell effects while Eco ran at ~10 ms with identical fps; 15 Pro (A17 Pro) to about 0.96x edge; 14 Pro and earlier to about 0.83x |
| Eco | 2.07 MP | 1080p-equivalent pixel count on every device |
| Balanced | 2.8 MP | Between the two |
| Native | no cap | Previous behavior: output size = window size |

Auto also has a measured closed loop on top of the table: when the 3D ratio is already at its floor and frames keep dropping (over 5% in the last 10 s or over 1.5% over 60 s), the whole-frame output steps down once more (Balanced 2.8 MP → Eco 2.07 MP) and steps back up after 60 s without drops; every change holds for 10 s. Any chip and thermal state therefore converges on measured frame pacing, and the generation table is only the starting point. Active only in Auto and not in "quality first"; `DM_RES_AUTO_OFF=1` pins it for measurements.

The cap only ever shrinks and keeps the window aspect ratio within 0.5%, so no black bars appear. The startup reserve is not reduced, so the level can be raised again at runtime without restarting. Status: implemented with unit tests for the budget math and the settings plumbing (`dm/host/tests/t_res_cap.c`, `t_render_profile.c`, `dm/ios/tests/test_touch_layout_options.py`); **not yet verified on a device**, and no power or GPU-time measurement for it exists yet. The slider is read by the iOS front end only; the Mac host keeps native output. The frame-rate setting is independent. Auto, Eco and Balanced can reduce 3D resolution further during sustained frame drops. Unsupported devices or initialization failure retain the original upscale pass automatically.

### Ray-traced shadows (experimental)

A separate **Ray-traced shadows (experimental)** checkbox on the Graphics tab, off by default. The row appears only on devices that pass all three checks: `supportsRaytracing`, GPU family Apple9 or later, and a model allowlist (iPhone 15 Pro / A17 Pro and later, iPad Pro M4 and later, Macs with M3 and later); anywhere else the setting is treated as off and the original shadow maps are used. When on, the translation layer still rasterizes the game's two 1024² cascaded shadow maps as a fallback, also builds Metal acceleration structures from the shadow-pass draws (per-mesh cache keyed by the full index and vertex content, static meshes built once, 64 MB budget by default), traces one occlusion ray per pixel toward the sun before the main opaque pass, and the 230 main-pass fragment shaders that sample the cascades are swapped for `_rt` variants (generated by `dm/gfx/tools/rts_variant.py` at packaging) that read the screen-space mask and take the darker of it and the original shadow-map sample (ambient term kept inside shadow). Only when the trace succeeds and every shadow-pass draw of that frame is in the acceleration structure are the shadow maps cleared this frame cleared back to fully lit before the main pass (a map the game keeps across frames and did not clear this frame is left as drawn); otherwise (a mesh not built yet, an unsupported primitive or format, no camera, sun or main pass, a failed trace) that frame keeps the original shadows, and after a failure the following frames skip tracing with a backoff. Switching it off releases the whole cache once the GPU is done with it. Cost: the shadow maps are rasterized as with the switch off, plus the trace and a per-frame digest of the shadow geometry. Point-light cube shadows are unchanged. Skinned units and alpha-tested foliage are handled from phase 2 / phase 4 (below). Metal 4 path only; no device performance or power measurement yet. Status: implemented; runs on the Mac native host, on-device validation in progress.

Phase 2 (same switch): shader variants are generated at packaging time; characters and monsters cast shadows in their real pose (skinned position export + acceleration-structure refit); every frame up to 4 of the closest, brightest point lights around the character get one occlusion ray each for point-light shadows (half resolution); alpha-tested occluders such as foliage and fences are ray traced too: at each hit the ray asks the game's own fragment shader whether the transparent part blocks (phase 4; `DM_RT_ALPHA=0` turns it off); sun shadow draws that are in the acceleration structure are not drawn into the shadow map and the ray tracing takes over, the others (new mesh pending, quota full…) are drawn as original, so one draw outside no longer sends the whole frame back to original (partial fallback; `DM_RT_SHADOW_PART=0` restores clearing the whole frame for comparison); the sun direction comes from the largest shadow map (the cascade), and non-sun maps such as the top-down soft shadow and hair stay original. Indoor / underground levels without a sun trace point lights only and are not darkened. `DM_RT_POINT=0` disables point lights only, `DM_RT_POINT_N` sets the count, `DM_RT_SOFT=<strength>` enables near-hard / far-soft shadows (hard by default), `DM_RT_PLAYER_LIGHT` sets the factor of the character's own light (0.8 by default). Sun rays are traced at half resolution by default and upsampled by depth (`DM_RT_SUN_HALF=0` for full resolution); point-light rays run at quarter resolution (`DM_RT_POINT_DIV`) and only within the light's radius. When the GPU is tight it steps down automatically (one point light → alpha-tested occluders original → point-light shadows off → ray tracing paused, original shadows) and steps back up once there is headroom, ignoring frames that build meshes and the first seconds; `DM_RT_LEVEL=<0–4>` pins a level, `DM_RT_AUTO=0` turns automatic off. **Experimental** ray-traced AO is off by default (`DM_RT_AO=1` turns it on): half-resolution hemisphere rays firm up wall bases, stone gaps and contact points, and it turns off automatically from level 1. The diagnostic log reports instances, variants, point lights and GPU ms every 600 frames in a 「光追阴影：第 N 帧 …」 line, and alpha-tested draws in / out of the ray tracing with reasons in a 「光追镂空：第 N 帧 …」 line; device validation and soft shadows are pending.

### Exploratory device power probe

These are historical readings from before the current local audit fixes. No new phone power measurement was performed during the audit.

Each profile was run on the same device and test save, with a fixed seed and time on Blood Moor using the same scripted encounter: six in-game enemies and identical skill inputs. The frame-rate target was 60, display brightness was 100%, and the device was unplugged with no active cooler. FPS and GPU timings below come from the game runtime logs; the power column is the whole-device battery drain rate estimated by Apple Power Profiler.

| Profile | FPS at about 30 s | GPU average / p95 | Power probe |
|---|---:|---:|---:|
| Auto | 59.9 FPS | 11.74 / 15.68 ms | 37.2%/hr |
| Eco | 59.8 FPS | 14.58 / 16.55 ms | 44.5%/hr |
| Balanced | 60.0 FPS | 14.71 / 16.01 ms | 48.5%/hr |
| Quality | 53.9 FPS | 18.56 / 19.74 ms | 31.2%/hr |
| Original | 60.0 FPS | 12.03 / 15.83 ms | 32.6%/hr |

**These readings cannot rank profile power use and must not be interpreted as savings percentages or watts.** Each profile has only one approximately 0.95-second power snapshot; Power Profiler then reported a device disconnect instead of recording the requested 8 seconds. Thermal states also differed between runs (for example, Quality reached a higher thermal state later), and the readings do not track rendering load consistently. The table preserves the exploratory observations and the measurement limitation; it does not claim that any profile saves power. A reliable comparison requires sustained device power sampling and repeated runs at matched thermal states.

See [build and verification notes](docs/PLUS.md) for other checks and limitations.

## Notification example

The frame is generated from the game’s button artwork, preserving its decorative ends. The wider text area accommodates longer English notifications.

![On-device English notification with complete frame](docs/images/notification-frame-en.jpg)

*Device UI demonstration. Game imagery belongs to its respective rights holders. No account/device information or save files are included; image metadata has been removed.*

## Branches

- `main` preserves the upstream baseline used for this preview.
- `codex/plus` contains the Plus changes and these documents.

Review the [changelog](CHANGELOG.md) for scope, validation and remaining limitations before updating. Back up saves before changing builds or signing identities.

## Build your own copy

Bring your own legally obtained game files and your own signing configuration. This repository contains no game program, game resources, saves, commercial fonts, signing certificates or installation packages.

```sh
git clone --branch codex/plus https://github.com/augustxxx-debug/d2r-mobile-plus.git
cd d2r-mobile-plus
./setup.sh check
```

Follow the upstream [game-file preparation guide](docs/GAME_FILES.md), [workspace setup](dm/README.md) and [iOS build guide](dm/ios/README.md). After preparing the workspace and local signing configuration, build with `cd dm/ios && ./run.sh build`. Configuration and generated resources remain local and ignored by Git.

The font override is **off by default**. See [optional font configuration](docs/PLUS.md#optional-font) for its dependencies, scope and limitations. A font file must be supplied locally by its owner; ARKai_T is not distributed here.

## Android (Qualcomm Adreno candidate)

The Android build follows the upstream NVN→Vulkan path (`dm/android/`, `dm/gfx/vulkan/`). The first candidate targets Qualcomm Adreno only (verified on an Adreno 740 handheld: camp and combat at 60 FPS, the 120 FPS stop, a physical controller). Mali (MediaTek / Samsung Exynos) still has subgroup-16 and ClipDistance gaps and gets no package yet. The package contains no game content: make the import pack on a computer from your own game files (`dm/tools/d2rm_android.py`), copy it to `Download/S2X/D2R Mobile/` on the phone and open the App to import it; the game `data` goes to the same place. See `dm/android/README.zh-CN.md`.

Android alignment in this round (relative to the upstream Android backend): crash fence wired to background / foreground / memory warnings; pause-menu "Share diagnostics" (redacted, system share sheet); version label and build number from the shared source; native output size plus the Graphics "Resolution" level; the 120 FPS stop (shown on screens of 120 Hz or more, switches the display to its high refresh mode); "Show frame rate"; physical controllers (Xbox layout, B / BACK no longer exit, controller / touch mode switching); 3D brightening cap 2; Adreno 740 driver workarounds (warp32 masks computed from the lane id, lane-id self-test).

**Known issue (Android): memory creeps up in long sessions.** On an Adreno 740 handheld, a 31.5-minute run that changed level every 90 seconds through all five acts (60 FPS) kept slowly gaining memory (about 8 MB a minute in the second half); the app was never closed by the system and received no low-memory warning. Longer sessions (an hour or more) are not verified yet. Restart the app after 1–2 hours of continuous play.

## Verification and limitations

The preview has passed focused host geometry/classification tests, the iOS touch suite, notification generator checks and a signed device build. The quest narration issue was reproduced before the change and retested after it. See [verification details](docs/PLUS.md#verification) for the checks actually performed and those still pending.

**Large black bars on both sides (on an iPhone)**: the package itself is universal (iPhone + iPad). If the signing tool's "iPad mode" is on (device type set to iPad), iOS runs it on the iPhone as an iPad app in compatibility mode, at the iPad aspect ratio (about 1.44:1) with black bars on both sides that the game cannot remove. Turn off "iPad mode", set the device type to iPhone, re-sign and reinstall. The app detects this at launch: the log says it is running with the iPad identity, and one alert is shown after entering the game.

Physical folding transitions, complete controller navigation, all achievement/notification combinations and every game panel still need broader regression testing. Please report reproducible cases with screen dimensions, input mode, steps and the source commit. Remove personal information from diagnostics before sharing; do not upload game files, keys, saves or signed packages.

## Credits and license

Based on [D2R Mobile](https://github.com/jhaizhou-ops/d2r-mobile), with thanks to its author and contributors. Existing copyright notices and the [MIT license](LICENSE) are retained. Diablo II: Resurrected and its assets belong to their respective rights holders. This is an independent community project.

The Android package bundles the LXGW WenKai font (霞鹜文楷) Medium v1.530 by LXGW, licensed under the [SIL Open Font License 1.1](https://openfontlicense.org) (the licence travels in the APK as `assets/dmgen/font/OFL.txt`; source: https://github.com/lxgw/LxgwWenKai). The on-phone generator uses it for the Chinese / Japanese glyphs of the UI font; the font is not modified in the package.

The Android package also bundles the tools that build the import pack on the phone: CPython 3.13 (Chaquopy's Android build, with OpenSSL, SQLite and the libraries CPython incorporates), NumPy, OpenBLAS, python-lz4, fontTools, Capstone, Pillow with FreeType, HarfBuzz and zlib-ng, zlib, SPIRV-Tools, and the shader translator built with .NET NativeAOT from the Ryujinx shader translator. Thanks to all of their authors. Versions and licences are listed in [docs/DEPENDENCIES.md](docs/DEPENDENCIES.md#bundled-in-the-android-package); the full licence texts travel in the APK as `assets/dmgen/THIRD_PARTY_NOTICES.txt` (same as [dm/android/THIRD_PARTY_NOTICES.txt](dm/android/THIRD_PARTY_NOTICES.txt)).
