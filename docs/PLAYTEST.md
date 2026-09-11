# First skirmish playtest

Use the Unreal Cinderline window. This pass checks the ordinary player controls before broader balance work and iOS.

1. Choose Shattered Rift and start a skirmish.
2. Select a Drudge worker, open BUILD, choose Kiln, then place it on clear explored ground near your workers.
3. Watch that Drudge stop mining and travel to the foundation. Progress must stay at zero until it arrives; the other workers should keep mining. Give the builder STOP or MOVE: the site should show PAUSED. Right-click the site with a Drudge selected, or select the site and choose ASSIGN DRUDGE, to resume without paying again. Let it finish and watch the original miner return to ore. Then select the completed Kiln and queue Ember infantry.
4. Select an Ember and right-click clear terrain to move. Try ARMY, ATTACK MOVE, STOP and HOLD. Space returns the camera home; arrows pan and the wheel zooms.
5. Build more workers, production and technology. Explore toward the red Anchor, reinforce losses and keep harvesting. Try an expansion before the starting ore runs out.
6. Finish through victory or defeat, inspect the results, then rematch. Record the duration and the first interaction that felt unclear or failed.

On touch layouts, tap a selected unit's destination to command it. ACTIONS shows contextual commands, TYPES shows subgroups, and QUEUE shows production. These compact layouts have desktop window checks; physical touch remains deferred.

Unfinished structures have a SITE shortcut on compact layouts. A selected Drudge can tap a site to resume it. Construction consumes one worker regardless of how many are selected. Cancelling a site refunds its unused construction cost and releases its builder. Old version 1 saves load unfinished sites paused; assign a Drudge to continue them.

The opponent now scouts and remembers what it has seen. Watch for reconnaissance around expansions and attacks on discovered bases. Switching to aircraft or armor should influence its later purchases after it sees those units. Hidden changes should not immediately alter its production. This pass is tracked in [AI_STRATEGY_PASS.md](AI_STRATEGY_PASS.md).

Useful feedback includes the selected unit/structure, the button or ground location clicked, what happened, and what you expected. Automated absolute mouse targeting is unreliable in Unreal on this Mac, so command integration tests do not replace this hands-on check.

With scouting-driven AI, the latest three scripted match trials lasted 10:56, 13:08 and 14:03. They used one seed per map against a simple scripted opponent, spent ordinary resources, and ended through gameplay. The prior construction checkpoint recorded 12:25, 12:23 and 12:44. Neither set establishes normal human balance; the intended 20–30 minute pacing remains a tuning target.
