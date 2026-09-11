# Cinderline

A touch-first RTS about prospectors defending a mining frontier. The first playable faction, the Cairn Assembly, builds low, split-hull machines around a bright central ore core. Teal lights identify your forces. Warm red lights identify the opposing command. The silhouette must explain the role even without its color.

## First playable match

Both sides begin with a headquarters, workers and the same resource budget. Workers carry finite ore back to a headquarters or processor. The opponent pays the same construction and production costs as the player. Destroy every opposing headquarters to win; there is no match deadline.

| Unit | Role | Intended silhouette |
| --- | --- | --- |
| Drudge | Worker; harvests ore and constructs buildings | Small split diamond, exposed cargo core |
| Ember | General ranged infantry | Compact upright wedge |
| Needle | Anti-armor and anti-air infantry | Twin forward prongs |
| Skim | Fast vision and harassment | Narrow horizontal dart |
| Anvil | Armored frontline | Broad split hull |
| Cinderthrow | Long-range siege | Wide chassis with long barrel |
| Mend | Repairs and sustains armies | Open ring around core |
| Veil | Air pressure and flanking | Swept triangular wing |

The Anchor headquarters produces Drudges. The Kiln produces infantry. The Crucible produces vehicles. The Resonator opens research and advanced production. The Siphon shortens mining travel and provides supply. The Ward protects approaches. Initial gameplay uses one faction on both sides; asymmetric factions require a later tested implementation.

Construction occupies one Drudge. Placing a foundation pays the cost and assigns one selected worker, preferring an available worker and then proximity. That Drudge stops harvesting, retains its cargo, and walks to the perimeter. Progress and construction health increase only while its assigned builder is alive and physically working there. Multiple selected workers do not accelerate construction; the others keep their orders. The listed build time starts after arrival.

Moving, stopping or retasking the builder pauses the site. Worker death also pauses it. An unfinished site retains its progress and damage until another Drudge resumes for no additional ore, or the player cancels for the unused portion of its cost. Completion, cancellation or site destruction releases its assigned worker to its previous mining job when applicable. A newer explicit order takes precedence. Both sides follow these rules, and the AI reassigns idle sites before starting more construction.

Version 3 saved matches preserve travelling and working builders, paused sites, mining return context and the opponent's scouting knowledge. Version 1 and 2 saves still load, with empty scouting knowledge until the opponent makes new observations. Unfinished structures from version 1 start paused and need a Drudge.

## Touch command policy

Tap a friendly unit to select it. Tap terrain to move selected troops, a resource to gather with workers, or a visible enemy to attack. Camera drag and selection drag have distinct modes so a camera motion cannot accidentally order an army. Pinch changes zoom. A visible attack-move control explicitly changes the next terrain command. Context buttons show available production and construction choices. Subgroup buttons narrow the selection by unit type.

With a Drudge selected, tap an unfinished friendly site to resume it on touch, or right-click it on desktop. Desktop left-click still selects the site. Select a paused site and choose ASSIGN DRUDGE to send the nearest available worker; this shortcut never takes a worker from another construction job. Site labels distinguish EN ROUTE, BUILDING and PAUSED. An amber work beam appears only while construction is active.

The native runner also accepts mouse and keyboard input. It exists to exercise the same C++ simulation before Unreal is ready. Native-runner results do not establish Unreal rendering, iOS usability or mobile performance.

## Simulation decisions

- Shared engine-independent C++ owns entities, economy, orders, movement, combat, vision, production, research and opponent behavior.
- One fixed 50 ms simulation step prevents frame rate from changing economy or weapon timing. Presentation renders between updates.
- Commands identify issuing player and entity IDs. They pass ownership, cost, placement, technology and visibility validation before changing gameplay.
- Stable IDs avoid presentation holding pointers across simulation steps.
- Central definitions hold balance values. Procedural visual assets let the gameplay run without purchased content.
- A command boundary prepares for authoritative networking. No network or cross-platform determinism claim follows from that structure alone.
- Fog has separate unexplored, previously explored and currently visible states. Enemy units must not be drawn or targeted through current fog.

The opponent remembers enemy structures it has seen until renewed vision confirms that their last-known site is empty. A witnessed death clears the report immediately; hidden destruction does not. Mobile reports retain their last-seen position and type for 90 seconds. Production uses reports from the last 60 seconds to favor counters, then falls back to its balanced mix. It never reads hidden enemy upgrades or army composition for those decisions.

Scouting visits public map landmarks according to observation age. Armies prioritize remembered Anchors and production structures, moving to last-known positions through fog. Direct attacks still require current vision. When no structure is known, they search less recently observed locations instead of repeatedly attacking a cleared starting base. One healthy scout can continue reconnaissance independently; existing local defense, retreat, support-follow and builder reservation rules remain active.

## Pacing validation

The product target is 20–30 minutes. Actual match durations must be reported from completed simulations and later human matches. Early production, travel, expansion distance and technology costs are tunable. A development runner completing a match is evidence of a loop, not evidence of competitive balance.

There is no artificial end timer. Finite starting deposits, exposed expansions and expensive advanced units should create decisions throughout the match. Any tuning still needs measured playtests.

## Evidence required for completion

Checkpoint 0 requires a successful Unreal launch into an interactive battlefield. Further checkpoint reports distinguish source implementation, automated simulation checks, native gameplay checks, Unreal play checks, iOS simulator checks and physical iPhone checks. Unverified paths remain open on the task board.
