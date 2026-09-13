# First-run guided match

Status on 2026-09-13: **the first-run flow and 14-objective guided match are implemented. The final Mac rebuild, all 21 Unreal tests, all 14 rendered Mac cases, the signed SDK 27 package and all four native iOS-on-Mac cases pass. Human and physical-device acceptance remain pending.** The final automated evidence was captured on 2026-09-12 UTC.

## Player flow

- A missing training preference presents a one-time choice between `GET GUIDED TUTORIAL` and `SKIP FOR NOW`. Accepting and skipping both resolve the offer, while completion remains a separate persisted value.
- Enter accepts the offer and Escape skips it. While the offer is open, underlying start, help, online, difficulty and tutorial actions remain blocked.
- Players who skip can start the guided match later from the menu. A legacy preference with `Completed=True` migrates as completed and does not show the new offer.
- The guided match uses a fixed local scenario with strategic AI disabled. It preserves the normal skirmish save and selected solo difficulty. Starting, restarting, exiting or skipping the tutorial does not replace the player's saved match.
- Completion is recorded only after all teaching objectives finish and ordinary combat produces a real player victory. An early forced victory does not mark training complete.
- The objective card gives a short instruction, progress, recovery hint, named selection/location action and relevant guide link. The staged opponent waits through the teaching phase, then issues a finite set of paid production and attack orders under ordinary simulation rules. [The tutorial control correction](TUTORIAL_CONTROLS_PASS.md) replaces FIND/MORE and fixes stale selection after focusing an objective.
- The completed result identifies the guided victory and leaves the tutorial available for replay.

## Objective sequence

1. Move the camera.
2. Select a Drudge.
3. Order ore gathering.
4. Train another Drudge.
5. Build a Kiln.
6. Train three Embers.
7. Build a Siphon and learn crew capacity.
8. Scout outside the base and reveal fog.
9. Use attack-move against a hostile unit.
10. Build a Resonator.
11. Complete a weapons upgrade.
12. Reinforce to six Embers.
13. Defend against the small enemy wave.
14. Destroy the enemy Anchor and win.

Late-step recovery handles canceled Resonator construction, canceled weapons research, lost Embers and destroyed production. The opponent does not advance while the player is deliberately idle during the teaching phase.

## Functional verification

- [x] First-run invitation, persisted accept/skip state and legacy completion migration.
- [x] Modal input isolation for Enter, Escape and blocked underlying menu actions.
- [x] Staged paid opponent, learner wait, finite pressure and late-step recovery guidance.
- [x] Real-victory completion, completion persistence and guided result state.
- [x] Deterministic ordinary-command full tutorial win, repeat match, cancellation and save/difficulty isolation regressions.
- [x] Final Mac Development rebuild and 21 of 21 Unreal tests.
- [x] Final rendered desktop and compact/mobile acceptance after the coach/feedback overlap fix and 14-case rerun.
- [x] Signed SDK 27 package and native iOS-on-Mac verification.
- [ ] Human full-tutorial playthrough.
- [ ] Physical tutorial acceptance on AEON at a safe installation time.

The end-to-end regression reached `winner=0`, step 14 and `complete=1` after 808.6 simulated seconds. That duration includes a deliberate 120-second idle wait. The run produced 9 units, lost 2, completed 1 upgrade, observed 4 opponent training completions and 6 opponent runtime orders, and ended with state hash `13225372430167087688`. A repeat match also passed. The driver used ordinary player commands, paid queues and normal combat without player debug grants or spawns. See [automation.json](../artifacts/guided-match/automation.json) and [source-snapshot.json](../artifacts/guided-match/source-snapshot.json).

## Render and device boundary

The final 14-case Mac sweep covers the first-run offer, skip state, lesson stages and victory at 2560 x 1440 desktop, 1024 x 768 iPad-like, 956 x 440 phone and 667 x 375 compact sizes. Every case fits the viewport with exact button bounds and no overlap. The compact coach/feedback overlap found in the first sweep is fixed. The added research-details phone and compact cases route `MORE` through the real HUD hit path, and the compact camera-feedback case keeps its status panel readable. Root inspection passed all 14 captures.

The sweep's protected-player-state record found no existing Mac save or preference file at the protected paths. Automated tests cover save and preference isolation; the render sweep does not prove preservation of pre-existing player data. Fast-forward victory previews can retain the start toast behind the result overlay because the preview advances simulation time without advancing the world timer by the same duration.

The SDK 27 iOS build passed 60 actions in 179.18 seconds, and packaging passed. The 493,176,589-byte app is a physical `IOS/arm64` package with minimum iOS 15 and executable SHA-256 `9b61b72e9c09abcd34effa487bac42dfe050f9c1e75c065686fb8da0c068cf2c`. Strict signing, the unexpired development profile, registered-phone coverage, Game Mode, and sustained-execution authorization in both signature and profile passed.

Four fresh runs used that exact packaged binary through the native iOS-on-Mac path at 2052 x 1536. Offer, camera, research-details and win states all passed exact button bounds, overlap checks and visual inspection with the actual touch instructions. No new crash report or ensure appeared, and all owned test processes closed. The native win recorded `winner=0`, step 14, `complete=1`, `preferenceCompleted=1` and 6 opponent orders. See [verification.json](../artifacts/guided-match/verification.json) and the [native iOS-on-Mac evidence](../artifacts/guided-match/native-ios-on-mac/win-verification.json).

The native protected paths were absent before and after the runs. This shows that the test did not create those files; it does not prove preservation of existing player data. The unchanged 75-input source snapshot ties the Mac and iOS evidence to the final source.

No human has played the full guided match end to end, and there is no physical touch proof for this revision. AEON still has navigation package `2bf6039a...`; it was not changed during this pass. Physical installation must wait for safe timing and must not interrupt active play.

The five-difficulty package `9e362e67...` remains separately verified in `artifacts/ai-difficulty`. Its evidence does not establish this later guided-match revision.
