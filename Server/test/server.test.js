import assert from "node:assert/strict";
import { EventEmitter } from "node:events";
import { existsSync } from "node:fs";
import { createConnection } from "node:net";
import { resolve } from "node:path";
import test from "node:test";
import { WebSocket } from "ws";
import { createGameServer } from "../server.js";

const workerPath = process.env.CINDERLINE_MATCH_WORKER ?? resolve(import.meta.dirname, "../../build/CinderlineMatchWorker");
const workerAvailable = existsSync(workerPath);
const protocolVersion = 7;

function workerFrame(opcode, payload = Buffer.alloc(0)) {
  const frame = Buffer.alloc(5 + payload.length);
  frame.writeUInt32LE(1 + payload.length, 0);
  frame[4] = opcode;
  payload.copy(frame, 5);
  return frame;
}

class ManualClock {
  constructor() { this.time = 0; this.next = 1; this.tasks = new Map(); }
  now = () => this.time;
  setTimeout = (fn, ms) => this.add(fn, ms, 0);
  clearTimeout = (id) => this.tasks.delete(id);
  setInterval = (fn, ms) => this.add(fn, ms, ms);
  clearInterval = (id) => this.tasks.delete(id);
  add(fn, ms, interval) {
    const id = this.next++; this.tasks.set(id, { fn, due: this.time + ms, interval }); return id;
  }
  advance(ms) {
    const end = this.time + ms;
    while (true) {
      let chosen = null;
      for (const [id, task] of this.tasks) if (task.due <= end && (!chosen || task.due < chosen.task.due)) chosen = { id, task };
      if (!chosen) break;
      this.time = chosen.task.due;
      if (chosen.task.interval) chosen.task.due += chosen.task.interval; else this.tasks.delete(chosen.id);
      chosen.task.fn();
    }
    this.time = end;
  }
}

function fakeWorkerFactory({ blocked = false, resultOnForfeit = false } = {}) {
  const workers = [];
  const factory = (_path, args) => {
    const playerCountIndex = args.indexOf("--players");
    const playerCount = playerCountIndex >= 0 ? Number(args[playerCountIndex + 1]) : 2;
    const child = new EventEmitter();
    child.args = args;
    child.playerCount = playerCount;
    child.eliminated = new Set();
    child.exitCode = null; child.signalCode = null;
    child.stdout = new EventEmitter();
    child.stderr = new EventEmitter(); child.stderr.setEncoding = () => {};
    child.stdin = new EventEmitter(); child.stdin.writable = true; child.stdin.writableNeedDrain = blocked; child.stdin.writes = [];
    child.stdin.write = (bytes) => {
      child.stdin.writes.push(Buffer.from(bytes));
      if (resultOnForfeit && bytes[4] === 3) {
        child.eliminated.add(bytes[5]);
        const survivors = Array.from({ length: playerCount }, (_, seat) => seat).filter((seat) => !child.eliminated.has(seat));
        const snapshots = Array.from({ length: playerCount }, (_, seat) => workerFrame(129, Buffer.concat([Buffer.from([seat]), Buffer.from("CSNP")])));
        const result = survivors.length <= 1 ? workerFrame(131, Buffer.from([survivors[0] ?? 255, bytes[5]])) : Buffer.alloc(0);
        queueMicrotask(() => child.stdout.emit("data", Buffer.concat([...snapshots, result])));
      }
      return !child.stdin.writableNeedDrain;
    };
    child.stdin.end = () => { child.stdin.writable = false; child.exitCode = 0; queueMicrotask(() => child.emit("exit", 0, null)); };
    child.kill = () => { child.signalCode = "SIGTERM"; child.stdin.writable = false; child.emit("exit", null, "SIGTERM"); };
    workers.push(child);
    queueMicrotask(() => {
      child.stdout.emit("data", Buffer.concat([
        workerFrame(128),
        ...Array.from({ length: playerCount }, (_, seat) => workerFrame(129, Buffer.concat([Buffer.from([seat]), Buffer.from("CSNP")]))),
      ]));
    });
    return child;
  };
  return { factory, workers };
}

function rawHttp(port, request) {
  return new Promise((resolveResponse, rejectResponse) => {
    const chunks = [];
    const socket = createConnection({ host: "127.0.0.1", port }, () => socket.end(request));
    socket.on("data", (chunk) => chunks.push(chunk));
    socket.on("end", () => resolveResponse(Buffer.concat(chunks).toString("utf8")));
    socket.on("error", rejectResponse);
  });
}

class Client {
  constructor(socket) {
    this.socket = socket;
    this.messages = [];
    this.waiters = [];
    socket.on("message", (data, binary) => {
      const message = binary ? Buffer.from(data) : JSON.parse(data.toString());
      const entry = { binary, message };
      this.messages.push(entry);
      this.flush();
    });
  }

  static async connect(url, options) {
    const socket = new WebSocket(url, options);
    await new Promise((resolveOpen, rejectOpen) => {
      socket.once("open", resolveOpen);
      socket.once("error", rejectOpen);
    });
    return new Client(socket);
  }

  flush() {
    for (const waiter of [...this.waiters]) {
      const index = this.messages.findIndex(waiter.predicate);
      if (index < 0) continue;
      const [entry] = this.messages.splice(index, 1);
      clearTimeout(waiter.timer);
      this.waiters.splice(this.waiters.indexOf(waiter), 1);
      waiter.resolve(entry.message);
    }
  }

  waitFor(predicate, timeoutMs = 4000) {
    return new Promise((resolveWait, rejectWait) => {
      const waiter = {
        predicate,
        resolve: resolveWait,
        timer: setTimeout(() => {
          this.waiters.splice(this.waiters.indexOf(waiter), 1);
          rejectWait(new Error("Timed out waiting for WebSocket message"));
        }, timeoutMs),
      };
      this.waiters.push(waiter);
      this.flush();
    });
  }

  json(type, timeoutMs) {
    return this.waitFor((entry) => !entry.binary && entry.message.type === type, timeoutMs);
  }

  snapshot() {
    return this.waitFor((entry) => entry.binary && entry.message.subarray(0, 4).toString() === "CSNP");
  }

  send(message) {
    this.socket.send(typeof message === "string" || Buffer.isBuffer(message) ? message : JSON.stringify(message));
  }

  async close() {
    if (this.socket.readyState === WebSocket.CLOSED) return;
    const closed = new Promise((resolveClose) => this.socket.once("close", resolveClose));
    this.socket.close();
    await closed;
  }
}

function readSnapshot(data) {
  assert.equal(data.subarray(0, 4).toString(), "CSNP");
  assert.equal(data.readUInt32LE(4), protocolVersion);
  let offset = 8;
  const map = data.readInt32LE(offset); offset += 4;
  const seed = data.readUInt32LE(offset); offset += 4;
  const ai = data[offset]; offset += 1;
  const aiAggression = data.readFloatLE(offset); offset += 4;
  const matchLength = data[offset]; offset += 1;
  assert(matchLength >= 0 && matchLength <= 2, "snapshot carries a valid match-length preset");
  const playerCount = data[offset]; offset += 1;
  assert(playerCount === 2 || playerCount === 4, "snapshot carries a valid player count");
  const tick = Number(data.readBigUInt64LE(offset)); offset += 8;
  const lastEffectId = data.readBigUInt64LE(offset); offset += 8;
  const winner = data.readInt8(offset); offset += 1;
  const eliminatedMask = data[offset]; offset += 1;
  const ore = data.readInt32LE(offset); offset += 4;
  const tier = data.readInt32LE(offset); offset += 4;
  const weapons = data.readInt32LE(offset); offset += 4;
  const armor = data.readInt32LE(offset); offset += 4;
  const armyRally = [data.readFloatLE(offset), data.readFloatLE(offset + 4)]; offset += 8;
  const armyRallySet = data[offset]; offset += 1;
  offset += 36;
  const obstacleCount = data.readUInt16LE(offset); offset += 2 + obstacleCount * 16;
  const entityCount = data.readUInt32LE(offset); offset += 4;
  const entities = [];
  for (let index = 0; index < entityCount; index += 1) {
    const entity = {};
    entity.id = data.readUInt32LE(offset); offset += 4;
    entity.kind = data[offset]; offset += 1;
    entity.team = data.readInt8(offset); offset += 1;
    entity.pos = [data.readFloatLE(offset), data.readFloatLE(offset + 4)]; offset += 8;
    entity.goal = [data.readFloatLE(offset), data.readFloatLE(offset + 4)]; offset += 8;
    entity.rally = [data.readFloatLE(offset), data.readFloatLE(offset + 4)]; offset += 8;
    entity.hp = data.readFloatLE(offset); offset += 4;
    entity.cooldown = data.readFloatLE(offset); offset += 4;
    entity.progress = data.readFloatLE(offset); offset += 4;
    entity.carried = data.readFloatLE(offset); offset += 4;
    entity.harvestTimer = data.readFloatLE(offset); offset += 4;
    entity.resource = data.readFloatLE(offset); offset += 4;
    entity.facing = data.readFloatLE(offset); offset += 4;
    entity.order = data[offset]; offset += 1;
    entity.target = data.readUInt32LE(offset); offset += 4;
    entity.resourceTarget = data.readUInt32LE(offset); offset += 4;
    entity.returning = data[offset]; offset += 1;
    entity.builderId = data.readUInt32LE(offset); offset += 4;
    entity.resumeGather = data[offset]; offset += 1;
    entity.rallyOverride = data[offset]; offset += 1;
    const queueCount = data[offset]; offset += 1;
    entity.queueCount = queueCount;
    entity.queue = [];
    for (let queueIndex = 0; queueIndex < queueCount; queueIndex += 1) {
      const item = {};
      item.kind = data[offset]; offset += 1;
      item.remaining = data.readFloatLE(offset); offset += 4;
      item.total = data.readFloatLE(offset); offset += 4;
      item.cost = data.readInt32LE(offset); offset += 4;
      item.research = data[offset]; offset += 1;
      item.id = data.readUInt32LE(offset); offset += 4;
      entity.queue.push(item);
    }
    const pathCount = data.readUInt16LE(offset); offset += 2 + pathCount * 8;
    offset += 8;
    entity.navigationExhausted = data[offset]; offset += 1;
    entity.nextQueueId = data.readUInt32LE(offset); offset += 4;
    entities.push(entity);
  }
  return { map, seed, ai, aiAggression, matchLength, playerCount, tick, lastEffectId, winner, eliminatedMask, ore, tier, weapons, armor, armyRally, armyRallySet, entities };
}

function command(sequence, type, units, { point = [0, 0], target = 0, kind = 0, queueIndex = 0 } = {}) {
  const data = Buffer.alloc(32 + units.length * 4);
  data.write("CCMD", 0, "ascii");
  data.writeUInt32LE(protocolVersion, 4);
  data.writeUInt32LE(sequence, 8);
  data[12] = type;
  data.writeUInt16LE(units.length, 13);
  let offset = 15;
  for (const unit of units) { data.writeUInt32LE(unit, offset); offset += 4; }
  data.writeFloatLE(point[0], offset); offset += 4;
  data.writeFloatLE(point[1], offset); offset += 4;
  data.writeUInt32LE(target, offset); offset += 4;
  data[offset] = kind; offset += 1;
  data.writeInt32LE(queueIndex, offset);
  return data;
}

async function fixture(t, overrides = {}) {
  const game = createGameServer({
    port: 0,
    workerPath,
    heartbeatMs: 60_000,
    tickIntervalMs: 5,
    stepsPerTick: 5,
    snapshotEverySteps: 10,
    logger: { error() {}, warn() {} },
    ...overrides,
  });
  const address = await game.listen();
  const url = `ws://127.0.0.1:${address.port}/play`;
  const clients = [];
  t.after(async () => {
    await Promise.all(clients.map((client) => client.close()));
    await game.close();
  });
  const connect = async (options) => {
    const client = await Client.connect(url, options);
    clients.push(client);
    return client;
  };
  return { game, connect };
}

async function startedRoom(t, overrides = {}, playerCount = 2) {
  const instance = await fixture(t, overrides);
  const names = ["Ash", "Cinder", "Coal", "Ember"];
  const players = [await instance.connect()];
  players[0].send({ type: "create", version: protocolVersion, name: names[0], map: 0, playerCount });
  const welcomes = [await players[0].json("welcome")];
  for (let seat = 1; seat < playerCount; seat += 1) {
    players.push(await instance.connect());
    players[seat].send({ type: "join", version: protocolVersion, name: names[seat], room: welcomes[0].room });
    welcomes.push(await players[seat].json("welcome"));
  }
  for (const player of players) player.send({ type: "ready", ready: true });
  await Promise.all(players.map((player) => player.json("started")));
  const snapshots = await Promise.all(players.map((player) => player.snapshot()));
  return {
    ...instance,
    players,
    welcomes,
    snapshots,
    first: players[0], second: players[1],
    firstWelcome: welcomes[0], secondWelcome: welcomes[1],
    firstSnapshot: snapshots[0], secondSnapshot: snapshots[1],
  };
}

async function snapshotUntil(client, predicate, attempts = 80) {
  for (let attempt = 0; attempt < attempts; attempt += 1) {
    const snapshot = readSnapshot(await client.snapshot());
    if (predicate(snapshot)) return snapshot;
  }
  throw new Error("Timed out waiting for matching authoritative snapshot");
}

async function eventually(predicate, attempts = 40) {
  for (let attempt = 0; attempt < attempts; attempt += 1) {
    if (predicate()) return;
    await new Promise((resolveTurn) => setImmediate(resolveTurn));
  }
  assert.fail("Timed out waiting for expected server state");
}

function bitCount(value) {
  let count = 0;
  for (let bits = value; bits !== 0; bits >>>= 1) count += bits & 1;
  return count;
}

test("health and lobby controls reject stale, malformed, repeated, and excessive input", async (t) => {
  const instance = await fixture(t, { rateLimitCount: 8 });
  const address = instance.game.server.address();
  const malformedTarget = "http://[::1";
  const badHttp = await rawHttp(address.port, `GET ${malformedTarget} HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n`);
  assert.match(badHttp, /^HTTP\/1\.1 400 /);
  assert.match(badHttp, /Bad request\n/);
  const badUpgrade = await rawHttp(
    address.port,
    `GET ${malformedTarget} HTTP/1.1\r\nHost: localhost\r\nConnection: Upgrade\r\nUpgrade: websocket\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n`,
  );
  assert.match(badUpgrade, /^HTTP\/1\.1 400 /);
  assert.match(badUpgrade, /Bad request\n/);
  const health = await fetch(`http://127.0.0.1:${address.port}/healthz`);
  assert.equal(health.status, 200);
  assert.deepEqual(await health.json(), {
    status: "ok", rooms: 0, connections: 0, workers: 0,
    states: { lobby: 0, starting: 0, active: 0, finished: 0 }, protocol: protocolVersion,
  });
  const ready = await fetch(`http://127.0.0.1:${address.port}/readyz`);
  assert.equal(ready.status, workerAvailable ? 200 : 503);
  assert.equal((await ready.json()).status, workerAvailable ? "ready" : "degraded");
  const info = await fetch(`http://127.0.0.1:${address.port}/info`);
  assert.equal(info.status, 200);
  assert.deepEqual(await info.json(), { name: "Cinderline", mode: "online", protocol: protocolVersion, maxRooms: 128 });

  const client = await instance.connect();
  client.send("{");
  assert.equal((await client.json("error")).message, "Malformed JSON.");
  client.send({ type: "create", version: 6, name: "Ash", map: 0 });
  assert.equal((await client.json("error")).message, "Unsupported protocol version.");
  client.send({ type: "join", version: protocolVersion, name: "Ash", room: "AAAAAA" });
  assert.equal((await client.json("error")).message, "Room not found.");
  client.send({ type: "create", version: protocolVersion, name: "Ash", map: 0 });
  await client.json("welcome");
  client.send({ type: "create", version: protocolVersion, name: "Again", map: 0 });
  assert.equal((await client.json("error")).message, "This connection already belongs to a room.");
  assert.equal(instance.game.rooms.size, 1);
  client.send({ type: "ping", nonce: "round-trip" });
  assert.deepEqual(await client.json("pong"), { type: "pong", nonce: "round-trip" });

  client.send({ type: "ping", nonce: 1 });
  client.send({ type: "ping", nonce: 2 });
  client.send({ type: "ping", nonce: 3 });
  assert.equal((await client.json("error")).message, "Message rate limit exceeded.");
});

test("rooms accept only two or four players and a four-player worker waits for every ready seat", async (t) => {
  const fake = fakeWorkerFactory();
  const instance = await fixture(t, { spawnWorker: fake.factory });
  for (const playerCount of [1, 3, 5, "4", null]) {
    const invalid = await instance.connect();
    invalid.send({ type: "create", version: protocolVersion, name: "Invalid", map: 0, playerCount });
    assert.equal((await invalid.json("error")).message, "Player count must be 2 or 4.");
  }

  const host = await instance.connect();
  host.send({ type: "create", version: protocolVersion, name: "Ash", map: 0, playerCount: 4 });
  const hostWelcome = await host.json("welcome");
  const hostLobby = await host.json("lobby");
  assert.equal(hostWelcome.playerCount, 4);
  assert.equal(hostLobby.playerCount, 4);
  assert.equal(hostLobby.players.length, 4);

  const players = [host];
  for (const name of ["Cinder", "Coal", "Ember"]) {
    const player = await instance.connect();
    player.send({ type: "join", version: protocolVersion, name, room: hostWelcome.room });
    const welcome = await player.json("welcome");
    assert.equal(welcome.playerCount, 4);
    players.push(player);
  }
  const extra = await instance.connect();
  extra.send({ type: "join", version: protocolVersion, name: "Fifth", room: hostWelcome.room });
  assert.equal((await extra.json("error")).message, "Room is full.");

  for (const player of players.slice(0, 3)) player.send({ type: "ready", ready: true });
  await eventually(() => [...instance.game.rooms.values()][0].players.filter((player) => player?.ready).length === 3);
  assert.equal(fake.workers.length, 0, "a four-player room cannot start with only three ready seats");
  players[3].send({ type: "ready", ready: true });
  await Promise.all(players.map((player) => player.json("started")));
  assert.equal(fake.workers.length, 1);
  assert.deepEqual(fake.workers[0].args.slice(-2), ["--players", "4"]);

  const defaultHost = await instance.connect();
  defaultHost.send({ type: "create", version: protocolVersion, name: "Legacy", map: 0 });
  assert.equal((await defaultHost.json("welcome")).playerCount, 2, "an omitted count preserves 1v1 rooms");
  assert.equal((await defaultHost.json("lobby")).players.length, 2);
});

test("four simultaneous disconnects preserve seats through grace and finish through the worker", async (t) => {
  const fake = fakeWorkerFactory({ resultOnForfeit: true });
  const room = await startedRoom(t, { spawnWorker: fake.factory, disconnectGraceMs: 20, heartbeatMs: 60_000 }, 4);
  const tokens = room.welcomes.map((welcome) => welcome.token);
  await Promise.all(room.players.map((player) => player.close()));
  assert.equal(room.game.rooms.size, 1);
  const serverRoom = [...room.game.rooms.values()][0];
  await eventually(() => serverRoom.players.every((player) => player && !player.connected));
  assert.equal(serverRoom.players.length, 4);
  assert.equal(serverRoom.players.every((player) => player && !player.connected), true, "active seat records survive the grace window");
  await new Promise((resolveWait) => setTimeout(resolveWait, 35));
  await eventually(() => serverRoom.state === "finished", 200);
  assert.equal(serverRoom.result.reason, "disconnect");
  assert(serverRoom.result.winner >= 0 && serverRoom.result.winner < 4);
  assert.equal(serverRoom.players.filter((player) => player.eliminated).length, 3);
  assert.equal(serverRoom.players[serverRoom.result.winner].eliminated, false);

  const observer = await room.connect();
  observer.send({ type: "reconnect", version: protocolVersion, room: room.welcomes[0].room, token: tokens[2] });
  assert.deepEqual(await observer.json("welcome"), {
    type: "welcome", room: room.welcomes[0].room, token: tokens[2], team: 2, playerCount: 4,
  });
  assert.deepEqual(await observer.json("result"), { type: "result", ...serverRoom.result });
});

test("multiple four-player disconnects can reconnect independently within grace", async (t) => {
  const clock = new ManualClock();
  const fake = fakeWorkerFactory();
  const room = await startedRoom(t, {
    spawnWorker: fake.factory, clock, disconnectGraceMs: 50, heartbeatMs: 60_000, tickIntervalMs: 60_000,
  }, 4);
  await Promise.all([room.players[0].close(), room.players[1].close()]);
  const serverRoom = [...room.game.rooms.values()][0];
  await eventually(() => !serverRoom.players[0].connected && !serverRoom.players[1].connected);
  assert.deepEqual(serverRoom.players.map((player) => player.connected), [false, false, true, true]);

  const replacements = await Promise.all([room.connect(), room.connect()]);
  for (let seat = 0; seat < 2; seat += 1) {
    replacements[seat].send({
      type: "reconnect", version: protocolVersion,
      room: room.welcomes[seat].room, token: room.welcomes[seat].token,
    });
  }
  const replacementWelcomes = await Promise.all(replacements.map((player) => player.json("welcome")));
  assert.deepEqual(replacementWelcomes.map((welcome) => welcome.team), [0, 1]);
  clock.advance(60);
  assert.equal(fake.workers[0].stdin.writes.some((frame) => frame[4] === 3), false, "reconnected seats are not forfeited by stale timers");
  assert.equal(serverRoom.state, "active");
  assert.deepEqual(serverRoom.players.map((player) => player.connected), [true, true, true, true]);
});

test("worker result byte 255 is exposed as a draw", async (t) => {
  const fake = fakeWorkerFactory();
  const room = await startedRoom(t, { spawnWorker: fake.factory, tickIntervalMs: 60_000 }, 4);
  fake.workers[0].stdout.emit("data", workerFrame(131, Buffer.from([255, 255])));
  const results = await Promise.all(room.players.map((player) => player.json("result")));
  assert.equal(results.every((result) => result.winner === -2 && result.reason === "victory"), true);
});

test("worker backpressure preserves a prioritized forfeit until drain", async (t) => {
  const fake = fakeWorkerFactory({ blocked: true });
  const room = await startedRoom(t, { spawnWorker: fake.factory, heartbeatMs: 60_000 });
  room.first.send({ type: "surrender" });
  await eventually(() => [...room.game.rooms.values()][0]?.players[0].eliminated);
  const worker = fake.workers[0];
  assert.equal(worker.stdin.writes.length, 0, "blocked worker input accepts no partial writes");
  worker.stdin.writableNeedDrain = false;
  worker.stdin.emit("drain");
  assert.equal(worker.stdin.writes[0][4], 3, "forfeit is the first serialized frame after drain");
  assert.equal(worker.stdin.writes.filter((frame) => frame[4] === 3).length, 1, "forfeit is written exactly once");
});

test("commands queued during worker backpressure receive and cache one authoritative acknowledgement", async (t) => {
  const fake = fakeWorkerFactory({ blocked: true });
  const room = await startedRoom(t, { spawnWorker: fake.factory, tickIntervalMs: 60_000, heartbeatMs: 60_000 });
  const request = command(7, 4, [11]);
  room.first.send(request);
  const serverRoom = [...room.game.rooms.values()][0];
  await eventually(() => serverRoom.players[0].pendingSequences.has(7));
  const worker = fake.workers[0];
  worker.stdin.writableNeedDrain = false;
  worker.stdin.emit("drain");
  assert.equal(worker.stdin.writes[0][4], 2, "the reliable command is retained until drain");

  const message = Buffer.from("accepted");
  const acknowledgement = Buffer.alloc(8 + message.length);
  acknowledgement[0] = 0;
  acknowledgement.writeUInt32LE(7, 1);
  acknowledgement[5] = 1;
  acknowledgement.writeUInt16LE(message.length, 6);
  message.copy(acknowledgement, 8);
  worker.stdout.emit("data", workerFrame(130, acknowledgement));
  assert.deepEqual(await room.first.json("ack"), { type: "ack", seq: 7, accepted: true, message: "accepted" });
  room.first.send(request);
  assert.deepEqual(await room.first.json("ack"), { type: "ack", seq: 7, accepted: true, message: "accepted" });
  assert.equal(worker.stdin.writes.filter((frame) => frame[4] === 2).length, 1, "a retry reuses the cached acknowledgement");
});

test("active worker progress timeout closes a hung room independently of socket pong", async (t) => {
  const clock = new ManualClock();
  const fake = fakeWorkerFactory();
  const room = await startedRoom(t, { spawnWorker: fake.factory, clock, workerProgressTimeoutMs: 500, heartbeatMs: 60_000 });
  const error = room.first.json("error");
  clock.advance(500);
  assert.deepEqual(await error, { type: "error", message: "The match worker stopped responding.", code: "worker_unavailable", retryable: true });
  assert.equal(room.game.rooms.size, 0);
});

test("sustained snapshot backpressure disconnects a slow client for normal recovery", async (t) => {
  const clock = new ManualClock();
  const fake = fakeWorkerFactory();
  let congested = false;
  let slowSocket = null;
  const room = await startedRoom(t, {
    spawnWorker: fake.factory, clock, heartbeatMs: 500, slowClientTimeoutMs: 500,
    bufferedAmountFor: (socket) => congested && socket === slowSocket ? 3 * 1024 * 1024 : 0,
  });
  slowSocket = [...room.game.webSocketServer.clients].find((socket) => socket.context?.player?.seat === 0);
  assert(slowSocket);
  const closed = new Promise((resolveClose) => room.first.socket.once("close", resolveClose));
  congested = true;
  fake.workers[0].stdout.emit("data", workerFrame(129, Buffer.concat([Buffer.from([0]), Buffer.from("CSNP")])))
  clock.advance(500);
  await closed;
  const peer = await room.second.json("peer");
  assert.equal(peer.connected, false);
  assert.equal(peer.team, 0);
  congested = false;
  const replacement = await room.connect();
  replacement.send({ type: "reconnect", version: protocolVersion, room: room.firstWelcome.room, token: room.firstWelcome.token });
  assert.equal((await replacement.json("welcome")).team, 0);
  await replacement.snapshot();
});

test("draining fails readiness and new room creation while keeping the listener observable", async (t) => {
  const instance = await fixture(t, { spawnWorker: fakeWorkerFactory().factory });
  const address = instance.game.server.address();
  instance.game.drain();
  const ready = await fetch(`http://127.0.0.1:${address.port}/readyz`);
  assert.equal(ready.status, 503);
  assert.equal((await ready.json()).status, "degraded");
  const health = await fetch(`http://127.0.0.1:${address.port}/healthz`);
  assert.equal(health.status, 200);
  const client = await instance.connect();
  client.send({ type: "create", version: protocolVersion, name: "Ash", map: 0 });
  assert.deepEqual(await client.json("error"), {
    type: "error", message: "The server is draining.", code: "server_busy", retryable: true,
  });
});

test("origin, per-address connections, room creation, and public metadata are bounded", async (t) => {
  const instance = await fixture(t, {
    allowedOrigins: ["https://play.example"], maxConnectionsPerIp: 2,
    roomCreateLimitCount: 1, serverName: "Cinderline LAN", networkMode: "lan",
  });
  const address = instance.game.server.address();
  const info = await fetch(`http://127.0.0.1:${address.port}/info`);
  assert.deepEqual(await info.json(), { name: "Cinderline LAN", mode: "lan", protocol: protocolVersion, maxRooms: 128 });
  const first = await instance.connect();
  first.send({ type: "create", version: protocolVersion, name: "Ash", map: 0 });
  await first.json("welcome");
  const second = await instance.connect();
  second.send({ type: "create", version: protocolVersion, name: "Coal", map: 0 });
  assert.deepEqual(await second.json("error"), { type: "error", message: "Room creation rate limit exceeded.", code: "rate_limited", retryable: true });
  await assert.rejects(Client.connect(`ws://127.0.0.1:${address.port}/play`), /Unexpected server response: 429/);
  const rejectedOrigin = new WebSocket(`ws://127.0.0.1:${address.port}/play`, { origin: "https://evil.example" });
  await assert.rejects(new Promise((resolveOpen, rejectOpen) => { rejectedOrigin.once("open", resolveOpen); rejectedOrigin.once("error", rejectOpen); }), /Unexpected server response: 403/);
});

test("forwarded addresses are trusted only from an explicitly configured proxy", async (t) => {
  const trusted = await fixture(t, { trustedProxyAddresses: ["127.0.0.1"], maxConnectionsPerIp: 1 });
  await trusted.connect({ headers: { "x-forwarded-for": "198.51.100.10" } });
  await trusted.connect({ headers: { "x-forwarded-for": "198.51.100.11" } });

  const untrusted = await fixture(t, { maxConnectionsPerIp: 1 });
  await untrusted.connect({ headers: { "x-forwarded-for": "198.51.100.20" } });
  await assert.rejects(
    untrusted.connect({ headers: { "x-forwarded-for": "198.51.100.21" } }),
    /Unexpected server response: 429/,
  );
});

test("active worker capacity queues a ready room and starts it when capacity returns", async (t) => {
  const fake = fakeWorkerFactory({ resultOnForfeit: true });
  const instance = await fixture(t, { spawnWorker: fake.factory, maxActiveWorkers: 1, heartbeatMs: 60_000 });
  const [a, b, c, d] = await Promise.all([instance.connect(), instance.connect(), instance.connect(), instance.connect()]);
  a.send({ type: "create", version: protocolVersion, name: "A", map: 0 });
  const firstWelcome = await a.json("welcome");
  b.send({ type: "join", version: protocolVersion, name: "B", room: firstWelcome.room });
  await b.json("welcome");
  a.send({ type: "ready", ready: true }); b.send({ type: "ready", ready: true });
  await Promise.all([a.json("started"), b.json("started")]);

  c.send({ type: "create", version: protocolVersion, name: "C", map: 1 });
  const secondWelcome = await c.json("welcome");
  d.send({ type: "join", version: protocolVersion, name: "D", room: secondWelcome.room });
  await d.json("welcome");
  c.send({ type: "ready", ready: true }); d.send({ type: "ready", ready: true });
  const capacity = await c.json("error");
  assert.equal(capacity.code, "worker_capacity");
  assert.equal(fake.workers.length, 1);

  const nextStarted = Promise.all([c.json("started"), d.json("started")]);
  a.send({ type: "surrender" });
  await nextStarted;
  assert.equal(fake.workers.length, 2, "queued room starts after the prior worker exits");
});

test("real clients create, join, reject a third seat, ready, and receive private snapshots", { skip: !workerAvailable }, async (t) => {
  const instance = await fixture(t);
  const first = await instance.connect();
  first.send({ type: "create", version: protocolVersion, name: "Ash", map: 1 });
  const firstWelcome = await first.json("welcome");
  const hostLobby = await first.json("lobby");
  assert.match(firstWelcome.room, /^[A-HJ-NP-Z2-9]{6}$/);
  assert.equal(firstWelcome.team, 0);
  assert.equal(firstWelcome.playerCount, 2);
  assert.equal(typeof firstWelcome.token, "string");
  assert.deepEqual(hostLobby.players, [
    { name: "Ash", connected: true, ready: false },
    { name: "", connected: false, ready: false },
  ]);
  assert.equal(hostLobby.playerCount, 2);

  const second = await instance.connect();
  second.send({ type: "join", version: protocolVersion, name: "Cinder", room: firstWelcome.room });
  const secondWelcome = await second.json("welcome");
  assert.equal(secondWelcome.team, 1);
  assert.notEqual(secondWelcome.token, firstWelcome.token);

  const third = await instance.connect();
  third.send({ type: "join", version: protocolVersion, name: "Coal", room: firstWelcome.room });
  assert.equal((await third.json("error")).message, "Room is full.");

  first.send({ type: "ready", ready: true });
  second.send({ type: "ready", ready: true });
  await Promise.all([first.json("started"), second.json("started")]);
  const [left, right] = await Promise.all([first.snapshot(), second.snapshot()]);
  const leftView = readSnapshot(left);
  const rightView = readSnapshot(right);
  assert.equal(leftView.map, 1);
  assert.equal(leftView.ai, 0);
  assert.equal(leftView.matchLength, 1, "hosted matches remain Standard by default");
  assert.equal(rightView.matchLength, 1, "both seats receive the shared Standard preset");
  assert.equal(leftView.ore, 500);
  assert.equal(rightView.ore, 500);
  assert(leftView.entities.some((entity) => entity.team === 0 && entity.kind === 8));
  assert(rightView.entities.some((entity) => entity.team === 0 && entity.kind === 8));
  assert.equal(leftView.entities.some((entity) => entity.team === 1), false, "initial view hides the distant opponent");
  assert.equal(rightView.entities.some((entity) => entity.team === 1), false, "initial view hides the distant opponent");
  const leftIds = new Set(leftView.entities.map((entity) => entity.id));
  assert.equal(rightView.entities.some((entity) => leftIds.has(entity.id)), false, "seat views use separate opaque handles");
});

test("both seats command only their view handles and command sequences survive reconnect", { skip: !workerAvailable }, async (t) => {
  const room = await startedRoom(t);
  const left = readSnapshot(room.firstSnapshot);
  const right = readSnapshot(room.secondSnapshot);
  const leftWorker = left.entities.find((entity) => entity.team === 0 && entity.kind === 0).id;
  const rightWorker = right.entities.find((entity) => entity.team === 0 && entity.kind === 0).id;
  room.first.send(command(1, 4, [leftWorker]));
  room.second.send(command(1, 4, [rightWorker]));
  assert.equal((await room.first.json("ack")).accepted, true);
  assert.equal((await room.second.json("ack")).accepted, true);

  room.second.send(command(2, 4, [leftWorker]));
  const spoofed = await room.second.json("ack");
  assert.equal(spoofed.seq, 2);
  assert.equal(spoofed.accepted, false);

  const leftHeadquarters = left.entities.find((entity) => entity.team === 0 && entity.kind === 8).id;
  const train = command(2, 7, [leftHeadquarters], { kind: 0 });
  room.first.send(train);
  const spent = await room.first.json("ack");
  assert.equal(spent.accepted, true);
  room.first.send(train);
  assert.deepEqual(await room.first.json("ack"), spent);
  let snapshot;
  do snapshot = readSnapshot(await room.first.snapshot()); while (snapshot.ore === 500);
  assert.equal(snapshot.ore, 440, "a duplicate train sequence spends ore once");

  await room.first.close();
  await room.second.json("peer");
  const attacker = await room.connect();
  attacker.send({ type: "reconnect", version: protocolVersion, room: room.firstWelcome.room, token: "wrong-token" });
  assert.equal((await attacker.json("error")).message, "Invalid reconnect credentials.");
  attacker.send({ type: "reconnect", version: protocolVersion, room: room.firstWelcome.room, token: room.secondWelcome.token });
  assert.equal((await attacker.json("error")).message, "That seat is already connected.");
  const replacement = await room.connect();
  replacement.send({ type: "reconnect", version: protocolVersion, room: room.firstWelcome.room, token: room.firstWelcome.token });
  assert.equal((await replacement.json("welcome")).team, 0);
  const reconnectedLobby = await replacement.json("lobby");
  assert.equal(reconnectedLobby.players.length, 2);
  assert.deepEqual(reconnectedLobby.players.map((player) => player.name), ["Ash", "Cinder"]);
  assert.equal(reconnectedLobby.players[0].connected, true);
  await replacement.snapshot();
  replacement.send(train);
  assert.deepEqual(await replacement.json("ack"), spent);
});

test("automatic batches and stable queue cancellation remain authoritative online", { skip: !workerAvailable }, async (t) => {
  const room = await startedRoom(t, { tickIntervalMs: 20, stepsPerTick: 1, snapshotEverySteps: 1 });
  const left = readSnapshot(room.firstSnapshot);
  const headquarters = left.entities.find((entity) => entity.team === 0 && entity.kind === 8);
  assert(headquarters);
  assert.equal(headquarters.queueCount, 0);
  assert.equal(headquarters.nextQueueId, 1);

  room.first.send(command(1, 15, [], { target: headquarters.id, kind: 0, queueIndex: 2 }));
  const queued = await room.first.json("ack");
  assert.equal(queued.seq, 1);
  assert.equal(queued.accepted, true);

  room.second.send(command(1, 15, [], { target: headquarters.id, kind: 0, queueIndex: 1 }));
  const unknownPin = await room.second.json("ack");
  assert.equal(unknownPin.seq, 1);
  assert.equal(unknownPin.accepted, false, "another seat cannot use a producer handle from the first seat's private view");

  const batchSnapshot = await snapshotUntil(room.first, (snapshot) => {
    const producer = snapshot.entities.find((entity) => entity.id === headquarters.id);
    return snapshot.ore === 380 && producer?.queueCount === 2;
  });
  const batchProducer = batchSnapshot.entities.find((entity) => entity.id === headquarters.id);
  assert.deepEqual(batchProducer.queue.map((item) => item.id), [1, 2]);
  assert.equal(batchProducer.nextQueueId, 3);
  assert.deepEqual(batchProducer.queue.map((item) => item.cost), [60, 60]);

  room.first.send(command(2, 8, [headquarters.id], { target: 2, queueIndex: 0 }));
  const cancelled = await room.first.json("ack");
  assert.equal(cancelled.accepted, true);
  const cancelledSnapshot = await snapshotUntil(room.first, (snapshot) => {
    const producer = snapshot.entities.find((entity) => entity.id === headquarters.id);
    return snapshot.ore === 440 && producer?.queueCount === 1;
  });
  const cancelledProducer = cancelledSnapshot.entities.find((entity) => entity.id === headquarters.id);
  assert.deepEqual(cancelledProducer.queue.map((item) => item.id), [1], "job ID targets the second item even though legacy index zero was supplied");
  assert.equal(cancelledProducer.nextQueueId, 3, "cancellation never reuses a producer job ID");

  room.first.send(command(3, 8, [headquarters.id], { target: 2, queueIndex: 0 }));
  assert.equal((await room.first.json("ack")).accepted, false, "a stale job ID cannot cancel the shifted queue head");

  room.first.send(command(4, 17, [], { point: [1550, 1400], kind: 14 }));
  assert.equal((await room.first.json("ack")).accepted, true, "team rally can be set online before a combat producer exists");
  const defaultRallySnapshot = await snapshotUntil(room.first, (snapshot) => snapshot.armyRallySet === 1);
  assert.deepEqual(defaultRallySnapshot.armyRally, [1550, 1400], "the viewer receives its persistent rally through the authoritative snapshot");

  room.first.send(command(5, 17, [], { point: [1300, 1150], target: headquarters.id, kind: 8 }));
  assert.equal((await room.first.json("ack")).accepted, true);
  const rallySnapshot = await snapshotUntil(room.first, (snapshot) => {
    const producer = snapshot.entities.find((entity) => entity.id === headquarters.id);
    return producer?.rally[0] === 1300 && producer.rally[1] === 1150;
  });
  assert.equal(rallySnapshot.ore, 440,"automatic rally remains a free authoritative order");

  room.first.send(command(6, 14, [], { point: [950, 950], kind: 10 }));
  const assignedBuild = await room.first.json("ack");
  assert.equal(assignedBuild.accepted, true);
  assert.equal(assignedBuild.message, "Kiln assigned to a Drudge. Open JOBS to view it.");
  assert.equal(/\d/.test(assignedBuild.message), false, "public acknowledgements do not leak authoritative worker or foundation IDs");
});

test("four authoritative clients keep private ownership through nonfinal eliminations and one final winner", { skip: !workerAvailable, timeout: 20_000 }, async (t) => {
  const room = await startedRoom(t, { tickIntervalMs: 5, stepsPerTick: 2, snapshotEverySteps: 2 }, 4);
  const views = room.snapshots.map(readSnapshot);
  for (let seat = 0; seat < 4; seat += 1) {
    assert.equal(room.welcomes[seat].team, seat);
    assert.equal(room.welcomes[seat].playerCount, 4);
    assert.equal(views[seat].playerCount, 4);
    assert.equal(views[seat].eliminatedMask, 0);
    assert(views[seat].entities.some((entity) => entity.team === 0 && entity.kind === 8));
    assert.equal(views[seat].entities.some((entity) => entity.team > 0), false, "initial fog hides every distant FFA opponent");
    const worker = views[seat].entities.find((entity) => entity.team === 0 && entity.kind === 0);
    assert(worker);
    room.players[seat].send(command(1, 4, [worker.id]));
  }
  const ownershipAcks = await Promise.all(room.players.map((player) => player.json("ack")));
  assert.equal(ownershipAcks.every((ack) => ack.accepted), true, "all four seats can command their own private handles");

  const foreignWorker = views[0].entities.find((entity) => entity.team === 0 && entity.kind === 0).id;
  assert.equal(views[2].entities.some((entity) => entity.id === foreignWorker), false);
  room.players[2].send(command(2, 4, [foreignWorker]));
  assert.equal((await room.players[2].json("ack")).accepted, false, "a seat cannot command another viewer's opaque handle");

  room.players[0].send({ type: "surrender" });
  const afterFirst = await Promise.all(room.players.map((player) => snapshotUntil(player, (snapshot) => bitCount(snapshot.eliminatedMask) === 1)));
  assert.equal([...room.game.rooms.values()][0].state, "active", "one FFA elimination does not finish the match");
  assert.equal(afterFirst.every((snapshot) => snapshot.winner === -1), true);
  room.players[0].send(command(3, 4, [views[0].entities[0].id]));
  assert.equal((await room.players[0].json("error")).message, "The match is not accepting commands.");

  await room.players[0].close();
  const disconnected = await Promise.all(room.players.slice(1).map((player) => player.json("peer")));
  assert.equal(disconnected.every((peer) => peer.team === 0 && !peer.connected && peer.graceSeconds === 0), true);
  const replacement = await room.connect();
  replacement.send({
    type: "reconnect", version: protocolVersion,
    room: room.welcomes[0].room, token: room.welcomes[0].token,
  });
  assert.equal((await replacement.json("welcome")).team, 0);
  await replacement.json("lobby");
  await replacement.json("started");
  assert.equal(bitCount(readSnapshot(await replacement.snapshot()).eliminatedMask), 1);
  const reconnected = await Promise.all(room.players.slice(1).map((player) => player.json("peer")));
  assert.equal(reconnected.every((peer) => peer.team === 0 && peer.connected && peer.graceSeconds === 0), true);
  room.players[0] = replacement;

  room.players[1].send({ type: "surrender" });
  const afterSecond = await Promise.all(room.players.map((player) => snapshotUntil(player, (snapshot) => bitCount(snapshot.eliminatedMask) === 2)));
  assert.equal([...room.game.rooms.values()][0].state, "active", "two FFA eliminations leave two players competing");
  assert.equal(afterSecond.every((snapshot) => snapshot.winner === -1), true);

  room.players[2].send({ type: "surrender" });
  const results = await Promise.all(room.players.map((player) => player.json("result")));
  assert.equal(results.every((result) => result.winner === 3 && result.reason === "forfeit"), true);
  room.players[0].send({ type: "leave" });
  await new Promise((resolveClose) => room.players[0].socket.once("close", resolveClose));
});

test("surrender keeps the socket for the authoritative result", { skip: !workerAvailable }, async (t) => {
  const room = await startedRoom(t);
  room.first.send({ type: "surrender" });
  const [leftResult, rightResult] = await Promise.all([room.first.json("result"), room.second.json("result")]);
  assert.deepEqual(leftResult, { type: "result", winner: 1, reason: "forfeit" });
  assert.deepEqual(rightResult, leftResult);
  assert.equal(room.first.socket.readyState, WebSocket.OPEN);
  room.first.send(command(9, 4, [readSnapshot(room.firstSnapshot).entities[0].id]));
  assert.equal((await room.first.json("error")).message, "The match is not accepting commands.");
});

test("disconnect grace expires into a canonical result", { skip: !workerAvailable }, async (t) => {
  const room = await startedRoom(t, { disconnectGraceMs: 30 });
  await room.first.close();
  const peer = await room.second.json("peer");
  assert.deepEqual(peer, { type: "peer", team: 0, connected: false, graceSeconds: 1 });
  const result = await room.second.json("result");
  assert.deepEqual(result, { type: "result", winner: 1, reason: "disconnect" });
});

test("a simultaneous outage preserves the active room for reconnect, then expires through the worker", { skip: !workerAvailable }, async (t) => {
  const room = await startedRoom(t, { disconnectGraceMs: 80 });
  await Promise.all([room.first.close(), room.second.close()]);
  assert.equal(room.game.rooms.size, 1, "both disconnected seats retain the active worker during grace");

  const replacement = await room.connect();
  replacement.send({ type: "reconnect", version: protocolVersion, room: room.firstWelcome.room, token: room.firstWelcome.token });
  assert.equal((await replacement.json("welcome")).team, 0);
  await replacement.snapshot();
  await replacement.close();

  await new Promise((resolveWait) => setTimeout(resolveWait, 110));
  const observer = await room.connect();
  observer.send({ type: "reconnect", version: protocolVersion, room: room.firstWelcome.room, token: room.secondWelcome.token });
  assert.equal((await observer.json("welcome")).team, 1);
  assert.deepEqual(await observer.json("result"), { type: "result", winner: 0, reason: "disconnect" });
});

test("ordinary attack-move play can finish a complete authoritative match", { skip: !workerAvailable, timeout: 30_000 }, async (t) => {
  const room = await startedRoom(t, { tickIntervalMs: 1, stepsPerTick: 5, snapshotEverySteps: 50 });
  const view = readSnapshot(room.firstSnapshot);
  const workers = view.entities.filter((entity) => entity.team === 0 && entity.kind === 0).map((entity) => entity.id);
  assert.equal(workers.length, 5);
  room.first.send(command(1, 2, workers, { point: [4200, 4200] }));
  assert.equal((await room.first.json("ack")).accepted, true);
  const [leftResult, rightResult] = await Promise.all([room.first.json("result", 25_000), room.second.json("result", 25_000)]);
  assert.deepEqual(leftResult, { type: "result", winner: 0, reason: "victory" });
  assert.deepEqual(rightResult, leftResult);
});
