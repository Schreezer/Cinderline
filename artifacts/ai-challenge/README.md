# Hard AI challenge benchmark

The controlled comparison changes **only `SimulationAI.cpp`** between builds. Both executables use the same frozen simulation/navigation sources, public headers, benchmark, and compiler flags. `comparison.json` records their source SHA-256 hashes and results. `before.jsonl` and `after.jsonl` contain the full match metrics.

Nine fixed challenges cover paid infantry rush, tier-2 macro, and five-turret tier-2 defense on all three standard maps, with a 900-second limit and seed 7300. Neither side receives debug units, ore, or modified combat rules. The script uses normal commands and checks fog for enemy targets. These are scripted opponents, not human playtest results.

| Hard AI | Wins | Losses | Unresolved at 900s | Median army at 300s | Challenges with base pressure |
| --- | ---: | ---: | ---: | ---: | ---: |
| Original | 0 | 7 | 2 | 7 | 2 |
| Improved | 9 | 0 | 0 | 13 | 9 |

Base pressure requires at least four live combat units within 1,000 world units of the opponent's starting Headquarters. All scripted commands were accepted. Earlier checks with seeds 7300 and 7301 yielded the same gameplay metrics; the final report counts the nine distinct strategy/map combinations once.

Run the current AI benchmark:

```sh
cmake --build build/ai-upgrade --target CinderlineAIChallengeBaseline
build/ai-upgrade/CinderlineAIChallengeBaseline --seeds 1 --seconds 900
```

Use `--scenario rush|macro|turtle`, `--map 0|1|2`, `--seed N`, or `--seconds N` for a focused run. A JSON `winner` of `1` means the Hard AI won, `0` means the scripted opponent won, and `-1` means the match remained unresolved at the limit.
