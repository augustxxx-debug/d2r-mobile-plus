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

This feature is experimental and disabled unless `DM_UI_FONT` is set. Supply a static TrueType font that you are entitled to use. ARKai_T is not included. A local AR ZhongkaiGBK Medium font was identified and its Chinese settings appearance inspected on device; this does not validate every string or layout. Font replacement happens at build time, with no in-game font selector.

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

Synthetic tests cover Unicode fallback, units-per-em conversion, original registration/vertical metrics, unchanged input files, content fingerprints, disabled cleanup and invalid input paths. Tests also cover custom vertical-table compatibility and preservation of original private-use game symbols, for six tests total. Chinese and English Graphics/Custom pages were inspected on device; full panel coverage remains pending.

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
| Physical device acceptance | Previous four-tab revision: user confirmed Xbox/folding; see below for five-tab regression scope |
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

### Remaining checks

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

This regression used a separate test save on the physical device: all five tabs opened; applying Graphics after disabling native Terror Zones preserved that setting; Defaults → Discard retained 30 FPS / Quality and disabled Terror Zones; Defaults → Apply restored 60 FPS / Auto, enabled Terror Zones and native offline scaling 1 while retaining language and voice. Injected controller input verified the five-tab cycle, frame-rate selection, upscaling switch and profile selection. This run does not substitute for pressing a physical controller, and the five-tab revision has not repeated physical folding acceptance.
