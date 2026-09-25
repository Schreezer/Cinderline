# Completed orders and interrupted ore delivery

Updated 2026-09-14. Status: bounded local pass complete. Ten portable suites, ten sanitizer suites, 32 server tests and 37 Unreal checks pass. Full P0 and physical acceptance remain open.

Roadmap: [P0 command reliability](SC2_GAMEPLAY_ROADMAP.md#p0-establish-measurable-match-quality). The preceding [movement and stress baseline](GAMEPLAY_BASELINE_PASS.md) remains a separate recorded revision.

## Behavior

- Completing a direct attack stops the attacker at its current position. The completed target's location no longer becomes an idle movement destination. Cached navigation is cleared. If an earlier unit kills the shared target during the same combat pass, later attackers finish that order before any incidental enemy can inherit explicit pursuit. Ordinary Idle auto-acquisition remains available afterward.
- A Mend whose followed ally disappears uses the same completion path. Construction abandonment also uses it, preserving the existing mining-resumption behavior.
- A live target that becomes hidden during the vision update keeps its explicit identity until movement switches to attack-move toward its last known position. It is not replaced by a nearby enemy during that intervening combat update.
- Attack-move keeps its accepted destination after an incidental target dies. Idle combat keeps the unit's original formation anchor. Neither transition uses the completed-target path.
- When the last known ore is exhausted, a worker first delivers any carried ore. With no remaining cargo or deposit, it becomes idle at its current position and clears the obsolete mining target.
- If no operational Anchor or Siphon exists, a loaded worker retains its delivery job and cargo. No route search runs while there is nowhere to unload. Once a depot becomes operational, existing route budgeting and retry behavior apply. The roster and information view describe blocked mining or delivery.

The `finishOrder` helper clears the active target, route and movement destination. It leaves worker cargo intact. Movement arrival still preserves the assigned formation slot for recovery after displacement.

## Evidence and remaining acceptance

The initial natural direct-attack reproduction moved an Ember 142.5 world units in the second after its target died. The corrected focused run has zero displacement. [Before](../artifacts/order-completion/direct-attack-before.log), [after](../artifacts/order-completion/direct-attack-after.log).

The [pre-change manifest](../artifacts/order-completion/source-before.json) identifies the simulation archive used for failure comparisons.

| Regression | Evidence |
| --- | --- |
| Direct Attack completion, with corpse present or removed by tick-100 cleanup | Ordinary combat kills; stop at completion position; save/load continues identically |
| Shared explicit target killed earlier in the same combat pass | Later attacker finishes its explicit order, retains its new idle anchor and may subsequently use normal idle combat |
| Live explicit target crosses into fog between movement and combat | No incidental-target substitution; next movement uses the existing last-known-position rule |
| AttackMove, idle acquisition and Mend following | Accepted destination, original guard anchor and follow completion preserved across save/load |
| Final ore exhaustion after an earlier Move | Last three ore harvested and delivered normally, credited exactly once, then worker stops at delivery point |
| No operational depot with an unfinished replacement Anchor | Real cargo waits; ordinary combat removes the old depot; paid construction restores it; no second Gather command needed; waiting state survives save/load |

[Combat before](../artifacts/order-completion/combat-before.log): three pass, four expected failures. [Combat after](../artifacts/order-completion/combat-after.log): seven pass. [Worker before](../artifacts/order-completion/worker-before.log): two expected failures. [Worker after](../artifacts/order-completion/worker-after.log): two pass.

The worker fixture starts with a nearly destroyed original Anchor and a deposit holding three ore. It checks legal attacker spawn clearance, then uses real harvesting, combat, paid construction and movement rather than injecting terminal orders or restoring cargo.

## Engine pointer coverage

The added WorldTapSelection scenario creates a synthetic local player, viewport and production camera. It exercises the controller's coordinate entry points, including projection, drag selection, contextual movement and deselection. It does not inject the selected-unit list for this scenario. The synthetic viewport first needed a nonzero world/camera-cache time and a valid PlayerInput before local-player subsystem initialization. Once those were established, it exercised selection and order submission. The engine rounds input screen coordinates to whole pixels in `FSceneView::DeprojectScreenToWorld`, so the test reprojects the recorded command and checks the actual cursor pixel to within 0.05 pixels on each axis. The earlier fractional world-coordinate assertion was invalid. Production pointer behavior is unchanged. The final engine run passes, including the controller pointer marker. The earlier failed runs are retained as fixture diagnostics; they do not count as passing evidence. Physical mouse/touch event delivery remains a separate P0 device requirement. Deselect is exercised through the existing controller action; this does not establish a physical button tap.


Save version 10 and protocol 7 retain their existing fields. The authoritative worker must be rebuilt to receive the corrected behavior. This pass has not packaged or installed iOS, deployed a public server, or established physical touch, GPU, battery or thermal acceptance.

## Verification ledger

| Check | Result | Scope |
| --- | --- | --- |
| Portable CTest suites | 10/10 passed | [Log](../artifacts/order-completion/portable.log), [individual cases](../artifacts/order-completion/portable-detail.log) |
| Address/undefined-behavior sanitizers | 10/10 passed | RelWithDebInfo; [log](../artifacts/order-completion/sanitize.log), [individual cases](../artifacts/order-completion/sanitize-detail.log) |
| Server tests | 32/32 passed, zero skipped | [Log](../artifacts/order-completion/server.log); uses the freshly rebuilt authoritative worker |
| Mac Unreal build / local-all automation | Passed, 37/37 checks; zero warnings/errors/unfinished tests | [Build](../artifacts/order-completion/unreal-build-final.log), [validated run](../artifacts/order-completion/unreal-tests.log), [report](../artifacts/order-completion/unreal-automation.json); includes pointer-created mixed selection and blocked-delivery information |
| Current synthetic navigation workloads | Passed, all 160/200/400 units arrived | [Current JSON](../artifacts/order-completion/stress-current.json); deterministic repeats and all three state hashes match the prior movement baseline. Synthetic loads do not establish device capacity |
| iPhone build, install and sustained physical match | Pending | Installed .35 package predates this pass |

The [verification manifest](../artifacts/order-completion/verification.json) records the tested source and binary hashes, exact commands, counts and remaining work.

Independent production review found the same-combat-pass target substitution problem and checked its correction. The follow-up review found no remaining important issues in this bounded order-lifecycle change. Broader P0 acceptance remains open.
