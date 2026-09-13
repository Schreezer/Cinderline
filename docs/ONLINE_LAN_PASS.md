# Online reliability and LAN hosting

Status: the backend, LAN host and client integration are implemented and locally verified. The combined evidence is in [the verification record](../artifacts/online-lan/verification.json).

## Delivered backend behavior

The default `npm start` path remains an online server bound to loopback for a TLS WebSocket proxy. It never advertises through Bonjour. `npm run start:lan` is an explicit local-network mode that binds IPv4 `0.0.0.0`, reports usable private IPv4 WebSocket URLs, and advertises `_cinderline._tcp` on the actual listener port. IPv6 records stay disabled because this listener is IPv4-only.

The Bonjour record carries protocol 6, `/play`, and a DNS-safe discovery name. Its instance and TXT name replace dots and truncate on a character boundary at 63 UTF-8 bytes; `/info` retains the configured public server name. `bonjour-service` 1.4.4 is pinned in the lockfile. The package is pure JavaScript, its current upstream uses current Node type definitions, and the focused suite passes under Node 22.23.2. Advertising failures leave the server running with manual addresses and an actionable warning. Signal shutdown withdraws the advertisement before closing the server.

The entry point validates network mode, name, port, drain interval, exact trusted-proxy IPs, exact allowed origins and bounded admission/worker settings. It forwards these controls to the authoritative server. Forwarded client addresses count only when the immediate peer is explicitly trusted; the default trusts no proxy.

The Docker/Caddy example keeps the game listener on Linux host loopback, trusts only local Caddy, uses `/readyz` for upstream and container readiness, caps CPU, memory, processes and active workers, and gives the server a bounded drain interval. Draining rejects new rooms while existing players can reconnect until the deadline. Restarting the process still loses all in-memory rooms.

## Verification

- The final Mac build passes in 28.54 seconds. The default `build/CinderlineMatchWorker` was rebuilt with protocol 6 and produced valid seat snapshots; see [the worker receipt](../artifacts/online-lan/default-worker.json) and [build log](../artifacts/online-lan/mac-build-verified.log).
- All 22 Node tests pass: six LAN/configuration tests under Node 22 and sixteen authoritative server tests. Coverage includes private address filtering, unreachable IPv6 exclusion, UTF-8-safe Bonjour names, actual-port TXT data, optional advertising failure, readiness drain, admission and worker bounds, slow clients, exact Origins, and ignored forwarding-header spoofing from untrusted peers.
- A real protocol 6 socket scenario passes with two clients on one Mac, one through loopback and one through the Mac's `192.168.31.109` private interface. Create, join, ready, private fog snapshots, opaque seat handles, command authority, reconnect and canonical forfeit all passed; the measured worker cadence was 19.33 Hz against the configured 20 Hz.
- Unreal passes the private-IP transport test, the exact live Bonjour service and port test with discovery-stop cleanup, and the rebuilt endpoint-policy and recovery tests. The prior stock-engine Online group also passes; its discovery case had no live service and is not counted again as Bonjour proof.
- Five Mac-rendered multiplayer panel cases and ten inspected screenshots pass at Internet/LAN 956 x 440, Internet/LAN 667 x 375, and Internet 1920 x 1080. Required labels and actions appear across the initial and second scroll positions, the smallest action is 44 px high, the header and Back action remain visible, and no overlap audit failed. These are Mac renderer results, not physical touch evidence.
- The server stopped cleanly after `SIGTERM`, and the test harness restored the tracked preferences and saves byte-for-byte.

The live Bonjour lane used the Mac's real multicast service and resolved the exact advertised record. The two clients still ran on the same Mac, so the result does not prove discovery through another router, a second computer or a physical iPhone.

## Remaining work

No public endpoint was provisioned or deployed. The Docker/Caddy example was not exercised on a remote host. No phone hosted the server, and no iOS package was built or installed for this pass. A LAN match between two physical devices, including local-network permission, discovery, touch and gameplay, remains unverified on AEON, as do device performance and thermals.
