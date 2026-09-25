# All-difficulty AI verification

This follow-up starts from the completed Hard/Expert improvement. It unifies the lower-level planner, adds explicit beginner pacing, strengthens Normal, and adds Expert composition preferences and observed expansion raids. See [the behavior notes](../../docs/AI_DIFFICULTY_PROGRESSION.md).

## Controlled comparison

`before-macro.jsonl` and `after-macro.jsonl` contain 15 matches each: five difficulties, three standard maps, seed 7300, a paid tier-two economy opponent, and a 900-second limit. All common simulation sources, public headers, and benchmark code are identical; only the AI implementation differs. `benchmark-control.json` records their hashes. `before-SimulationAI.cpp` preserves the baseline implementation for reproduction.

| Difficulty | Before W/L/unfinished | After W/L/unfinished |
| --- | --- | --- |
| Very Easy | 0/3/0 | 0/3/0 |
| Easy | 0/3/0 | 0/3/0 |
| Normal | 0/3/0 | 2/0/1 |
| Hard | 3/0/0 | 3/0/0 |
| Expert | 3/0/0 | 3/0/0 |

The baseline already includes the earlier Hard/Expert improvements; these numbers must not be combined with the earlier nine-trial Hard comparison as independent matches. No debug units or resources are supplied. All scripted commands are validated through the ordinary game API. These trials are not human playtests.

`after-practice.jsonl` additionally tests all five levels against a passive opponent on map 0 with a 1,200-second limit. It verifies they can finish an undefended game. `expert-seeds.jsonl` checks three deterministic Expert composition seeds against the economy opponent on map 0. `comparison.json` summarizes the raw records. `verification.json` records final checks and hashes; earlier diagnostic logs are retained but are not final pass receipts.

## Reproduction

```sh
cmake -S . -B build/ai-upgrade -DCMAKE_BUILD_TYPE=Release -DCINDERLINE_BUILD_NATIVE=OFF
cmake --build build/ai-upgrade --parallel 4
build/ai-upgrade/CinderlineAIChallengeBaseline --difficulty all --scenario macro --seconds 900
build/ai-upgrade/CinderlineAIChallengeBaseline --difficulty all --scenario passive --map 0 --seconds 1200
build/ai-upgrade/CinderlineAIChallengeBaseline --difficulty expert --scenario macro --map 0 --seed 7300 --seeds 3
ctest --test-dir build/ai-upgrade --output-on-failure
```

To reproduce the baseline, compile the same benchmark and common simulation files with `before-SimulationAI.cpp` replacing the current `SimulationAI.cpp`, never both. Avoid modifying the working tree to do so. Use the recorded hashes to check common-source parity before comparing results.

Each JSON line is one match. `winner=1` means the selected AI won; `0` means the script won; `-1` means the limit was reached. Pressure requires four combat/support units within 1,000 world units of the opponent's original Headquarters, so it excludes lone scouting passes. A missing checkpoint is null if the match already ended.
