# Global commands and automatic allocation

Authorized 2026-09-13. This milestone removes routine building and worker selection from economy commands while preserving explicit control when the player wants it.

## Intended behavior

- BUILD, TRAIN, RESEARCH and ARMY remain available as persistent top-level controls during play.
- Global BUILD enters the existing placement flow. After a legal site is chosen, the simulation assigns a reachable idle Drudge first, then a reachable mining Drudge. The allocator must not take a worker that is constructing, carrying out an explicit command or unable to reach a valid work slot.
- Global TRAIN accepts single or batched orders and distributes them deterministically across eligible operational producers. Allocation should balance projected work instead of filling the first building. Paid cost, reserved crew, prerequisites and the existing per-queue limit remain authoritative.
- Global RESEARCH selects an eligible operational Resonator automatically, rejects duplicate or unavailable upgrades clearly and retains the normal research cost and completion rules.
- A global jobs view shows construction, training and research assignments with their producer or worker, queued/running state and progress. Cancellation uses the authoritative existing refund and queue behavior.
- Global rally applies one destination to all currently completed producers, or to a chosen producer type. Selecting and pinning one building keeps the deliberate per-building queue, cancellation and rally override.
- ARMY continues to open the combat roster, ALL selection and three squads without depending on another selection.

## Functional checklist

- [x] Persistent BUILD, TRAIN, RESEARCH and ARMY controls fit the implemented desktop and compact layouts without requiring preparatory selection.
- [x] Global commands remain usable with no unit or building selected and preserve the player's current selection.
- [x] Construction chooses a reachable idle Drudge before a mining Drudge, uses the real construction work slot and reports a clear failure when no worker can reach it.
- [x] Explicit worker/site recovery remains available for paused or abandoned construction.
- [x] Batched production is load balanced across every eligible completed producer with deterministic tie breaking.
- [x] A selected producer provides an explicit local-order override and its own queue/rally controls.
- [x] Global research chooses an eligible Resonator deterministically and prevents duplicate research.
- [x] The jobs view reports queued and active work, progress, producer or worker assignment, construction state and cancellation results from authoritative simulation state.
- [x] Save/load preserves global jobs, per-building queues and rally points. Rematch deliberately clears interaction state and the completed match's queues.
- [x] Multiplayer commands remain authoritative and deterministic and expose no hidden enemy state. Protocol 4 requires client and server to update together.
- [x] Existing construction refunds, paid queues, crew reservation, prerequisites and queue limits remain intact.

## Verification checklist

- [x] Portable simulation tests cover reachable-worker priority, mining fallback, unreachable sites, concurrent builds, deterministic batch distribution, full/disabled producers, research allocation, cancellation and save/network round trips.
- [x] Unreal tests cover persistent controls, selection independence, local overrides, jobs/progress/cancellation and tutorial routing.
- [x] Desktop, phone, small-phone and tablet captures are visually inspected for labels, touch bounds, drawers, progress and failure feedback. All 17 fresh cases pass, including the construction worker-identity row.
- [x] A comparable bounded Mac performance sample is recorded for the persistent UI and job polling.
- [x] SDK 27 IOS/arm64 build, assembly, package, signature/profile, IoStore and exact-binary native iOS-on-Mac checks pass.
- [ ] Physical AEON touch and sustained thermal acceptance remains a separate final check.

Source implementation and desktop verification are complete. The final Mac build passes, as do all 27 Unreal tests, all six portable CTest executables, seven Node server integration tests and the local Unreal transport test with no warnings or errors. Protocol 4 has only been exercised against the matching local server; no public deployment is claimed. Evidence is under `artifacts/command-allocation`, with transient output under `Saved/CommandAllocation`.

The short 956 x 440 Mac renderer sample recorded 120 GPU frames at mean 10.780 ms, median 10.413 ms and p95 13.228 ms. The matched canyon baseline was mean 9.706 ms, median 9.321 ms and p95 14.997 ms. MetalFX spatial upscaling stayed at 80% with zero fallback frames. This is a bounded Mac sample, not physical-phone performance, sustained load or heat proof.

The final SDK 27 IOS/arm64 build, Xcode assembly and packaging pass. The strictly verified 494,781,655-byte app at `Saved/Packages/IOS/Cinderline.app` contains executable SHA-256 `bb634a6de18cda60d75c7689e9beacbb3409224c7951a62544e48644d285a395`; its development profile is current and covers the registered phone, and its IoStore containers and native IOS platform are verified.

Five sequential exact-executable iOS-on-Mac cases pass for construction, training, building, research and jobs at 2052 x 1536. They use `ES3_1`, `METAL_ES3_1_IOS` and Mobile HDR, keep all required controls within the viewport without button overlap, report no new crashes and leave the protected player-state paths unchanged. These are native Mac executions of the signed iOS binary. The new package has not been installed or launched on AEON; physical touch, sustained gameplay and thermal acceptance remain pending.

## Tutorial and guide alignment

The tutorial source now teaches global BUILD for Kiln, Siphon and Resonator placement; global TRAIN for the sixth Drudge, batched Embers and scout; and global RESEARCH for weapons. Its completion gates accept both local and automatic commands, then wait for the real unit, building, upgrade or victory outcome. Queue guidance reads aggregate automatic-job status rather than inspecting only the first completed producer.

The primary tutorial action opens BUILD, TRAIN or RESEARCH when the current objective needs a global catalog. A placed construction foundation switches the action back to world focus, as does a trained scout. The reinforcement lesson deliberately remains local: select one Kiln and set that building's rally, then use global TRAIN for the batch. The guided test driver exercises automatic building, batched production, research and a rally pinned to the selected Kiln.

The field guide now explains reachable automatic worker assignment, load-balanced production, global research, JOBS progress and cancellation, and selected-building queue/rally overrides. `CinderUnitInfo` contains producer and capability facts rather than control instructions, so it needed no selection-copy change. Controller/HUD integration maps tutorial actions to `OPEN BUILD`, `OPEN TRAIN` and `OPEN RESEARCH` and the corresponding global catalogs; the updated onboarding test confirms that opening BUILD preserves the existing selection and issues no simulation command.
