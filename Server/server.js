import { randomBytes } from "node:crypto";
import { spawn } from "node:child_process";
import { createServer } from "node:http";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { WebSocket, WebSocketServer } from "ws";

const here = dirname(fileURLToPath(import.meta.url));
const ROOM_ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
const COMMAND_MAGIC = Buffer.from("CCMD");
const PROTOCOL_VERSION = 1;

const IPC = Object.freeze({
  step: 1,
  command: 2,
  forfeit: 3,
  snapshotRequest: 4,
  ready: 128,
  snapshot: 129,
  acknowledgement: 130,
  result: 131,
});

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

function ipcFrame(opcode, payload = Buffer.alloc(0)) {
  const frame = Buffer.allocUnsafe(5 + payload.length);
  frame.writeUInt32LE(1 + payload.length, 0);
  frame[4] = opcode;
  payload.copy(frame, 5);
  return frame;
}

function commandSequence(data) {
  if (data.length < 12 || !data.subarray(0, 4).equals(COMMAND_MAGIC)) return null;
  if (data.readUInt32LE(4) !== PROTOCOL_VERSION) return null;
  const sequence = data.readUInt32LE(8);
  return sequence === 0 ? null : sequence;
}

export function createGameServer(options = {}) {
  const clock = options.clock ?? defaultClock();
  const logger = options.logger ?? console;
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
  };

  const rooms = new Map();
  const httpServer = createServer((request, response) => {
    const path = requestPath(request);
    if (path === null) {
      response.writeHead(400, { "content-type": "text/plain", "cache-control": "no-store", connection: "close" });
      response.end("Bad request\n");
      return;
    }
    if (request.method === "GET" && path === "/healthz") {
      const body = jsonBuffer({ status: "ok", rooms: rooms.size, connections: wss.clients.size, protocol: PROTOCOL_VERSION });
      response.writeHead(200, { "content-type": "application/json", "cache-control": "no-store" });
      response.end(body);
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
    if (socket.bufferedAmount + bodySize > config.maxBackpressureBytes) {
      if (volatile) return false;
      socket.terminate();
      return false;
    }
    socket.send(body, { binary });
    return true;
  }

  function sendError(socket, message) {
    send(socket, { type: "error", message });
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
    if (room.handshakeTimer !== null) {
      clock.clearTimeout(room.handshakeTimer);
      room.handshakeTimer = null;
    }
    if (room.worker && room.worker.exitCode === null && room.worker.signalCode === null) {
      room.worker.stdin.end();
      const child = room.worker;
      clock.setTimeout(() => {
        if (child.exitCode === null && child.signalCode === null) child.kill("SIGTERM");
      }, 1000);
    }
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
  }

  function finishRoom(room, winner) {
    if (room.state === "finished") return;
    room.state = "finished";
    room.result = { winner, reason: room.pendingEndReason ?? "victory" };
    for (const player of room.players) clearPlayerTimer(player);
    if (room.tickTimer !== null) {
      clock.clearInterval(room.tickTimer);
      room.tickTimer = null;
    }
    broadcast(room, { type: "result", ...room.result });
    broadcastLobby(room);
    if (room.worker?.stdin.writable) room.worker.stdin.end();
    room.expiryTimer = clock.setTimeout(() => destroyRoom(room), config.finishedExpiryMs);
  }

  function writeWorker(room, opcode, payload = Buffer.alloc(0)) {
    if (!room.worker || !room.worker.stdin.writable || room.worker.stdin.writableNeedDrain) return false;
    if (payload.length + 1 > config.maxPayloadBytes + 2) return false;
    room.worker.stdin.write(ipcFrame(opcode, payload));
    return true;
  }

  function forfeit(room, seat, reason) {
    if (room.state !== "starting" && room.state !== "active") return;
    if (room.pendingForfeit !== null) return;
    room.pendingForfeit = seat;
    room.pendingEndReason = reason;
    const body = Buffer.from([seat]);
    if (!writeWorker(room, IPC.forfeit, body)) room.queuedForfeit = body;
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
      if (seat > 1 || payload.length - 1 > config.maxPayloadBytes) throw new Error("invalid worker snapshot seat");
      const body = Buffer.from(payload.subarray(1));
      room.lastSnapshots[seat] = body;
      room.initialSnapshots.add(seat);
      if (room.state === "starting" && room.initialSnapshots.size === 2) {
        if (room.handshakeTimer !== null) clock.clearTimeout(room.handshakeTimer);
        room.handshakeTimer = null;
        room.state = "active";
        room.lastStepAt = clock.now();
        broadcast(room, { type: "started" });
        for (let initialSeat = 0; initialSeat < 2; initialSeat += 1) {
          send(room.players[initialSeat]?.socket, room.lastSnapshots[initialSeat], true, true);
        }
        broadcastLobby(room);
        if (room.queuedForfeit) {
          writeWorker(room, IPC.forfeit, room.queuedForfeit);
          room.queuedForfeit = null;
        }
        room.tickTimer = clock.setInterval(() => {
          if (room.state !== "active") return;
          const step = Buffer.allocUnsafe(4);
          step.writeUInt32LE(config.stepsPerTick);
          if (!writeWorker(room, IPC.step, step)) return;
          room.stepsSinceSnapshot += config.stepsPerTick;
          if (room.stepsSinceSnapshot >= config.snapshotEverySteps) {
            room.stepsSinceSnapshot %= config.snapshotEverySteps;
            writeWorker(room, IPC.snapshotRequest);
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
      if (seat > 1 || payload.length !== 8 + messageBytes) throw new Error("invalid worker acknowledgement");
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
      if (payload.length !== 1 || payload[0] > 1) throw new Error("invalid worker result frame");
      finishRoom(room, payload[0]);
      return;
    }
    throw new Error("unknown worker output opcode");
  }

  function startWorker(room) {
    if (room.state !== "lobby" || room.players.some((player) => !player?.ready)) return;
    room.state = "starting";
    broadcastLobby(room);
    const child = spawn(config.workerPath, ["--map", String(room.map), "--seed", String(room.seed)], {
      stdio: ["pipe", "pipe", "pipe"],
      windowsHide: true,
    });
    room.worker = child;
    let output = Buffer.alloc(0);
    child.stdout.on("data", (chunk) => {
      try {
        output = output.length === 0 ? chunk : Buffer.concat([output, chunk]);
        while (output.length >= 4) {
          const length = output.readUInt32LE(0);
          if (length < 1 || length > config.maxPayloadBytes + 2) throw new Error("invalid worker frame length");
          if (output.length < 4 + length) break;
          const frame = output.subarray(4, 4 + length);
          output = output.subarray(4 + length);
          handleWorkerFrame(room, frame[0], frame.subarray(1));
        }
        if (output.length > config.maxPayloadBytes + 6) throw new Error("worker output buffer exceeded limit");
      } catch (error) {
        logger.error?.(`Match worker protocol error: ${error instanceof Error ? error.message : String(error)}`);
        broadcast(room, { type: "error", message: "The match worker sent invalid data." });
        destroyRoom(room);
      }
    });
    child.stderr.setEncoding("utf8");
    child.stderr.on("data", (message) => logger.warn?.(`Match worker: ${message.trim().slice(0, 1000)}`));
    child.on("error", (error) => {
      logger.error?.(`Could not start match worker: ${error.message}`);
      broadcast(room, { type: "error", message: "The match worker could not start." });
      destroyRoom(room);
    });
    child.on("exit", (code, signal) => {
      room.worker = null;
      if (room.state === "finished" || !rooms.has(room.code)) return;
      logger.error?.(`Match worker stopped before the result, code=${code ?? "none"} signal=${signal ?? "none"}`);
      broadcast(room, { type: "error", message: "The match worker stopped." });
      destroyRoom(room);
    });
    room.handshakeTimer = clock.setTimeout(() => {
      if (room.state === "starting") {
        broadcast(room, { type: "error", message: "The match worker did not become ready." });
        destroyRoom(room);
      }
    }, config.workerHandshakeMs);
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
    };
  }

  function attach(socket, room, player, reconnecting = false) {
    socket.context = { room, player };
    socket.isAlive = true;
    player.socket = socket;
    player.connected = true;
    clearPlayerTimer(player);
    send(socket, { type: "welcome", room: room.code, token: player.token, team: player.seat });
    send(socket, lobbyMessage(room));
    if (room.state === "active") send(socket, { type: "started" });
    if (reconnecting && room.lastSnapshots[player.seat]) send(socket, room.lastSnapshots[player.seat], true);
    if (room.state === "finished" && room.result) send(socket, { type: "result", ...room.result });
    if (reconnecting) {
      const peer = room.players[1 - player.seat];
      send(peer?.socket, { type: "peer", connected: true, graceSeconds: 0 });
    }
    broadcastLobby(room);
  }

  function createRoom(socket, message) {
    if (rooms.size >= config.maxRooms) return sendError(socket, "The server has reached its room limit.");
    const name = cleanName(message.name);
    const map = cleanMap(message.map);
    if (!name) return sendError(socket, "Name must be 1 to 32 printable characters.");
    if (map === null) return sendError(socket, "Unknown map.");
    let code;
    do code = makeRoomCode(); while (rooms.has(code));
    const room = {
      code,
      map,
      seed: randomBytes(4).readUInt32LE(),
      state: "lobby",
      players: [null, null],
      worker: null,
      workerReady: false,
      initialSnapshots: new Set(),
      lastSnapshots: [null, null],
      result: null,
      pendingEndReason: null,
      pendingForfeit: null,
      queuedForfeit: null,
      stepsSinceSnapshot: 0,
      lastStepAt: 0,
      tickTimer: null,
      handshakeTimer: null,
      expiryTimer: null,
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
    if (!room || !player) return sendError(socket, "Invalid reconnect credentials.");
    if (player.connected) return sendError(socket, "That seat is already connected.");
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
    if (room.state === "active" || room.state === "starting") {
      forfeit(room, player.seat, "forfeit");
      room.players[player.seat] = null;
    } else if (room.state === "lobby") {
      room.players[player.seat] = null;
      broadcastLobby(room);
      if (room.players.every((candidate) => candidate === null)) destroyRoom(room);
    }
    socket.context = null;
    player.connected = false;
    player.socket = null;
    socket.close(1000, "Left match");
  }

  function surrender(socket) {
    const context = socket.context;
    if (!context || (context.room.state !== "active" && context.room.state !== "starting")) {
      return sendError(socket, "Surrender is only available during a match.");
    }
    forfeit(context.room, context.player.seat, "forfeit");
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
    if (!context || context.room.state !== "active" || context.room.pendingForfeit !== null) {
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
    if (!writeWorker(room, IPC.command, payload)) {
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
    if (room.players.filter(Boolean).every((candidate) => !candidate.connected)) {
      destroyRoom(room);
      return;
    }
    const graceSeconds = Math.ceil(config.disconnectGraceMs / 1000);
    send(room.players[1 - player.seat]?.socket, { type: "peer", connected: false, graceSeconds });
    broadcastLobby(room);
    player.disconnectTimer = clock.setTimeout(() => {
      player.disconnectTimer = null;
      if (player.connected || !rooms.has(room.code)) return;
      if (room.state === "active" || room.state === "starting") {
        forfeit(room, player.seat, "disconnect");
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
    socket.on("pong", () => { socket.isAlive = true; });
    socket.on("message", (data, isBinary) => {
      const now = clock.now();
      if (now - socket.rateWindowStart >= config.rateLimitWindowMs) {
        socket.rateWindowStart = now;
        socket.rateCount = 0;
      }
      socket.rateCount += 1;
      if (socket.rateCount > config.rateLimitCount) {
        sendError(socket, "Message rate limit exceeded.");
        socket.close(1008, "Rate limit");
        return;
      }
      const body = Buffer.isBuffer(data) ? data : Buffer.from(data);
      if (isBinary) handleCommand(socket, body);
      else handleJson(socket, body);
    });
    socket.on("close", () => onDisconnected(socket));
    socket.on("error", () => {});
  });

  httpServer.on("upgrade", (request, socket, head) => {
    const path = requestPath(request);
    if (path === null) {
      socket.write("HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 12\r\n\r\nBad request\n");
      socket.destroy();
      return;
    }
    if (path !== "/play") {
      socket.write("HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n");
      socket.destroy();
      return;
    }
    if (wss.clients.size >= config.maxConnections) {
      socket.write("HTTP/1.1 503 Service Unavailable\r\nConnection: close\r\n\r\n");
      socket.destroy();
      return;
    }
    wss.handleUpgrade(request, socket, head, (webSocket) => wss.emit("connection", webSocket, request));
  });

  const heartbeatTimer = clock.setInterval(() => {
    for (const socket of wss.clients) {
      if (socket.readyState !== WebSocket.OPEN) continue;
      if (!socket.isAlive) {
        socket.terminate();
        continue;
      }
      socket.isAlive = false;
      socket.ping();
    }
  }, config.heartbeatMs);

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
    clock.clearInterval(heartbeatTimer);
    for (const room of [...rooms.values()]) destroyRoom(room);
    for (const socket of wss.clients) socket.terminate();
    wss.close();
    if (!listening) return;
    await new Promise((resolveClose) => httpServer.close(resolveClose));
    listening = false;
  }

  return { listen, close, server: httpServer, webSocketServer: wss, rooms, config };
}
