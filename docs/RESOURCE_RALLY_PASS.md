# Anchor resource rally

Updated 2026-09-15. Focused follow-up requested by the player; P2 remains deferred. Implementation, local checks and Android package verification pass. Physical device acceptance remains pending.

## Player controls

- Select an operational Anchor, choose **Rally** on its action bar, then tap an ore deposit. Future trained Drudges use that mine. Already trained workers keep their current orders.
- On desktop, right-click the ore with the Anchor selected.
- Plain-ground rally still sends new Drudges to the chosen position. If the chosen mine is depleted or unreachable, the existing ground-rally fallback remains available rather than silently choosing a different mine.
- Select the Anchor and choose **Orders > Auto Mine** or **Jobs > Auto Mine** to restore automatic reachable-ore allocation.

## Implementation

Production already recognized an explicit Anchor rally overlapping a deposit and assigned a paid newborn a reachable mining round trip. The missing controls now resolve the picked ore entity's position rather than its mesh's offset ground-ray intersection, and desktop resource context commands on buildings issue Rally instead of Gather. The selected producer's action bar exposes Rally directly; Anchor Orders exposes its worker rally and Auto Mine. The field guide documents these choices.

The existing coordinate-based rally representation, production retry limits, saved state and protocol remain unchanged: save 13 / protocol 10. This pass does not deploy a public backend or change the wire format.

## Verification

- Mac Unreal build and all **41 local Unreal checks** pass. `ProductionControls` exercises the picked-resource versus ground-coordinate distinction, newborn gathering, worker-order preservation, desktop context commands, and ordinary ground rally/deselection. Report: `Saved/Automation/Integration/20260915T104637Z-19855/index.json`.
- All **four targeted Release suites** pass: production exit, allocation, traffic, and network. Added cases cover exact resource assignment despite load balancing, paid deferred jobs, save/load continuation, changing the rally, ground-rally fallback and Auto Mine. Existing production coverage also checks that a resource-rallied worker harvests and delivers ore.
- Independent review found no actionable blocker. Resource IDs are not added to wire commands; existing opponent-rally privacy is preserved.
- Physical Android and iPhone acceptance are separate and remain pending.

- Two Mac compact-layout previews were reviewed. The producer panel and pinned Anchor Jobs remain legible; the latter shows Rally This, Auto Mine and the Worker Rally marker. These scripted ground-rally captures do not prove native touch or separately capture the new action-bar button.
- Android **0.1.2 / version code 3** is packaged at [Cinderline-Android-arm64.apk](</Volumes/Codex Storage/Cinderline/Android/packages/Cinderline-Android-arm64.apk>). Signature verification and 16 KB ZIP alignment pass, with the same signing certificate as 0.1.1. Package SHA-256: `dad37087f9fabaa080a63a417fbcee4b5b177c4edeeb3448bb41864e8040b48c`, size 274,457,418 bytes. Focused source hashes match the packaging workspace.

The [verification receipt](../artifacts/resource-rally/verification.json) records source, binary, test and package evidence. The prior APK was preserved on the external SSD. No physical installation, iOS package update, public backend deployment, or P2 work was performed.
