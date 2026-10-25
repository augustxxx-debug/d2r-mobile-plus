# Changelog

[简体中文](CHANGELOG.zh-CN.md)

This changelog records changes introduced by **D2R Mobile Plus** relative to its upstream baseline. It does not restate the upstream release history. Entries describe implementation status; an unreleased entry is not a published binary release.

## [Unreleased]

Baseline: upstream [`195ae11`](https://github.com/jhaizhou-ops/d2r-mobile/commit/195ae11dc70f1ba7735c6063e1ed81102e1dbf52). Preview validation recorded on **2026-10-25**.

### Added

- Experimental MetalFX spatial upscaling with Auto, Eco, Balanced, Quality and Original profiles. Apply switches profiles live without restarting; render-target recreation may briefly pause rendering. Only 3D color is scaled, retaining the original mask and subsequent UI. Unsupported devices or initialization failure use the original pass.

- Adaptive front-end scene coverage and bottom-edge layout, with a centered interface and safe-area-aware positioning.
- Notification frame generation using locally supplied game artwork. Corner ornaments remain unstretched; the center uses an opaque shaded background.
- Optional build-time main-font override through `DM_UI_FONT`, including original-font fallback for missing characters. Disabled by default; no font files are distributed.
- Focused regression tests for front-end coordinate conversion, quest-control classification, notification frame construction and font composition.
- English and Simplified Chinese project, build, verification and change documentation.

### Fixed

- Optional font composition now removes incompatible custom-only vertical tables and preserves original private-use game symbols, preventing merge failures and icon replacement.
- Live upscale changes preserve an explicit dynamic-resolution floor override.

- Misalignment between character-list content and its frame, off-center class creation actions and raised front-end bottom controls.
- A stale selected-tab outline and native hover/focus state when switching game panels.
- Placement of both lower-right indicator families: the layered fire-logo loading animation and the red save/loading symbol. Placement is prepared while hidden, and redundant resolution transitions are avoided.
- Quest narration controls ignoring stationary touches held past the ordinary **0.35-second** tap limit. The exception is restricted to quest-log controls; dragging, cancellation and other touch targets retain their existing behavior.

### Validation

- Metal 3 and Metal 4 upscalers each passed 64 color/mask samples and invalid-extent rejection. Profile migration, persistence and Apply/Discard/Defaults semantics passed 86 checks; scaling policy and system notifications passed 61 checks.
- One device process applied Original, Quality, Eco, Auto and Balanced in sequence without restarting. Five-profile radio/scroll bounds checks and five font-composition tests passed.

- Reproduced the quest-log issue on the baseline: a **0.8-second** stationary release emitted no click, while a quick tap stopped narration. Retested on device: both quick and 0.8-second releases stopped narration.
- Touch regression coverage includes releases at 0.12, 0.35, 0.45, 0.8 and 2 seconds, cancellation, dragging and duplicate releases. The iOS touch suite passed **49 cases / 4,487 checks**.
- Host UI classification tests passed **381 checks**; front-end geometry tests passed **44 checks**.
- Reviewed 44 captured loading-indicator frames in the preceding layout test build. Fire-logo animation bounds varied by one pixel within the sampled entry sequence; the sampled red indicator position remained stable.
- A signed iOS device build completed successfully. Tests requiring three private native-binding fixtures were skipped because those fixtures are not distributed.

### Experimental / pending validation

- Font composition has synthetic-font coverage; an actual **ARKai_T / AR ZhongkaiGBK Medium** font was inspected locally in Chinese settings; complete English/Chinese panel coverage remains pending. This changes the shared main font and generated text that uses it, not every decorative face or bitmap label.
- Complete physical folding transitions, controller navigation and all panel/achievement combinations remain pending. The preview does not claim universal UI correctness.
- MetalFX spatial upscaling is implemented; frame interpolation is not. An initial matched-map scripted combat power probe is complete. Power Profiler returned only an approximately 0.95-second snapshot per profile, and thermal states were not fully matched; readings are exploratory raw observations only, with no profile ranking, savings percentage or wattage claim. See README.

### Distribution

- `main` retains the upstream baseline; `codex/plus` carries the enhancement preview.
- Only source and documentation are published. Game-derived resources, local diagnostics, saves, signing material and commercial font files are excluded.

[Unreleased]: https://github.com/augustxxx-debug/d2r-mobile-plus/compare/main...codex/plus
