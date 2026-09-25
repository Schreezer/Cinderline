# P1.3 formation facing and spacing

Updated 2026-09-15. Status: **implementation and local feature checks complete; physical acceptance open**. Evidence: [FORMATION_ORDERS_PASS.md](FORMATION_ORDERS_PASS.md). Work is stopped at the player's P1 boundary.

## Player behavior

A player can choose Tight, Standard or Wide spacing for the next Move, Attack-move or Defend command. The existing Orders sheet exposes one `SPACE` control that cycles those presets. The setting is a local, unsaved controller preference and applies to later eligible commands until changed. Accepted per-unit destinations preserve the chosen layout; changing the preference does not rewrite an order already accepted by the simulation.

Normal destination taps retain today's automatic travel-facing behavior. `FACE NEXT` explicitly arms facing for the current Move, Attack-move or Defend destination mode. Pressing the intended formation center, dragging toward the desired facing direction and releasing submits one command containing both the destination and facing. A drag below the input threshold issues no command, retains the armed targeting mode and explains that a longer drag is required. This bounded pass has no second tap-tap facing mode.

Explicit facing creates a rotated formation with roughly twice as much frontage as depth. Units face the chosen direction when they complete a Move or Attack-move waypoint. That arrival orientation is not a permanent fire lock: later movement, combat or displacement may turn an idle unit. Defend is persistent, so a defender that finishes an engagement and returns to its assigned anchor faces the chosen direction again when it is no longer actively firing.

An ordinary unqueued Move replacement is the withdrawal control. It immediately replaces current combat work, clears the earlier future route and uses the selected spacing at the rear destination. Move already suppresses automatic acquisition and attacks while travelling. P1.3 does not add a Retreat order, rigid marching block or group speed lock.

Patrol and Escort keep the P1.2 contract. Facing and spacing modifiers do not change their endpoints, pursuit policy, escort slots or leader rules. Existing squads remain the selection mechanism; a squad `TACTICS` action opens the same Orders sheet instead of creating another formation or selection system.

## Command contract

Append the following types and members without changing existing enum values or the order of existing aggregate-initialized fields:

```cpp
enum class FormationSpacing : int { Tight = 0, Standard = 1, Wide = 2 };

struct TacticalOrder {
    Order order = Order::Move;
    Vec2 point{};
    Id supportTarget = 0;
    bool hasArrivalFacing = false;
    float arrivalFacing = 0;
};

struct Command {
    // Existing members remain in their current order.
    FormationSpacing spacing = FormationSpacing::Standard;
    bool hasArrivalFacing = false;
    float arrivalFacing = 0;
};
```

`Entity` appends `bool hasArrivalFacing = false; float arrivalFacing = 0.0f;` for its current tactical order. These fields and the additions to `TacticalOrder` are appended so existing aggregate call sites continue to receive Standard spacing with automatic facing.

The nominal center pitch is 48 world units for Tight, 64 for Standard and 96 for Wide. Unit footprint clearance, world bounds, terrain and already reserved destinations take priority over nominal pitch. The normalizer may expand or relocate a slot to keep it legal. Tight therefore does not promise a smaller accepted footprint than Standard for every heavy-unit selection.

Without explicit facing, all presets use the legacy axis-aligned square and ID ordering, changing only nominal pitch. Standard without facing must preserve the legacy recipient ordering and fallback, except that an impossible layout now rejects and active Hold footprints remain reserved. Explicit facing uses this exact nominal layout, centered on the press point:

- `columns = min(N, ceil(sqrt(2 * N)))`, with `rows = ceil(N / columns)`.
- Row `r` contains `count[r] = min(columns, N - r * columns)` units. Its weighted center is `centerRow = sum(r * count[r]) / N`.
- Within that row, `lateral = (column - (count[r] - 1) / 2) * pitch` and `depth = (centerRow - r) * pitch`.
- With `forward = (cos(angle), sin(angle))` and `right = (-sin(angle), cos(angle))`, the nominal point is `pressPoint + right * lateral + forward * depth`. The weighted centroid is the press point; row zero is the front.
- Sort recipients by ascending current lateral projection, then descending depth projection (front before rear), then ascending entity ID before assigning explicit-facing slots. Safety fallback may alter the final geometry to preserve legal footprints.

The facing direction is the normalized world-space vector from the press point to the release point. Store its canonical angle in radians in `[-pi, pi)`. Short, nonfinite or out-of-world drags do not submit a command.

Spacing and arrival facing are legal only on Move, Attack-move and Defend. Append remains legal only on Move and Attack-move. Patrol, Escort, Attack, Gather, Hold, Stop, production, rally and global automatic commands require Standard spacing, `hasArrivalFacing == false` and `arrivalFacing == 0`. Reject an invalid enum, nonfinite or out-of-range angle, a false flag with a nonzero angle, and every ineligible combination before changing state or recording the command. Normalize a submitted negative-zero angle to positive zero before recording or mutation. Stored current/future state and decoded snapshots require canonical positive zero whenever the angle is zero; reject negative-zero persisted or snapshot fields. This avoids distinct hashes for the same default intent.

Normalize the complete recipient set before mutation. Preserve current ownership and eligibility rules and filter buildings as today. Generate distinct accepted destinations, validate each unit's footprint and reject the whole command when any recipient has no legal slot. Reserved destinations mean slots already assigned in this normalization plus the projected Hold footprints described below; unrelated moving units' future destinations are not global reservations. Retain the existing bounded fallback search envelope and reject if it exhausts without a complete layout. A fallback may adapt around an edge or obstacle, but it must not silently overlap recipients, cross static terrain or discard the requested command.

Reserve the current footprint of every live same-layer Hold whose projected current order remains Hold. This includes unselected Holds and selected Holds under Append; only a selected Hold receiving a replacing command releases that reservation. In mixed Idle-plus-Hold Append, Idle recipients start immediately and must avoid those selected Hold footprints. Hold recipients retain their current order and defer the appended step indefinitely under the existing persistent-order contract. Ignore a Hold's own footprint only when validating its own deferred point. Use recipient radius + Hold radius + the existing 10-unit formation safety margin. A ground Hold does not block an air slot, or vice versa. No formation may push or retask an unselected Hold. Workers remain eligible and must retain blocked intent rather than silently lose their order.

Spacing is not persistent formation metadata after acceptance: the accepted points are authoritative. Current and future arrival-facing fields are persistent because Move/Attack-move completion and Defend return need them. Installing a tactical step copies its facing fields to the entity. Completing a step applies its arrival facing only when no successor activates immediately, then clears current arrival-intent fields on the resulting Idle order; activating a successor installs the successor's own facing. Persistent Defend retains its arrival intent. Replacement, Stop, death, elimination, Patrol and Escort clear incompatible current facing state. Clear queued removes only future steps and their facing fields.

## Input and acknowledgement lifecycle

`SPACE` changes the local preset only when neither a facing pointer nor an online acknowledgement is pending. Latch the preset at valid facing pointer-down so the preview and submitted command agree. `FACE NEXT` is available only with Move, Attack or Defend armed. While it is armed, the first pointer contact on playable ground latches the destination center and the drag controls facing. This dedicated gesture takes priority over pan, long-press selection and box selection until release. When it is not armed, existing tap, drag, Option pan, touch pan, long press, contextual command, Patrol, Escort and selection behavior remains unchanged.

Desktop Shift is sampled when the facing drag releases. It appends only Move or Attack-move. Touch Queue next can carry the same spacing and facing fields on its one appended destination. Defend always replaces. A second touch, Escape, selection or destination-mode change, pause/background, match transition and explicit cancellation clear the entire unsent facing gesture: latched pointer, preview/capture, facing arm and destination/Queue-next intent under the existing cancellation rules. Its later release must not fall through to an ordinary terrain command. A short or invalid release releases pointer capture and preview, sends nothing and retains the workflow for retry.

Online facing commands use the existing exact-sequence pending-intent lifecycle. Lock sampled queue mode, spacing, angle, center, selection and generation together. After send, cancelling visible targeting must retain internal sequence correlation until the exact ACK is consumed; a stale ACK cannot change a newer gesture. Store or lock the spacing and facing values associated with the pending command so later local UI changes cannot make an acknowledgement consume a different intent. Matching acceptance consumes the one-shot destination and facing arm. Matching rejection retains the same mode, spacing and facing workflow so the player can correct the placement. An uncertain reconnect clears local targeting; the refreshed authoritative route must be checked before retrying because cancellation cannot recall a command already sent.

## Movement and combat semantics

- Move and Attack-move retain their accepted per-unit points throughout travel and recovery. Different unit speeds may separate temporarily; every reachable unit must still make progress and reach its own slot. This pass does not slow the group to its slowest member or preserve a rigid shape in transit.
- Move performs the existing noncombat withdrawal. Units do not acquire or fire while it remains current. Local avoidance and route recovery must not turn the withdrawal into Attack-move or silently skip the destination.
- Attack-move may face incidental enemies during combat. Losing the target resumes the saved waypoint. Its requested arrival facing applies only after actual waypoint completion.
- Defend may turn to engage a legal target. After the engagement it returns to the saved anchor and restores the requested facing while idle at that anchor. Facing restoration must not suppress a legal shot or reset navigation every tick.
- An unselected Hold unit remains stationary. Formation movement routes around it under the existing collision and recovery rules. Failure to find a legal route reports blocked state and retains the accepted destination.
- Automatic mining, construction, production exits, rallies, Mender support and P1.2 sustained pursuit remain unchanged unless the player explicitly replaces that unit's current order.

Defend re-facing occurs only after target refresh/acquisition has left no legal combat target and the defender is within the anchor arrival tolerance. A retained legal target keeps combat-facing during cooldown/reload ticks. A shot or target decision wins on the same tick; restoration must not reset navigation, cooldown or order state.

## Save 13 and protocol 10

Emit save version 13. Preserve every save-12 section and row unchanged, including final `ORDER_QUEUES 1` followed by `SUSTAINED_ORDERS 1`, then append:

```text
FORMATION_ORDERS 1
entityCount
entityId hasArrivalFacing arrivalFacing futureCount [hasArrivalFacing arrivalFacing]...
```

Write exactly one row per entity in saved-ID order. `futureCount` must equal that entity's count in `ORDER_QUEUES 1`. Versions 1–12 migrate with no current or future arrival-facing intent while preserving the already-saved rendered `Entity::facing`. Emit only save 13. Validate the section tag/version, exact entity sequence and count, boolean flags, finite canonical angles, future-count agreement, legal current/future order association and no trailing data. Reject malformed files atomically.

Save-13 recorded command rows use this exact order:

```text
tick type team pointX pointY target kind queueIndex queueMode spacing hasFacing facing unitCount [unitId...]
```

Versions through 12 retain their existing recording layouts and migrate `Standard`, `false`, `0`. Include command spacing/facing and every current/future arrival-facing field in deterministic recording comparisons and state hashes.

Advance the network protocol to 10. Append spacing (`u8`), has-facing (`u8`, strictly 0/1) and facing (`f32`) to the command packet after the existing queue-mode field, and preserve them through opaque-ID translation and worker preflight. Each entity row appends the current pair (`u8 hasArrivalFacing`, `f32 arrivalFacing`) immediately after `u32 supportTarget`, before the order-dependent sustained payload. Each future step appends its pair after that step's existing `u32 supportTarget`, making a step 18 bytes. Owned snapshots retain these exact fields. Opponent rows encode canonical `false,+0.0f` for the fixed current pair and retain an empty future list, while keeping the already-public rendered `Entity::facing`. Reject hostile true flags or noncanonical/private angles. Decoders independently reject invalid modifiers, illegal order associations, hostile private formation intent and oversized or trailing payloads. Client, LAN advertisement, server admission and worker constants move together; protocol-9 clients fail admission clearly.

Projected snapshot-size validation must use the complete normalized recipient state before authority mutation. A mixed Append preflight applies immediate Idle installs and deferred non-Idle tails to a copied Simulation plus copied ViewMemory for every active seat; rejection must preserve live opaque identities as well as authoritative state and recording. Keep the existing command recipient, future-order and frame-size bounds. A spacing or facing command that would exceed the snapshot budget rejects atomically without changing destinations, construction links, recording or opaque view identity.

## Implementation touch points

| Area | Required work |
| --- | --- |
| Shared types | Extend `Source/Cinderline/Public/Sim/Simulation.h` with the enum and appended command, current-order and tactical-step fields. |
| Authority | Update command validation, deterministic formation normalization, tactical install/activation/completion, replacement cleanup, Defend re-facing, hashing and save/load in `Source/Cinderline/Private/Sim/Simulation.cpp`. Keep P1.2 sustained helpers separate. |
| Movement | Preserve held footprints, arrival orientation and mixed-speed recovery in `Source/Cinderline/Private/Sim/SimulationMovement.cpp`; do not add persistent group-speed state. |
| Network | Update command/snapshot encoding, validation, privacy, opaque translation and projected-size checks in `Source/Cinderline/Public/Sim/Network.h` and `Source/Cinderline/Private/Sim/Network.cpp`. |
| Server and LAN | Advance protocol constants, parsers, admission fixtures and worker translation together. Preserve global automatic commands and exact sequence acknowledgement behavior. |
| Controller | Extend `CinderPlayerController.h/.cpp` with the local spacing preset, one-shot facing arm, world-space drag reducer, command modifiers, cancellation and pending-intent identity. Route squad `TACTICS` to the existing Orders sheet. |
| HUD and help | Add `SPACE` and `FACE NEXT`, the facing arrow and projected slot preview in `CinderHUD.cpp`; update `CinderHelpContent.cpp` and `CinderMobileHUDPreview.cpp`. Keep compact controls, marker hit tests and global catalogs usable. |
| Determinism adapters | Update all command equality and recording hashes, including profiling and workload tools, so evidence cannot omit spacing or facing. |

The drag overlay uses a shared lightweight nominal-slot helper and is labeled `FORMATION GUIDE`. It does not run full obstacle, edge or Hold normalization every pointer frame. Once accepted, markers use authoritative per-unit destinations; safety fallback can differ from the nominal guide.

## Acceptance checklist

Checked items refer to the recorded local fixtures and reports, not all possible matches or physical-device acceptance.

- [x] Tight, Standard and Wide commands produce deterministic, distinct, nonoverlapping accepted points for mixed-radius groups. Nominal pitch differences are demonstrated without claiming that footprint expansion always preserves width ordering.
- [x] Explicit formations at 0, 45, 90 and 180 degrees produce the intended rotated 2:1 layout, centered incomplete rows and correct per-unit arrival facing.
- [x] Standard commands without `FACE NEXT` preserve current assignment and automatic-facing behavior.
- [x] Map edges, structures and blocked ground adapt to legal slots. An impossible complete layout rejects before state or recording changes.
- [x] Unselected Hold units never move or receive overlapping same-layer slots. Mixed Idle/Hold Append reserves the selected Hold while installing the Idle recipient immediately. Air/ground layers remain independent; replacement releases only deliberately selected Holds.
- [x] Mixed Worker, ground-combat, Mender and air groups at different speeds reach their accepted slots through open terrain, turns and a constrained passage without unexplained loss, deadlock or trapped workers.
- [x] A replacing Move withdraws an engaged group immediately, clears its previous tail, suppresses attacks during travel and reaches the rear formation. No test infers a rigid marching block or speed lock.
- [x] Attack-move engagement and target loss preserve the assigned point and delay arrival facing until actual waypoint completion.
- [x] Defenders retain combat-facing through cooldown ticks, return to their anchors and restore explicit facing only after losing their legal target, without navigation churn or missed legal attacks.
- [x] Appended Move/Attack-move steps retain independent accepted points and arrival facings. Clear queued, Stop and ordinary replacement preserve P1.1 semantics.
- [x] Patrol, Escort, Gather, construction, production exit, rally, direct Attack/follow and Mender support regressions remain unchanged. Invalid modifier combinations reject atomically.
- [x] Current save round trip, explicit save-12 migration, recording replay and per-tick continuation preserve state. Missing, duplicate, reordered, mismatched-count, nonfinite and illegal `FORMATION_ORDERS` data reject atomically.
- [x] Protocol-10 commands round trip exact modifiers; malformed enums/flags/angles and incompatible versions reject. Owned snapshots preserve planned facing, hostile snapshots cannot expose or inject it, and frame-size rejection leaves authority and recording unchanged.
- [x] Two- and four-player local server/worker admission and actual Unreal loopback commands preserve spacing, facing, opaque identity and exact acknowledgement behavior.
- [x] Injected controller tests use projected desktop and touch press-drag paths. Cover short drag, cancellation, selection change, second touch, Shift append, Queue next acceptance/rejection, pause/background and uncertain reconnect.
- [x] Reviewed desktop and compact captures show all spacing presets, armed facing, drag arrow, projected slots, accepted route markers and pending feedback with correct hit boxes and no clipped essential labels.
- [ ] Real mouse and iPhone touch on the exact matching package perform line placement, queued facing, withdrawal under engagement, camera gestures and Hold preservation through a complete match.

Implementation alone does not mark P1.3 accepted, close P1 physical acceptance or authorize P2 work. P1 implementation and remaining acceptance are now reported. Work is stopped before abilities, faction, terrain/economy or other P2–P7 scope.
