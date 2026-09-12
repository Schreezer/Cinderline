# Terra Cinderline skirmish attempt, 2026-09-11

**Result:** blocked before meaningful player gameplay; this is **not** a completed skirmish or balance result.

- Started an ordinary Shattered Rift match from the normal menu with `Return`. The live HUD showed five Drudges and ore rose from 500 to 1,256 by match time 00:55, while the starting workers harvested normally.
- Confirmed keyboard camera controls: Right-arrow panned, Space returned home, and Escape opened the paused-skirmish menu. No save was made.
- CUA absolute left-click on a visible Drudge moved the rendered cursor but did not select it; the contextual HUD remained “Tap a unit or structure.” A CUA left-drag box across the nearby Drudges likewise produced no selection. The paused **MAIN MENU** pointer control also did not respond.
- Without selection, there was no way to issue worker mining/build commands, place/pause/resume a Kiln, train an Ember, command an army, expand, or play to results/rematch. I stopped rather than let an unattended AI victory count as a playtest.
- `restartlevel` was used only to discard the transient paused attempt; it produced a fresh 00:00 match rather than the menu. `cinder.visualpreview reset` was then used only as cleanup (no preview scene was used) and visibly restored the normal Shattered Rift menu.

These failures are consistent with the documented Mac issue involving synthetic pointer events and Unreal's cached mouse position. This attempt did not independently instrument the cached coordinates. This attempt does not show that a physical mouse is broken; previous physical-mouse Start confirmation remains separate evidence. A hands-on human match still requires a physical pointer source that Unreal accepts.
