# Solo AI difficulty pass

Status, 2026-09-12: implementation, portable tests, Mac engine tests, signed SDK 27 packaging and rendered native iOS-on-Mac verification complete. The update is ready; AEON installation is awaiting confirmation of a safe time to replace the running game. The user requested five solo difficulty levels and asked whether Apple frame generation is active.

## Scope

- Five visible main-menu choices: Very Easy, Easy, Normal, Hard and Expert. Normal preserves the existing opponent.
- Change AI development, army planning and attack timing through shared presets. Preserve paid economy, unit stats, construction rules and fog knowledge.
- Reuse saved `Config.aiAggression` values 0.5, 0.75, 1, 1.5 and 2 so existing saves and protocol 2 remain compatible.
- Remember the selected menu preference; Enter and touch start agree. Continue and rematch retain the actual match setting. Training and online rooms keep their existing rules.
- Preserve the approved menu safe area and full-bleed artwork, and keep the five choices clear on desktop and mobile.

## Implemented opponent tuning

These are strategy targets and timing gates, not free resources or unit bonuses. Travel, resources and combat can delay actual attacks.

| Level | Development timing | Workers, one / multiple bases | Army target | Attack gate | Minimum group | Attack interval |
| --- | --- | --- | --- | --- | --- | --- |
| Very Easy | 1.50x normal time | 8 / 12 | 65% | 420 s | 4 | 40 s |
| Easy | 1.25x | 10 / 15 | 80% | 320 s | 5 | 32 s |
| Normal | Original | 12 / 18 | 100% | 240 s | 7 | 24 s |
| Hard | 0.80x | 14 / 21 | 120% | 190 s | 9 | 18 s |
| Expert | 0.65x | 16 / 24 | 145% | 150 s | 11 | 12 s |

## Validation

- All four portable CTests pass in 13.26 seconds: simulation, navigation, network and difficulty. The difficulty suite covers preset mapping, deterministic decisions, worker targets, attacks, equal gameplay rules with AI disabled, and save/snapshot/replay continuity.
- In a controlled fixture, first attacks occurred at 440.8 / 321.9 / 241.95 / 198.9 / 157.9 seconds from Very Easy through Expert. These checks establish behavior differences, not a measured whole-match win-rate ranking.
- Normal compatibility was checked against the actual pre-change AI source extracted from Git HEAD and compiled as a separate object. Linker maps confirm the historical object was used. All 24 sampled simulation hashes match across the three maps at ticks 0 through 5600. The shared hash-file SHA-256 is `d9f72bc820d34ba48090f27b3efe541a5c482b34a368b23dbb892094db05f96f`. The in-source default-vs-preset test makes only that narrower equivalence claim.
- Mac development build and all 18 Unreal tests pass, including the new `Cinderline.Integration.SoloAIDifficulty`. It exercises all five starts, preference roundtrip, saved difficulty, rematches, training isolation and invalid-choice fallback.
- Inspected actual Unreal menu captures at 2560×1440 desktop, 956×440 phone, 667×375 small phone and 1024×768 tablet sizes. All five selected states are captured across the cases. Every case has 13 distinct button bounds within the viewport and no button overlap. Compact difficulty controls are 44 layout units tall. These are rendered and programmatic-action checks, not physical taps.
- A separate static review found no product defect in the bounded difficulty code. Its finding about an overstated test name was corrected.
- Signed SDK 27 iOS build passed all 54 actions in 192.72 seconds; cook, package and strict signature verification passed. Executable SHA-256: `9e362e674d9b840fe8af6ef036555a2e197b369b1836d2e44276706797a84230`. The development profile covers AEON; Game Mode and signed/profile sustained-execution entitlement remain enabled.
- Two fresh native iOS-on-Mac runs of that exact signed binary launched and rendered Very Easy and Expert selections. Both 2052×1536 captures are visually inspected, all 13 button bounds pass, and there are no new crash reports, assertions or handled ensures. Test processes were closed afterward.
- AEON has not been reinstalled or relaunched during this pass. The prepared update awaits installation timing confirmation, to avoid interrupting another match. Physical taps, new-package gameplay and sustained thermal acceptance remain separate.

Evidence: [portable results](../artifacts/ai-difficulty/tests-portable.txt), [Unreal results](../artifacts/ai-difficulty/automation.json), [phone menu](../artifacts/ai-difficulty/mac/phone.png), [desktop menu](../artifacts/ai-difficulty/mac/desktop.png), and the other per-layout captures/bounds under `artifacts/ai-difficulty/mac`.

## Player behavior

Select a difficulty in the main menu before starting a new solo skirmish. The choice is remembered locally in `Saved/Config/Skirmish.ini`. Continue uses the difficulty stored inside the save; a rematch keeps that match's level. Normal remains the default. Training still has no attacking AI, and online matches remain server-controlled. The existing menu-only safe-area transform and full-bleed artwork are preserved.

## MetalFX assessment

The project does not currently enable MetalFX frame interpolation. Apple's interpolator takes adjacent rendered frames, motion vectors and depth; UI composition and frame pacing require integration. Apple recommends at least 30 rendered FPS before interpolation. Whether it reduces total power for this game requires measurement. No interpolation feature is enabled by this difficulty change.

Reference: [Apple's MetalFX integration guidance](https://developer.apple.com/videos/play/wwdc2025/211/).

The installed UE 5.8 audit also found no active MetalFX framework linkage, symbols, runtime dependencies or source consumer in the project, Metal RHI, renderer or scanned plugins. Bundled MetalCPP headers expose spatial/temporal scaling without integrating them. Mobile FXAA does not activate the temporal velocity path. Depth retention, motion output, UI composition and generated-frame presentation would require implementation. This finding is specific to this installed engine and current project.

Final evidence: [verification manifest](../artifacts/ai-difficulty/verification.json), [signed package](../artifacts/ai-difficulty/package-verification.json), [native Very Easy menu](../artifacts/ai-difficulty/native-ios-on-mac/menu-0.png), [native Expert menu](../artifacts/ai-difficulty/native-ios-on-mac/menu-4.png). The package is at `Saved/Packages/IOS/Cinderline.app`.
