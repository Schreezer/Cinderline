# Match-length render review

Reviewed all eight final Mac captures generated after the world-size material patch. The capture runner used real rendered menu, Start and minimap hit targets. Each map fixture adds one explicitly logged friendly Scout to reveal the far corner, then freezes the simulation; this is rendered layout and boundary evidence, not a human skirmish or physical touch test.

- The 667 × 375 Short menu and 956 × 440 Standard/Long menus expose all three length choices, all five difficulty choices and all three maps. Selected choices and descriptions are readable.
- Mocked left and right 62-pixel menu insets with a 21-pixel bottom inset keep every control inside the safe area. The backdrop fills the viewport.
- The 1920 × 1080 desktop menu contains the new row and all launch controls without overlap.
- Both Short and both Long map captures show terrain and natural entities in the revealed region, a bounded black exterior and aligned fog. Scripted Scout visibility is explicit in every map receipt.
- The live rendered FogV2 and CanyonGround materials report 1/3600 for Short and 1/6000 for Long. Long minimap input reaches (5580,5580), beyond the prior fixed 4800 limit.
- The runner verifies at least 44 logical units for compact hit targets, viewport containment, no button overlap, fresh PNGs, unchanged player files and an observed 30 FPS cap.

No important visual defect remains in these captures. Short and Long use generated ground with modular scenery; the authored Landscape assets remain Standard-only. These results do not establish iOS packaging, physical-device interaction, sustained thermal behavior, or guaranteed match duration. All owned Unreal processes were closed.
