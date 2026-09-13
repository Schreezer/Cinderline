# Four-player free-for-all

Status: implementation and local verification are complete. Native, Node, worker, Mac build, Unreal runtime and rendered visual checks pass.

## Player behavior

Cinderline now supports private two-player 1v1 and four-player free-for-all rooms through the same Internet and LAN flows. The host chooses the format before creating the room, while joiners inherit it. A four-player room exposes exactly four seats and cannot start with only two or three connected and ready players.

Each FFA player has an independent economy, army, fog view, faction colour and command authority. Losing every Anchor, surrendering, leaving, or reaching the disconnect timeout eliminates only that seat. Three-player and two-player survivor states continue normally. Eliminated players can stay connected to receive survivor counts and final results without gaining enemy vision, and a disconnected seat can reconnect with its token while the room remains available. Results use the original global seat: `0` through `3` for a winner and `-2` for a draw.

Solo AI remains 1v1. This pass adds free-for-all play rather than teams or 2v2 alliances.

## Server contract

A protocol 7 `create` message accepts optional `playerCount`; omission preserves the two-player default, and only `2` or `4` is valid. Welcome and lobby messages carry the selected count, and the lobby player array always has that exact length. The authoritative worker starts with `--players 2` or `--players 4`, waits for every initial private snapshot, and routes snapshots and acknowledgements only within the room's seat range.

Forfeits are ordered worker commands rather than server-authored outcomes. The worker reports a two-byte internal result containing the winner and the exact forfeit cause seat, allowing the public result to distinguish combat victory, surrender and disconnect without relying on output timing. Peer connection messages include the affected global team and go to every other connected seat.

Protocol 7 is intentionally incompatible with earlier client/server/worker combinations. Upgrade the game client, Node server and `CinderlineMatchWorker` together. The default server worker has been rebuilt and validated against protocol 7.

## Verification ledger

- All eight native portable suites pass, covering four-team simulation, map placement, deterministic elimination, save/replay behavior and protocol snapshots.
- All 27 Node tests pass against the fresh protocol 7 worker. The real four-client test verifies four-seat capacity and readiness, private snapshots and opaque handles, per-seat command ownership, eliminated-seat observation and reconnect, two nonfinal forfeits and the final global winner. Separate tests cover simultaneous disconnect grace, concurrent reconnects, invalid player counts and worker draw decoding. Evidence: [server-tests.log](../artifacts/four-player/server-tests.log).
- The default worker accepts four-player startup and emits valid protocol 7 snapshots. Evidence: [default-worker-verification.json](../artifacts/four-player/default-worker-verification.json).
- The native client compile passes. Evidence: [native-client-build.log](../artifacts/four-player/native-client-build.log).
- The final Mac Unreal build succeeds. Evidence: [mac-build-verified.log](../artifacts/four-player/mac-build-verified.log).
- All 40 Unreal tests pass. The runtime checks include a real four-client match on one Mac through its private LAN address, live Bonjour discovery, readiness, command ownership, private snapshots, reconnect after elimination, nonfinal forfeits and final result coherence. Evidence: [unreal-verification.json](../artifacts/four-player/unreal-verification.json).
- Four responsive Internet, LAN and four-seat lobby cases pass across phone and small viewports, producing eight images. Both four-seat lobby sizes were visually inspected. All four player cards remain visible, and the pinned Ready, Leave and Back controls retain at least 44 logical-unit touch targets. Evidence: [panel summary](../artifacts/four-player/panel-final/summary.json).
- A warmed 956 x 440 disposable battlefield fixture renders all four faction colours over visible terrain with zero shader jobs at capture. This image demonstrates presentation routing rather than a live multiplayer battle. Evidence: [battle verification](../artifacts/four-player/battle-verification.json) and [battle capture](../artifacts/four-player/battle.png).
- The combined receipt binds the source and module hashes, passing checks, reviewed renders, preserved preferences and saves, and closed owned processes: [verification.json](../artifacts/four-player/verification.json).

## Remaining work

No public protocol 7 Internet server was deployed or tested end to end over public WSS. No new iOS package was built or installed, and no AEON or four-physical-device match was run. Physical input, device performance, thermal behavior and full four-human balance remain unverified. Server rooms remain memory-backed and do not survive a backend restart.
