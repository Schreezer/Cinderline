# Army command and HUD pass

Status on 2026-09-13: implementation and automated/rendered verification complete; physical acceptance pending. The player reported difficulty tracking the army, selecting and recalling a distant individual, organizing separate defense lines, and understanding the purpose and combat stats of selected objects. This pass addresses those gameplay tasks together with a substantial HUD rework.

## Intended player experience

- Open ARMY to see living combat troops by type and as individual entries with health and order status. Select or locate a specific troop without finding its tiny model on the map. Queued units remain separate from available troops.
- Assign the current combat selection to squad A, B or C. Each unit belongs to at most one squad. Recall a squad without selecting the other defenders. Groups belong to the current match, survive ordinary selection/pause/help, prune losses, and clear across new matches, loads and online transitions.
- Use MOVE to issue an unambiguous destination even near friendly structures or ore. A visible unit near a map edge must remain selectable when the pointer's ground intersection lies outside the map. Construction and destinations still respect map bounds.
- Use DEFEND to move a squad into legal formation slots and hold those positions. Defenders fight or heal within range, do not chase or follow another squad, and reclaim their anchor after traffic displacement. Another explicit order replaces defense.
- Selecting a troop or building shows identity, live health, useful combat stats and current work/order. The information button explains purpose, capabilities and relevant rules. Weapon damage is before target armor and matchup modifiers; healing is labeled separately.
- Show a selected tower or unit's weapon/healing radius in the world. The circle is bounded and does not reveal enemies or hidden map state. Weapon reach includes a target's radius when deciding an actual hit.
- Keep mobile battle controls in compact, contextual groups with a clear central battlefield. Use expandable army, squad, information and orders panels. Preserve queue paging, training guidance, menu-only safe area, online notices and pause/help behavior.

## Implementation boundaries

The controller owns selection, pointer interpretation and local squads. The simulation owns Defend behavior, including online execution. Defend is appended to existing enums so existing command/order numeric values remain stable. The online protocol is now version 3: version 2 clients/servers reject the mismatch before a match, because older clients cannot decode a returning own-unit Defend order. Save format 6 carries Defend; ordinary saves from versions 1–5 still load with their original enum limits. Unsupported versions and malformed new orders are rejected atomically.

The HUD uses a shared unit-information model based on actual definitions and upgrade rules. Selection and roster actions retain full unsigned entity IDs, including opaque online handles. Only current friendly entities supply roster and selected-object information. The information drawer separates STATS from a GUIDE explaining purpose, cost, production and unlock requirements, with a COMBAT page for full weapon/healing rules and matchup details. No hidden opponent upgrade data is inferred.

This work uses the existing game assets and Canvas renderer. Range overlays and roster scans remain bounded. Builds use at most two compile workers and one shader worker. Performance policy, Game Mode and sustained-execution settings remain in place.

## Work and evidence

- [x] Reproduce and fix individual/edge/air selection and explicit destination commands.
- [x] Add complete army roster, individual selection, type selection and three independent squads.
- [x] Add authoritative Defend movement and anchoring, with save/network coverage.
- [x] Add shared purpose/stats model, selected-object information and world range indicators.
- [x] Rework mobile and desktop HUD, including queue/tutorial/help/online coexistence.
- [x] Run focused portable, server and Unreal regressions.
- [x] Inspect real rendered phone, small-phone, tablet and desktop interaction states.
- [x] Build and verify the signed SDK 27 app, then inspect the exact binary on Mac's native iOS path.
- [ ] Human gameplay and physical AEON acceptance after a safe installation time is confirmed.

The previous guided-match package is `9b61b72e...`, verified in `artifacts/guided-match`. It has not been installed on AEON. The last recorded installed package is navigation revision `2bf6039a...`. The device session remains untouched during this pass unless the player confirms installation timing.

## Controls and regression scope

ARMY opens the roster. ALL selects all living combat units; a type tile selects that type across the map; an individual entry selects and focuses exactly that troop. Use the page arrows to reach later entries. Available troops and production queues stay separate.

To set up separate defense lines, select the units for the first line, open ARMY > SQUADS and assign them to A. Repeat for B and C. RECALL selects/focuses one squad; DEFEND followed by a ground point moves it there and anchors its formation. On desktop, 1/2/3 recalls squads and Control/Command + 1/2/3 assigns them. Assignment moves members out of another squad to keep orders independent.

MOVE is available to combat units and Drudges. It means move to that point even when the tap lies near ore or a friendly object. ATTACK arms attack-move; ORDERS includes Stop and Hold. The selected-object strip shows live health and useful combat stats; its `i` button opens the detailed information pages. A selected friendly unit/tower draws its weapon or healing radius, with 48 bounded segments clipped to the visible map.

The individual-selection regression came from checking the pointer's ground intersection before checking a visible projected model. Edge and elevated units now get their selection test first. Double-tap type selection also requires the same entity, so two quick taps on different troops do not unexpectedly select the whole type.

Portable tests cover deterministic Defend formation, arrival, no pursuit, Mend healing, order overrides, traffic displacement, legacy/current saves and network privacy. Unreal checks cover controller selection, opaque unsigned IDs, explicit destinations, exclusive squads, death pruning, pause/help preservation and actual new-match/load/online-transition resets. Rendered development fixtures use clearly separated debug spawning/resources to show representative HUD states; they are not claims of an ordinary human match or physical touch delivery. The existing guided tutorial's normal-rule victory test is also rerun.

Online protocol 3 requires the updated client and server to be deployed together. This pass runs the local server integration suite; it does not deploy a persistent public backend.

## Mac validation

The final Mac build passes with two compile workers. All five portable CTest targets, six real server integration tests and 23 Unreal tests pass. The final renderer pass covers 26 gameplay states across 956×440, 667×375, 1024×768 and 2560×1440, plus three first-run/tutorial regressions. Every recorded button is inside the viewport with no overlapping hit targets. Representative roster, squad, selection, guide, combat, range, worker, queue and tutorial screens were visually inspected. Player save and preference hashes were unchanged. An 80-file source/config snapshot ties the final build and subsequent iOS packaging together.

Evidence: [Unreal results](../artifacts/army-command/automation.json), [rendered phone roster](../artifacts/army-command/mac/army-phone.png), [defense group](../artifacts/army-command/mac/defend-phone.png), [small-screen combat information](../artifacts/army-command/mac/unit-combat-small.png), [source snapshot](../artifacts/army-command/source-snapshot.json).

## Signed iOS package and remaining acceptance

The 65-action SDK 27 build passed in 182.18 seconds. Xcode assembly, cooking, staging, packaging and strict signature verification passed. The prepared app is `Saved/Packages/IOS/Cinderline.app`, executable SHA-256 `025d6ce3026f43543a79d67daa53b30664a6bd11b4a8427e0406ff87612975ff`. Its signed native-iOS executable targets SDK 27.0 and retains Game Mode plus sustained-execution authorization in both signature and profile.

The exact executable passed four fresh native iOS-on-Mac launches at 2052×1536: roster, three squads, Ward information/range, and the 20-item queue. The running executable hash was checked as well as the package/wrapper hashes. All four captures were visually inspected; button geometry passes, no new crash reports or runtime assertions were found, and every owned test process was closed. Player save/preference hashes remained unchanged. The first capture harness attempt failed to recognize macOS's temporary executable path; the game itself rendered correctly. The harness now identifies its launch with a unique argument and verifies the running binary hash. See [recovery record](../artifacts/army-command/native-harness-recovery.json).

[Complete verification](../artifacts/army-command/verification.json) records the separate evidence boundaries. This build has **not been installed on AEON**, and no physical match was interrupted. The last installation recorded by this task remains `2bf6039a...`. Physical checks still include single-troop recall, splitting squads, repeated defense/override commands, layout and thumb reach in both landscape orientations, and a sustained normal match. No new physical thermal/GPU performance result is claimed.
