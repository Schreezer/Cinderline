import { Buffer } from "node:buffer";
import { WORKER_IPC } from "./protocol.js";

function frame(opcode, payload = Buffer.alloc(0)) {
  const encoded = Buffer.allocUnsafe(5 + payload.length);
  encoded.writeUInt32LE(1 + payload.length, 0);
  encoded[4] = opcode;
  payload.copy(encoded, 5);
  return encoded;
}

function clearQueue(bridge) {
  bridge.commandQueue.length = 0;
  bridge.forfeitQueue.length = 0;
  bridge.pendingSteps = 0;
  bridge.snapshotQueued = false;
}

export class WorkerBridge {
  constructor({
    spawnWorker,
    workerPath,
    args,
    clock,
    maxPayloadBytes,
    maxCommandQueue,
    maxBacklogSteps,
    handshakeMs,
    onFrame,
    onFailure,
    onExit,
    onStderr,
  }) {
    this.spawnWorker = spawnWorker;
    this.workerPath = workerPath;
    this.args = [...args];
    this.clock = clock;
    this.maxPayloadBytes = maxPayloadBytes;
    this.maxCommandQueue = maxCommandQueue;
    this.maxBacklogSteps = maxBacklogSteps;
    this.handshakeMs = handshakeMs;
    this.onFrame = onFrame;
    this.onFailure = onFailure;
    this.onExit = onExit;
    this.onStderr = onStderr;

    this.child = null;
    this.output = Buffer.alloc(0);
    this.commandQueue = [];
    this.forfeitQueue = [];
    this.pendingSteps = 0;
    this.snapshotQueued = false;
    this.flushing = false;
    this.handshakeTimer = null;
    this.lastProgressAt = 0;
    this.started = false;
    this.stopped = false;
    this.ending = false;
    this.failed = false;
  }

  get running() {
    return this.child !== null
      && !this.stopped
      && this.child.exitCode === null
      && this.child.signalCode === null;
  }

  start() {
    if (this.started || this.stopped) return false;
    this.started = true;
    let child;
    try {
      child = this.spawnWorker(this.workerPath, this.args, {
        stdio: ["pipe", "pipe", "pipe"],
        windowsHide: true,
      });
    } catch (error) {
      this.fail("spawn", error);
      return false;
    }
    this.child = child;
    this.lastProgressAt = this.clock.now();
    child.stdin.on("drain", () => {
      if (!this.stopped && !this.ending) this.flush();
    });
    child.stdin.on("error", (error) => this.fail("inputStopped", error));
    child.stdout.on("data", (chunk) => this.consume(chunk));
    child.stderr.setEncoding("utf8");
    child.stderr.on("data", (message) => {
      if (!this.stopped && !this.ending) this.onStderr?.(String(message));
    });
    child.on("error", (error) => this.fail("spawn", error));
    child.on("exit", (code, signal) => {
      if (!this.stopped) this.onExit?.({ code, signal });
      if (!this.stopped && !this.ending) this.fail("exit", null, { code, signal });
    });
    this.handshakeTimer = this.clock.setTimeout(() => {
      this.handshakeTimer = null;
      this.fail("handshake");
    }, this.handshakeMs);
    return true;
  }

  completeHandshake() {
    if (this.handshakeTimer !== null) {
      this.clock.clearTimeout(this.handshakeTimer);
      this.handshakeTimer = null;
    }
  }

  enqueueCommand(payload) {
    if (!this.canQueue(payload) || this.commandQueue.length >= this.maxCommandQueue) return false;
    this.commandQueue.push(Buffer.from(payload));
    this.flush();
    return true;
  }

  enqueueForfeit(payload) {
    if (!this.canQueue(payload)) return false;
    this.forfeitQueue.push(Buffer.from(payload));
    this.flush();
    return true;
  }

  enqueueSteps(count) {
    if (!this.running || this.ending || !Number.isInteger(count) || count < 1 || count > 5) return false;
    if (this.pendingSteps + count > this.maxBacklogSteps) return false;
    this.pendingSteps += count;
    this.flush();
    return true;
  }

  requestSnapshot() {
    if (!this.running || this.ending) return false;
    this.snapshotQueued = true;
    this.flush();
    return true;
  }

  checkProgress(timeoutMs, now = this.clock.now()) {
    if (!this.running || this.ending || now - this.lastProgressAt < timeoutMs) return false;
    this.fail("progress");
    return true;
  }

  finish() {
    if (this.stopped || this.ending) return;
    this.ending = true;
    this.completeHandshake();
    clearQueue(this);
    try {
      if (this.child?.stdin.writable) this.child.stdin.end();
    } catch {}
  }

  stop() {
    if (this.stopped) return;
    this.stopped = true;
    this.completeHandshake();
    clearQueue(this);
    const child = this.child;
    if (!child || child.exitCode !== null || child.signalCode !== null) return;
    try { child.stdin.end(); } catch {}
    this.clock.setTimeout(() => {
      if (child.exitCode === null && child.signalCode === null) child.kill("SIGTERM");
    }, 1000);
  }

  canQueue(payload) {
    return this.running && !this.ending && Buffer.isBuffer(payload) && payload.length + 1 <= this.maxPayloadBytes + 2;
  }

  flush() {
    const stream = this.child?.stdin;
    if (!stream?.writable || this.flushing || this.stopped || this.ending) return;
    this.flushing = true;
    try {
      while (stream.writable && !stream.writableNeedDrain) {
        let encoded = null;
        if (this.forfeitQueue.length > 0) {
          encoded = frame(WORKER_IPC.forfeit, this.forfeitQueue.shift());
        } else if (this.commandQueue.length > 0) {
          encoded = frame(WORKER_IPC.command, this.commandQueue.shift());
        } else if (this.pendingSteps > 0) {
          const count = Math.min(5, this.pendingSteps);
          const payload = Buffer.allocUnsafe(4);
          payload.writeUInt32LE(count);
          this.pendingSteps -= count;
          encoded = frame(WORKER_IPC.step, payload);
        } else if (this.snapshotQueued) {
          this.snapshotQueued = false;
          encoded = frame(WORKER_IPC.snapshotRequest);
        } else {
          break;
        }
        stream.write(encoded);
      }
    } catch (error) {
      this.fail("input", error);
    } finally {
      this.flushing = false;
    }
  }

  consume(chunk) {
    if (this.stopped || this.ending || this.failed) return;
    try {
      if (!Buffer.isBuffer(chunk)) throw new Error("worker output was not a buffer");
      this.output = this.output.length === 0 ? chunk : Buffer.concat([this.output, chunk]);
      while (this.output.length >= 4) {
        const length = this.output.readUInt32LE(0);
        if (length < 1 || length > this.maxPayloadBytes + 2) throw new Error("invalid worker frame length");
        if (this.output.length < 4 + length) break;
        const encoded = this.output.subarray(4, 4 + length);
        this.output = this.output.subarray(4 + length);
        this.lastProgressAt = this.clock.now();
        this.onFrame?.(encoded[0], encoded.subarray(1));
        if (this.stopped || this.ending || this.failed) return;
      }
      if (this.output.length > this.maxPayloadBytes + 6) throw new Error("worker output buffer exceeded limit");
    } catch (error) {
      this.fail("protocol", error);
    }
  }

  fail(kind, error = null, details = {}) {
    if (this.stopped || this.ending || this.failed) return;
    this.failed = true;
    this.stop();
    this.onFailure?.({ kind, error, ...details });
  }
}
