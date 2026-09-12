# Online multiplayer pass

Started 12 September 2026 at the player's request. The first online mode is a private, invite-code 1v1 skirmish using the existing faction and maps.

## Acceptance

- [x] Dedicated authoritative simulation using the same portable C++ rules.
- [x] Versioned, bounded commands and per-player filtered snapshots, with no hidden enemy economy/orders or fog sent to opponents.
- [x] Two-seat room creation/join, ready synchronization and server-selected teams.
- [x] Unreal connection/lobby UI with editable endpoint, name and room code.
- [x] Gameplay sends intent; replicas cannot run local rules or write online state to the offline save.
- [x] Reconnect tokens, command deduplication, explicit leave/forfeit, disconnect grace and authoritative results.
- [x] Client-only menu/help overlay explains that online play continues.
- [x] Latency/stale-connection feedback and bounded network buffers.
- [x] Repeatable two-client network tests, native simulation regression, Unreal build and rendered UI checks.
- [x] Deployment package, TLS proxy setup and exact local/public validation evidence.

## Architecture

A small Node WebSocket service owns private rooms and seat credentials. Each active room owns an isolated C++ match worker linked to the existing Simulation library. The worker validates intent and emits compact binary views at 10 Hz while rules advance at 20 Hz. The transport never decides costs, movement, combat or victory. Unreal uses its built-in WebSockets client and a passive replica of its own perspective. Visual assets and animation are local.

Both clients use the existing local team-zero presentation. The server normalizes each view so its recipient is team zero, with stable opaque entity handles specific to that seat. Enemy queues, resources, orders, targets, paths, hidden units and opponent fog are not transmitted. Lobby controls use small JSON messages; gameplay uses a shared binary codec. The transport binds every command to the authenticated seat and never accepts a client-provided team or clock.

The first release supports invite rooms without accounts or ranked matchmaking. Reconnect credentials remain in memory for the running app session. Room/server restarts end active matches. Public hosting must be configured before persistent Internet play is claimed.

## Coordination contract

Shared codec: `Source/Cinderline/Public/Sim/Network.h` and `Private/Sim/Network.cpp`. Snapshot and command encoding is explicit little endian, versioned and bounded. Commands do not carry player identity; the trusted server assigns it.

Worker IPC is a four-byte little-endian length followed by an opcode and payload. Input: 1 step (u32 count, max 5), 2 command (u8 seat + encoded command), 3 forfeit (u8 seat), 4 snapshot request. Output: 128 ready, 129 snapshot (u8 seat + encoded snapshot), 130 acknowledgement (u8 seat + u32 sequence + u8 accepted + u16 UTF-8 byte count + message), 131 result (u8 winner). No worker IPC operation is exposed directly to clients. Worker argv accepts `--map` and `--seed`.

Public WebSocket path `/play`. JSON client messages: `{type:"create",version:1,name,map}`, `{type:"join",version:1,name,room}`, `{type:"reconnect",version:1,room,token}`, `{type:"ready",ready:true}`, `{type:"leave"}`, `{type:"surrender"}`, `{type:"ping",nonce}`. Binary messages are encoded commands. JSON replies: `welcome` (room,token,team), `lobby` (room,map,players:[{name,connected,ready}],state), `started`, `ack` (seq,accepted,message), `peer` (connected,graceSeconds), `result` (winner canonical 0/1,reason), `error` (message), `pong` (nonce). Snapshot bodies are the unwrapped shared codec. Finished rooms retain results for reconnect during a bounded expiry. Hosting contract and tests can add documented fields, but do not change existing fields silently.

## Player behavior

Open ONLINE 1V1 or press O from the menu. Both clients use the same server address; one creates a room and shares its six-character code. Both players ready up. Three existing maps and the full existing faction/economy/technology/combat rules are available.

The Slate lobby supports typing, keyboard navigation, Escape, native DPI sizing and scrolling on compact screens. Nonsecret endpoint and display name preferences are stored separately from the offline save. Room credentials are never copied with the invite or written to preferences.

Gameplay sends intent to the server and displays its acknowledgement. Movement interpolates between snapshots; selection and health indicators follow the displayed positions. Missing/stale snapshots block new orders and show a connection notice. The server never receives a client clock or team selection.

Pause/help/connection screens are local overlays. The server keeps playing and the client still receives state. Surrender waits for the server result; Leave confirms the forfeit and drains its control message before closing. A 60-second reconnect window applies while the other player remains connected. Both clients disappearing causes room cleanup. Completed results remain available for five minutes unless the server restarts.

## Evidence

- `native-tests.log`: all four CTests passed, covering 27 simulation groups, five protocol groups, native smoke and 3,000 native render frames. The codec agent also passed its five groups under ASan/UBSan.
- `server-tests.log`: six real WebSocket/native-worker tests passed, including full ordinary attack-move victory, command ownership/deduplication, snapshot privacy, reconnects, expiry, malformed input and rate limits. `dependency-audit.log` reports zero known vulnerabilities.
- `local-unreal-report.json`: `Cinderline.Online.Transport` passed with no errors or warnings. Two separate GameInstances use real sockets; the host also runs the real Battlefield and PlayerController. It covers start, passive snapshots, command acknowledgements, paused updates, reconnect, protected offline actions, confirmed surrender, drained leave and return to offline play.
- `public-unreal-report.json`: the same expanded Unreal test passed through a temporary Cloudflare public **WSS** endpoint, with both clients taking the public TLS route. Duration 4.91 seconds for the test body. No errors; two warnings came from UE's existing iOS `idevice_id` helper reporting `Bad CPU type in executable`. TLS validation remained enabled.
- `offline-unreal-report.json`: all eight offline/tutorial Unreal tests passed without errors or warnings after integration.
- `unreal-build-final.log`: the final Mac Editor module builds. Earlier compile/fixture failures remain in the log history; the latest build is successful.
- Rendered checks exercised desktop room creation, waiting/ready state, online battlefield, menu/connection behavior and return to menu using keyboard input. A second Node client connected through public WSS while the rendered Unreal client used loopback. The later expanded headless test proves Unreal WSS separately.
- Desktop screenshots use a 2560x1440 framebuffer. Compact screenshots use 1334x750, corresponding to a 667x375 content area on this display. Previews were capped at 30 FPS, with no additional GPU benchmark or sustained latency claim.

Automated absolute pointer input remains unreliable in Unreal on this Mac, as recorded in prior passes. Keyboard and real transport checks are separate from physical mouse/touch acceptance. Final desktop and compact room creation succeeded. The lobby uses the existing HUD scale and scrolls focused controls into view. The temporary service reported zero rooms and zero connections after the final UI leave. Unreal, the test server and the temporary public tunnel were stopped after verification. Saved `Online.ini` was inspected: only quoted server URL and display name are persisted.

## Deployment and remaining checks

`Server/README.md` documents local use, Linux/Docker deployment, DNS and a Caddy TLS proxy. The build context includes only server and simulation source. The service has bounded rooms, connections, message rate, payloads and socket/worker buffers.

- Persistent public hosting and a stable domain are not configured. A temporary test tunnel does not provide a production service.
- Docker image execution remains unverified because the local Docker daemon is unavailable.
- Native macOS/Unreal testing does not prove an iOS build, touch keyboard or physical-device network lifecycle.
- Human-versus-human balance, sustained late-game latency/bandwidth, large room counts, accounts, public matchmaking and ranking remain later work.


## References

- [Unreal WebSockets API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/WebSockets/IWebSocket)
- [ws server API](https://github.com/websockets/ws/blob/master/doc/ws.md)
