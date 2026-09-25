# Hard and Expert ore control

The previous AI could clear an enemy mining area and leave the remaining ore unused. Expansion selection considered four authored locations, productive-base targets stopped at two, and defensive units responded to raids without retaining a standing guard. Expert's separate raiding group looked for Headquarters, missing exposed workers and Siphons elsewhere.

## Changes

- Hard and Expert add currently visible ore patches to their expansion candidates. This includes cleared enemy starts and sites outside the authored expansion list. Expansion still requires enough observed ore, available funds and workers, and no known nearby enemy base or armed threat.
- Empty miners rebalance in batches of at most two, checked twice as often on Hard and Expert. Workers carrying ore finish delivery before a transfer. Worker targets scale with productive bases; Expert can pursue three productive bases and fund their workforce. The existing four-Headquarters ceiling remains.
- Ward construction prioritizes threatened and productive forward bases. Placement anchors sit between the base and its nearest mineral line. A planned Ward must be within 500 units of that anchor; distant placement cannot cause repeated purchases that never satisfy coverage.
- Up to two productive forward bases retain small guards: two soldiers on Hard, three on Expert, when enough troops are available. A full main launch group remains available. Nearby reserves are preferred, and distant troops already attacking are not pulled away to fill a sentry slot. Guards hold the mining line and fight nearby enemies through ordinary combat rules.
- Expert's three-unit raiding group also targets observed Siphons and recent clusters of exposed workers. Miner density increases target priority. It still avoids known costly garrisons, requires enough troops for the main assault, and suspends separate raids when its bases are visibly threatened.

These changes use normal commands, costs, construction times, supply and damage. Ore must be observed before it can justify a new claim. Worker reports expire after 20 seconds for harassment decisions. Saves retain the simulation tick, observations and ordinary guard/assault orders; no new save or network fields were added.

## Verification

`Tests/AIResourceControlTests.cpp` adds five regression scenarios. The exact same tests against the previous AI fail capture-to-income, standing guards, exposed-worker harassment, and Expert's third productive base; hidden-ore isolation already passes. All five pass with the revision.

The capture fixture starts at the end of a won fight, with one enemy base at one hit point. Ordinary weapons finish that base. The AI then uses a fixed setup bank and ordinary purchases to construct at a previously enemy-held ore patch outside all four authored expansion sites. Within the 175-second observation window:

| Difficulty | Completed claimed base | Completed Ward | Delivered ore from the captured patch |
| --- | --- | --- | ---: |
| Hard | Yes | Yes | 1,638 |
| Expert | Yes | Yes | 1,170 |

The fixture contains no other ore, so the income is attributable to the reclaimed site. This is an isolated regression fixture, not a naturally played full match or a comparison of the two difficulties' overall mining efficiency. Additional checks verify counterraid engagement, preserving a main attack group, save/load continuity, worker funding for the third productive base, hidden ore isolation, and avoiding guarded or unseen miners.

The [controlled challenge evidence](../artifacts/ai-ore-control/README.md) separately runs paid rush, economy and turtle opponents on all three standard maps. It compares identical common simulation sources and benchmark code, replacing only the AI implementation. These scripted trials do not establish human difficulty.

Final build, test, source-hash and packaging boundaries are recorded in [verification.json](../artifacts/ai-ore-control/verification.json). Existing mobile and Linux packages require a separate rebuild and installation.
