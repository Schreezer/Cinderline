# P1.2 patrol and escort

Updated 2026-09-14. Status: contract frozen; implementation in progress. Parent: [gameplay roadmap](SC2_GAMEPLAY_ROADMAP.md#p1-complete-the-tactical-command-model). P1.1 remains locally verified in its [pass report](TACTICAL_ORDERS_PASS.md); its saved evidence is historical once P1.2 source changes begin.

## Player behavior

Patrol repeats between the unit's position when the order is accepted and its assigned destination. A group receives distinct destination slots. Units engage nearby visible enemies within a bounded pursuit, return to the interrupted route, and continue patrolling. Terrain blockage preserves the route and uses existing recovery. A stationary or very short patrol must not reset navigation every tick.

Escort follows and guards an owned mobile unit. Group members receive stable relative slots. The chosen leader keeps its existing orders even when included in the original selection. Escorts can fight near their assigned guard position, but must return when an enemy would pull them away from the leader. Menders continue their existing nearby healing behavior. Workers can receive explicit Patrol/Escort; ordinary automatic training, mining and building behavior remains unchanged.

Choose PATROL or ESCORT in the existing Orders sheet, then choose a ground destination or friendly mobile target. Targeting is explicit and cancellable; ordinary friendly taps continue to select units. Global Build, Train, Research and Army controls stay available. Invalid targets explain the reason without altering selection or current authoritative orders.

## Persistent order rules

- Patrol and Escort are persistent replacing commands. They do not become finite queued Move/Attack-move steps. Issuing either clears the recipients' earlier future steps only after the complete command passes validation.
- Move/Attack-move steps may still be appended behind a persistent order, and wait while that order continues. Clear queued removes only those steps. Stop clears current and future work. Ordinary replacement retains the established P1.1 semantics.
- Patrol captures both endpoints at acceptance. Reaching one switches the active leg; incidental combat, visibility loss or a blocked route never skips an endpoint.
- A patrol's pursuit anchor is captured once when an engagement begins. Chasing must not repeatedly move that anchor. After the chase, Return completes the interrupted active endpoint with new acquisition suppressed, then switches legs. Merely returning within a small tolerance of the engagement anchor allowed repeated bait to ratchet the patrol away, so that policy was rejected during review.
- Escort's guard position follows the owned leader plus the escort's accepted relative slot. Movement and combat use the same leash policy. An escort that falls behind or loses an incidental combat target resumes following its leader.
- Selecting a leader as an escort recipient leaves that leader unchanged. A selection containing only the leader is rejected. Reject direct or indirect escort cycles for the whole command before mutation.
- If the leader dies or otherwise becomes unavailable, an explicit queued Move/Attack-move successor takes priority. With no successor, defend the last assigned follow position. Never choose another leader or resume mining silently.
- Any replacement, death or team elimination clears incompatible persistent state. Worker construction cleanup and carried ore obey the existing replacement/completion contract.

## Frozen shared contract

Append `Order::Patrol = 8`, `Order::Escort = 9`, `CommandType::Patrol = 19` and `CommandType::Escort = 20`, preserving all old numeric values. Both commands require Replace and queueIndex zero. Patrol uses point and requires target zero. Escort uses target and requires point `{0,0}`. The authority normalizes all recipients before committing; rejected commands cannot change orders, worker links, recording or opaque identity.

```cpp
enum class SustainedOrderPhase : int { Travel, Pursuit, Return };
struct SustainedOrderState {
    Vec2 patrolOrigin{}, patrolDestination{};
    bool patrolTowardDestination = false;
    Id escortTarget = 0;
    Vec2 escortOffset{};
    Id pursuitTarget = 0;
    Vec2 pursuitAnchor{};
    SustainedOrderPhase phase = SustainedOrderPhase::Travel;
};
// Entity::sustained; TacticalOrder remains unchanged.
```

Use `Simulation::SustainedPursuitRadius = 600.0f`, `SustainedReturnTolerance = 24.0f` for Escort, `EscortSpacing = 96.0f` and `MaxEscortOffset = 2048.0f`. Offsets have finite Euclidean length no greater than that cap, are stable in world axes, and avoid the leader's own footprint. Normal supported armies are below the command's 500-recipient bound. At world edges, reproject effective follow positions deterministically so clamping does not collapse slots into the leader or each other; retain nominal slot identity. Escort slots use ordinary collision-aware movement; a blocked slot must report and retry rather than discard the escort. A moving follow goal must invalidate an exhausted same-geometry route at a bounded cadence.

Patrol's `goal` equals its immutable destination or origin according to `patrolTowardDestination`. Escort's `goal` is the latest valid bounded follow position around its target and is retained for the target-loss fallback. Keep `Entity::target` and `supportTarget` zero during both sustained orders; their incidental enemy lives only in `sustained.pursuitTarget`. Do not duplicate the combat damage/effects routine: gate its target selection through the same sustained-order policy as movement.

The leash center is the patrol's fixed acquisition position or the escort's current assigned follow position. Both the pursuing unit and candidate enemy must remain within 600 units of that center, in addition to existing local vision, hostility and weapon eligibility. Stop pursuing as soon as visibility/identity/leash validity is lost. Patrol Return suppresses enemy acquisition and attacks until it physically completes the interrupted active endpoint, then flips the leg and clears the anchor. Escort Return suppresses acquisition and attacks until within 24 units of the current effective slot. Escort enters pursuit only after approaching its assigned slot; a moving leader cannot leave an escort fighting indefinitely behind it. Navigation may detour during Return; it is not a new chase. Menders heal nearby as before and do not acquire hostile pursuit targets.

Allow acyclic escort chains. Reject a projected edge when walking from the chosen target would reach any recipient or repeat a node. Exclude a selected leader before slot assignment; the leader's own command, future list, cargo and construction links stay unchanged. Slot assignment must reserve existing followers of the same leader, including followers assigned by earlier separate commands. A new assignment must not reuse their nominal slot or create overlapping effective goals. Reassigning a follower releases its old reservation only in the projected transaction; failure leaves every live reservation unchanged.

Keep the pure edge projection in shared `Sim/SustainedOrderRules.h` so simulation and network validation use one formula. Nominal offsets remain immutable, while effective goals near edges must retain at least combined unit-radius separation. Escorted workers must retain normal delivery/work access; followers yield to their own leader rather than pinning it against a work area.

Source changes belong in the shared simulation, with sustained movement/validation helpers in `SimulationTactics.cpp` where practical. `net::sustainedPlanFitsSnapshot(const Simulation&, int, const std::vector<Entity>&)` receives complete prevalidated recipient candidates and checks projected owned snapshot size without mutating authority or view handles. Server admission also checks copied authority and copied active views for Append, Patrol and Escort before committing to its live state.

## Save 12 and protocol 9

Keep the command wire shape unchanged and enforce the new type semantics. After an entity's existing supportTarget, encode a conditional sustained payload before futureCount: Patrol stores origin, destination, direction (u8), pursuitTarget, pursuitAnchor, phase (u8); Escort stores escortTarget, escortOffset, pursuitTarget, phase (u8). Other orders have no payload. Escort pursuitAnchor and patrol-only fields stay zero. Opponent/resource entities are sanitized to empty sustained state before encoding.

Append final save section `SUSTAINED_ORDERS 1` after the existing `ORDER_QUEUES 1`: exact entity count, then one row per entity in saved-ID order containing `id origin.x origin.y destination.x destination.y towardDestination escortTarget offset.x offset.y pursuitTarget anchor.x anchor.y phase`. Include every field in stateHash. Recorded command rows retain the version-11 layout. Versions 1–11 migrate empty sustained state; emit only save 12. Old protocol-8 clients fail admission clearly.

For all inactive, dead, building or resource entities require empty sustained state. Patrol requires zero escort fields and goal matching its selected endpoint. Escort requires zero patrol/anchor fields, a live owned mobile nonself target and a bounded offset. Travel has no pursuit reference/anchor; Pursuit requires a valid live hostile eligible reference; Return has no pursuit reference. Validate phases, exact rows, flags, graph acyclicity and coordinate ranges independently in save and snapshot readers. Cleanup on enemy/leader death must keep snapshots valid even before periodic corpse removal. Fog filtering must not leak a hidden pursuit reference: defensively project an unrepresentable Pursuit as Return with target zero, retaining the owned patrol anchor or escort slot. Never include a hidden enemy to make the reference valid.

Owned snapshots may expose their patrol endpoints, escort target/slot and pursuit phase. Enemy snapshots must expose none of that private intent. Escort references use the existing opaque identity mapping. Validate finite world coordinates, legal modes, empty inactive state, live owned mobile references and acyclic escort graphs. Corrupt saves/snapshots reject atomically. Keep the existing frame-size and future-order limits.

## Verification gates

- [ ] Multiple complete patrol laps with unchanged accepted endpoints, different speeds and stable group slots.
- [ ] Visible enemy engagement, fog loss and bounded pursuit; forced return cannot immediately reacquire and drift.
- [ ] Blocked endpoints and reopened geometry recover without discarding the route; zero-length patrol does not churn navigation.
- [ ] Escort follows moving ground/air leaders, preserves the selected leader's orders, and uses stable distinct slots.
- [ ] Escort combat stays within its guard policy, then catches up; Mender healing and worker cargo/construction remain correct.
- [ ] Self/foreign/building/resource/dead targets and direct/indirect cycles reject atomically.
- [ ] Leader death activates an explicit future step or defends the last follow position; Stop/replace/clear and elimination clean up correctly.
- [ ] Save/load, current and legacy recordings, per-tick replay, malformed state and deterministic repeats.
- [ ] Command codec, owned snapshots, opaque references, hostile enemy-plan rejection and size bounds.
- [ ] Actual two/four-player Unreal loopback commands, acceptance/rejection and reconnect snapshots.
- [ ] Injected mouse/touch controller paths, cancellations, friendly selection and unchanged P1.1 queue behavior.
- [ ] Desktop/compact rendered controls, active order, patrol endpoints and escort target/leash feedback.
- [ ] Appropriate Release, sanitizer, server and Unreal regression suites and measured route/command workload checks.
- [ ] Matching-package real mouse/iPhone touch playtest, full match and sustained device acceptance.

This pass delivers repeating two-endpoint patrol and owned-unit escort. Editable multi-stop patrol loops and formation facing/spacing are separate enhancements; P1.3 remains open. The overall P0–P7 capability goal remains unchanged.
