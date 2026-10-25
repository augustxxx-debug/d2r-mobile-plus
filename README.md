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
| Optional font | Build-time main-font override with missing-character fallback | Experimental; ARKai_T inspected locally on device, broader layout coverage pending |

These are changes on top of upstream commit [`195ae11`](https://github.com/jhaizhou-ops/d2r-mobile/commit/195ae11dc70f1ba7735c6063e1ed81102e1dbf52). The branch adds experimental MetalFX spatial upscaling; it does not introduce multiplayer or frame generation. Upstream performance measurements and platform support claims are not new benchmarks for this fork.

## MetalFX spatial upscaling (experimental)

Settings retains five separate tabs: **Audio / Gameplay / Settings / Graphics / Custom**. The original offline Terror Zones control remains in Settings; Graphics uses a separate destination.

Open **Options → Graphics** in game. Enable **MetalFX spatial upscaling** to reveal the four-position Auto / Eco / Balanced / Quality slider. Turning the switch off restores the original upscale pass and remembers the selected profile. Frame rate has a separate left/right 30 / 60 FPS selector. Press **Apply** to activate changes without restarting; switching may briefly pause rendering. **Discard** restores the active settings. **Defaults** preserves the native global reset, including game options, Graphics and Custom, while retaining language and voice; press Apply to save. Native Terror Zones and offline difficulty scaling also return to defaults; Discard cancels a pending reset.

![English Graphics settings on device](docs/images/graphics-settings-five-tabs-en.jpg)

*Five-tab revision on device: Audio / Gameplay / Settings / Graphics / Custom remain separate; Graphics includes frame-rate selection, the MetalFX switch and four upscaling profiles. The Chinese documentation example uses an optional local font; default builds retain the game font, and no font file is included.*

| Profile | Initial 3D width/height ratio | Behavior |
|---|---|---|
| Auto | 70% or 80% | Chooses by output pixel count; uses 65% in Low Power Mode or serious/critical thermal states, with 60 seconds of stable recovery before raising the ratio; also adapts to frame timing |
| Eco | 60% | Targets lower 3D rendering load; measured energy savings are not guaranteed |
| Balanced | 75% | Balances 3D clarity and rendering load |
| Quality | 85% | Prioritizes a higher 3D ratio |

Auto samples system state at most once per second. Reductions take effect at the next sample; increases require an unchanged healthy target for 60 continuous seconds. Renewed thermal pressure, Low Power Mode or a changed target restarts recovery. Gaps longer than 3 seconds (such as backgrounding or loading) do not count toward recovery. Manual profiles bypass this recovery delay. The frame-timing controller retains its 3-second post-change hold, approximately 30-second clean recovery, and approximately 90-second recovery after a failed upward step. Low Power Mode refers to the system switch, not battery percentage. Simulated state-sequence unit tests cover these timing rules; device stutter and power measurements for this change remain pending.

Ratios apply separately to width and height, not energy savings. Only 3D color is upscaled, retaining the original mask; subsequent UI composition retains its resolution. The frame-rate setting is independent. Auto, Eco and Balanced can reduce 3D resolution further during sustained frame drops. Unsupported devices or initialization failure retain the original upscale pass automatically.

### Exploratory device power probe

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

## Verification and limitations

The preview has passed focused host geometry/classification tests, the iOS touch suite, notification generator checks and a signed device build. The quest narration issue was reproduced before the change and retested after it. See [verification details](docs/PLUS.md#verification) for the checks actually performed and those still pending.

Physical folding transitions, complete controller navigation, all achievement/notification combinations and every game panel still need broader regression testing. Please report reproducible cases with screen dimensions, input mode, steps and the source commit. Remove personal information from diagnostics before sharing; do not upload game files, keys, saves or signed packages.

## Credits and license

Based on [D2R Mobile](https://github.com/jhaizhou-ops/d2r-mobile), with thanks to its author and contributors. Existing copyright notices and the [MIT license](LICENSE) are retained. Diablo II: Resurrected and its assets belong to their respective rights holders. This is an independent community project.
