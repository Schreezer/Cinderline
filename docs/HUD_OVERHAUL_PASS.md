# HUD overhaul pass

Status: implementation and Mac verification complete in the current working tree. No commit or push was made.

## Delivered HUD

The battle HUD now uses one compact composition across Mac and phone layouts:

- BUILD, TRAIN, RESEARCH and ARMY remain permanently available in a vertical icon rail on the left;
- ore, crew, technology/upgrades and match time use separate slim resource chips;
- the minimap sits at the bottom-left;
- illustrated, title-case unit cards form the bottom ribbon;
- selection identity and a small group of context commands occupy the bottom-right;
- build, train, research, army, squads, jobs, queues and unit information use rounded palettes with short labels and explicit active states;
- the unit information palette exposes purpose, combat effects and trustworthy simulation-derived stats.

The responsive palette is 416 logical units wide when space permits and 336 logical units on narrower layouts. The narrow TRAIN palette uses a 4-by-2 portrait grid, while wider layouts keep all eight portraits in one row. Quantity and queue actions remain separate 44-unit targets. Queue entries use a distinct right-aligned cancel cross instead of appending an ambiguous `x` to the job text.

All interactive controls retain at least 44 logical units even where their visible icon surfaces are smaller. Compact resource chips, the icon rail, shorter status copy and dark feedback backplates leave more of the battlefield readable without changing the underlying commands.

## Preserved behavior

The overhaul changes presentation and layout while preserving gameplay commands, selection, queues, stable job cancellation, pinned production and rally behavior, minimap information, safe-area handling and central world interaction.

Tutorial button identity remains the exact `Action + Argument + EntityId` tuple. Highlights and arrows resolve against the final button hit geometry, while entity and ground rings remain clear of panels. SHOW continues to focus an offscreen target without selecting a unit or issuing an order. The 15 tutorial render cases confirm that open palettes and the next-action card remain usable together.

## Verification

The source-frozen Mac build passed with at most two parallel actions. The selected automation run passed all 32 tutorial, integration, UI and presentation tests.

Rendered verification passed and received visual review for 25 main HUD cases and 15 tutorial cases across desktop, tablet, 956 x 440 phone and 667 x 375 small-phone layouts. The checks cover viewport bounds, control overlap, 44-unit action targets, responsive palettes, protected player state and tutorial target identity. Final polish confirmed readable title-case unit cards, complete build and research status text, high-contrast feedback and rally instructions, concise job guidance, and clear queue cancellation.

The combined [verification record](../artifacts/hud-overhaul/verification.json) binds the source, build, automation, capture and receipt hashes. The bounded [source review](../artifacts/hud-overhaul/source-review.md) found no important issue.

For the idle 956 x 440 fixture, the union of logged input-blocking HUD rectangles fell from 31.68% to 18.83% of the viewport. The [geometry comparison](../artifacts/hud-overhaul/geometry-comparison.json) measures the union of axis-aligned input rectangles, counting overlaps once. It does not measure rendered pixel coverage, phone performance or physical touch behavior.

The capture fixtures preserved the tracked player save and preference states. All owned Unreal and game processes were closed after verification.

No iOS package was built or installed for this pass. Physical-device touch, performance and thermal behavior remain unverified. The prior command-allocation package remains the latest prepared iOS package.
