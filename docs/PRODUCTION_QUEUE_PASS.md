# Larger production queues

The player asked to queue more than three army units at a time. The simulation accepted eight entries, but the desktop HUD displayed only the first three and offered no way to inspect later entries.

## Behavior

- Each production building accepts up to 20 entries, including its unit and research orders. A shared simulation constant controls command admission, save loading and the displayed limit.
- The desktop queue shows six entries per page; compact layouts show eight. The header displays the total count and visible range. Arrow buttons reach every queued entry, and clicking an entry cancels that exact position.
- Selecting another producer returns to its first page. Cancelling or completing entries clamps the page to the remaining queue.
- Ore is paid and crew capacity reserved when an order is added. Units still finish in order. Cancelling an unstarted entry refunds its cost; cancelling production already in progress refunds the unused portion.
- Full queues produce an explicit rejection message. Resource, capacity, technology and producer requirements continue to apply.
- Existing saves remain readable. The AI retains its separate two-order production policy.

## Verification

- [x] Focused simulation regression reaches 20 paid orders, verifies exact aggregate cost and crew reservation, rejects the twenty-first without charging, cancels late and active entries, checks production order, and round-trips a full queue through a save. All three tests matching `production` passed, including the existing payment/supply and AI composition checks.
- [x] Mac Unreal Development build passed in 34.93 seconds with at most two compilation actions in parallel.
- [x] All four headless Unreal integration checks passed with zero errors, warnings or unfinished tests.
- [ ] Physical queue navigation and cancellation on desktop and compact layouts.

The game remains closed following the player's fan complaint. The updated module is ready for the next launch. Live rendering and heat tests were outside this pass.

The queue HUD received a source review for pagination, absolute cancellation indices and the 1280×720 / 667×375 layouts. The queue sheet now falls back to contextual actions when selection moves to a unit or unfinished building. This does not substitute for the outstanding physical UI check.

Build output, portable results, engine integration results and a source hash manifest are saved in [artifacts/production-queue](../artifacts/production-queue/).
