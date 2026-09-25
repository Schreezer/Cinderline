# Ore-control evidence

See [behavior and regression details](../../docs/AI_ORE_CONTROL.md).

`before-SimulationAI.cpp` preserves the AI immediately before the ore-control changes. `control.json` records common-source, header, benchmark and AI hashes. Baseline executables link that preserved implementation against the same common simulation library as the revised executables; the library's revised AI object is not pulled into the baseline because its symbol is supplied by the preserved object.

`before-ore-tests.log` records four failing feature checks and one passing hidden-information check. `ore-tests-final.log` records all five feature checks passing, including actual income from captured ore. These use explicit development fixtures and normal commands after setup.

`before-hard.jsonl`, `after-hard.jsonl`, `before-expert.jsonl`, and `after-expert.jsonl` contain nine paid matches each: three policies on three standard maps, seed 7300, a 900-second limit. `comparison.json` summarizes them. No debug resources or units are added in these full challenge matches. `winner=1` is the selected game AI, `0` the scripted opponent, and `-1` an unfinished match.

Hard and Expert each won all nine matches before and after this follow-up. All scripted commands were accepted. Final checks passed: 28 release suites, 28 AddressSanitizer/UndefinedBehaviorSanitizer suites, and 18 Unreal integration tests with zero errors or warnings. The Unreal Development Editor build succeeded.

The baseline is the already improved AI from the earlier difficulty pass. These trials test preservation of challenge performance alongside new territorial behavior; they are not independent human win-rate evidence. This baseline and the earlier reports share some identical scenarios, so their counts must not be pooled as new samples.

Final passing receipts are `tests-final.log`, `sanitize-tests-final.log`, and the Unreal report named in `verification.json`. Logs without the `final` suffix may contain diagnostic failures or checks run before the final placement fix.

```sh
cmake --build build/ai-upgrade --parallel 4
build/ai-upgrade/CinderlineAIResourceControlTests
ctest --test-dir build/ai-upgrade --output-on-failure
build/ai-upgrade/CinderlineAIChallengeBaseline --difficulty hard --scenario all --seconds 900
build/ai-upgrade/CinderlineAIChallengeBaseline --difficulty expert --scenario all --seconds 900
```
