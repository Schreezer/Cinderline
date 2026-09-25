# Authored map redesign

Updated: 2026-09-22. Status: first authored Shattered Rift layout implemented and desktop verified. Gameplay checks, physical-iOS compilation, and final cook/sign/archive pass. Further environmental art, other maps/variants and device verification remain open. This follows the completed desktop terrain-art pass in `TERRAIN_POLISH_TRACKER.md`.

## Why another pass is needed

The new fractured cliffs, materials, planting and motion improve individual assets. The battlefield still reads as a broad uniform floor with isolated barriers. The user's two supplied StarCraft reference images show the next missing layer: deliberately shaped base districts, expansion pockets, connected fighting spaces, strong surface boundaries and a dense surrounding world.

Observations from the supplied images (visual reference, not a specification of StarCraft mechanics):

- The overview composes large base terraces and smaller resource pockets around several routes. Broad, quiet fighting surfaces contrast with detailed edges.
- Terrain boundaries enclose spaces. Ramps and openings explain how spaces connect; repeated rock props alone do not establish that structure.
- Paving, darker roads, pale ground, planting and shoreline separate districts at a glance. Most environmental detail sits outside the usable building and fighting areas.
- Resources form curved lines around an obvious base location. Nearby ground treatment and structures make each economy site feel intentional.
- The close-up shows buildings grounded with connected paving, worn soil and shadows. This composition must also work at the ordinary gameplay camera, not only in an overhead overview.

## Constraints recorded before implementation

- `Source/Cinderline/Private/Sim/Simulation.cpp:121`: the three duel maps use 4, 5 and 6 impassable rectangles. Their spawn and resource coordinates are shared (`:142`). Four-player layouts use 4, 5 and 8 rectangles.
- `Source/Cinderline/Public/Sim/Simulation.h:41`: gameplay positions and obstacle geometry are two-dimensional. There are no shared plateau, ramp, terrain-level or buildable-surface definitions.
- `Source/Cinderline/Private/Sim/SimulationJobs.cpp:118` and `Navigation.cpp:453`: movement uses deterministic custom XY navigation, not Unreal Landscape collision or NavMesh.
- `Source/Cinderline/Private/Sim/Simulation.cpp:1243`: direct ground fire checks obstacle boxes. `:1262`: vision reveals radius discs without terrain occlusion. Height-aware vision is new work.
- `Source/Cinderline/Private/Sim/Simulation.cpp:246`: placement checks visibility and occupancy; it does not enforce terrain slope or plateau membership.
- `Source/Cinderline/Private/Presentation/CinderLandscapeTerrain.cpp:341` and its public header `:164`: road guides and canonical geometry also exist in presentation. A redesign must remove these duplicated sources of truth.
- `Source/Cinderline/Public/Sim/MatchLength.h:40`: Short and Long scale the map while unit sizes stay fixed. Choke widths and building room need explicit validation for each size.
- `Source/Cinderline/Private/Sim/Network.cpp:562`: network map reconstruction must be extended or tied to a checked map revision. Existing saves, hashes and snapshots also need compatibility decisions.

These are the pre-implementation constraints, retained to explain the redesign. The shared map definition, revision-aware save/network formats, placement rules and height-aware vision described below now replace those original limitations; line references have moved.

## First map: Shattered Rift, two players, Standard

Use the existing original frontier geology with an abandoned industrial excavation theme. Keep the present map identity while replacing its layout. The attached references guide spatial organization; do not copy their exact map or assets.

Proposed spatial structure:

1. Two diagonally opposed main bases on broad raised terraces. Each needs useful production/building space, a curved home ore line behind the headquarters, and one clearly readable ground exit.
2. One nearby expansion per player, reached through the main exit. A recognizable pocket makes the first expansion legible without making it invulnerable.
3. One more exposed expansion per player near a flank. This creates a meaningful choice between economy and territorial control.
4. A broad central basin for army movement and fighting, with two offset cliff shoulders to shape approach angles. Avoid a single tiny central passage.
5. Two longer flank routes. They reconnect to the opposing expansion approaches; avoid decorative dead-end roads.
6. A nonplayable outer belt of broken cliff, sparse industrial remnants, plants and a few focal landmarks. A dry ravine is the initial boundary treatment; water can be a later art option.

Keep six economy sites and existing total ore initially. Reposition deposits into deliberate arcs and verify worker throughput before changing resource counts or introducing another resource type. Preserve rotational equivalence in gameplay geometry; vary decorative dressing without affecting routes or visibility.

Surface hierarchy: paved main pads and ramp thresholds; compacted route surfaces; quieter soil in fighting areas; gravel/vegetation at boundaries. Give the two sides recognizable landmarks. Use concentrated detail at edges and infrastructure junctions, with clean silhouettes in movement lanes.

## Ordered work and acceptance

| Stage | State | Deliverable | Acceptance |
| --- | --- | --- | --- |
| M0. Reference and architecture review | Done | Current limitations, original map concept, implementation order | Source audit and user-provided image comparison recorded here |
| M1. Shared map definition | Done; portable and Unreal verified | Portable map record for bounds, spawns, resource sites, collision, buildable regions, terrain regions, route guides and revision | Simulation and presentation consume the same data; migrating existing maps preserves their behavior; network/save revision policy is explicit |
| M2. Playable layout blockout | Done; route/economy checks pass | Standard duel with the six sites, base pockets, center and two flanks | Mirrored route lengths and ore access checked; largest units can traverse every intended route; headquarters and production fit; no unintended access through barriers |
| M3. Plateaus and ramps | Done; simulation and presentation verified | Shared height regions, ramp openings and consistent collision/buildability/visibility rules | Ground crosses cliffs only at ramps; units, buildings, selection and effects agree on height; ramp placement is rejected; high-ground visibility behavior is explicitly implemented and tested |
| M4. Ground and infrastructure | Done for first map; desktop and phone-proportion rendering verified | Ground regions, retaining walls, ramp kit, ore aprons, road edges and base wear generated from authored data | A base, expansion, flank and center are visually distinct at the normal camera and phone proportions; art boundaries agree with movement and placement |
| M5. Surrounding world and local composition | First map pass implemented; richer landmarks remain | Perimeter ravines, industrial landmarks, clustered vegetation, rubble and restrained ambient movement | Map reads as a place; lanes and resources remain clear; fog hides unrevealed content; instancing/LOD and actual frame cost reviewed |
| M6. Gameplay and visual acceptance | Desktop checks/captures complete; physical verification pending | Overview, normal-camera base/expansion/battle views, motion, portable and Unreal checks, device capture/profile | Route/economy symmetry, traffic, formation movement, attacks, height/fog, placement and determinism pass; genuine phone evidence recorded separately |
| M7. Other maps and match variants | Pending | Individually designed identities for the other maps; explicit Short/Long and four-player layouts | No blind scaling of chokes; repeat clearance/economy checks and visual review for each supported configuration |

M2 can use compound rectangles for an early honest blockout. If final diagonal or curved boundaries are needed, introduce common polygon/edge predicates used by movement, fire and placement before presenting those boundaries as playable geometry. Do not hide large rectangular collision behind a contradictory silhouette.

For M3, movement may remain deterministic XY with authored height sampled for presentation. Elevation still needs authoritative connectivity and visibility rules. The initial rule proposal is that low ground cannot reveal upper terrain without an allied source on that level or air vision; ramp boundaries must behave continuously. Confirm the chosen rule in implementation notes before adding tests. No unrequested combat damage bonus is assumed.

At each stage record: implemented, desktop visually verified, and device verified separately. Capture one representative main-base-to-expansion sector before expanding art across the map. Preserve campaign/custom-map fallbacks until migrated deliberately. Keep Unreal build/import/capture processes serialized.

## Work log

- 2026-09-22: Compared the two supplied reference images with the latest map 0 gameplay capture. Completed read-only map/topology audit. Added this next-pass plan and linked it from the terrain tracker. No gameplay coordinates, assets, builds, installed apps or devices changed during this planning step.

- Implementation: revision1 changes Standard two-player Shattered Rift; revision0 reconstructs legacy layouts. Save format15 carries map revision; network protocol12 explicitly rejects protocol11 peers and forged authored geometry. Client, service and worker must update together before online deployment. No deployment performed.
- Focused portable evidence: shared geometry symmetry/ore/buildable pads, heavy-unit routes, sealed-ramp isolation, height visibility, save/replay and network roundtrips pass. Existing fixed-coordinate regression fixtures retain explicit revision0 where their test subject is unrelated to this new layout.

- First Unreal editor build and material/Landscape import passed. First gameplay capture exposes overly pale paving, oversized slabs and overlapping road geometry; rejected as final art. Refinement darkens concrete, reduces panel scale, removes duplicate floor/route meshes and adds contained wall details.
- Complete portable regression: 30/30 CTest suites pass; real-worker Node/LAN/bridge 37/37 pass. Independent review found and assigned campaign checkpoint v15 compatibility, terrain-aware pointer projection and missing-bake runtime terrain fallback.

- Final layout refinement adds four mirrored rock backdrops around natural/third ore pockets and two compact paved natural pads. All 20 ore-node access/return routes and completed-expansion collection approaches pass deterministic navigation checks; ore reserves, resource positions and main/flank guides stay fixed.
- Review fixes implemented: campaign checkpoints explicitly migrate simulation v14/revision0 and save v15; pointer projection follows the last-presented fog-flattened terrain height; stale/missing authored Landscape data uses a cached runtime height mesh. Snapshot application avoids constructing and discarding a full terrain/fog cache each tick.
- Final editor build and import/rebake pass (`build-final.log`, `import-final.log`). Final automation, captures and phone-target compilation are being verified. The phone is currently disconnected; no new authored-map hardware visual/performance evidence is claimed.

- Fresh final portable rebuild: 30/30 CTest suites and 37/37 real-worker Node/LAN/bridge checks pass (`portable-final-build.log`, `portable-final-ctest.log`, `node-final-tests.log`).
- First final Unreal regression: all 63 selected tests report Success, but one unrelated editor Home Panel HTTP timeout prevents strict clean-report acceptance. Source-verified per-run startup override `-ini:Engine:[ConsoleVariables]:HomeScreen.EnableHomeScreen=False` avoids constructing that unrelated panel; warnings remain enabled. Re-run follows the camera correction.
- Rendered fallback capture verifies one runtime height mesh and zero visible Landscapes/flat planes, with 3,150 unknown fog cells preserved. Its rendered lifecycle assertions pass but expose unnecessary collision cooking; fixing that before clean acceptance.
- Visual review rejected the distant sector inspection camera. It also exposed a genuine gameplay issue: camera focus remained at Z=0 above a 180 cm terrace. Terrain-aware focus/bounds and ordinary-zoom capture are being corrected before completion.

- Additional camera acceptance now checks the actual 250×250×185 cm Anchor bounds at desktop and phone aspect ratios, closest/default/farthest requested zooms, elevated map-edge bounds, panning down a ramp, unknown fog and flat fallback. Home/selection framing must keep the building roof in view, not merely its ground point.
- Latest device discovery (`devices-final.json`) still reports Aeon disconnected. The pre-existing installed terrain-art build is not evidence for the authored map changes.

- Render-enabled fallback lifecycle is now strictly clean: 1/1 Success, zero warnings/errors (`unreal-fallback-accepted/index.json`). Runtime mesh collision data is never cooked, CPU-access copies stay disabled, and the test verifies no simple/triangle shapes or cooked payload after explicitly visiting physics creation.
- Updated full regression has 62 clean successes; only the new elevated-camera framing assertions fail. The subject must fit inside both the real map boundary and the safe viewport. Correcting focus translation by intersecting these constraints, while keeping the existing boundary allowance and roof assertions unchanged.
- Regression scope is explicit: the 63-test manifest excludes two tests requiring an unconfigured external Unreal transport server and the pre-existing `Tutorial.EarlyActionsAndContent` mismatch (Construction now has four help sections; that unrelated test still permits three). Full portable and real-worker protocol checks are recorded above. No unrelated help content or test assertion was changed to make this pass green.

- Final editor build succeeds (`build-accepted.log`). Strict full selected regression now passes **63/63 expected Unreal tests with zero errors, warnings or unfinished tests** (`unreal-verified/index.json`). This includes campaign migration, fog-aware picking, authored scenery, runtime-fallback geometry/lifecycle, and six elevated-home camera framing cases after 30 normal camera ticks.
- Focus framing uses the closest feasible in-bounds camera position. Only explicit full-building focus may use the existing 360 cm maximum edge allowance when the normal scaled margin cannot fit the subject; ordinary panning retains its prior limit. No test or world-edge maximum was relaxed.

- Historical motion diagnosis, superseded by the ramp visibility follow-up below: the navy patch was a real 2→1 explored-fog transition rather than mesh corruption, but the transition itself was incorrect while the adjacent Anchor remained within sight range. The prior replay had 3,150 unknown / 0 explored / 946 visible cells at tick44 and 3,149 / 13 / 934 at tick104 (`ramp-fog-repro.log`). The material improvement retained 28% dim detail on remembered authored paving and static infrastructure; it did not address this visibility-rule defect.
- Historical precision defect, now addressed by the follow-up: the maximum surface height within each 75 cm fog cell could leave a descending unit's own ramp cell explored. Treating ramp approaches as their lower vision level fixes the safe ramp interior while retaining actual upper-terrace privacy.
- Physical-device ARM64 iOS compilation passes (`build-ios.log`, 124 actions, 310 seconds). First cook/sign/archive also passes (`package-ios.log`); final explored-detail materials will be recooked before the package is considered current. The device remains disconnected, so no install, physical image or performance claim is made for this map pass.

## Current delivery and evidence

Select **Shattered Rift / Standard / two players** for the authored map. Other sizes, four-player matches, other maps and existing campaign layouts retain their prior layout until deliberately migrated.

Implemented and verified for this first map:

- Two 180 cm main terraces with controlled ramp exits, six mirrored economy sites and unchanged 70,400 total ore.
- Central army space, two flank routes, 20 shared collision obstacles, expansion back cliffs and compact natural pads.
- Shared simulation/rendering geometry, height-aware vision and buildability, height-correct picking, selection/Home camera framing and a runtime mesh when the baked Landscape cannot bind.
- Worn paved districts, retaining walls, ramp thresholds, wall infrastructure, planted edges and fog-safe remembered paving.
- Save v15 / map revision1 compatibility, legacy v14 / revision0 migration, protocol12 roundtrips and explicit rejection of mismatched authored geometry.

| Evidence | Result | Artifact under `artifacts/map-redesign/` |
| --- | --- | --- |
| Portable regression | 30/30 CTest suites pass | `portable-final-ctest.log` |
| Real-worker Node/LAN/bridge | 37/37 pass, zero skips | `node-final-tests.log` |
| Selected Unreal regression | 63/63, zero errors/warnings/unfinished | `unreal-verified/index.json` |
| Render-enabled fallback lifecycle | 1/1, zero errors/warnings | `unreal-fallback-accepted/index.json` |
| Runtime fallback image | One runtime relief mesh; no Landscape or flat planes | `verified-visual/run-20260922T152512Z-IsB3Ld/sectorfallback.png` |
| Overview | Final geometry and fully explored layout | `final-visual/run-20260922T145641Z-wDVslt/map0.png` |
| Real gameplay camera | Main exit and Home-action framing | `verified-visual/run-20260922T152512Z-IsB3Ld/sector.png`, `sectorbase.png` |
| Visibility frontier | Ordinary vision, no global reveal | `verified-fog/run-20260922T152855Z-XTMfPI/fog.png` |
| Phone-proportion Home view | Entire Anchor clear of HUD at 1170×540 | `verified-phone/run-20260922T153007Z-OpVTyQ/sectorbase.png` |
| Final motion | 90 verified frames; 1170×540, 3 seconds, encoded at 30 FPS | `final-motion/run-20260922T154951Z-7ouKu9/motion.mp4` and `manifest.json` |
| Material import | Ground v5.2 and architecture v2.1 pass | `import-explored-detail.log` |
| iOS ARM64 compile | 124 actions; succeeded | `build-ios.log` |

The final encoded opening, middle and ending frames were visually inspected independently. Explored ramp shading preserves its seams and edges; no blocking issue remains in those sampled frames. The 90-frame capture validates motion/content and encoding, **not real-time performance**. Earlier stills use a paused inspection fixture; the live clip is the representative settled unit-animation view. The final material change affects remembered authored surfaces only, so fully visible stills and the prior geometry/fallback evidence remain applicable.

## Work still open, in order

1. **M5 environmental art:** reduce paving/border repetition, introduce distinctive peripheral industrial landmarks, and give expansions/central approaches more localized surface breakup and planting. The broad quiet spaces are playable and readable, but the reference still has much richer environmental composition. SC2/Clash production parity is not established.
2. **M7 map coverage:** design Glass Basin and Iron Reach individually; explicitly tune Short/Long and four-player authored layouts instead of scaling narrow passages blindly. Repeat economic symmetry, heavy-unit access and visual checks for each.
3. **M6 hardware acceptance:** connect and unlock Aeon, install a newly verified archive for this map pass, capture the live game and measure foreground frame cost. The older installed terrain-art build does not verify these authored-map changes.
4. **Online rollout:** client, Node service and native worker must move together to protocol12. Local matching-worker tests pass; no service deployment has been performed.
5. **Fog boundary refinement:** mixed cells containing both the ramp crest and actual upper terrace still conservatively require upper-ground vision. Safe ramp-interior visibility is addressed in the follow-up below.

- Final iOS recook/sign/archive passes in108 seconds (`package-ios-final.log`). Fresh archive verification independently confirms strict signing, IOS platform, matching raw/archive ARM64 UUID and cooked IOStore containers. Current local archive build is `56702186.0.37`; proof is `package-verification.json`. The app has **not** been installed or run on the disconnected phone for this pass.
- Final motion material revision is accepted after independent opening/middle/end encoded-frame review: the known ramp retains its panel detail through fog, with stable HUD framing. These are sampled visual checks plus full90-frame file/encoding validation, not an assertion about device FPS.

## Ramp visibility follow-up — 2026-09-22

Status: the ramp visibility fix is complete and verified in portable tests, Unreal and the rendered patrol; the updated local iOS archive is verified. Physical-device installation and performance checks remain pending because Aeon is disconnected. The user correctly noted that the adjacent lowland Anchor is within sight range of the ramp foot. The old rule compared every ramp cell's maximum surface height against the observer's foundation height, incorrectly treating a gentle slope as a succession of high-ground barriers.

- [x] Reproduce the adjacent-building failure and descending-unit visibility holes with tests that fail on the prior implementation.
- [x] Classify whole fog-cell coverage: a traversable ramp inherits its lower vision level; any actual upper-terrace portion retains the higher requirement.
- [x] Verify both ramps, adjacent building sight, moving units, range limits, mixed-cell privacy, air vision and save/replay behavior.
- [x] Run portable/Unreal regressions and recapture the same patrol with the existing fog materials.
- [x] Update the local iOS archive and record device status separately.

This fix changes the visibility rule without changing texture brightness, ordinary sight ranges, geometry or network/save formats. Actual upper-terrace coverage keeps its height requirement. Evidence for this follow-up belongs under `artifacts/ramp-visibility-fix/`; the prior first-map evidence remains historical.

Focused before/after proof: all three new regression groups fail against the preserved prior implementation and pass after the fix. The exact patrol replay keeps all 13 formerly darkened cells visible: tick104 now has 3,149 unknown / 0 explored-only / 947 visible cells. No own-unit cell fades at ticks44–104. Isolated Anchor tests verify the building reveals both ramp orientations while a nearby actual upper-plateau point within its sight radius remains hidden. Independent read-only review found no actionable issue in the geometric classifier or cache. See `portable-evidence.md` and `portable-evidence.json` in the follow-up artifact directory.

The editor behavior build succeeds (`build-editor.log`). All 30 portable suites and all 37 real-worker Node checks have passing results. The first concurrent run had two AI-suite timeouts and four Node timeouts while the editor was compiling; all six passed unchanged when retried in isolation. The original limits and assertions were retained. Both initial runs and isolated retries are preserved as `portable-ctest.log`, `portable-failed-isolation.log`, `node-tests.log`, and `node-failed-isolation.log`.

The selected Unreal regression passes strict validation: **63/63 expected tests, zero errors, warnings or unfinished tests** (`unreal/index.json`, same explicit manifest/scope as the first-map pass). This covers authoritative fog integration, terrain picking, camera framing, authored geometry/fallback, campaign compatibility and ordinary game controls.

The fresh 90-frame, 1170×540 patrol capture passes file/encoding validation (`motion/run-20260922T163035Z-kWdRmM/motion.mp4`). Opening, middle and ending frames decoded from that MP4 were inspected twice, including an independent review. The ramp foot remains lit after both patrol units leave, with readable paving and ordinary wall/unit shadows; the former broad dark patch is absent. This is desktop rendering at phone proportions, not physical-device or real-time performance proof. Consolidated evidence is `verification.json`.

Physical-device ARM64 compilation passes (124 actions, 318 seconds), followed by successful cook/sign/archive (82 seconds). Fresh archive verification confirms strict code signing, IOS platform, matching raw/archive ARM64 UUID and both cooked IOStore containers for build **56702186.0.38** (`package-verification.json`). Final device discovery still reports Aeon disconnected (`devices-final.json`), so this archive has not been installed or visually/performance-tested on the phone.
