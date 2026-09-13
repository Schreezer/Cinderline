# Match length presets

Status: implementation and local verification are complete. The [combined verification record](../artifacts/match-length/verification.json) binds the tested source, material assets, logs and rendered cases.

## Player choices

Solo skirmishes now offer three presets:

| Preset | World size | Home ore per node | Expansion ore per node | Build, train and research time | Intended play |
| --- | ---: | ---: | ---: | ---: | --- |
| Short | 3600 | 1500 | 2500 | 80% | Less travel and faster development |
| Standard | 4800 | 2500 | 4200 | 100% | The established map scale and pacing |
| Long | 6000 | 3750 | 6300 | 100% | More travel, room and finite ore |

Every map retains four home nodes per team and twelve neutral expansion nodes. Total finite ore is 42,000 for Short, 70,400 for Standard and 105,600 for Long. Movement, combat, unit radii, vision, costs and node counts stay the same. These presets change the conditions that shape a match; they do not guarantee a finish time or force every strategy into the same ordering.

## Implementation

The active preset supplies one authoritative world size to command validation, camera and map bounds, navigation, fog, building placement, starting positions, resource nodes, rally points and AI planning. Map obstacles, bases, expansion resources and authored AI landmarks scale from the 4800 Standard map. Starting workers retain legal local spacing on the smaller map.

Short applies its 0.8 duration scale to paid construction, training and research for both teams. It still supports a normal mining, construction, production and combat route to victory. Long adds distance and finite reserves without changing combat rules or granting information or resources.

Fog and ground materials use the runtime `CinderWorldSizeInverse` parameter. The four owned material assets compile without shader errors and keep `1 / 4800` as their Standard default. Standard continues to use the authored Unreal Landscape. Short and Long use correctly sized generated ground and modular scenery because this pass did not regenerate authored Landscapes for those sizes.

Save version 9 stores the preset. Versions 1 through 8 load as Standard, and invalid or future save data is rejected. Network protocol 6 carries the preset and validates snapshots against its bounds. Hosted online matches remain Standard; Short and Long are solo choices. A matching protocol 6 service has passed local tests but has not been publicly deployed.

## Verification

The frozen local pass records:

- all seven portable Release suites passing, including 5,368 match-length assertions;
- all nine map and preset combinations passing bounds, overlap, rotational symmetry, route, legal first-production-site and mirrored ore-delivery checks;
- a paid Short victory path with ordinary gathering, construction, six trained Embers and a cross-map attack;
- deterministic traversal proxies of 244, 326 and 408 ticks for Short, Standard and Long;
- manual and automatic Short construction, training and research timing checks;
- scaled Short AI reconnaissance and expansion orders that remain inside the active world;
- save, load, replay, legacy migration, snapshot and authoritative command checks;
- a successful Mac Unreal build, 33 Unreal tests, seven Node server tests and a native-client compile;
- eight fresh Mac renders covering the three menu choices, Short and Long map edges, small, phone and desktop layouts, and mocked left and right safe-area insets;
- runtime fog and ground material instances reporting the exact inverse for each active world size, with aligned fog, visible ground and black map edges in the reviewed captures; and
- unchanged player saves and preferences, with all owned Unreal processes closed after verification.

The native-client check compiled but did not launch a native client. This pass did not build or install an iOS package, run on AEON, test physical touch, measure device performance or thermals, or deploy protocol 6 to a public server. Human pacing and balance across complete matches still need playtesting.
