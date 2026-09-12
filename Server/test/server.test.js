import assert from "node:assert/strict";
import { existsSync } from "node:fs";
import { createConnection } from "node:net";
import { resolve } from "node:path";
import test from "node:test";
import { WebSocket } from "ws";
import { createGameServer } from "../server.js";

const workerPath = process.env.CINDERLINE_MATCH_WORKER ?? resolve(import.meta.dirname, "../../build/CinderlineMatchWorker");
const workerAvailable = existsSync(workerPath);

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

  static async connect(url) {
    const socket = new WebSocket(url);
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
  assert.equal(data.readUInt32LE(4), 1);
  let offset = 8;
  const map = data.readInt32LE(offset); offset += 4;
  const seed = data.readUInt32LE(offset); offset += 4;
  const ai = data[offset]; offset += 1;
  const aiAggression = data.readFloatLE(offset); offset += 4;
  const tick = Number(data.readBigUInt64LE(offset)); offset += 8;
  const lastEffectId = data.readBigUInt64LE(offset); offset += 8;
  const winner = data.readInt8(offset); offset += 1;
  const ore = data.readInt32LE(offset); offset += 4;
  const tier = data.readInt32LE(offset); offset += 4;
  const weapons = data.readInt32LE(offset); offset += 4;
  const armor = data.readInt32LE(offset); offset += 4;
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
    const queueCount = data[offset]; offset += 1;
    entity.queueCount = queueCount;
    offset += queueCount * 14;
    const pathCount = data.readUInt16LE(offset); offset += 2 + pathCount * 8;
    offset += 8;
    entities.push(entity);
  }
  return { map, seed, ai, aiAggression, tick, lastEffectId, winner, ore, tier, weapons, armor, entities };
}

function command(sequence, type, units, { point = [0, 0], target = 0, kind = 0, queueIndex = 0 } = {}) {
  const data = Buffer.alloc(32 + units.length * 4);
  data.write("CCMD", 0, "ascii");
  data.writeUInt32LE(1, 4);
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
  const connect = async () => {
    const client = await Client.connect(url);
    clients.push(client);
    return client;
  };
  return { game, connect };
}

async function startedRoom(t, overrides = {}) {
  const instance = await fixture(t, overrides);
  const first = await instance.connect();
  first.send({ type: "create", version: 1, name: "Ash", map: 0 });
  const firstWelcome = await first.json("welcome");
  const second = await instance.connect();
  second.send({ type: "join", version: 1, name: "Cinder", room: firstWelcome.room });
  const secondWelcome = await second.json("welcome");
  first.send({ type: "ready", ready: true });
  second.send({ type: "ready", ready: true });
  await Promise.all([first.json("started"), second.json("started")]);
  const [firstSnapshot, secondSnapshot] = await Promise.all([first.snapshot(), second.snapshot()]);
  return { ...instance, first, second, firstWelcome, secondWelcome, firstSnapshot, secondSnapshot };
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
  assert.deepEqual(await health.json(), { status: "ok", rooms: 0, connections: 0, protocol: 1 });

  const client = await instance.connect();
  client.send("{");
  assert.equal((await client.json("error")).message, "Malformed JSON.");
  client.send({ type: "create", version: 99, name: "Ash", map: 0 });
  assert.equal((await client.json("error")).message, "Unsupported protocol version.");
  client.send({ type: "join", version: 1, name: "Ash", room: "AAAAAA" });
  assert.equal((await client.json("error")).message, "Room not found.");
  client.send({ type: "create", version: 1, name: "Ash", map: 0 });
  await client.json("welcome");
  client.send({ type: "create", version: 1, name: "Again", map: 0 });
  assert.equal((await client.json("error")).message, "This connection already belongs to a room.");
  assert.equal(instance.game.rooms.size, 1);
  client.send({ type: "ping", nonce: "round-trip" });
  assert.deepEqual(await client.json("pong"), { type: "pong", nonce: "round-trip" });

  client.send({ type: "ping", nonce: 1 });
  client.send({ type: "ping", nonce: 2 });
  client.send({ type: "ping", nonce: 3 });
  assert.equal((await client.json("error")).message, "Message rate limit exceeded.");
});

test("real clients create, join, reject a third seat, ready, and receive private snapshots", { skip: !workerAvailable }, async (t) => {
  const instance = await fixture(t);
  const first = await instance.connect();
  first.send({ type: "create", version: 1, name: "Ash", map: 1 });
  const firstWelcome = await first.json("welcome");
  const hostLobby = await first.json("lobby");
  assert.match(firstWelcome.room, /^[A-HJ-NP-Z2-9]{6}$/);
  assert.equal(firstWelcome.team, 0);
  assert.equal(typeof firstWelcome.token, "string");
  assert.deepEqual(hostLobby.players, [
    { name: "Ash", connected: true, ready: false },
    { name: "", connected: false, ready: false },
  ]);

  const second = await instance.connect();
  second.send({ type: "join", version: 1, name: "Cinder", room: firstWelcome.room });
  const secondWelcome = await second.json("welcome");
  assert.equal(secondWelcome.team, 1);
  assert.notEqual(secondWelcome.token, firstWelcome.token);

  const third = await instance.connect();
  third.send({ type: "join", version: 1, name: "Coal", room: firstWelcome.room });
  assert.equal((await third.json("error")).message, "Room is full.");

  first.send({ type: "ready", ready: true });
  second.send({ type: "ready", ready: true });
  await Promise.all([first.json("started"), second.json("started")]);
  const [left, right] = await Promise.all([first.snapshot(), second.snapshot()]);
  const leftView = readSnapshot(left);
  const rightView = readSnapshot(right);
  assert.equal(leftView.map, 1);
  assert.equal(leftView.ai, 0);
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
  attacker.send({ type: "reconnect", version: 1, room: room.firstWelcome.room, token: "wrong-token" });
  assert.equal((await attacker.json("error")).message, "Invalid reconnect credentials.");
  attacker.send({ type: "reconnect", version: 1, room: room.firstWelcome.room, token: room.secondWelcome.token });
  assert.equal((await attacker.json("error")).message, "That seat is already connected.");
  const replacement = await room.connect();
  replacement.send({ type: "reconnect", version: 1, room: room.firstWelcome.room, token: room.firstWelcome.token });
  assert.equal((await replacement.json("welcome")).team, 0);
  const reconnectedLobby = await replacement.json("lobby");
  assert.equal(reconnectedLobby.players.length, 2);
  assert.deepEqual(reconnectedLobby.players.map((player) => player.name), ["Ash", "Cinder"]);
  assert.equal(reconnectedLobby.players[0].connected, true);
  await replacement.snapshot();
  replacement.send(train);
  assert.deepEqual(await replacement.json("ack"), spent);
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
  assert.deepEqual(peer, { type: "peer", connected: false, graceSeconds: 1 });
  const result = await room.second.json("result");
  assert.deepEqual(result, { type: "result", winner: 1, reason: "disconnect" });
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
