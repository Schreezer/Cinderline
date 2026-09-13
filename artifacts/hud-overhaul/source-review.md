# HUD overhaul source review

Reviewed 2026-09-13 against the current shared working tree. No important source issue was found in the bounded HUD overhaul.

Scope reviewed:

- `FCinderMobileHUDLayout` safe-area anchoring, resolution scaling, 44-unit targets, explicit gaps and open-world region;
- `ACinderHUD` compact resource chips, permanent global rail, bottom-left minimap, unit ribbon, selection identity, context actions, palettes, queue cancellation and feedback surfaces;
- tutorial button lookup through the unchanged target/action/argument contract and world-target occlusion checks;
- rounded Canvas surfaces and action glyph drawing in `CinderHUDStyle.cpp`;
- layout automation for phone, native-pixel phone, tablet and desktop cases.

The action dispatcher retains entity and job identifiers for pinned production and stable queue cancellation. Panels and buttons are added to UI hit regions, and reverse button traversal gives the last-drawn control precedence. Global and contextual commands continue through the existing controller actions; the overhaul does not add gameplay authority.

This is a source-only review. Rendered layout, native input, performance, physical touch and thermal behavior require separate evidence.
