# Scouting-driven opponent

This is the working checklist for the next mechanics checkpoint. The broader roadmap remains in [TASKBOARD.md](TASKBOARD.md).

## Already done

- [x] First faction, three maps and the full offline skirmish loop.
- [x] Physical ore harvesting, paid production/research, fog, unit counters, healing, basic AI defense/retreat and expansion.
- [x] Worker-driven construction, interruption/resume, mining return, save migration and AI builder reservation.
- [x] Previous checkpoint verified with 19 simulation groups, native checks, sanitizers and two Unreal integration tests. Evidence: [CONSTRUCTION_PASS.md](CONSTRUCTION_PASS.md).

## This checkpoint

Goal: make the opponent choose where to scout, attack and invest using information it has actually observed. Expanding away from the starting base or changing army composition should change the opponent's decisions.

- [x] Implement enemy structure and recent army memory from fog-limited observations.
- [x] Implement last-known structure retention, visible empty-site confirmation and mobile observation expiry.
- [x] Implement scouting of public landmarks by observation age and replace the fixed-start attack fallback.
- [x] Implement remembered objectives alongside existing defense, retreat, Mender follow and builder assignments.
- [x] Implement paid production responses to observed army composition and a fallback for missing/stale information.
- [x] Implement deterministic knowledge persistence and hashing in version 3, retaining version 1 and 2 readers.
- [x] Add regressions for hidden-information isolation, memory aging/invalidation, relocated bases, scouting, adaptive production, save/load and existing mechanics.
- [x] Review the final diff; run portable/native tests, sanitizers, fresh Unreal integration checks and normal-command match trials.
- [x] Rebuild and reopen Unreal; record the exact tested source, results and remaining playtest limits here.

## Acceptance checks

- Unseen enemy units/buildings cannot change strategy. Observed units can change it.
- A remembered base can guide an attack through fog, but a base confirmed gone stops being an attack objective.
- A relocated or additional headquarters can be found and targeted after scouting.
- Scouting and support orders remain coordinated with the army. Construction workers keep their jobs.
- Every purchase and order still uses the ordinary public command rules, costs and prerequisites.
- Saved matches continue the same decisions after reloading, without leaking current hidden enemy state.

## After this checkpoint

- [ ] Hands-on construction and full-skirmish feedback from the current Unreal build.
- [ ] Combat sound events, readable weapon/impact/destruction effects, roster inspection and animation.
- [ ] Balance using human matches and a broader sample of scripted trials. The 20–30 minute target remains unproven.
- [ ] iOS packaging and physical-device testing after mechanics, presentation and playtesting.

## Progress

- 2026-09-11: Source assessment found fixed-start attack fallback and production based on own-unit ratios. Selected scouting memory, objective selection and observed-composition responses as the next bounded mechanics pass.
- Implemented the first source pass. Mobile memory lasts 90 seconds; production responses use sightings from the last 60 seconds. Building reports persist until disproved through vision.
- Review caught a witnessed-death timing gap around corpse cleanup. A visible death now removes its report immediately without revealing hidden deaths.
- Final portable verification passed all 23 groups and all 3 CTests, including 3,000 native render frames. A fresh ASan/UBSan run passed all 23 groups. All 3 Unreal integration paths passed with zero warnings/errors in run `20260911T145449Z-61548`.
- The witnessed-death regression initially killed its unit outside the intended cleanup interval. Its corrected ordinary-combat fixture records death at tick 96, after the prior AI decision and before tick 100 cleanup, and verifies immediate removal and subsequent absence. No mechanics change was needed for that fixture correction.
- Final frozen-source match trials ended naturally in 656.35, 788.15 and 843.00 seconds across the three maps, seed 42. All were won by the game AI using ordinary commands and resources. These are below the pacing target and are not human balance evidence.
- The rebuilt Unreal game was relaunched and its menu visually confirmed through CUA. It is left open for playtesting. No player save was overwritten by the automated fixtures.

## Checkpoint report

CHECKPOINT: Scouting-driven opponent mechanics

STATUS: Complete for the bounded implementation and automated verification goal. Human playtesting, presentation and iOS tasks above remain open.

WORKING: Observed enemy memory, independent scouting, remembered objectives, counter production and continued saved-game decisions.

IMPLEMENTED: Fog-limited sightings, 90-second mobile memory, 60-second production reports, visible destruction/empty-site invalidation, observed-cell ages, shared queue reservations and version 3 persistence with version 1/2 compatibility.

TESTED: 23/23 portable groups; 3/3 CTests including 3,000 native render frames, 18.15 seconds total; 23/23 groups under ASan/UBSan, 30.03 seconds; Unreal Development Editor build; 3/3 strict Unreal integration paths; three natural match trials. A separate final source review found no remaining important issue.

ISSUES FOUND: Witnessed mobile deaths could be missed between strategic updates and corpse cleanup. The initial regression fixture also missed its intended timing window.

FIXES MADE: Clear a witnessed death at the damage event. Verify the regression at actual death tick 96. Hidden deaths remain unknown.

CURRENT PLAYABLE EXPERIENCE: A rebuilt, open Unreal skirmish with the new opponent logic. Current-build full human matches, mouse/touch acceptance, late-game rendering and mobile performance are not established by these automated checks.

NEXT CHECKPOINT: Hands-on skirmish feedback, followed by combat sound events and readable battle effects/animation. Keep iOS deferred.

Evidence: [portable output](../artifacts/ai-strategy-portable-details.txt), [CTest](../artifacts/ai-strategy-ctest-results.txt), [sanitizers](../artifacts/ai-strategy-sanitizer-details.txt), [Unreal integration](../artifacts/unreal-ai-strategy-integration-results.json), [match trials](../artifacts/ai-strategy-match-results.txt), [tested source hashes](../artifacts/ai-strategy-tested-source-sha256.txt), and [verification metadata](../artifacts/ai-strategy-verification.json).
