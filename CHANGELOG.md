# Changelog

[简体中文](CHANGELOG.zh-CN.md)

This changelog records changes introduced by **D2R Mobile Plus** relative to its upstream baseline. It does not restate the upstream release history. Entries describe implementation status; an unreleased entry is not a published binary release.

## [Unreleased]

Baseline: upstream [`195ae11`](https://github.com/jhaizhou-ops/d2r-mobile/commit/195ae11dc70f1ba7735c6063e1ed81102e1dbf52). Validation updated on **2026-10-26**.

### Added

- Custom tab **Show frame rate** switch (off by default): a small translucent gold label at the top right of the picture with the actual presented frame rate, refreshed every 0.5 s, drawn by UIKit (Palatino Bold as the closest system font to the game's text face; exact in-game font rendering is still to do). `DM_FPS=120` with `DM_PRESENT_HZ=120` is a 120 Hz experiment switch for the pacer, ProMotion request and 3D scale controller; not a settings option.
- Graphics tab **Resolution** slider (Auto / Eco / Balanced / Native) capping the whole-frame output size by pixel budget (4.0 / 2.07 / 2.8 MP / none), aimed at very dense screens such as the iPhone Duo inner screen where the UI layer, final composite and present blit at 2852x2006 dominate GPU time regardless of the 3D ratio. Shrink-only, aspect-preserving, applies live, defaults keep regular iPhones unchanged. Pure budget math in `dm/host/dm_res_cap.h` with unit tests; localized in six text sets. Implemented and unit-tested; not yet device-verified or power-measured.
- Auto render resolution now picks its pixel budget by GPU generation parsed from the Metal device name (A16 and earlier 2.07 MP, A17 Pro / A18 2.8 MP, A18 Pro / A19 3.8 MP, A19 Pro / A20+ 4.0 MP, M series 4.5 MP); screens above 4.5 MP native (the iPhone Duo inner screen) take the Balanced 2.8 MP budget, after a 10-minute device run under full-screen spell effects showed Native pinned at ~15 ms GPU per frame and Eco at ~10 ms with identical fps and dropped frames. Unit-tested; the GPU-generation table is not device-verified on older chips.
- Launch-time detection of an iPhone running the package with the iPad identity (a re-signing tool's "iPad mode"), which makes iOS letterbox the game at the iPad aspect ratio: logged as a failure and shown once as an alert with the fix. Diagnosed from a user report on iPhone 16 Pro; the shipped package itself is universal.
- Measurement build switch: `DM_DIAG_SAMPLING=1 dm/ios/run.sh build`, or a `Documents/diag_sampling.txt` file on any build, turns on per-pass GPU timing, per-thread CPU and heat-attribution sampling into `Documents/diag/`. Diagnostic output only; never set for release builds.
- Added on-device Graphics settings and notification screenshots to both READMEs, with image metadata removed. Graphics screenshots now show the five-tab revision and use distinct filenames.

- Dedicated Graphics tab with an independent MetalFX enable switch, a four-position profile slider, and a separate left/right 30 / 60 FPS selector. Help uses smaller, muted secondary text. New labels and application guidance are localized in Chinese, English, Spanish, Latin American Spanish, Russian and Korean.

- Experimental MetalFX spatial upscaling with Auto, Eco, Balanced, Quality and Original profiles. Apply switches profiles live without restarting; render-target recreation may briefly pause rendering. Only 3D color is scaled, retaining the original mask and subsequent UI. Unsupported devices or initialization failure use the original pass.

- Adaptive front-end scene coverage and bottom-edge layout, with a centered interface and safe-area-aware positioning.
- Notification frame generation using locally supplied game artwork. Corner ornaments remain unstretched; the center uses an opaque shaded background.
- Optional build-time main-font override through `DM_UI_FONT`, including original-font fallback for missing characters. Disabled by default; no font files are distributed.
- Focused regression tests for front-end coordinate conversion, quest-control classification, notification frame construction and font composition.
- English and Simplified Chinese project, build, verification and change documentation.

### Fixed

- Route enhanced UI notifications through a bounded queue drained on the game main thread. Paused-menu deposit feedback no longer waits for the server to resume; controller gold/item tooltips no longer move the notice.
- Fit the notification frame to native font measurements, preserving fixed corner and medallion art with single-frame slices. Sprite and widget writes both require matching ownership, frame and atlas checks. Common modal backgrounds share the game-art treatment while retaining native controls and actions.
- Diagnostic exports now include applied Graphics/MetalFX state and bounded log tails, with export-only redaction of recognized identifiers and known character names. Original logs remain unchanged.

- Settings tab labels now use a local, non-wrapping text style, including the controller layout's inherited style. This keeps English Gameplay on one line without changing tab artwork, hit rectangles or destinations.

- MetalFX can now activate from a recognized native 1:1 pass, without waiting for frame drops to lower resolution first. Off/unsupported/failed paths retain the original scaler, and switching away from Auto resets its recovery sampling.

- Preserved frame-time resolution reductions when Auto changes its thermal/power cap; raising the cap no longer resets the frame-time controller. The game thread consumes one absolute target to avoid transient cap/level mismatches. Added production-entry regression coverage, rapid cap-change checks, and a disabled-controller fallback check.

- Fixed Graphics incorrectly replacing Game Settings and hiding the native offline Terror Zones control. Settings now has five distinct destinations in the same touch/controller order: Audio, Gameplay, Settings, Graphics, Custom. Native Audio, Gameplay and Settings layouts were compared against the pre-Graphics version: five are byte-identical, and controller Game Settings changes only table row height to prevent divider overlap. Added preservation, tab-artwork and new-layout path regression coverage.

- Font composition now falls back to original visible glyphs when the selected font maps a character to an empty placeholder. Language choices have additional space and smaller text to avoid wrapping with replacement fonts.

- Preserved upstream global defaults across Graphics and Custom, including pending changes on the other enhanced tab; language and voice remain unchanged.

- Widened localized system notifications in touch and controller layouts, with text insets that keep longer English sentences clear of the decorative ends.

- Optional font composition now removes incompatible custom-only vertical tables and preserves original private-use game symbols, preventing merge failures and icon replacement.
- Live upscale changes preserve an explicit dynamic-resolution floor override.

- Misalignment between character-list content and its frame, off-center class creation actions and raised front-end bottom controls.
- A stale selected-tab outline and native hover/focus state when switching game panels.
- Placement of both lower-right indicator families: the layered fire-logo loading animation and the red save/loading symbol. Placement is prepared while hidden, and redundant resolution transitions are avoided.
- Quest narration controls ignoring stationary touches held past the ordinary **0.35-second** tap limit. The exception is restricted to quest-log controls; dragging, cancellation and other touch targets retain their existing behavior.

### Validation

- Audit build `c7ea42a`: user-confirmed physical Xbox navigation across all five settings tabs, Graphics controls and Apply, plus landscape inner/outer-screen folding on Graphics. Final configuration: 60 FPS + Quality. Loading transitions and other panel/modal combinations remain outside this acceptance.

- Dedicated Graphics settings passed device touch tests and a user-confirmed physical Xbox controller test, including left/right 30 / 60 FPS selection. The final configuration was 60 FPS with Quality.
- The user confirmed landscape fold/unfold transitions on the Graphics page across inner and outer displays. Menu/loading transitions and all other pages still require broader coverage.
- Profile migration, persistence and Apply/Discard/global Defaults semantics passed **195 checks**. The iOS touch suite passed **49 cases / 4,487 checks**; host UI classification passed **381 checks** and front-end geometry passed **44 checks**.
- Metal 3 and Metal 4 upscalers each passed **64 color/mask samples** and invalid-extent rejection. Device traces also recorded live changes between original rendering and all four upscaling profiles without restarting.
- Font composition passed **6 tests**, including empty mapped-glyph fallback; asset-cache contracts passed **14 tests**. Chinese and English Graphics/Custom pages were inspected on device, including notification width and language-choice labels.
- Earlier device traces reproduced the quest-log issue on the baseline: a stationary **0.8-second** release emitted no click. Both quick and 0.8-second releases stopped narration after the fix. Prior loading-indicator review covered 44 captured frames; this does not establish every transition is stable.
- Native-write, widget-cache, input-mode and localization gates passed; **475 localization entries** are synchronized. The signed iOS device build succeeded. Three tests requiring private native-binding fixtures were skipped because those fixtures are not distributed.

### Experimental / pending validation

- Font composition has synthetic-font coverage; an actual **ARKai_T / AR ZhongkaiGBK Medium** font was inspected locally in Chinese and English settings; complete English/Chinese panel coverage remains pending. This changes the shared main font and generated text that uses it, not every decorative face or bitmap label.
- Graphics-page physical controller and fold transitions are confirmed; other controller flows, folding during loading and all panel/achievement combinations remain pending. The preview does not claim universal UI correctness.
- MetalFX spatial upscaling is implemented; frame interpolation is not. An initial matched-map scripted combat power probe is complete. Power Profiler returned only an approximately 0.95-second snapshot per profile, and thermal states were not fully matched; readings are exploratory raw observations only, with no profile ranking, savings percentage or wattage claim. See README.

### Distribution

- `main` retains the upstream baseline; `codex/plus` carries the enhancement preview.
- Only source and documentation are published. Game-derived resources, local diagnostics, saves, signing material and commercial font files are excluded.

[Unreleased]: https://github.com/augustxxx-debug/d2r-mobile-plus/compare/main...codex/plus
