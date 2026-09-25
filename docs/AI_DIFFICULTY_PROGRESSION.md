# AI difficulty progression

The subsequent [ore-control pass](AI_ORE_CONTROL.md) adds captured-site development, standing guards, a third productive-base target for Expert, and exposed-worker harassment. Its verification receipts are separate from this checkpoint.

The lower three difficulties previously used a separate planner with one-factory production, excessive savings, and damaged-unit benching. All five levels now share the reliable economy and combat implementation introduced in the [Hard/Expert rework](AI_HARD_REWORK.md). Difficulty controls the amount and timing of pressure, production capacity, counter-unit priorities, and combat focus frequency.

## Intended behavior

| Level | Earliest offensive launch | Pressure and recovery | Production and tactics |
| --- | --- | --- | --- |
| Very Easy | 7:00 | Up to four armed raiders; 90-second raid window followed by 90 seconds of recovery | Slower development, at most two infantry factories and 12 total combat/support units, no deliberate counter composition or target focus |
| Easy | 5:20 | Up to six armed raiders; 105-second raid window followed by 75 seconds of recovery | At most two infantry factories and 20 total combat/support units, basic counter composition, ordinary automatic combat |
| Normal | 4:00 | Grouped attacks with reinforcement groups | Up to three infantry factories, practical expansion, moderate counter composition and less frequent target focus |
| Hard | 3:10 | Concentrated assaults and sustained reinforcements | Retains the earlier improved economy, local force assessment, counter composition and responsive defense |
| Expert | 2:30 | Sustained main assault plus opportunistic expansion raids | Retains advanced macro; seed selects one of three infantry preferences, observed threats override that preference; three fast raiders can attack a known weak expansion while the main force remains together |

These are launch permissions, not guaranteed arrival times. Units must be produced and assembled first. Beginner raid durations scale with map size for travel time; their recovery durations do not. Wave limits count simultaneous assigned armed raiders. Losses can be replaced during an active window. Reconnaissance is separate, and beginner repair units stay home. Defense remains active during recovery.

Every level now replaces lost miners, plans supply for concurrent production, continues producing during research, and uses observed resource sites for expansion. Beginner factories cannot bypass their slower schedule merely because ore accumulates. Reduced difficulty comes from explicit limits and pacing rather than broken economic behavior.

Expert's secondary attack requires at least 18 available soldiers, no visible home raid, and an observed secondary Headquarters sufficiently far from the main objective. It avoids a known costly garrison and detaches exactly three suitable fast units. Enemy intelligence comes from visible units and remembered scouting reports. No difficulty gets free resources, faster production, extra damage, or hidden enemy composition.

Raid windows derive from the saved simulation tick. Composition derives from the saved seed. Assaults and support use ordinary saved orders. Difficulty values, save format, and network format remain unchanged.

## Evidence

The [controlled benchmark records](../artifacts/ai-levels/README.md) compare the previous Hard/Expert checkpoint against this revision, changing only `SimulationAI.cpp` while using identical common simulation sources, headers, and benchmark code.

Against the paid economy opponent on three standard maps, seed 7300, with a 900-second limit:

| Level | Before: wins / losses / unfinished | After: wins / losses / unfinished |
| --- | --- | --- |
| Very Easy | 0 / 3 / 0 | 0 / 3 / 0 |
| Easy | 0 / 3 / 0 | 0 / 3 / 0 |
| Normal | 0 / 3 / 0 | 2 / 0 / 1 |
| Hard | 3 / 0 / 0 | 3 / 0 / 0 |
| Expert | 3 / 0 / 0 | 3 / 0 / 0 |

Normal establishes four-unit base pressure at 271–283 seconds in all three revised trials; it never met that threshold in the original trials. A separate ordinary-economy test gives Normal three completed infantry factories and 13 combat/support units at 300 seconds. Beginner levels remain beatable by the stronger scripted economy opponent and can finish a passive opponent, rather than permanently stalling.

These results cover fixed scripted opponents and isolated regressions. They do not establish human difficulty or broad win rates. Expert's larger opening group can arrive later than Hard's in a passive match even though its launch permission is earlier.

## Regression coverage and delivery

`Tests/AIDifficultyProgressionTests.cpp` checks paid worker recovery at every level, beginner opening grace periods and wave limits, home repair support, recovery after save/load, defense during recovery, Normal parallel production, deterministic Expert composition, and visible versus hidden or defended expansion targets. Saved split assaults also retain identical simulation hashes after continuation.

Existing economy, tactics, scouting, support, and expansion tests run against the common planner. The expansion placement fixture supplies the now-required production capacity; the support fixture checks actual Escort orders; objective cleanup waits for the old Headquarters to be destroyed before testing scouting toward a new one.

Exact build/test receipts and source hashes are recorded in [verification.json](../artifacts/ai-levels/verification.json). The Unreal Development Editor is rebuilt locally. Existing iOS, Android, and Linux distribution packages are not replaced by this source change.
