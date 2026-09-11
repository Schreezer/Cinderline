You are the lead game engineer, systems designer, technical designer, UI/UX designer, gameplay programmer, AI programmer, technical artist, and production owner for a new mobile real-time strategy game.

Your job is to autonomously design and build the best playable game you can.

Do not stop at planning, architecture documents, pseudocode, or recommendations. Implement working systems, run the project, test them, inspect failures, fix them, and continuously improve the playable game.

The target is an original touch-first sci-fi RTS inspired by the strategic depth, responsiveness, economy management, army control, scouting, technological progression, and competitive feel of games such as StarCraft, while using completely original factions, units, buildings, names, visuals, lore, audio, and game mechanics.

TARGET PLATFORM

Primary:

* iOS
* iPhone
* iPad

Engine:

* Unreal Engine 5
* Prefer C++ for foundational gameplay systems and performance-critical logic
* Use Blueprints where they accelerate iteration
* Structure systems cleanly enough that online multiplayer can be added naturally

CORE PRODUCT VISION

Create a polished mobile RTS in which a player:

1. Starts with a headquarters and workers
2. Collects resources
3. Builds structures
4. Unlocks technologies
5. Produces military units
6. Scouts the map
7. Expands their economy
8. Controls groups of units in real time
9. Counters enemy compositions
10. Fights for map control
11. Destroys the opposing headquarters

TARGET MATCH DURATION

Target normal competitive match duration:

20–30 minutes.

Design the economy, technology tree, map size, expansion timings, unit production, combat pacing, and resource availability around this duration.

A normal match should have a meaningful progression resembling:

0–5 minutes:

* Opening economy
* Initial build choices
* First scouting
* Early defensive units
* Early harassment opportunities

5–10 minutes:

* First serious military engagements
* Technology branching
* Expansion decisions
* Scouting becomes increasingly important
* Players begin revealing strategic direction

10–20 minutes:

* Multiple production structures
* Larger mixed armies
* Additional bases/expansions
* Counter-unit decisions
* Map control
* Harassment
* Technology transitions
* Major engagements

20–30 minutes:

* Mature economies
* Advanced units
* Large combined-arms battles
* High-value technology
* Strategic positioning
* Decisive engagements
* Base pressure
* End-game victory conditions

Matches may occasionally finish substantially earlier due to successful aggressive strategies or continue beyond 30 minutes when players are evenly matched, but the game should naturally push most standard matches toward resolution within roughly 20–30 minutes.

Do not force match endings with arbitrary timers.

Let gameplay, resource pressure, map design, strategic advantages, and escalating technology naturally drive matches toward a conclusion.

CORE DESIGN PRINCIPLES

Prioritize:

* immediate responsiveness
* satisfying army control
* strong unit silhouettes
* clear counters
* low-friction touch interaction
* short moment-to-moment decision loops
* meaningful long-term strategic decisions
* meaningful scouting
* meaningful economic decisions
* tactical positioning
* technological progression
* strategic adaptation
* visually satisfying battles
* readable battlefield information
* minimal UI obstruction
* fast iteration
* maintainable modular architecture

The game should have enough strategic depth that players can change plans several times during a 20–30 minute match.

Players should be able to recognize and react to:

* enemy economic investment
* early aggression
* defensive play
* fast technology
* air transitions
* armored compositions
* infantry-heavy armies
* expansion strategies
* harassment
* hidden technology
* counter-tech

Avoid unnecessary menus and meta systems until the core battle loop is excellent.

AUTONOMY

Make reasonable decisions yourself.

Do not repeatedly ask me how every feature should work.

When several approaches are possible:

1. evaluate them
2. choose the strongest approach
3. document the decision briefly
4. implement it
5. test it

Do not stop merely because some assets, systems, or details have not been specified.

Create suitable placeholders where necessary and continue building.

Whenever possible, leave systems data-driven so parameters can later be adjusted without rewriting major gameplay code.

CONTINUOUS WORK LOOP

For every major feature:

PLAN
→ IMPLEMENT
→ BUILD
→ RUN
→ TEST
→ INSPECT
→ FIX
→ POLISH
→ COMMIT/CHECKPOINT
→ CONTINUE

Do not mark a checkpoint complete merely because code exists.

A checkpoint is complete only when the feature works inside the playable game.

CHECKPOINT 0: PROJECT FOUNDATION

Create the Unreal project and establish:

* clean source structure
* gameplay modules
* data definitions
* game mode
* player controller
* RTS camera
* unit architecture
* building architecture
* resource architecture
* command architecture
* faction architecture
* combat architecture
* AI architecture
* save/config architecture
* mobile input abstraction
* debugging utilities
* performance instrumentation

Create a basic playable battlefield.

Success condition:

The game launches on the development target, loads a battlefield, and allows the player to move around the map using touch-oriented camera controls.

CHECKPOINT 1: RTS CAMERA AND TOUCH CONTROLS

Implement excellent mobile camera controls.

Support:

* one-finger interactions where appropriate
* camera pan
* pinch zoom
* zoom limits
* smooth acceleration/deceleration
* world bounds
* camera focus
* edge-safe UI interaction
* tap detection
* drag gestures
* long press
* multi-touch recognition

Controls should feel intentional rather than merely functional.

Success condition:

Navigating the battlefield on a phone feels natural and responsive.

CHECKPOINT 2: UNIT SELECTION AND COMMAND

Implement:

* tap unit to select
* drag/circle selection
* multi-selection
* selection indicators
* contextual command handling
* tap terrain to move
* tap enemy to attack
* attack-move
* stop
* hold position
* patrol if useful
* selection by unit type
* double-tap unit type selection
* visible unit composition UI
* command feedback markers

For selected mixed armies, automatically expose subgroups such as:

Infantry × 8
Tank × 3
Support × 2

Allow the user to quickly command individual subgroups.

Success condition:

A player can smoothly select and command a mixed army using only touch controls.

CHECKPOINT 3: MOVEMENT AND FORMATIONS

Implement robust unit movement.

Units should:

* navigate around terrain
* avoid buildings
* avoid excessive unit overlap
* move coherently in groups
* maintain useful spacing
* converge naturally on destinations
* avoid obvious traffic jams
* preserve responsive commands

Add formations where they improve gameplay.

Test movement with increasingly large unit groups.

Success condition:

Large groups can traverse the battlefield without movement becoming frustrating.

CHECKPOINT 4: ECONOMY

Implement the first complete economy loop.

Start with one primary resource.

Create:

* resource nodes
* worker units
* gather command
* harvesting
* carrying
* returning resources
* resource storage
* player resource count
* worker production
* resource depletion where appropriate

Workers should intelligently continue their economic loop.

Design resource income and expansion incentives around a 20–30 minute match.

Players should gain meaningful advantages from expansion, but expansion should expose them to strategic risk.

Success condition:

A player can begin with workers and grow their economy through actual gameplay.

CHECKPOINT 5: BUILDING SYSTEM

Implement:

* building placement mode
* placement preview
* valid/invalid placement feedback
* construction cost
* construction time
* worker-assisted or automatic construction
* collision validation
* terrain validation
* cancellation
* destruction
* building health
* production buildings
* defensive structures
* technology structures

Initial building set:

* Headquarters
* Resource Processing Structure
* Infantry Production Structure
* Vehicle Production Structure
* Technology Structure
* Defensive Turret

Success condition:

The player can build a functioning base from scratch.

CHECKPOINT 6: UNIT PRODUCTION

Create a production system.

Support:

* production queues
* resource costs
* build times
* rally points
* queue cancellation
* multiple production structures
* visual queue feedback

Initial combat roster should include at least:

* Worker
* Basic ranged infantry
* Heavy infantry or anti-armor unit
* Fast scout
* Main battle vehicle
* Long-range unit
* Support unit
* Air unit

Give every unit a clear battlefield role.

Production pacing should allow armies to grow substantially over a 20–30 minute match without reaching maximum strategic complexity immediately.

Success condition:

The player can convert economic growth into increasingly sophisticated armies.

CHECKPOINT 7: COMBAT

Implement satisfying RTS combat.

Include:

* target acquisition
* attack range
* attack cooldown
* projectile or hitscan weapons
* armor
* damage
* health
* death
* target priorities
* chase behavior
* retaliation
* attack-move
* line-of-sight considerations where useful
* ground/air targeting rules
* damage categories
* counters

Combat should have readable effects:

* muzzle flashes
* projectiles
* impacts
* explosions
* health bars
* destruction feedback
* responsive audio hooks

Major engagements should create meaningful consequences without automatically ending every match.

Allow players opportunities to:

* retreat
* reinforce
* counterattack
* change composition
* rebuild
* exploit another part of the map

Success condition:

Two armies can engage in battles that are understandable, tactical, and enjoyable to watch and control.

CHECKPOINT 8: TECH TREE

Create meaningful progression during each match.

Implement:

* technology requirements
* building prerequisites
* unit unlocks
* upgrades
* weapon upgrades
* armor upgrades
* utility upgrades
* advanced units

Structure technology progression so strategic evolution continues across the full match rather than unlocking everything during the opening minutes.

Players should face genuine choices between:

* economy
* military production
* expansion
* upgrades
* technology
* specialized units

Success condition:

Players must choose how to develop their economy and army rather than simply building everything immediately.

CHECKPOINT 9: FOG OF WAR AND SCOUTING

Implement:

* unexplored areas
* explored but currently unseen areas
* active vision
* unit vision radius
* building vision radius
* enemy visibility rules
* minimap fog
* scouting

Information should become a strategic resource.

Because matches are 20–30 minutes long, scouting should remain relevant throughout the entire game rather than only during the opening.

Success condition:

A player cannot reliably know the opponent's strategy without continued scouting.

CHECKPOINT 10: MINIMAP AND BATTLEFIELD UI

Build a clean mobile RTS HUD.

Include:

* resources
* minimap
* selected-unit panel
* unit subgroup panel
* build menu
* production queue
* technology menu
* contextual commands
* alerts
* objective state

Keep interaction reachable and readable on modern iPhones.

Test multiple aspect ratios.

Success condition:

The full RTS can be operated comfortably without UI clutter overwhelming the battlefield.

CHECKPOINT 11: ENEMY AI

Create an actual RTS opponent.

The AI should:

* gather resources
* produce workers
* build a base
* scout
* recognize threats
* expand
* choose technologies
* produce armies
* adapt composition
* defend
* attack
* retreat where appropriate
* exploit vulnerable locations
* transition strategies during long matches
* recover intelligently after losing engagements

Create several behavior profiles if practical:

* Aggressive
* Economic
* Defensive
* Adaptive

Do not make the AI simply cheat with arbitrary units.

Success condition:

A complete 20–30 minute match against the AI feels like playing against an opponent with evolving strategy.

CHECKPOINT 12: FIRST COMPLETE MATCH

Create a complete skirmish loop:

Main Menu
→ Start Match
→ Loading
→ Match
→ Victory/Defeat
→ Results
→ Rematch

Win condition:

Destroy the enemy headquarters.

Add suitable match statistics such as:

* match duration
* resources collected
* units produced
* units lost
* units destroyed
* structures built
* structures destroyed
* damage dealt
* expansions established
* technology upgrades completed

Success condition:

A new user can launch the game and play an entire match without developer intervention.

CHECKPOINT 13: GAME FEEL

Now heavily polish the actual experience.

Improve:

* camera feel
* selection responsiveness
* unit acknowledgement
* movement
* attack feedback
* explosions
* death effects
* production feedback
* resource collection feedback
* building construction
* animation
* particles
* UI transitions
* audio cues
* vibration/haptics where appropriate

Every player action should create immediate feedback.

Success condition:

The game begins to feel like a commercial product rather than a prototype.

CHECKPOINT 14: PERFORMANCE

Profile actual gameplay.

Create stress scenarios with:

* 50 units
* 100 units
* 200 units
* large simultaneous battles
* multiple bases
* projectiles
* effects
* pathfinding load

Optimize:

* simulation
* pathfinding
* collision
* AI updates
* target searching
* rendering
* animation
* particles
* memory
* UI
* networking-ready state representation

Use data-oriented approaches, batching, LOD, instancing, MassEntity/MassGameplay, spatial partitioning, asynchronous work, or other suitable Unreal systems where they materially improve the game.

Success condition:

Large late-game battles remain responsive on the target mobile hardware.

CHECKPOINT 15: ORIGINAL FACTION IDENTITY

Once gameplay works, establish a strong original universe.

Create the first faction with coherent:

* visual language
* architecture
* infantry
* vehicles
* aircraft
* weapons
* UI iconography
* technology philosophy
* naming convention
* lore

Avoid copying recognizable Blizzard designs.

The faction should be identifiable from silhouettes alone.

Success condition:

The game begins developing its own recognizable identity.

CHECKPOINT 16: SECOND FACTION

Create a mechanically distinct second faction.

Do not merely reskin the first faction.

Give it different:

* economic mechanics
* production mechanics
* army composition
* mobility
* strengths
* weaknesses
* technology paths

Design asymmetric counters while preserving competitive readability.

Success condition:

Faction choice significantly changes strategy.

CHECKPOINT 17: MAP DESIGN

Create at least three strong competitive maps.

Maps should contain:

* starting bases
* expansion locations
* resource distribution
* chokepoints
* open combat areas
* alternate routes
* scouting opportunities
* defensible positions
* contestable strategic areas

Maps must support the target 20–30 minute match duration.

Map scale should leave room for:

* multiple bases
* flanking
* scouting
* harassment
* defensive positioning
* territorial control
* large late-game armies

Success condition:

The same factions play differently across different maps.

CHECKPOINT 18: ADVANCED COMMAND UX

Improve mobile army management.

Experiment with:

* smart selection
* intelligent subgrouping
* quick control groups
* saved armies
* command radial menus
* gesture shortcuts
* double-tap commands
* focus camera shortcuts
* intelligent contextual commands
* automatic formation handling

Optimize for the question:

"How can a player command a complicated army in under one second?"

Success condition:

Army control begins feeling designed around touch rather than adapted from mouse controls.

CHECKPOINT 19: MULTIPLAYER ARCHITECTURE

Prepare and implement real-time 1v1 multiplayer.

Use an authoritative match simulation.

Implement:

* player connection
* match start synchronization
* command replication
* simulation synchronization
* reconnect behavior
* deterministic or reconciliation strategies as appropriate
* latency handling
* disconnect handling
* victory validation

Do not replicate unnecessary visual state.

Replicate player intent and authoritative gameplay state efficiently.

Success condition:

Two players can complete an entire match over the network.

CHECKPOINT 20: MATCHMAKING AND COMPETITIVE LOOP

Add:

* accounts/player identity
* matchmaking
* casual queue
* ranked queue
* rating
* match history
* rematch
* friends/invite support where appropriate

Success condition:

Two players can discover each other through the game and play without developer involvement.

CHECKPOINT 21: REPLAY SYSTEM

Record gameplay using compact command/state information.

Support:

* saved match replay
* pause
* speed control
* camera freedom
* player perspective
* timeline

Design replay data so it can later support:

* spectating
* debugging
* balance analysis
* highlights

Success condition:

A previously completed match can be reconstructed and watched.

CHECKPOINT 22: CONTENT AND BALANCE

Expand each faction.

Aim for enough units and technologies to enable:

* early rush
* economic opening
* defensive opening
* air strategy
* armored strategy
* infantry strategy
* mixed armies
* siege
* harassment
* counter-teching
* strategic transitions during the same match

Build balance parameters into editable data assets.

Create automated simulations or battle test scenarios where useful.

Measure actual match duration during testing.

Track:

* average match duration
* median match duration
* duration by strategy
* duration by map
* early surrender frequency
* late-game stalemate frequency

Tune systems toward a healthy 20–30 minute competitive average.

Success condition:

There are multiple viable strategic paths rather than one dominant build, and match pacing naturally creates substantial early, mid, and late-game phases.

CHECKPOINT 23: ONBOARDING

Create a polished first-time experience.

Teach:

* camera movement
* unit selection
* movement
* gathering
* construction
* army production
* attacking
* technologies
* scouting
* expansions
* counters
* victory conditions

Prefer teaching through gameplay rather than walls of text.

Success condition:

A first-time RTS player can understand the game without external instructions.

CHECKPOINT 24: AUDIO/VISUAL PASS

Establish a coherent presentation layer.

Improve:

* environment
* terrain
* buildings
* unit models
* materials
* animations
* weapon effects
* explosions
* destruction
* lighting
* soundscape
* unit voices
* interface sounds
* music

Prioritize clarity and distinctive silhouettes over excessive detail.

Success condition:

Screenshots and gameplay footage look intentionally designed rather than assembled from prototypes.

CHECKPOINT 25: IOS PRODUCT PASS

Build the game as an actual iOS product.

Verify:

* iPhone layouts
* iPad layouts
* touch behavior
* safe areas
* orientation behavior
* loading
* suspend/resume
* application lifecycle
* performance
* thermals
* battery behavior
* memory
* package size
* settings
* graphics quality levels
* audio
* haptics
* offline AI play
* network play

Test complete 20–30 minute matches on physical mobile hardware, including late-game battles and thermal/performance behavior over the entire session.

Success condition:

The complete game can be installed and played naturally as an iOS application.

WORKING STYLE

Maintain a living task board:

NOW
NEXT
LATER
DONE
BLOCKED

At the end of every checkpoint produce a concise report:

CHECKPOINT:
STATUS:

WORKING:

* ...

IMPLEMENTED:

* ...

TESTED:

* ...

ISSUES FOUND:

* ...

FIXES MADE:

* ...

CURRENT PLAYABLE EXPERIENCE:

* ...

NEXT CHECKPOINT:

* ...

Do not pause after reporting.

Immediately continue to the next checkpoint unless external information is genuinely required.

ENGINEERING RULES

Prefer:

* modular systems
* composition
* data-driven design
* clear interfaces
* gameplay tags where useful
* event-driven systems
* reusable components
* automated tests where valuable
* debug visualization
* profiling
* deterministic calculations where appropriate
* configuration through data assets

Avoid:

* giant monolithic classes
* unnecessary Tick usage
* hardcoded balancing values scattered throughout code
* gameplay logic embedded deeply inside UI
* duplicated systems
* premature menu/meta complexity

Create debugging commands and visualization tools for:

* unit state
* AI state
* paths
* targets
* vision
* resource flow
* production
* performance
* network state

ART DIRECTION

Use an original stylized sci-fi visual language optimized for an RTS camera.

Prioritize:

* strong silhouettes
* readable faction colors
* identifiable unit classes
* clean terrain
* highly visible projectiles
* restrained environmental clutter
* satisfying destruction
* polished effects

Generate or create temporary assets whenever needed so implementation continues.

GAMEPLAY PRIORITY ORDER

When deciding what to spend time on, use this priority:

1. Fun
2. Controls
3. Responsiveness
4. Strategic depth
5. Battlefield readability
6. Performance
7. AI quality
8. Visual polish
9. Content quantity
10. Meta systems

Do not sacrifice the first six to prematurely build the last four.

QUALITY BAR

Whenever a system technically works, ask:

"Would a player enjoy using this?"

If not, iterate again.

Whenever a UI interaction requires unnecessary taps, simplify it.

Whenever an army command produces surprising behavior, fix it.

Whenever a battle becomes visually confusing, improve readability.

Whenever the AI behaves obviously stupidly, improve it.

Whenever frame time spikes, profile the actual cause.

Whenever architecture starts resisting iteration, refactor it.

MATCH PACING QUALITY BAR

The game should feel like it has three genuinely different strategic phases:

EARLY GAME
Establish economy, scout, probe defenses, choose opening strategy.

MID GAME
Expand, reveal technology choices, counter enemy compositions, contest territory, conduct raids and meaningful engagements.

LATE GAME
Large economies, advanced units, combined-arms warfare, sophisticated positioning, decisive attacks, and high-impact strategic choices.

Do not let late-game units make early and mid-game units completely irrelevant.

Do not let every match reach the same end-state.

Players should be able to win through aggression, economy, superior scouting, tactical execution, technology, positioning, or strategic adaptation.

START NOW

Begin with Checkpoint 0.

Inspect the existing repository/project first if one exists.

Understand what is already implemented before replacing anything.

Then build forward autonomously.

Your objective is not to create a design document.

Your objective is to leave behind the best functioning, polished, playable iOS RTS game you can build.
