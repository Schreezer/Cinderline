# Army Rally Control visual review

Reviewed the current PNGs only after their matching verification JSON files existed. All ten requested main captures report fresh captures, in-viewport buttons, no button overlap, and the expected state actions:

- `selected-units-small.png`
- `deselected-small.png`
- `army-roster-small.png`
- `rally-unset-small.png`
- `rally-set-small.png`
- `rally-marker-small.png`
- `rally-override-small.png`
- `worker-rally-small.png`
- `selected-units-desktop.png`
- `rally-marker-desktop.png`

The Deselect, Select, and Home controls are visually distinct, enclosed, and separated at small and desktop sizes. Deselect disappears after clearing the selection, leaving Select and Home aligned in their existing positions. Army roster tabs and cards fit their panel. Rally instructions, SET FLAG, MOVE FLAG, FIND FLAG, RALLY THIS, USE DEFAULT, and AUTO MINE are legible and enclosed. The focused army flag remains visible in the world at small and desktop sizes.

`rally-override-small.png` shows the noninteractive `LOCAL RALLY` world caption clipped at the right screen edge. The amber flag itself is visible, and the Jobs panel buttons remain fully visible and readable. This is the known world-marker caption edge behavior, not HUD button clipping.

Also reviewed these fresh tutorial captures after their verification records were written:

- `train-portrait-small.png`
- `rally-chain-phone.png`
- `scout-chain-desktop.png`
- `offscreen-recovery-small-before-show.png`
- `offscreen-recovery-small.png`

Tutorial cards, copy, arrows, and highlighted targets are readable and do not overlap the relevant controls. The train arrow points inside the Drudge portrait. The rally and scout arrows terminate on clear world targets. Offscreen recovery presents an unobstructed SHOW button before focus, then places the Drudge target and card in separate clear areas after focus.

No material visual issue was found. This is visual and geometry-report evidence from the listed Mac renderer captures; it is not physical-device or interactive-touch proof.
