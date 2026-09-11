# First skirmish mechanics pass

CHECKPOINT: First-faction mechanics and Unreal foundation

STATUS: Automated mechanics and engine integration pass. Desktop launch and camera navigation are demonstrated. Full human playtesting, competitive pacing and physical touch acceptance remain open.

WORKING:

- Paid harvesting, construction, production, research, combat, fog, AI, saved state and natural HQ destruction.
- Unreal menu, lit battlefield, camera pan/zoom/Home, command HUD and pause/resume.

FIXES MADE:

- Ranged attackers find a firing position around cover instead of stopping behind a wall.
- Hold resists passing allies and recovers its anchor after displacement.
- Menders follow a selected armed ally during Attack and AttackMove and continue healing.
- AI expansion choices require visible remaining ore; scouting refreshes its knowledge.
- Each base receives local defense, including a worker dispatched from another base.
- Expansion scouting proceeds alongside army advances. New and orphaned Menders join attacks without repeatedly resetting existing orders.
- Explicit PAUSE always pauses; Escape retains cancel-first behavior. Menus, pause and results reject new gameplay modes. Start, menu and successful load clear stale interactions.

TESTED:

- 14/14 portable rule groups and 3/3 CTests, including 3,000 offscreen native frames.
- 14/14 groups under AddressSanitizer and UndefinedBehaviorSanitizer.
- 2/2 Unreal integration tests with no warnings, errors or unfinished tests. They use real transient worlds, game mode, battlefield components, controller transitions and ordinary paid commands.
- Two independent review findings led to the AI coordination fixes above; their final source review is clear.
- Natural match trials and 50/100/200-unit simulation measurements are recorded in [the verification record](../artifacts/verification-summary.md).

ISSUES STILL OPEN:

- Current scripted-opponent trials finish below the requested 20–30 minute target. They do not establish normal human balance.
- Absolute synthetic mouse clicks are unreliable in Unreal on this Mac. The user confirmed physical menu activation; viewport/touch targeting is separate from command integration.
- The latest rules have not received a full human playthrough in Unreal. Models, readable UI, animation, effects and audio need further presentation work.

CURRENT PLAYABLE EXPERIENCE:

The Mac Unreal development target runs the first faction against a paid economic AI on three maps. The shared simulation also runs in the native development client. iOS is deferred at the user's request.

NEXT CHECKPOINT:

Integrate the original Blender models and audio, improve HUD readability, inspect the resulting Unreal build, then perform a fuller playtest. Continue to iOS after these stages are ready.
