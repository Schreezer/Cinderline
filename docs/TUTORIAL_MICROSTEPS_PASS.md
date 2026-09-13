# Tutorial microsteps pass

Status: implementation and Mac verification complete. The Mac build, all 32 selected Unreal automation tests and all 15 reviewed Mac visual cases pass. This pass did not create or install a new iOS package, and it does not provide physical touch or thermal evidence.

## Delivered behavior

The guided match now presents one action at a time, derived from the live simulation, current selection, open catalog, placement or command mode, production queues and completed objectives. The small next-action card remains beside an open catalog so it does not hide the control it is teaching.

Each prompt uses a short instruction. The first worker prompt introduces the Drudge and its resource-gathering role in plain language. Guidance then uses the target appropriate to the current action:

- actual button highlights and arrows for HUD actions;
- world rings around the required friendly unit, building or ground location;
- a SHOW button and arrow when the required world target is outside the usable view;
- a suggested valid construction preview before the player confirms a building site.

SHOW focuses and zooms close enough to clear the target from the edge HUD. It does not select a unit, issue an order or advance the lesson. Tutorial guidance does not grant resources, finish production, damage an enemy or bypass ordinary outcomes. Progress still requires the accepted player action and resulting simulation state.

Training is taught one click at a time: open TRAIN, select the unit portrait, increase the requested quantity and confirm the queue. Before a portrait is chosen, the catalog uses the neutral prompt `CHOOSE A UNIT TO TRAIN`; the card names the requested unit and points to its portrait. The rally lesson pins the producer used by the tutorial before guiding the rally command and ground target. The existing fourteen outcome lessons remain intact, including paid construction and production, scouting, research, the finite defense, and ordinary player victory at the enemy anchor.

## State recovery

The current prompt is recomputed when the player departs from the expected path:

| Live state | Guidance |
| --- | --- |
| Wrong catalog or command mode | Close or leave the wrong mode, then highlight the required action. |
| Canceled or shifted queue item | Read the live queue and return to the missing portrait, quantity or queue action. |
| Abandoned construction site | Offer SITE to return to it, ASSIGN when a worker is needed, and Close when the open panel blocks the next action. |
| Stopped or lost scout | Restore the selection/order step or guide ordinary replacement production. |
| Stopped or lost army | Return to the applicable selection and command step using the surviving or replacement army. |
| Offscreen unit, building or ground target | Show SHOW; focus and zoom without selecting, issuing an order or earning credit. |
| Insufficient ore, supply or technology | Guide the next ordinary remedy without granting resources or skipping requirements. |

The card and target remain mobile readable, respect safe areas and avoid covering the highlighted control or world target. Wrong actions are recoverable; they do not falsely complete the current microstep.

## Verification

The Mac build passes. The focused runner completed 32 Unreal tests successfully across tutorial, integration, UI and presentation coverage:

```sh
python3 Saved/TutorialMicrosteps/run-tests.py
```

The exported results are recorded in [automation.json](../artifacts/tutorial-microsteps/automation.json). They include the new introduction, economy sequence, army sequence, remaining-batch and interrupted-order guidance tests, along with the existing fourteen-objective end-to-end tutorial and ordinary-command completion.

The final Mac visual matrix is:

```sh
python3 Saved/TutorialMicrosteps/capture.py
```

It covers 15 desktop, 956 x 440 phone and 667 x 375 small-phone cases: worker and ore pointers; build open, choice, placement and wait states; train open, portrait, quantity, queue and wait states; research; the pinned rally chain; scouting; and offscreen recovery before and after SHOW. Each case checks its deterministic state marker, target type, action chain, viewport bounds, button overlap and protected player saves. All 15 final captures, including the before-SHOW state, were visually reviewed for readable copy, correct pointers and clear targets.

The combined [verification record](../artifacts/tutorial-microsteps/verification.json) binds the successful Mac build capped at two parallel actions, 32 passing automation tests, 15 reviewed captures, protected player state and source hashes. The capture pipeline exited successfully, and no Unreal or game process was left running.

The prior command-allocation package remains the latest prepared iOS package. No iOS package was produced or installed for this tutorial pass. Physical iPhone touch behavior, readability and thermal behavior remain unverified.
