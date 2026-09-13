# Tutorial control clarity

Requested 2026-09-13 after the player found FIND and MORE unclear on the Build a Kiln lesson.

FIND previously moved the camera without changing selection. After worker training, the Anchor could remain selected while the camera moved to a Drudge, leaving production controls visible. MORE expanded the tutorial description, recovery hint and progress.

## Changes

- The focus button names its exact action, such as SELECT DRUDGE, SELECT KILN or SELECT RESONATOR. Authored locations name the base, scout point, defense point or enemy base.
- Focus selects the friendly entity resolved by the lesson and clears the previous action drawer. It does not issue an order or spend resources. Location-only focus preserves selection and cannot select a hidden enemy.
- Automated camera centering does not complete the camera gesture lesson.
- DETAILS expands the lesson; HIDE collapses it. GUIDE opens the relevant field-guide topic. Compact titles wrap beside the controls, and labels retain enough width to stay readable.
- Phone-aspect Mac previews use touch instructions.

## Verification

- [x] Mac build and eight focused tutorial/controller/layout regressions. Exact actor selection, unfinished foundations, research focus, preserved selection at markers, unchanged command/cost state, real camera input gating and the end-to-end training match pass.
- [x] Ten fresh phone, small-phone and desktop renders, including the reported Kiln lesson and real HUD focus/details actions. Every capture was visually inspected; button bounds and overlap checks pass. Player save/preferences remained unchanged, and only the preview processes were closed.
- [x] SDK 27 ARM64 iOS compilation, Xcode assembly and fresh signed packaging. Strict signature, profile coverage/expiry, native IOS platform and cooked-container checks pass. The 71-action build took 202.79 seconds; packaging took 83 seconds.
- [ ] Physical-device touch acceptance. No device launch or installation is part of this pass.

The prepared package is `Saved/Packages/IOS/Cinderline.app`, executable SHA-256 `1390d92a603da62a72efae870cd2a362b09db5600bcb41ac5020d947b4189040`. It has not been installed or launched on AEON; the last verified installed executable remains `594f5a62...`. This package has not been run through the native iOS-on-Mac wrapper either. The captures establish Mac Unreal rendering at phone proportions.

See [verification](../artifacts/tutorial-controls/verification.json), [automated tests](../artifacts/tutorial-controls/automation.json), [the corrected Kiln selection](../artifacts/tutorial-controls/mac/kiln-focus-phone.png) and [expanded compact instructions](../artifacts/tutorial-controls/mac/kiln-details-small.png). The source snapshot and each capture's geometry report are retained alongside those files.

This focused fix does not implement the separately proposed global Build, Train and Research allocation system.
