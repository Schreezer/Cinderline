import assert from "node:assert/strict";
import { EventEmitter } from "node:events";
import test from "node:test";
import { WORKER_IPC } from "../protocol.js";
import { WorkerBridge } from "../worker-bridge.js";

function workerFrame(opcode, payload = Buffer.alloc(0)) {
  const encoded = Buffer.alloc(5 + payload.length);
  encoded.writeUInt32LE(1 + payload.length, 0);
  encoded[4] = opcode;
  payload.copy(encoded, 5);
  return encoded;
}

class ManualClock {
  constructor() {
    this.time = 0;
    this.next = 1;
    this.tasks = new Map();
  }

  now = () => this.time;
  setTimeout = (fn, ms) => {
    const id = this.next++;
    this.tasks.set(id, { fn, due: this.time + ms });
    return id;
  };
  clearTimeout = (id) => this.tasks.delete(id);

  advance(ms) {
    const end = this.time + ms;
    while (true) {
      let selected = null;
      for (const [id, task] of this.tasks) {
        if (task.due <= end && (!selected || task.due < selected.task.due)) selected = { id, task };
      }
      if (!selected) break;
      this.time = selected.task.due;
      this.tasks.delete(selected.id);
      selected.task.fn();
    }
    this.time = end;
  }
}

function fakeChild({ blocked = false } = {}) {
  const child = new EventEmitter();
  child.exitCode = null;
  child.signalCode = null;
  child.stdout = new EventEmitter();
  child.stderr = new EventEmitter();
  child.stderr.setEncoding = () => {};
  child.stdin = new EventEmitter();
  child.stdin.writable = true;
  child.stdin.writableNeedDrain = blocked;
  child.stdin.writes = [];
  child.stdin.endCount = 0;
  child.stdin.write = (bytes) => {
    child.stdin.writes.push(Buffer.from(bytes));
    return !child.stdin.writableNeedDrain;
  };
  child.stdin.end = () => {
    child.stdin.endCount += 1;
    child.stdin.writable = false;
  };
  child.killCount = 0;
  child.kill = (signal) => {
    child.killCount += 1;
    child.signalCode = signal;
  };
  return child;
}

function bridgeFixture(overrides = {}) {
  const clock = overrides.clock ?? new ManualClock();
  const child = overrides.child ?? fakeChild();
  const frames = [];
  const failures = [];
  const exits = [];
  const bridge = new WorkerBridge({
    spawnWorker: () => child,
    workerPath: "/test/CinderlineMatchWorker",
    args: ["--map", "0", "--seed", "1", "--players", "2"],
    clock,
    maxPayloadBytes: 64,
    maxCommandQueue: 2,
    maxBacklogSteps: 10,
    handshakeMs: 100,
    onFrame: (opcode, payload) => frames.push({ opcode, payload: Buffer.from(payload) }),
    onFailure: (failure) => failures.push(failure),
    onExit: (event) => exits.push(event),
    ...overrides.options,
  });
  assert.equal(bridge.start(), true);
  return { bridge, child, clock, frames, failures, exits };
}

function writtenFrames(child) {
  return child.stdin.writes.map((bytes) => ({
    length: bytes.readUInt32LE(0),
    opcode: bytes[4],
    payload: bytes.subarray(5),
  }));
}

test("WorkerBridge decodes fragmented and coalesced worker frames", () => {
  const fixture = bridgeFixture();
  const ready = workerFrame(WORKER_IPC.ready);
  const snapshot = workerFrame(WORKER_IPC.snapshot, Buffer.from([1, 0x43, 0x53, 0x4e, 0x50]));
  const output = Buffer.concat([ready, snapshot]);

  fixture.child.stdout.emit("data", output.subarray(0, 2));
  fixture.child.stdout.emit("data", output.subarray(2, 7));
  assert.equal(fixture.frames.length, 1);
  fixture.child.stdout.emit("data", output.subarray(7));

  assert.deepEqual(fixture.frames.map((entry) => entry.opcode), [WORKER_IPC.ready, WORKER_IPC.snapshot]);
  assert.deepEqual(fixture.frames[1].payload, Buffer.from([1, 0x43, 0x53, 0x4e, 0x50]));
  assert.equal(fixture.failures.length, 0);
  fixture.bridge.stop();
});

test("WorkerBridge rejects malformed framing once and ignores later child output", () => {
  const fixture = bridgeFixture();
  const malformed = Buffer.alloc(4);
  malformed.writeUInt32LE(0);
  fixture.child.stdout.emit("data", malformed);

  assert.equal(fixture.failures.length, 1);
  assert.equal(fixture.failures[0].kind, "protocol");
  assert.match(fixture.failures[0].error.message, /invalid worker frame length/);

  fixture.child.stdout.emit("data", workerFrame(WORKER_IPC.ready));
  fixture.child.emit("error", new Error("late child error"));
  fixture.child.emit("exit", 1, null);
  assert.equal(fixture.failures.length, 1);
  assert.equal(fixture.frames.length, 0);

  const oversized = bridgeFixture();
  const oversizedHeader = Buffer.alloc(4);
  oversizedHeader.writeUInt32LE(67);
  oversized.child.stdout.emit("data", oversizedHeader);
  assert.equal(oversized.failures.length, 1);
  assert.equal(oversized.failures[0].kind, "protocol");
  assert.match(oversized.failures[0].error.message, /invalid worker frame length/);
});

test("WorkerBridge preserves queue bounds, collapsing, and write priority across backpressure", () => {
  const child = fakeChild({ blocked: true });
  const fixture = bridgeFixture({
    child,
    options: { maxCommandQueue: 1, maxBacklogSteps: 5 },
  });

  assert.equal(fixture.bridge.enqueueCommand(Buffer.from([7])), true);
  assert.equal(fixture.bridge.enqueueCommand(Buffer.from([8])), false);
  assert.equal(fixture.bridge.enqueueSteps(3), true);
  assert.equal(fixture.bridge.enqueueSteps(3), false);
  assert.equal(fixture.bridge.requestSnapshot(), true);
  assert.equal(fixture.bridge.requestSnapshot(), true);
  assert.equal(fixture.bridge.enqueueForfeit(Buffer.from([1])), true);
  assert.equal(child.stdin.writes.length, 0);

  child.stdin.writableNeedDrain = false;
  child.stdin.emit("drain");
  const written = writtenFrames(child);
  assert.deepEqual(written.map((entry) => entry.opcode), [
    WORKER_IPC.forfeit,
    WORKER_IPC.command,
    WORKER_IPC.step,
    WORKER_IPC.snapshotRequest,
  ]);
  assert.equal(written[2].payload.readUInt32LE(0), 3);
  assert.equal(written[3].length, 1);
  fixture.bridge.stop();
});

test("WorkerBridge owns handshake and progress timeouts and suppresses callbacks after stop", () => {
  const handshake = bridgeFixture();
  handshake.clock.advance(100);
  assert.deepEqual(handshake.failures.map((failure) => failure.kind), ["handshake"]);

  const progress = bridgeFixture();
  progress.bridge.completeHandshake();
  progress.clock.advance(11);
  assert.equal(progress.bridge.checkProgress(10), true);
  assert.deepEqual(progress.failures.map((failure) => failure.kind), ["progress"]);

  const stopped = bridgeFixture();
  stopped.bridge.stop();
  stopped.bridge.stop();
  stopped.child.stdout.emit("data", Buffer.alloc(4));
  stopped.child.stdin.emit("error", new Error("late stdin error"));
  stopped.child.emit("error", new Error("late child error"));
  stopped.child.emit("exit", 1, null);
  stopped.clock.advance(1000);
  assert.equal(stopped.child.stdin.endCount, 1);
  assert.equal(stopped.child.killCount, 1);
  assert.equal(stopped.frames.length, 0);
  assert.equal(stopped.failures.length, 0);
  assert.equal(stopped.exits.length, 0);
});
