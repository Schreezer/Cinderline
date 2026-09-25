# Hard and Expert opponent rework

This is the initial Hard/Expert checkpoint. The subsequent [all-difficulty progression pass](AI_DIFFICULTY_PROGRESSION.md) extends the shared planner to lower levels and adds further Expert behavior; its verification receipts are separate.

Hard previously accumulated ore behind one infantry factory, capped its opening army below its own attack requirement after allocating a scout, and reserved the cost of research it had already purchased. Combat decisions counted harmless buildings as threats, redirected the whole army toward one visible target, and excluded badly damaged survivors indefinitely.

## Behavior changes

- Build multiple infantry factories early, then add production when queues are busy and ore accumulates. Supply planning accounts for concurrent factories and vehicle production.
- Grow the army beyond the old opening cap, expand from observed resource sites, distribute empty miners in small batches, and keep producing during research.
- Protect worker replacement funds after raids, including when an expansion builder is already near its destination.
- Assemble an army before launching, retain an assault objective, and send reinforcement groups. Use nearby defenders for small raids, including mixed aircraft and infantry attacks.
- Estimate nearby fighting strength using health, weapons, armor and range. Harmless economy buildings do not force retreats.
- Pick compatible local targets and reduce wasted siege volleys. Keep wounded veterans useful and attach repair units to frontline troops.
- Keep a scout active independently of the main army and check expansion sites earlier.

All purchases and orders use ordinary commands, costs, production times and combat rules. Enemy information still comes from vision and remembered observations. The opening attack timing presets, lower difficulty policies, save format and network format are unchanged.

## Regression coverage

`Tests/AIEconomyTests.cpp` adds seven scenarios covering paid economic growth, reinforcement purchases, deterministic continuation after saving, research already in progress, supply demand, mining distribution, recovery after losing miners, protected expansion funds and hidden enemy composition. Several checks share a scenario.

`Tests/AITacticsTests.cpp` adds nine scenarios covering harmless buildings, unequal local forces, limited raid responses, mixed raids, target compatibility, damaged veterans, army assembly, unseen enemy isolation and saved assault/support orders.

The paid growth test uses the ordinary match economy. Other focused tests use explicit development fixtures to isolate specific decisions. They are regression checks, not win-rate evidence.

## Controlled challenge trials

`Tools/AIChallengeBaseline.cpp` runs paid infantry rush, tier-two economy, and five-turret defensive opponents on each of the three standard maps. Neither side receives debug resources or units. Both use the same command, economy, fog and combat rules. The scripted opponents are fixed; these trials do not establish difficulty against skilled human players.

Before and after binaries must use identical simulation sources, headers and benchmark code, replacing only `SimulationAI.cpp` with the preserved original or revised version. Movement work was concurrent, so comparing complete old and new simulation trees would confound the result. The evidence manifest records the common files and the two AI hashes.

Final controlled results, seed 7300, standard match length, 900-second limit:

| Measure | Original Hard | Revised Hard |
| --- | ---: | ---: |
| Wins | 0 | 9 |
| Losses | 7 | 0 |
| Unfinished at the limit | 2 | 0 |
| Median army at 300 seconds | 7 | 13 |
| Scenarios with at least four units pressuring the enemy base | 2 | 9 |
| Rejected scripted commands | 0 | 0 |

The revised AI won rush trials in 313.80–322.85 seconds, economy trials in 360.30–408.00 seconds, and defensive trials in 452.20–506.25 seconds. Raw records, common-source hashes and per-scenario results are in [the challenge evidence](../artifacts/ai-challenge/README.md). The common-source and revised-AI hashes were checked against the final workspace with no mismatches.

Run the current version with:

```sh
cmake -S . -B build/ai-upgrade -DCMAKE_BUILD_TYPE=Release -DCINDERLINE_BUILD_NATIVE=OFF
cmake --build build/ai-upgrade --target CinderlineAIChallengeBaseline --parallel 4
build/ai-upgrade/CinderlineAIChallengeBaseline --seconds 900 --seeds 1
ctest --test-dir build/ai-upgrade -L ai --output-on-failure
```

Each output line is a JSON match record. `winner=1` means the game AI won, `winner=0` means the script won, and `winner=-1` means the 900-second limit was reached. Army pressure requires at least four combat units near the opponent's starting base, so a lone scout does not count as an attack.

## Final verification

- 28/28 release CTests, including native smoke and rendering checks.
- 26/26 optimized AddressSanitizer/UndefinedBehaviorSanitizer CTests.
- Unreal Development Editor build succeeded; 18/18 integration tests completed with no errors. One test retained the two explicitly allowed `idevice_id` host warnings.
- Independent review reproduced and verified fixes for worker recovery, expansion budgeting and mixed-raid defense. No remaining confirmed findings.
- `git diff --check` passed for the AI changes. Unrelated changes in the shared checkout were preserved.

The full release and sanitizer runs were shared with the concurrent movement task against the final combined sources. [Verification receipts](../artifacts/ai-upgrade/verification.json) distinguish those runs from the earlier diagnostic failures.

This source change does not update existing installed iOS or Android packages. Those require a separate platform rebuild and installation.
