import { randomBytes } from "node:crypto";
import { spawn } from "node:child_process";
import { accessSync, constants as fsConstants } from "node:fs";
import { createServer } from "node:http";
import { isIP } from "node:net";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { WebSocket, WebSocketServer } from "ws";
import { PROTOCOL_VERSION, WEBSOCKET_PATH, WORKER_IPC as IPC } from "./protocol.js";
import { WorkerBridge } from "./worker-bridge.js";

const here = dirname(fileURLToPath(import.meta.url));
const ROOM_ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
const COMMAND_MAGIC = Buffer.from("CCMD");

function defaultClock() {
  return {
    now: () => Date.now(),
    setTimeout: (fn, ms) => setTimeout(fn, ms),
    clearTimeout: (id) => clearTimeout(id),
    setInterval: (fn, ms) => setInterval(fn, ms),
    clearInterval: (id) => clearInterval(id),
  };
}

function boundedInteger(value, fallback, minimum, maximum) {
  const parsed = Number(value);
  return Number.isInteger(parsed) && parsed >= minimum && parsed <= maximum ? parsed : fallback;
}

function makeRoomCode() {
  const bytes = randomBytes(8);
  let code = "";
  for (let index = 0; index < 6; index += 1) code += ROOM_ALPHABET[bytes[index] & 31];
  return code;
}

function makeToken() {
  return randomBytes(32).toString("base64url");
}

function cleanName(value) {
  if (typeof value !== "string") return null;
  const name = value.trim();
  if (name.length < 1 || name.length > 32 || /[\u0000-\u001f\u007f]/u.test(name)) return null;
  return name;
}

function cleanRoom(value) {
  if (typeof value !== "string") return null;
  const room = value.trim().toUpperCase();
  return /^[A-HJ-NP-Z2-9]{6}$/u.test(room) ? room : null;
}

function cleanMap(value) {
  return Number.isInteger(value) && value >= 0 && value <= 2 ? value : null;
}

function cleanPlayerCount(value) {
  if (value === undefined) return 2;
  return value === 2 || value === 4 ? value : null;
}

function jsonBuffer(message) {
  return JSON.stringify(message);
}

function requestPath(request) {
  try {
    return new URL(request.url ?? "/", "http://localhost").pathname;
  } catch {
    return null;
  }
}

function commandSequence(data) {
  if (data.length < 12 || !data.subarray(0, 4).equals(COMMAND_MAGIC)) return null;
  if (data.readUInt32LE(4) !== PROTOCOL_VERSION) return null;
  const sequence = data.readUInt32LE(8);
  return sequence === 0 ? null : sequence;
}

function normalizedIp(address) {
  if (typeof address !== "string") return "unknown";
  const mapped = address.toLowerCase().startsWith("::ffff:") ? address.slice(7) : address;
  return isIP(mapped) ? mapped : address;
}

export function createGameServer(options = {}) {
  const clock = options.clock ?? defaultClock();
  const logger = options.logger ?? console;
  const spawnWorker = options.spawnWorker ?? spawn;
  const bufferedAmountFor = options.bufferedAmountFor ?? ((socket) => socket.bufferedAmount);
  const trustedProxyAddresses = new Set(
    Array.isArray(options.trustedProxyAddresses) ? options.trustedProxyAddresses.map(normalizedIp) : [],
  );
  const allowedOrigins = new Set(Array.isArray(options.allowedOrigins) ? options.allowedOrigins : []);
  const config = {
    host: options.host ?? process.env.HOST ?? "127.0.0.1",
    port: boundedInteger(options.port ?? process.env.PORT, 8787, 0, 65535),
    workerPath: options.workerPath ?? process.env.CINDERLINE_MATCH_WORKER ?? resolve(here, "../build/CinderlineMatchWorker"),
    maxRooms: boundedInteger(options.maxRooms, 128, 1, 10_000),
    maxConnections: boundedInteger(options.maxConnections, 256, 1, 100_000),
    maxPayloadBytes: boundedInteger(options.maxPayloadBytes, 1024 * 1024, 1024, 16 * 1024 * 1024),
    maxJsonBytes: boundedInteger(options.maxJsonBytes, 8192, 256, 64 * 1024),
    maxBackpressureBytes: boundedInteger(options.maxBackpressureBytes, 2 * 1024 * 1024, 64 * 1024, 64 * 1024 * 1024),
    rateLimitCount: boundedInteger(options.rateLimitCount, 120, 1, 10_000),
    rateLimitWindowMs: boundedInteger(options.rateLimitWindowMs, 10_000, 100, 3_600_000),
    disconnectGraceMs: boundedInteger(options.disconnectGraceMs, 60_000, 10, 3_600_000),
    finishedExpiryMs: boundedInteger(options.finishedExpiryMs, 300_000, 100, 86_400_000),
    heartbeatMs: boundedInteger(options.heartbeatMs, 30_000, 100, 3_600_000),
    workerHandshakeMs: boundedInteger(options.workerHandshakeMs, 10_000, 100, 120_000),
    tickIntervalMs: boundedInteger(options.tickIntervalMs, 50, 1, 1000),
    stepsPerTick: boundedInteger(options.stepsPerTick, 1, 1, 5),
    snapshotEverySteps: boundedInteger(options.snapshotEverySteps, 2, 1, 20),
    maxSequenceCache: boundedInteger(options.maxSequenceCache, 256, 8, 4096),
    maxActiveWorkers: boundedInteger(options.maxActiveWorkers, 32, 1, 1024),
    maxWorkerCommandQueue: boundedInteger(options.maxWorkerCommandQueue, 256, 8, 4096),
    maxWorkerBacklogSteps: boundedInteger(options.maxWorkerBacklogSteps, 40, 5, 400),
    workerProgressTimeoutMs: boundedInteger(options.workerProgressTimeoutMs, 15_000, 500, 300_000),
    slowClientTimeoutMs: boundedInteger(options.slowClientTimeoutMs, 10_000, 500, 300_000),
    maxConnectionsPerIp: boundedInteger(options.maxConnectionsPerIp, 16, 1, 1000),
    roomCreateLimitCount: boundedInteger(options.roomCreateLimitCount, 8, 1, 1000),
    roomCreateLimitWindowMs: boundedInteger(options.roomCreateLimitWindowMs, 60_000, 1000, 3_600_000),
    serverName: typeof options.serverName === "string" && options.serverName.trim() ? options.serverName.trim().slice(0, 64) : "Cinderline",
    networkMode: options.networkMode === "lan" ? "lan" : "online",
  };

  const rooms = new Map();
  const connectionCounts = new Map();
  const creationWindows = new Map();
  let draining = false;
  let closing = false;
  let workerFailures = 0;
  const roomCounts = () => {
    const counts = { lobby: 0, starting: 0, active: 0, finished: 0 };
    for (const room of rooms.values()) if (Object.hasOwn(counts, room.state)) counts[room.state] += 1;
    return counts;
  };
  const workerCount = () => [...rooms.values()].filter((room) => room.workerBridge?.running).length;
  const publicInfo = () => ({ name: config.serverName, mode: config.networkMode, protocol: PROTOCOL_VERSION, maxRooms: config.maxRooms });
  const workerExecutableReady = () => {
    if (options.spawnWorker) return true;
    try { accessSync(config.workerPath, fsConstants.X_OK); return true; } catch { return false; }
  };
  const httpServer = createServer((request, response) => {
    const path = requestPath(request);
    if (path === null) {
      response.writeHead(400, { "content-type": "text/plain", "cache-control": "no-store", connection: "close" });
      response.end("Bad request\n");
      return;
    }
    if (request.method === "GET" && path === "/healthz") {
      const body = jsonBuffer({ status: "ok", rooms: rooms.size, connections: wss.clients.size, workers: workerCount(), states: roomCounts(), protocol: PROTOCOL_VERSION });
      response.writeHead(200, { "content-type": "application/json", "cache-control": "no-store" });
      response.end(body);
      return;
    }
    if (request.method === "GET" && path === "/readyz") {
      const ready = !draining && !closing && workerExecutableReady();
      const body = jsonBuffer({ status: ready ? "ready" : "degraded", rooms: rooms.size, connections: wss.clients.size, workers: workerCount(), states: roomCounts(), workerFailures, protocol: PROTOCOL_VERSION });
      response.writeHead(ready ? 200 : 503, { "content-type": "application/json", "cache-control": "no-store" });
      response.end(body);
      return;
    }
    if (request.method === "GET" && path === "/info") {
      response.writeHead(200, { "content-type": "application/json", "cache-control": "no-store" });
      response.end(jsonBuffer(publicInfo()));
      return;
    }
    response.writeHead(404, { "content-type": "text/plain", "cache-control": "no-store" });
    response.end("Not found\n");
  });
  const wss = new WebSocketServer({ noServer: true, maxPayload: config.maxPayloadBytes, perMessageDeflate: false });

  function send(socket, message, binary = false, volatile = false) {
    if (!socket || socket.readyState !== WebSocket.OPEN) return false;
    const body = binary ? message : jsonBuffer(message);
    const bodySize = typeof body === "string" ? Buffer.byteLength(body) : body.length;
    if (bufferedAmountFor(socket) + bodySize > config.maxBackpressureBytes) {
      if (volatile) {
        if (socket.slowSince === null) socket.slowSince = clock.now();
        if (clock.now() - socket.slowSince >= config.slowClientTimeoutMs) socket.terminate();
        return false;
      }
      socket.terminate();
      return false;
    }
    socket.slowSince = null;
    socket.send(body, { binary });
    return true;
  }

  function sendError(socket, message, code, retryable = false) {
    const body = { type: "error", message };
    if (code) { body.code = code; body.retryable = retryable; }
    send(socket, body);
  }

  function playerView(player) {
    return player
      ? { name: player.name, connected: player.connected, ready: player.ready }
      : { name: "", connected: false, ready: false };
  }

  function lobbyMessage(room) {
    const state = room.state === "lobby" ? "lobby" : room.state === "finished" ? "finished" : "playing";
    return {
      type: "lobby",
      room: room.code,
      map: room.map,
      playerCount: room.playerCount,
      players: room.players.map(playerView),
      state,
    };
  }

  function broadcast(room, message, except = null) {
    for (const player of room.players) {
      if (player?.socket !== except) send(player?.socket, message);
    }
  }

  function broadcastLobby(room) {
    broadcast(room, lobbyMessage(room));
  }

  function broadcastPeer(room, subject, connected, graceSeconds) {
    const message = { type: "peer", team: subject.seat, connected, graceSeconds };
    for (const player of room.players) {
      if (player && player !== subject) send(player.socket, message);
    }
  }

  function clearPlayerTimer(player) {
    if (player && player.disconnectTimer !== null) {
      clock.clearTimeout(player.disconnectTimer);
      player.disconnectTimer = null;
    }
  }

  function stopWorker(room) {
    if (room.tickTimer !== null) {
      clock.clearInterval(room.tickTimer);
      room.tickTimer = null;
    }
    room.workerBridge?.stop();
  }

  function destroyRoom(room) {
    if (!rooms.delete(room.code)) return;
    if (room.expiryTimer !== null) clock.clearTimeout(room.expiryTimer);
    for (const player of room.players) {
      clearPlayerTimer(player);
      if (!player) continue;
      const socket = player.socket;
      if (socket?.context?.room === room) socket.context = null;
      player.socket = null;
      player.connected = false;
      if (socket?.readyState === WebSocket.OPEN) socket.close(1000, "Room closed");
    }
    stopWorker(room);
    queueMicrotask(pumpWorkerQueue);
  }

  function finishRoom(room, winner, reason = "victory") {
    if (room.state === "finished") return;
    room.state = "finished";
    room.result = { winner, reason };
    for (const player of room.players) clearPlayerTimer(player);
    if (room.tickTimer !== null) {
      clock.clearInterval(room.tickTimer);
      room.tickTimer = null;
    }
    broadcast(room, { type: "result", ...room.result });
    broadcastLobby(room);
    room.workerBridge?.finish();
    room.expiryTimer = clock.setTimeout(() => destroyRoom(room), config.finishedExpiryMs);
  }

  function queueWorker(room, opcode, payload = Buffer.alloc(0)) {
    const bridge = room.workerBridge;
    if (!bridge) return false;
    if (opcode === IPC.forfeit) return bridge.enqueueForfeit(payload);
    if (opcode === IPC.command) return bridge.enqueueCommand(payload);
    if (opcode === IPC.step) return payload.length === 4 && bridge.enqueueSteps(payload.readUInt32LE(0));
    if (opcode === IPC.snapshotRequest) return payload.length === 0 && bridge.requestSnapshot();
    return false;
  }

  function forfeit(room, player, reason) {
    if ((room.state !== "starting" && room.state !== "active") || player.eliminated) return false;
    player.eliminated = true;
    player.eliminationReason = reason;
    const body = Buffer.from([player.seat]);
    if (!queueWorker(room, IPC.forfeit, body)) {
      workerFailures += 1;
      broadcast(room, { type: "error", message: "The match worker became unavailable.", code: "worker_unavailable", retryable: true });
      destroyRoom(room);
      return false;
    }
    return true;
  }

  function cacheAcknowledgement(player, sequence, acknowledgement) {
    player.pendingSequences.delete(sequence);
    player.acknowledgements.set(sequence, acknowledgement);
    while (player.acknowledgements.size > config.maxSequenceCache) {
      const oldest = player.acknowledgements.keys().next().value;
      player.acknowledgements.delete(oldest);
    }
  }

  function handleWorkerFrame(room, opcode, payload) {
    if (opcode === IPC.ready) {
      if (payload.length !== 0 || room.workerReady) throw new Error("invalid worker ready frame");
      room.workerReady = true;
      return;
    }
    if (opcode === IPC.snapshot) {
      if (!room.workerReady || payload.length < 2) throw new Error("invalid worker snapshot frame");
      const seat = payload[0];
      if (seat >= room.playerCount || payload.length - 1 > config.maxPayloadBytes) throw new Error("invalid worker snapshot seat");
      const body = Buffer.from(payload.subarray(1));
      room.lastSnapshots[seat] = body;
      room.initialSnapshots.add(seat);
      if (room.state === "starting" && room.initialSnapshots.size === room.playerCount) {
        room.workerBridge?.completeHandshake();
        room.state = "active";
        room.lastStepAt = clock.now();
        broadcast(room, { type: "started" });
        for (let initialSeat = 0; initialSeat < room.playerCount; initialSeat += 1) {
          send(room.players[initialSeat]?.socket, room.lastSnapshots[initialSeat], true, true);
        }
        broadcastLobby(room);
        room.tickTimer = clock.setInterval(() => {
          if (room.state !== "active") return;
          const step = Buffer.allocUnsafe(4);
          step.writeUInt32LE(config.stepsPerTick);
          if (!queueWorker(room, IPC.step, step)) {
            workerFailures += 1;
            broadcast(room, { type: "error", message: "The match worker is not keeping up.", code: "worker_unavailable", retryable: true });
            destroyRoom(room);
            return;
          }
          room.stepsSinceSnapshot += config.stepsPerTick;
          if (room.stepsSinceSnapshot >= config.snapshotEverySteps) {
            room.stepsSinceSnapshot %= config.snapshotEverySteps;
            queueWorker(room, IPC.snapshotRequest);
          }
        }, config.tickIntervalMs);
      } else if (room.state !== "starting") {
        send(room.players[seat]?.socket, body, true, true);
      }
      return;
    }
    if (opcode === IPC.acknowledgement) {
      if (payload.length < 8) throw new Error("short worker acknowledgement");
      const seat = payload[0];
      const sequence = payload.readUInt32LE(1);
      const accepted = payload[5] !== 0;
      const messageBytes = payload.readUInt16LE(6);
      if (seat >= room.playerCount || payload.length !== 8 + messageBytes) throw new Error("invalid worker acknowledgement");
      const player = room.players[seat];
      if (!player || !player.pendingSequences.has(sequence)) return;
      const acknowledgement = {
        type: "ack",
        seq: sequence,
        accepted,
        message: payload.subarray(8).toString("utf8"),
      };
      cacheAcknowledgement(player, sequence, acknowledgement);
      send(player.socket, acknowledgement);
      return;
    }
    if (opcode === IPC.result) {
      if (payload.length !== 2) throw new Error("invalid worker result frame");
      const winner = payload[0] === 255 ? -2 : payload[0];
      if (winner !== -2 && winner >= room.playerCount) throw new Error("invalid worker result frame");
      const causeSeat = payload[1];
      if (causeSeat !== 255 && causeSeat >= room.playerCount) throw new Error("invalid worker result cause");
      const causePlayer = causeSeat === 255 ? null : room.players[causeSeat];
      if (causeSeat !== 255 && (!causePlayer?.eliminated || !causePlayer.eliminationReason)) {
        throw new Error("worker result cause was not a pending elimination");
      }
      finishRoom(room, winner, causePlayer?.eliminationReason ?? "victory");
      return;
    }
    throw new Error("unknown worker output opcode");
  }

  function handleWorkerFailure(room, failure) {
    if (!rooms.has(room.code) || room.state === "finished") return;
    workerFailures += 1;
    const detail = failure.error instanceof Error ? failure.error.message : String(failure.error ?? "");
    if (failure.kind === "input") {
      logger.warn?.(`Match worker input failed: ${detail}`);
      broadcast(room, { type: "error", message: "The match worker became unavailable.", code: "worker_unavailable", retryable: true });
    } else if (failure.kind === "inputStopped") {
      logger.warn?.(`Match worker input stopped: ${detail}`);
      broadcast(room, { type: "error", message: "The match worker became unavailable.", code: "worker_unavailable", retryable: true });
    } else if (failure.kind === "protocol") {
      logger.error?.(`Match worker protocol error: ${detail}`);
      broadcast(room, { type: "error", message: "The match worker sent invalid data." });
    } else if (failure.kind === "exit") {
      logger.error?.(`Match worker stopped before the result, code=${failure.code ?? "none"} signal=${failure.signal ?? "none"}`);
      broadcast(room, { type: "error", message: "The match worker stopped.", code: "worker_unavailable", retryable: true });
    } else if (failure.kind === "handshake") {
      broadcast(room, { type: "error", message: "The match worker did not become ready." });
    } else if (failure.kind === "progress") {
      logger.warn?.("Match worker stopped making progress.");
      broadcast(room, { type: "error", message: "The match worker stopped responding.", code: "worker_unavailable", retryable: true });
    } else {
      logger.error?.(`Could not start match worker: ${detail}`);
      broadcast(room, { type: "error", message: "The match worker could not start.", code: "worker_unavailable", retryable: true });
    }
    destroyRoom(room);
  }

  function startWorker(room) {
    if (room.state !== "lobby" || room.players.some((player) => !player?.ready)) return;
    if (workerCount() >= config.maxActiveWorkers) {
      room.waitingForWorker = true;
      broadcast(room, { type: "error", message: "The server is waiting for match capacity.", code: "worker_capacity", retryable: true });
      return;
    }
    room.waitingForWorker = false;
    room.state = "starting";
    broadcastLobby(room);
    const bridge = new WorkerBridge({
      spawnWorker,
      workerPath: config.workerPath,
      args: ["--map", String(room.map), "--seed", String(room.seed), "--players", String(room.playerCount)],
      clock,
      maxPayloadBytes: config.maxPayloadBytes,
      maxCommandQueue: config.maxWorkerCommandQueue,
      maxBacklogSteps: config.maxWorkerBacklogSteps,
      handshakeMs: config.workerHandshakeMs,
      onFrame: (opcode, payload) => handleWorkerFrame(room, opcode, payload),
      onFailure: (failure) => handleWorkerFailure(room, failure),
      onExit: () => pumpWorkerQueue(),
      onStderr: (message) => logger.warn?.(`Match worker: ${message.trim().slice(0, 1000)}`),
    });
    room.workerBridge = bridge;
    bridge.start();
  }

  function pumpWorkerQueue() {
    if (closing) return;
    for (const room of rooms.values()) {
      if (workerCount() >= config.maxActiveWorkers) break;
      if (room.state === "lobby" && room.waitingForWorker && room.players.every((player) => player?.ready)) startWorker(room);
    }
  }

  function newPlayer(name, socket, seat) {
    return {
      name,
      token: makeToken(),
      seat,
      socket,
      connected: true,
      ready: false,
      disconnectTimer: null,
      highestSequence: 0,
      pendingSequences: new Set(),
      acknowledgements: new Map(),
      eliminated: false,
      eliminationReason: null,
      departed: false,
    };
  }

  function attach(socket, room, player, reconnecting = false) {
    socket.context = { room, player };
    socket.isAlive = true;
    player.socket = socket;
    player.connected = true;
    clearPlayerTimer(player);
    send(socket, { type: "welcome", room: room.code, token: player.token, team: player.seat, playerCount: room.playerCount });
    send(socket, lobbyMessage(room));
    if (room.state === "active") send(socket, { type: "started" });
    if (reconnecting && room.lastSnapshots[player.seat]) send(socket, room.lastSnapshots[player.seat], true);
    if (room.state === "finished" && room.result) send(socket, { type: "result", ...room.result });
    if (reconnecting) {
      broadcastPeer(room, player, true, 0);
    }
    broadcastLobby(room);
  }

  function createRoom(socket, message) {
    if (draining) return sendError(socket, "The server is draining.", "server_busy", true);
    const now = clock.now();
    const key = socket.clientIp ?? "unknown";
    let window = creationWindows.get(key);
    if (!window || now - window.startedAt >= config.roomCreateLimitWindowMs) {
      window = { startedAt: now, count: 0 };
      creationWindows.set(key, window);
    }
    window.count += 1;
    if (window.count > config.roomCreateLimitCount) return sendError(socket, "Room creation rate limit exceeded.", "rate_limited", true);
    if (rooms.size >= config.maxRooms) return sendError(socket, "The server has reached its room limit.", "server_busy", true);
    const name = cleanName(message.name);
    const map = cleanMap(message.map);
    const playerCount = cleanPlayerCount(message.playerCount);
    if (!name) return sendError(socket, "Name must be 1 to 32 printable characters.");
    if (map === null) return sendError(socket, "Unknown map.");
    if (playerCount === null) return sendError(socket, "Player count must be 2 or 4.");
    let code;
    do code = makeRoomCode(); while (rooms.has(code));
    const room = {
      code,
      map,
      playerCount,
      seed: randomBytes(4).readUInt32LE(),
      state: "lobby",
      players: Array(playerCount).fill(null),
      workerBridge: null,
      workerReady: false,
      initialSnapshots: new Set(),
      lastSnapshots: Array(playerCount).fill(null),
      result: null,
      stepsSinceSnapshot: 0,
      lastStepAt: 0,
      tickTimer: null,
      expiryTimer: null,
      waitingForWorker: false,
    };
    const player = newPlayer(name, socket, 0);
    room.players[0] = player;
    rooms.set(code, room);
    attach(socket, room, player);
  }

  function joinRoom(socket, message) {
    const name = cleanName(message.name);
    const code = cleanRoom(message.room);
    if (!name) return sendError(socket, "Name must be 1 to 32 printable characters.");
    if (!code) return sendError(socket, "Invalid room code.");
    const room = rooms.get(code);
    if (!room) return sendError(socket, "Room not found.");
    if (room.state !== "lobby") return sendError(socket, "The match has already started.");
    const seat = room.players.findIndex((player) => player === null);
    if (seat < 0) return sendError(socket, "Room is full.");
    const player = newPlayer(name, socket, seat);
    room.players[seat] = player;
    attach(socket, room, player);
  }

  function reconnect(socket, message) {
    const code = cleanRoom(message.room);
    if (!code || typeof message.token !== "string" || message.token.length > 128) {
      return sendError(socket, "Invalid reconnect credentials.");
    }
    const room = rooms.get(code);
    const player = room?.players.find((candidate) => candidate?.token === message.token);
    if (!room || !player || player.departed) return sendError(socket, "Invalid reconnect credentials.", "invalid_credentials", false);
    if (player.connected) return sendError(socket, "That seat is already connected.", "seat_connected", true);
    attach(socket, room, player, true);
  }

  function setReady(socket, message) {
    const context = socket.context;
    if (!context || context.room.state !== "lobby") return sendError(socket, "Ready is only available in the lobby.");
    if (typeof message.ready !== "boolean") return sendError(socket, "Ready must be true or false.");
    context.player.ready = message.ready;
    broadcastLobby(context.room);
    startWorker(context.room);
  }

  function leave(socket) {
    const context = socket.context;
    if (!context) return sendError(socket, "You are not in a room.");
    const { room, player } = context;
    const active = room.state === "active" || room.state === "starting";
    if (active) {
      forfeit(room, player, "forfeit");
    } else if (room.state === "lobby") {
      room.players[player.seat] = null;
      if (room.players.every((candidate) => candidate === null)) destroyRoom(room);
    }
    player.departed = true;
    socket.context = null;
    player.connected = false;
    player.socket = null;
    if (active) broadcastPeer(room, player, false, 0);
    if (rooms.has(room.code)) broadcastLobby(room);
    socket.close(1000, "Left match");
  }

  function surrender(socket) {
    const context = socket.context;
    if (!context || (context.room.state !== "active" && context.room.state !== "starting")) {
      return sendError(socket, "Surrender is only available during a match.");
    }
    forfeit(context.room, context.player, "forfeit");
  }

  function handleJson(socket, data) {
    if (data.length > config.maxJsonBytes) return sendError(socket, "Control message is too large.");
    let message;
    try {
      message = JSON.parse(data.toString("utf8"));
    } catch {
      return sendError(socket, "Malformed JSON.");
    }
    if (!message || typeof message !== "object" || Array.isArray(message) || typeof message.type !== "string") {
      return sendError(socket, "Malformed control message.");
    }
    if (message.type === "ping") {
      if ((typeof message.nonce !== "string" && typeof message.nonce !== "number") || String(message.nonce).length > 128) {
        return sendError(socket, "Invalid ping nonce.");
      }
      return send(socket, { type: "pong", nonce: message.nonce });
    }
    if (message.type === "create" || message.type === "join" || message.type === "reconnect") {
      if (socket.context) return sendError(socket, "This connection already belongs to a room.");
      if (message.version !== PROTOCOL_VERSION) return sendError(socket, "Unsupported protocol version.");
      if (message.type === "create") return createRoom(socket, message);
      if (message.type === "join") return joinRoom(socket, message);
      return reconnect(socket, message);
    }
    if (message.type === "ready") return setReady(socket, message);
    if (message.type === "surrender") return surrender(socket);
    if (message.type === "leave") return leave(socket);
    return sendError(socket, "Unknown control message.");
  }

  function handleCommand(socket, data) {
    const context = socket.context;
    if (!context || context.room.state !== "active" || context.player.eliminated) {
      return sendError(socket, "The match is not accepting commands.");
    }
    if (data.length > config.maxPayloadBytes) return sendError(socket, "Command is too large.");
    const sequence = commandSequence(data);
    if (sequence === null) return sendError(socket, "Malformed command header.");
    const { room, player } = context;
    const cached = player.acknowledgements.get(sequence);
    if (cached) return send(socket, cached);
    if (player.pendingSequences.has(sequence)) return;
    if (sequence <= player.highestSequence) {
      return send(socket, { type: "ack", seq: sequence, accepted: false, message: "Stale command sequence." });
    }
    player.highestSequence = sequence;
    player.pendingSequences.add(sequence);
    const payload = Buffer.allocUnsafe(1 + data.length);
    payload[0] = player.seat;
    data.copy(payload, 1);
    if (!queueWorker(room, IPC.command, payload)) {
      player.pendingSequences.delete(sequence);
      const acknowledgement = { type: "ack", seq: sequence, accepted: false, message: "Server command queue is busy." };
      cacheAcknowledgement(player, sequence, acknowledgement);
      send(socket, acknowledgement);
    }
  }

  function onDisconnected(socket) {
    const context = socket.context;
    socket.context = null;
    if (!context) return;
    const { room, player } = context;
    if (player.socket !== socket) return;
    player.socket = null;
    player.connected = false;
    player.ready = room.state === "lobby" ? false : player.ready;
    if (!rooms.has(room.code) || room.state === "finished") return;
    if (room.state === "lobby" && room.players.filter(Boolean).every((candidate) => !candidate.connected)) {
      destroyRoom(room);
      return;
    }
    const graceSeconds = Math.ceil(config.disconnectGraceMs / 1000);
    broadcastPeer(room, player, false, player.eliminated ? 0 : graceSeconds);
    broadcastLobby(room);
    if (player.eliminated) return;
    player.disconnectTimer = clock.setTimeout(() => {
      player.disconnectTimer = null;
      if (player.connected || !rooms.has(room.code)) return;
      if (room.state === "active" || room.state === "starting") {
        forfeit(room, player, "disconnect");
      } else if (room.state === "lobby") {
        room.players[player.seat] = null;
        broadcastLobby(room);
        if (room.players.every((candidate) => candidate === null)) destroyRoom(room);
      }
    }, config.disconnectGraceMs);
  }

  wss.on("connection", (socket) => {
    socket.context = null;
    socket.isAlive = true;
    socket.rateWindowStart = clock.now();
    socket.rateCount = 0;
    socket.slowSince = null;
    socket.on("pong", () => { socket.isAlive = true; });
    socket.on("message", (data, isBinary) => {
      const now = clock.now();
      if (now - socket.rateWindowStart >= config.rateLimitWindowMs) {
        socket.rateWindowStart = now;
        socket.rateCount = 0;
      }
      socket.rateCount += 1;
      if (socket.rateCount > config.rateLimitCount) {
        sendError(socket, "Message rate limit exceeded.", "rate_limited", true);
        socket.close(1008, "Rate limit");
        return;
      }
      const body = Buffer.isBuffer(data) ? data : Buffer.from(data);
      if (isBinary) handleCommand(socket, body);
      else handleJson(socket, body);
    });
    socket.on("close", () => {
      if (socket.connectionCounted) {
        const count = connectionCounts.get(socket.clientIp) ?? 0;
        if (count <= 1) connectionCounts.delete(socket.clientIp); else connectionCounts.set(socket.clientIp, count - 1);
        socket.connectionCounted = false;
      }
      onDisconnected(socket);
    });
    socket.on("error", () => {});
  });

  httpServer.on("upgrade", (request, socket, head) => {
    const path = requestPath(request);
    if (path === null) {
      socket.write("HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 12\r\n\r\nBad request\n");
      socket.destroy();
      return;
    }
    if (path !== WEBSOCKET_PATH) {
      socket.write("HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n");
      socket.destroy();
      return;
    }
    if (closing) {
      socket.write("HTTP/1.1 503 Service Unavailable\r\nConnection: close\r\n\r\n");
      socket.destroy();
      return;
    }
    const origin = typeof request.headers.origin === "string" ? request.headers.origin : "";
    if (origin && allowedOrigins.size > 0 && !allowedOrigins.has(origin)) {
      socket.write("HTTP/1.1 403 Forbidden\r\nConnection: close\r\n\r\n");
      socket.destroy();
      return;
    }
    const remote = normalizedIp(request.socket.remoteAddress);
    let clientIp = remote;
    if (trustedProxyAddresses.has(remote)) {
      const forwarded = typeof request.headers["x-forwarded-for"] === "string"
        ? request.headers["x-forwarded-for"].split(",", 1)[0].trim() : "";
      if (isIP(forwarded)) clientIp = normalizedIp(forwarded);
    }
    if (wss.clients.size >= config.maxConnections) {
      socket.write("HTTP/1.1 503 Service Unavailable\r\nConnection: close\r\n\r\n");
      socket.destroy();
      return;
    }
    if ((connectionCounts.get(clientIp) ?? 0) >= config.maxConnectionsPerIp) {
      socket.write("HTTP/1.1 429 Too Many Requests\r\nConnection: close\r\n\r\n");
      socket.destroy();
      return;
    }
    wss.handleUpgrade(request, socket, head, (webSocket) => {
      webSocket.clientIp = clientIp;
      webSocket.connectionCounted = true;
      connectionCounts.set(clientIp, (connectionCounts.get(clientIp) ?? 0) + 1);
      wss.emit("connection", webSocket, request);
    });
  });

  const heartbeatTimer = clock.setInterval(() => {
    const now = clock.now();
    for (const socket of wss.clients) {
      if (socket.readyState !== WebSocket.OPEN) continue;
      if (socket.slowSince !== null && now - socket.slowSince >= config.slowClientTimeoutMs) {
        socket.terminate();
        continue;
      }
      if (!socket.isAlive) {
        socket.terminate();
        continue;
      }
      socket.isAlive = false;
      socket.ping();
    }
    for (const [address, window] of creationWindows) {
      if (now - window.startedAt >= config.roomCreateLimitWindowMs) creationWindows.delete(address);
    }
  }, config.heartbeatMs);

  const workerWatchdogTimer = clock.setInterval(() => {
    for (const room of [...rooms.values()]) {
      if (room.state === "active") room.workerBridge?.checkProgress(config.workerProgressTimeoutMs);
    }
  }, Math.max(250, Math.min(config.workerProgressTimeoutMs / 2, config.heartbeatMs)));

  let listening = false;
  async function listen() {
    if (listening) return httpServer.address();
    await new Promise((resolveListen, rejectListen) => {
      const onError = (error) => {
        httpServer.off("listening", onListening);
        rejectListen(error);
      };
      const onListening = () => {
        httpServer.off("error", onError);
        listening = true;
        resolveListen();
      };
      httpServer.once("error", onError);
      httpServer.once("listening", onListening);
      httpServer.listen(config.port, config.host);
    });
    return httpServer.address();
  }

  async function close() {
    draining = true;
    closing = true;
    clock.clearInterval(heartbeatTimer);
    clock.clearInterval(workerWatchdogTimer);
    for (const room of [...rooms.values()]) destroyRoom(room);
    for (const socket of wss.clients) socket.terminate();
    wss.close();
    if (!listening) return;
    await new Promise((resolveClose) => httpServer.close(resolveClose));
    listening = false;
  }

  function drain() {
    draining = true;
  }

  return { listen, drain, close, server: httpServer, webSocketServer: wss, rooms, config };
}
