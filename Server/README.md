# Cinderline game server

This Node service hosts private two-player 1v1 or four-player free-for-all rooms at `/play`. It has no accounts, public matchmaking, ranking or purchased service dependency. Each active room runs one authoritative `CinderlineMatchWorker` process built from the same simulation code as the game. Rooms, reconnect tokens and completed results stay in one server process and do not survive a restart.

## Run on one computer

The server requires Node 22 or newer. Build the worker from the repository root, then install the pinned dependencies:

```sh
cmake -S . -B build -DCINDERLINE_BUILD_NATIVE=OFF -DCINDERLINE_BUILD_SERVER=ON \
  -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build --target CinderlineMatchWorker
cd Server
npm ci
CINDERLINE_MATCH_WORKER=../build/CinderlineMatchWorker npm start
```

Keep `CINDERLINE_BUILD_SERVER=ON` explicit because an existing CMake cache can otherwise retain a disabled server target. This command configures a Release worker for hosting.

`npm start` is the online/proxy mode. It binds `127.0.0.1:8787` by default and does not publish a Bonjour service. Put a TLS WebSocket proxy such as Caddy in front of it before accepting internet traffic.

## Host a LAN match

Use the explicit LAN command on the computer that will host the match:

```sh
cd Server
CINDERLINE_MATCH_WORKER=../build/CinderlineMatchWorker \
CINDERLINE_SERVER_NAME="Chirag's Cinderline" \
npm run start:lan
```

LAN mode binds IPv4 `0.0.0.0`. It prints a `ws://.../play` URL for every private or link-local IPv4 address on the host. The server also advertises `_cinderline._tcp` through Bonjour on the actual bound port with TXT fields `protocol=12`, `path=/play` and a DNS-safe discovery name. Its service record disables IPv6 because the listener is IPv4-only. Join a private IPv4 network before starting the host; IPv6-only hosting is not supported in this pass.

Bonjour discovery is a convenience. If multicast setup fails, the server stays available and prints an actionable warning; clients can use one of the manual URLs. Set `CINDERLINE_LAN_ADVERTISE=false` to skip advertising. A firewall must allow the selected TCP port, and Bonjour discovery needs multicast DNS on UDP 5353. Plain `ws://` is suitable only on a trusted local network. LAN mode does not turn a phone into the host and does not provision an internet endpoint.

## Configuration

The entry point validates its environment before it opens a listener.

| Variable | Default | Purpose |
| --- | --- | --- |
| `HOST` | `127.0.0.1` | Online-mode bind address. LAN mode requires `0.0.0.0` or no value. |
| `PORT` | `8787` | HTTP and WebSocket port, from 0 through 65535. Port 0 selects an ephemeral port and LAN output reports the actual result. |
| `CINDERLINE_NETWORK_MODE` | `online` | `online` or `lan`. The `start:lan` script supplies `--lan`. |
| `CINDERLINE_SERVER_NAME` | `Cinderline` | Public `/info` name, 1 to 64 printable characters. Its Bonjour instance and TXT discovery name replace dots and truncate safely to 63 UTF-8 bytes. |
| `CINDERLINE_LAN_ADVERTISE` | `true` in LAN mode | Enable Bonjour. Enabling it in online mode is rejected. |
| `CINDERLINE_MATCH_WORKER` | `../build/CinderlineMatchWorker` | Authoritative worker executable. The path is resolved by the server when omitted. |
| `CINDERLINE_SHUTDOWN_GRACE_MS` | `0` | Drain interval before active rooms end, from 0 through 30000 ms. The example container uses 5000 ms. |
| `CINDERLINE_TRUSTED_PROXIES` | empty | Comma-separated exact proxy IP addresses allowed to supply `X-Forwarded-For`. Do not trust all addresses. |
| `CINDERLINE_ALLOWED_ORIGINS` | empty | Comma-separated exact HTTP or HTTPS origins. An empty list retains native clients and accepts any supplied Origin. |

The bounded admission and worker controls are:

| Variable | Default | Valid range |
| --- | ---: | ---: |
| `CINDERLINE_MAX_ROOMS` | 128 | 1 to 10000 |
| `CINDERLINE_MAX_CONNECTIONS` | 256 | 1 to 100000 |
| `CINDERLINE_MAX_ACTIVE_WORKERS` | 32 | 1 to 1024 |
| `CINDERLINE_MAX_WORKER_COMMAND_QUEUE` | 256 | 8 to 4096 |
| `CINDERLINE_MAX_WORKER_BACKLOG_STEPS` | 40 | 5 to 400 |
| `CINDERLINE_WORKER_PROGRESS_TIMEOUT_MS` | 15000 | 500 to 300000 |
| `CINDERLINE_SLOW_CLIENT_TIMEOUT_MS` | 10000 | 500 to 300000 |
| `CINDERLINE_MAX_CONNECTIONS_PER_IP` | 16 | 1 to 1000 |
| `CINDERLINE_ROOM_CREATE_LIMIT_COUNT` | 8 | 1 to 1000 |
| `CINDERLINE_ROOM_CREATE_LIMIT_WINDOW_MS` | 60000 | 1000 to 3600000 |

`GET /healthz` reports liveness, protocol and aggregate room, connection and worker counts. `GET /readyz` returns 200 only while the server can accept work and the worker executable is ready; it returns 503 while draining or degraded. `GET /info` returns the server name, `online` or `lan` mode, protocol and room limit. These endpoints contain no room codes or reconnect credentials.

## Deploy with Docker and Caddy

The example uses Linux host networking so the Caddy process on the host reaches the container through loopback and the server can trust only `127.0.0.1` as its proxy. Port 8787 is not bound to a public interface. The Compose limits are two CPUs, 1 GiB of memory, 256 processes, 32 rooms, 128 connections and eight active workers.

1. Provision a Linux host with a public IP, Docker with the Compose plugin, and Caddy. Allow inbound TCP ports 80 and 443. Do not expose 8787 publicly.

2. Create a DNS `A` record such as `game.example.com` for the host. Add an `AAAA` record only when the host has working public IPv6.

3. Copy the repository to the host and run from its root:

   ```sh
   docker compose -f Server/docker-compose.example.yml up -d --build
   docker compose -f Server/docker-compose.example.yml ps
   curl --fail http://127.0.0.1:8787/healthz
   curl --fail http://127.0.0.1:8787/readyz
   curl --fail http://127.0.0.1:8787/info
   ```

   The image compiles `CinderlineSimulation` and `Server/MatchWorker.cpp` before copying the worker into the Node 22 runtime. The readiness check prevents a missing or failed worker from looking healthy.

4. Replace `game.example.com` in `Server/Caddyfile.example`, install the file as Caddy's active configuration, validate it, and reload Caddy:

   ```sh
   sudo cp Server/Caddyfile.example /etc/caddy/Caddyfile
   sudo caddy validate --config /etc/caddy/Caddyfile
   sudo systemctl reload caddy
   ```

   Caddy terminates TLS, forwards WebSocket upgrades, and uses `/readyz` for its upstream health check.

5. From another network, verify `https://game.example.com/healthz`, `https://game.example.com/readyz`, and a two-client room through `wss://game.example.com/play`.

The repository has not provisioned or tested a public endpoint. The example's restart policy can restart the process after failure, but it cannot restore in-memory matches. On `SIGTERM`, the server stops Bonjour, marks readiness unavailable, rejects new rooms, permits existing reconnects during the configured drain interval, and then ends the remaining rooms before Docker's 15-second stop deadline.

## Room lifecycle and protocol

The first player sends `create` with protocol version 12, a name, map number `0`, `1`, or `2`, and an optional `playerCount`. Omitting the count creates a two-player 1v1; the only accepted values are `2` and `4`. Welcome and lobby messages carry the chosen count, and the lobby always has exactly that many player entries. A four-player room remains in the lobby with two or three ready players and starts only after all four seats are occupied, connected and ready.

Protocol version 12 adds an explicit signed 32-bit `mapRevision` after the snapshot configuration's player count. Revision 0 selects the legacy flat maps; revision 1 selects the authored Shattered Rift terrain for Standard two-player matches and retains existing geometry for other variants. Both encoder and decoder reject unsupported revisions, and authored snapshots must match their shared map definition's complete obstacle list. This prevents a replica from attaching plateau heights to unrelated geometry. Fresh hosted matches use revision 1. Client, Node service and match worker must update together: older lobby handshakes and binary command/snapshot headers are rejected. Legacy offline saves load separately as revision 0; they are not accepted as older wire packets.

Protocol version 11 introduced private Drudge construction and mining plans. Append accepts Build, Resume Construction and Gather alongside movement. Planned buildings spend ore only when they start; invalid plans are skipped by the authoritative simulation. Snapshots include the queued building kind, and the owner retains opaque references to planned foundation or ore targets.

Protocol version 10 introduced formation spacing and optional arrival facing on eligible commands, current orders and queued tactical steps. These modifiers remain private to the owning seat, and malformed enums, flags, angles or incompatible order combinations are rejected before authority changes. Append commands use the copied-simulation and copied-view admission below because their tails can grow a snapshot. Replace clears tails and sustained state while every entity keeps the same fixed five-byte current-facing pair, so it cannot increase the frame and stays on the ordinary command path. Protocol version 9 introduced private Patrol and Escort state. Patrol stores immutable endpoints and bounded pursuit/return state; Escort uses an opaque owned mobile target, a stable slot and an acyclic follow graph. Hidden pursuit identities are projected as Return without exposing the enemy handle. Protocol version 8 introduced tactical order queues and a command queue mode. In version 8, Append was limited to movement orders; Clear Orders removes pending steps while retaining the active job. Before accepting an append, Patrol or Escort command, the authoritative worker applies it to a copied simulation and copied per-viewer memories and encodes every resulting seat snapshot. It rejects the command if any snapshot is empty or exceeds 1 MiB, leaving authoritative state, command recording and opaque identities unchanged. The accepted command is then applied to the original simulation immediately; the candidate is discarded so its isolated Navigation copy cannot replace the authority's warmed terrain caches. Protocol version 7 introduced the room player count and per-viewer elimination mask. Snapshot and acknowledgement routing accepts only the room's seat range, and result winner values are global seats; `-2` is a draw. Protocol version 6 introduced the match-length preset and active bounds. Hosted rooms currently use Standard; Short and Long remain solo choices. Protocol version 5 introduced each recipient's private army rally and owned producer override state. Version 4 introduced automatic commands and stable queue job IDs. Update the game client, Node service and match worker together. The lobby rejects older protocol handshakes before matchmaking.

Lobby states are `lobby`, `playing` and `finished`. Binary commands carry no team; the server binds each command to the authenticated seat. Surrender, leave and disconnect expiry eliminate only that seat, so a four-player match continues until the authoritative worker reports one winner or a draw. Eliminated players may stay connected as observers, and disconnected seat records and tokens remain available through the grace period. Peer connection messages identify the affected global team. Finished rooms retain the final result for five minutes. `surrender` keeps the socket open, while `leave` closes it.

All state lives in one Node process. Multiple replicas do not share rooms, and a service restart invalidates every room and token. Public scale requires connection affinity plus shared room persistence that this implementation does not provide.
