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

This feature is experimental and disabled unless `DM_UI_FONT` is set. Supply a static TrueType font that you are entitled to use. ARKai_T is not included, and its actual appearance has not yet been evaluated.

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

Synthetic tests cover Unicode fallback, units-per-em conversion, original registration/vertical metrics, unchanged input files, content fingerprints, disabled cleanup and invalid input paths. They do not establish visual correctness for ARKai_T.

## Verification

Validation record: **2026-10-25**, upstream baseline `195ae11dc70f1ba7735c6063e1ed81102e1dbf52`.

| Check | Result / coverage |
|---|---|
| iOS touch recognizer | 49 cases; 4,487 checks passed, including slow quest releases and cancellation/drag guards |
| Host UI classification | 381 checks passed |
| Front-end geometry | 44 checks passed |
| Notification generator | Automated frame construction checks passed |
| Font composition | 3 synthetic-font tests passed; target font pending |
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

The font test explicitly skips when its optional dependency is absent. Some broader tests require game-derived tables or private runtime fixtures; see [contribution requirements](../CONTRIBUTING.md). No full rewrite-equivalence, entire graphics-suite or retail Switch ground-truth acceptance claim is made for this preview. The rewriter and graphics engine are not changed by the Plus patch.

### Remaining checks

- Physically fold/unfold while on the same panel, then repeat during menu/loading transitions.
- Exercise controller focus, prompts and actions throughout front-end and in-game panels.
- Reproduce all achievement/notification overlaps, including bottom HUD masking.
- Check all localizations and panel layouts with the actual replacement font.

These require broader device/runtime coverage. A successful focused test does not close every related visual issue.

## Release hygiene

Publish source and documentation only. Keep generated resources, fonts, saves, device captures, private diagnostics, signing configuration and keys outside the published tree. Inspect newly committed file contents and author/committer metadata before pushing. Review local diagnostics before attaching them to an issue.

The `main` branch retains the upstream baseline; `codex/plus` contains the preview changes. To compare behavior, build either branch from your own workspace. Always back up saves first; do not uninstall an app that holds your only copy.
