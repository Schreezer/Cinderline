# Cinderline game server

This service hosts private two-player rooms at `/play`. It has no accounts, public matchmaking, ranking, or purchased service dependency. Each active room runs one authoritative `CinderlineMatchWorker` process built from the same simulation code as the game.

## Run locally

The direct Node setup requires Node 22 or newer. Build the worker from the repository root, then install the pinned server dependency.

```sh
cmake -S . -B build -DCINDERLINE_BUILD_NATIVE=OFF -DBUILD_TESTING=OFF
cmake --build build --target CinderlineMatchWorker
cd Server
npm ci
CINDERLINE_MATCH_WORKER=../build/CinderlineMatchWorker npm start
```

The executable reads these environment variables:

| Variable | Default | Purpose |
| --- | --- | --- |
| `HOST` | `127.0.0.1` | HTTP and WebSocket bind address. |
| `PORT` | `8787` | HTTP and WebSocket port. |
| `CINDERLINE_MATCH_WORKER` | `../build/CinderlineMatchWorker`, resolved from `Server/server.js` | Worker executable path. The container sets this to `/app/CinderlineMatchWorker`. |
| `NODE_ENV` | unset locally | The container sets this to `production`. |

`GET /healthz` returns protocol status plus room and connection counts. It contains no room codes or reconnect credentials.

Code that imports `createGameServer(options)` can also set room, connection, payload, backpressure, message-rate, disconnect, result-expiry, heartbeat, worker-handshake, and simulation scheduling limits. The production defaults are 128 rooms, 256 connections, a 60-second disconnect grace, a five-minute finished-room expiry, 20 rule steps per second, and 10 snapshot updates per second. These options are trusted server configuration and are not client controls.

## Deploy with Docker and Caddy

The supplied Compose file assumes Docker runs the game service while Caddy runs directly on the same host. Port `8787` stays bound to host loopback; only Caddy accepts public traffic.

1. Provision a Linux host with a public IP, Docker with the Compose plugin, and Caddy. Allow inbound TCP ports `80` and `443`. Do not expose `8787` publicly.

2. Create a DNS `A` record such as `game.example.com` pointing to the host's public IPv4 address. Add an `AAAA` record only if the host has working public IPv6. Wait until the record resolves from outside the host.

3. Copy the whole repository to the host and run the build from the repository root. The Compose file deliberately uses the repository root as its Docker build context because the image compiles `CinderlineSimulation` and `Server/MatchWorker.cpp` before copying the worker into the Node runtime image.

   ```sh
   docker compose -f Server/docker-compose.example.yml up -d --build
   docker compose -f Server/docker-compose.example.yml ps
   curl --fail http://127.0.0.1:8787/healthz
   ```

4. Replace `game.example.com` in `Server/Caddyfile.example` with the DNS name from step 2. Install it as Caddy's active configuration, validate it, and reload Caddy.

   ```sh
   sudo cp Server/Caddyfile.example /etc/caddy/Caddyfile
   sudo caddy validate --config /etc/caddy/Caddyfile
   sudo systemctl reload caddy
   ```

   Caddy obtains and renews the public certificate. Its `reverse_proxy` directive supports the WebSocket upgrade used by `/play`.

5. Verify the public TLS route from a machine outside the host network.

   ```sh
   curl --fail https://game.example.com/healthz
   ```

   Configure the game endpoint as `wss://game.example.com/play`. A usable public deployment needs both the HTTPS health check and a two-client room test through this public WebSocket URL. The Docker image and public TLS path have not been validated by the repository's local server tests.

`docker-compose.example.yml` uses `restart: unless-stopped`, so Docker restarts the service after a host reboot or process failure. This keeps the service available, but it does not make rooms persistent.

## Room lifecycle

The first player sends `create` with protocol version 1, a name, and map number `0`, `1`, or `2`. The server returns a six-character room code, reconnect token, and canonical seat in `welcome`. The second player sends `join` with the room code and receives separate seat credentials. Both players must send `ready` before the service starts the match worker.

Lobby messages always contain two player entries in canonical seat order. Public lobby states are `lobby`, `playing`, and `finished`. Binary commands carry no team; the server binds each command to the authenticated seat and preserves command sequence history across socket reconnects.

If one active player disconnects, that seat has 60 seconds to reconnect with its room code and token. Expiry forfeits the disconnected seat. `surrender` forfeits while keeping the socket open for the authoritative result. `leave` forfeits an active match, vacates the seat, and closes that connection. Finished rooms keep the final per-seat snapshot and result for five minutes, allowing a disconnected player to recover the outcome during that window.

All room state, reconnect tokens, sequence history, and retained results live in one Node process. A service restart ends every lobby and active match and invalidates every token. If both players disconnect before a match finishes, the service cleans up that room instead of retaining a reconnect window. Multiple server replicas do not share room state, so horizontal scaling requires connection affinity plus a shared room design that this release does not provide.
