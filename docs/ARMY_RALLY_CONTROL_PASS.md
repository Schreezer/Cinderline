# Selection, rally and worker control pass

Status: implementation and local Mac verification are complete in the current working tree. The source is frozen for packaging.

## Selection and movement

Pointer intent is latched when a press begins. A stationary default tap that begins on a friendly actor selects that actor even if it moves before release, while a press that begins on terrain remains a terrain action if a friendly actor crosses the release point. This removes direction-dependent selection and movement mistakes. Resuming an unfinished construction site remains an explicit action.

MOVE followed by a ground point remains the deliberate way to position a selection in crowded friendly traffic. DESELECT clears the current selection and transient command modes through its own 44-unit navigation target. It preserves current orders and saved squad membership.

A normal ground tap with a building selected now deselects it. Changing a producer rally requires its explicit RALLY action; desktop secondary-click remains the direct rally shortcut. This prevents an accidental Anchor rally from suppressing a newly trained Drudge's automatic mining assignment.

## Shared army rally

The ARMY palette includes a RALLY tab. SET RALLY places the player's shared army rally, and FIND RALLY focuses it. The simulation stores this rally in player state and applies it to current and future completed combat producers. Anchors are excluded because they produce workers.

A combat producer can retain a building-specific rally override. USE DEFAULT removes that override and returns the producer to the shared army rally. Existing queues, paid production and explicit unit orders remain authoritative. On a pinned Anchor, JOBS > AUTO MINE clears its local worker-rally override without changing the shared army rally.

Required save version 8 stores the shared rally, producer overrides and deferred worker plan in a tagged tail. The legacy reader migrates earlier saves. Network protocol 5 carries the state and validates its private rally and worker-planning invariants.

## Autonomous workers

Each newly completed Drudge independently chooses reachable ore in explored territory and begins gathering when no stronger explicit behavior applies. The planner honors an explicit Anchor rally and validates the exact ore-deposit interaction disk plus reachable outward and return routes before assigning GATHER. Existing worker orders remain unchanged.

Fresh-worker planning performs at most 16 route checks per simulation step and persists its candidate list and cursor between steps. A completed paid worker item stays ready in its producer queue until the route work can finish, so planning pressure cannot consume the unit or leave it unassigned. This uses ordinary resources, movement and visibility rules.

## Verification

The [combined verification record](../artifacts/army-rally-control/verification.json) records the final successful Mac build, 33 passing Unreal tests, and 18 HUD render cases across eight phone, eight small and two desktop layouts. Four tutorial cases also pass their target checks, with the additional before-SHOW recovery frame reviewed. All 22 final captures passed automated layout checks and visual inspection. Player saves and preferences were preserved, and all owned Unreal processes were closed afterward.

The [native summary](../artifacts/army-rally-control/native/summary.json) records all six portable CTest suites passing. The full run predates only the final snapshot rally-invariant validator; its affected Network suite then passed 5/5 and Production Allocation passed 7/7 after rebuild. The Node server suite passed 7/7 before that validator-only change. The bounded [source review](../artifacts/army-rally-control/source-review.md) and [visual review](../artifacts/army-rally-control/visual-review.md) found no material defect.

The noninteractive `LOCAL RALLY` world caption can clip when its flag lies at the viewport edge. The flag and its actionable HUD controls remain visible and contained; caption positioning is a small cosmetic follow-up.

No iOS package was built or installed in this pass. Physical touch, device performance, sustained thermal behavior and an online protocol-5 server deployment remain unverified. The next release step is to package this working tree for AEON and pair it with the matching protocol-5 server.
