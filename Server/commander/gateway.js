import { randomUUID, timingSafeEqual, createHash } from 'node:crypto';
import { createServer } from 'node:http';
import { EventEmitter } from 'node:events';
import WebSocket, { WebSocketServer } from 'ws';

const LIVE_URL = 'wss://api.openai.com/v1/live/sessions';
const FINAL = new Set(['accepted', 'rejected', 'cancelled', 'uncertain']);
const OPEN = 1;
const MAX_BUFFER = 512 * 1024;
const bounded = (value, max = 1024) => typeof value === 'string' ? value.slice(0, max) : '';
const id = () => randomUUID();
const abortError = () => new DOMException('Operation cancelled', 'AbortError');
const tokenHash = value => createHash('sha256').update(value).digest();

// Only the game-generated player view crosses this boundary. In particular, no
// arbitrary top-level simulation object is forwarded to either provider.
export function validateObservation(value) {
  if (!value || typeof value !== 'object' || !bounded(value.contextId, 128) || value.contextId.length > 128
      || !bounded(value.generation, 128) || value.generation.length > 128 || !['string', 'number'].includes(typeof value.revision)) {
    throw new Error('Invalid observation');
  }
  const candidates = (key, mapper) => {
    if (!Array.isArray(value[key]) || value[key].length > 240) throw new Error('Invalid candidates');
    const result = value[key].map(v => {
      if (!v || typeof v.id !== 'string' || !v.id || v.id.length > 128) throw new Error('Invalid candidate');
      return { id: v.id, label: bounded(v.label, 160), ...mapper(v) };
    });
    if (new Set(result.map(v => v.id)).size !== result.length) throw new Error('Duplicate candidate');
    return result;
  };
  const summary = value.summary && JSON.stringify(value.summary).length <= 8192 ? value.summary : {};
  return structuredClone({ contextId: value.contextId, generation: value.generation,
    revision: value.revision, capturedAt: value.capturedAt,
    selected_group: bounded(value.selected_group, 128),
    unit_groups: candidates('unit_groups', v => ({ commandable: v.commandable === true,
      units: Array.isArray(v.units) ? v.units.filter(n => Number.isSafeInteger(n) && n > 0).slice(0, 512) : [] })),
    locations: candidates('locations', v => ({ commandable: v.commandable === true, explored: v.explored === true })),
    targets: candidates('targets', v => ({ visible: v.visible === true,
      relationship: v.relationship === 'friendly' ? 'friendly' : v.relationship === 'enemy' ? 'enemy' : 'neutral' })),
    summary });
}

export function createLiveConnection({ apiKey, voice = 'marin', WebSocketImpl = WebSocket }) {
  if (!apiKey) throw new Error('OPENAI_API_KEY is required');
  const events = new EventEmitter();
  const socket = new WebSocketImpl(LIVE_URL, {
    headers: { Authorization: `Bearer ${apiKey}` }, maxPayload: 1024 * 1024,
    handshakeTimeout: 10_000, perMessageDeflate: false,
  });
  let started = false, closing = false, closeTimer;
  const send = event => {
    if (socket.readyState !== OPEN || socket.bufferedAmount > MAX_BUFFER) return false;
    socket.send(JSON.stringify(event));
    return true;
  };
  socket.on('open', () => send({ type: 'session.start', event_id: id(), session: {
    model: 'gpt-live-1',
    instructions: 'You are the concise voice of Cinderline, a strategy game. Delegate EVERY gameplay request, question, correction, and clarification reply to the client backend. Only the backend decides what is unclear and what question to ask. Never invent game state or say an order succeeded before a verified result. Read the backend question naturally. A sent or uncertain order is not accepted. Respect interruptions. Keep responses short.',
    audio: { format: { type: 'audio/pcm', rate: 24000 }, output: { voice } },
    delegation: { type: 'client' },
  } }));
  socket.on('message', (data, binary) => {
    if (binary) return;
    try {
      const event = JSON.parse(data.toString());
      if (event.type === 'session.started') started = true;
      if (event.type === 'session.closed') { clearTimeout(closeTimer); socket.close(); }
      events.emit('event', event);
    } catch { events.emit('fault'); }
  });
  socket.on('error', () => events.emit('fault'));
  socket.on('close', () => { clearTimeout(closeTimer); events.emit('closed'); });
  return Object.assign(events, {
    send,
    close() {
      if (closing) return;
      closing = true;
      if (started && send({ type: 'session.close' })) {
        closeTimer = setTimeout(() => socket.terminate(), 15_000); closeTimer.unref?.();
      } else socket.terminate();
    },
  });
}

export class VoiceSession {
  constructor(client, { router, liveFactory, metrics = {}, idleMs = 120_000,
    maxSessionMs = 20 * 60_000, rpcTimeoutMs = 25_000, jobTimeoutMs = 45_000,
    allowText = false, transcriptSettleMs = 350, transcriptMaxWaitMs = 2000, now = Date.now }) {
    Object.assign(this, { client, router, liveFactory, metrics, idleMs, rpcTimeoutMs, jobTimeoutMs, allowText, transcriptSettleMs, transcriptMaxWaitMs, now });
    this.createdAt = this.lastActivity = now(); this.maxSessionMs = maxSessionMs;
    this.rpc = new Map(); this.seen = new Set(); this.transcripts = []; this.contexts = [];
    this.history = []; this.pending = null; this.closed = false; this.ready = false;
    this.audioBytes = 0; this.audioMs = 0; this.windowStart = now(); this.messageCount = 0;
    this.commandWindow = now(); this.commandCount = 0;
    this.watchdog = setInterval(() => {
      if (now() - this.lastActivity > idleMs || now() - this.createdAt > maxSessionMs) this.end('Voice session ended. Tap Voice to reconnect.');
      if (this.pending && now() - this.pendingAt > 60_000) this.pending = null;
    }, Math.min(1000, idleMs)); this.watchdog.unref?.();
    client.on('message', (data, binary) => {
      try { if (binary) throw new Error(); this.receive(JSON.parse(data.toString())); }
      catch { this.end('Invalid voice message.'); }
    });
    client.on('close', () => this.end()); client.on('error', () => this.end());
  }

  send(event) {
    if (this.client.readyState !== OPEN) return false;
    if (this.client.bufferedAmount > MAX_BUFFER) { this.end(); return false; }
    this.client.send(JSON.stringify(event)); return true;
  }
  state(state, message) { this.send({ type: 'session.state', state, ...(message ? { message } : {}) }); }
  liveSend(event) { return !this.closed && this.ready && this.live?.send(event); }

  capture(raw) {
    const observation = validateObservation(raw);
    if (this.observation && observation.generation !== this.observation.generation) {
      this.end('Match changed. Tap Voice to reconnect.'); return false;
    }
    this.observation = observation;
    this.contexts.push({ at: this.audioMs, observation });
    this.contexts = this.contexts.slice(-48);
    return true;
  }

  receive(event) {
    if (this.closed) return;
    if (this.now() - this.windowStart >= 1000) { this.windowStart = this.now(); this.messageCount = 0; this.audioBytes = 0; }
    if (++this.messageCount > 160) return this.end('Voice message rate exceeded.');
    if (event.type === 'session.end') return this.end();
    if (event.type === 'rpc.result') return this.receiveRPC(event);
    if (event.type === 'session.open') {
      if (this.live || event.protocol !== 1 || !this.capture(event.observation)) return this.end('Invalid voice session.');
      this.state('connecting');
      this.live = this.liveFactory();
      this.live.on('event', e => this.liveEvent(e));
      this.live.on('fault', () => this.end('Voice provider unavailable.'));
      this.live.on('closed', () => this.end(this.finalUsage ? undefined : 'Voice connection ended; final usage is unconfirmed.'));
      this.startTimer = setTimeout(() => { if (!this.ready) this.end('Voice connection timed out.'); }, 15_000);
      this.startTimer.unref?.(); return;
    }
    if (!this.live) return this.end('Open a voice session first.');
    if (event.type === 'context.capture') {
      this.lastActivity = this.now();
      if (!this.capture(event.observation)) return;
      this.latestSpeechBoundary = this.audioMs;
      // A delegation for speech before this interruption cannot commit later.
      // Retain transcript context so the next delegation can resolve corrections.
      this.waitingDelegation = null;
      clearTimeout(this.transcriptTimer);
      this.cancelJob();
      this.send({ type: 'audio.clear' });
      return;
    }
    if (event.type === 'session.mute') {
      if (typeof event.muted !== 'boolean') return this.end('Invalid mute state.');
      this.muted = event.muted; this.lastActivity = this.now();
      this.liveSend({ type: event.muted ? 'session.input_audio.mute' : 'session.input_audio.unmute' });
      return this.state(event.muted ? 'muted' : 'listening');
    }
    if (event.type === 'audio.append') {
      if (!this.ready || this.muted) return;
      if (typeof event.audio !== 'string' || event.audio.length > 16384 || !/^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(event.audio)) return this.end('Invalid audio chunk.');
      const bytes = Buffer.byteLength(event.audio, 'base64');
      this.audioBytes += bytes;
      if (!bytes || bytes % 2 || this.audioBytes > 144000) return this.end('Invalid audio pacing.');
      this.audioMs += bytes / 48;
      if (!this.liveSend({ type: 'session.input_audio.append', audio: event.audio })) this.end('Voice stream is congested.');
      return;
    }
    if (event.type === 'text.submit') {
      if (!this.allowText || !this.ready || !bounded(event.text, 4000)) return this.end('Typed voice input is unavailable.');
      if (!this.capture(event.observation)) return;
      this.dispatch(bounded(event.text, 4000), null, this.observation); return;
    }
    this.end('Unknown voice message.');
  }

  liveEvent(event) {
    if (event.type === 'session.closed') { this.finalUsage = event.usage ?? {}; this.metrics.finalized = (this.metrics.finalized ?? 0) + 1; return this.end(); }
    if (this.closed) return;
    switch (event.type) {
      case 'session.started':
        this.ready = true; clearTimeout(this.startTimer); this.send({ type: 'session.ready' });
        if (this.muted) this.liveSend({ type: 'session.input_audio.mute' });
        this.state(this.muted ? 'muted' : 'listening'); break;
      case 'session.output_audio.delta':
        if (typeof event.delta === 'string' && event.delta.length <= 512000) {
          const pcm = Buffer.from(event.delta, 'base64');
          if (pcm.length % 2) { this.end('Voice provider sent invalid audio.'); break; }
          for (let offset = 0; offset < pcm.length; offset += 9600)
            this.send({ type: 'audio.delta', audio: pcm.subarray(offset, offset + 9600).toString('base64') });
        }
        break;
      case 'session.input_transcript.delta': {
        const text = bounded(event.delta, 4000);
        if (!text) break;
        this.lastActivity = this.now();
        const fragment = { text, start: Number(event.start_ms) || 0, end: Number(event.end_ms) || 0 };
        this.transcripts.push(fragment);
        this.transcripts = this.transcripts.slice(-128);
        this.send({ type: 'caption', role: 'user', text });
        if (this.waitingDelegation) this.scheduleTranscript();
        else if (this.committedSpeech && fragment.start < this.committedSpeech.offset
            && fragment.end <= this.committedSpeech.offset && this.job === this.committedSpeech.job) {
          this.cancelJob();
          this.send({ type: 'caption', role: 'assistant', text: 'Speech changed after processing began. Unsubmitted work stopped; check any orders already sent.' });
        }
        break;
      }
      case 'session.output_transcript.delta':
        this.send({ type: 'caption', role: 'assistant', text: bounded(event.delta, 4000) }); break;
      case 'session.delegation.created':
        if (event.delegation?.target === 'client') this.delegate(event); break;
      case 'error': this.end('Voice provider rejected the session.'); break;
    }
  }

  delegate(event) {
    const delegationId = bounded(event.delegation?.id, 160);
    if (!delegationId || this.seen.has(delegationId)) return;
    if (!Number.isFinite(event.offset_ms)) return;
    const offset = event.offset_ms;
    if (offset < (this.latestSpeechBoundary ?? 0)) { this.seen.add(delegationId); return; }
    // A delegation authorizes assembly; it does not guarantee its transcript
    // tail already arrived. The settle window never starts from silence alone.
    if (this.waitingDelegation?.delegation.id === delegationId) return;
    this.waitingDelegation = event;
    this.transcriptDeadline = this.now() + this.transcriptMaxWaitMs;
    this.scheduleTranscript();
  }

  scheduleTranscript() {
    clearTimeout(this.transcriptTimer);
    const remaining = this.transcriptDeadline - this.now();
    if (remaining <= 0) {
      this.waitingDelegation = null;
      this.send({ type: 'caption', role: 'assistant', text: 'The speech transcript did not settle. No new order was issued.' });
      return;
    }
    // Never shorten the quiet interval to squeeze a mutation in at the deadline.
    this.transcriptTimer = setTimeout(() => {
      if (remaining < this.transcriptSettleMs) {
        this.waitingDelegation = null;
        this.send({ type: 'caption', role: 'assistant', text: 'The speech transcript did not settle. No new order was issued.' });
      } else this.commitTranscript();
    }, Math.min(this.transcriptSettleMs, remaining));
    this.transcriptTimer.unref?.();
  }

  commitTranscript() {
    const event = this.waitingDelegation;
    if (!event || this.closed) return;
    if (this.now() >= this.transcriptDeadline) { this.scheduleTranscript(); return; }
    const offset = event.offset_ms;
    const fragments = this.transcripts.filter(t => t.end > (this.lastDelegatedOffset ?? -1) && t.start <= offset)
      .sort((a, b) => a.start - b.start || a.end - b.end);
    const text = fragments.map(t => t.text).join('').trim();
    if (!text) { this.scheduleTranscript(); return; }
    this.waitingDelegation = null;
    const delegationId = event.delegation.id;
    this.seen.add(delegationId);
    if (this.seen.size > 256) return this.end('Voice session limit reached.');
    this.lastDelegatedOffset = Math.max(...fragments.map(t => t.end), offset);
    const start = fragments[0].start;
    const context = [...this.contexts].reverse().find(c => c.at <= start)?.observation ?? this.contexts[0]?.observation;
    if (!context) return this.end('Voice context is unavailable.');
    this.dispatch(text, delegationId, context);
    this.committedSpeech = { offset, job: this.job };
  }

  cancelJob() {
    if (!this.job) return;
    const job = this.job; this.job = null; job.controller.abort();
    for (const operationId of job.operations) this.send({ type: 'operation.cancel', operationId });
  }

  async dispatch(text, delegationId, observation) {
    if (this.now() - this.commandWindow > 60_000) { this.commandWindow = this.now(); this.commandCount = 0; }
    if (++this.commandCount > 30) return this.end('Voice command rate exceeded.');
    this.cancelJob(); this.lastActivity = this.now(); this.state('working');
    const controller = new AbortController();
    const job = { id: this.pending?.operationId ?? id(), controller, operations: new Set(), started: this.now() };
    this.job = job;
    const timeout = setTimeout(() => controller.abort(), this.jobTimeoutMs); timeout.unref?.();
    try {
      const result = await this.router.run({ text, observation, history: structuredClone(this.history),
        pending: this.pending, signal: controller.signal, operationId: job.id }, {
        getObservation: async () => validateObservation(await this.requestRPC('get_observation', {}, controller.signal)),
        execute: async (proposal, captured, { operationId = job.id, signal = controller.signal } = {}) => {
          signal.throwIfAborted();
          if (this.closed || this.job !== job || captured.generation !== this.observation.generation) throw abortError();
          job.operations.add(operationId);
          return this.requestRPC('execute', { operationId, contextId: captured.contextId, generation: captured.generation, proposal }, signal);
        },
      });
      controller.signal.throwIfAborted();
      if (this.job !== job || this.closed) return;
      this.pending = result.status === 'needs_clarification' ? { ...result.pending, operationId: job.id } : null;
      this.pendingAt = this.now();
      const message = bounded(result.message, 1200);
      this.history.push({ role: 'user', text }, { role: 'assistant', text: message }); this.history = this.history.slice(-16);
      if (delegationId) this.liveSend({ type: 'session.commentary.append', event_id: id(), delegation_id: delegationId, content: message });
      this.send({ type: 'caption', role: 'assistant', text: message });
      this.metrics.completed = (this.metrics.completed ?? 0) + 1;
      this.metrics[result.route ?? 'unknown'] = (this.metrics[result.route ?? 'unknown'] ?? 0) + 1;
      this.metrics.lastLatencyMs = this.now() - job.started;
    } catch (error) {
      if (this.job === job && !this.closed) {
        const message = controller.signal.aborted ? 'Voice request stopped. Already submitted orders may still complete.' : 'That request could not be resolved. Please try again.';
        this.send({ type: 'caption', role: 'assistant', text: message });
        if (delegationId) this.liveSend({ type: 'session.commentary.append', event_id: id(), delegation_id: delegationId, content: message });
        this.metrics.failed = (this.metrics.failed ?? 0) + 1;
      }
    } finally {
      clearTimeout(timeout);
      if (this.job === job) { this.job = null; this.state(this.muted ? 'muted' : 'listening'); }
    }
  }

  requestRPC(method, params, signal) {
    signal.throwIfAborted();
    return new Promise((resolve, reject) => {
      const rpcId = id();
      const finish = (error, result) => {
        const rpc = this.rpc.get(rpcId); if (!rpc) return;
        clearTimeout(rpc.timer); signal.removeEventListener('abort', onAbort); this.rpc.delete(rpcId);
        if (error) reject(error); else resolve(result);
      };
      const onAbort = () => {
        // Once sent, retain the receipt listener: cancellation cannot undo an
        // order already submitted to the game authority.
        if (method !== 'execute') finish(abortError());
        else this.send({ type: 'operation.cancel', operationId: params.operationId });
      };
      const timer = setTimeout(() => {
        if (method === 'execute') {
          const result = { status: 'uncertain', message: 'Order result is unknown. It will not be retried automatically.' };
          this.send({ type: 'receipt', operationId: params.operationId, ...result }); finish(null, result);
        } else finish(new Error('Observation timed out'));
      }, this.rpcTimeoutMs); timer.unref?.();
      this.rpc.set(rpcId, { method, params, finish, timer }); signal.addEventListener('abort', onAbort, { once: true });
      if (!this.send({ type: 'rpc.request', id: rpcId, method, params })) finish(new Error('Client disconnected'));
    });
  }

  receiveRPC(event) {
    const rpc = this.rpc.get(event.id); if (!rpc) return;
    const result = event.result;
    if (rpc.method === 'get_observation') {
      try {
        const observation = validateObservation(result);
        if (observation.generation !== this.observation.generation) throw new Error();
        rpc.finish(null, observation);
      } catch { rpc.finish(new Error('Observation is unavailable')); }
      return;
    }
    if (!result || (!FINAL.has(result.status) && result.status !== 'pending')) return;
    const safe = { status: result.status, message: bounded(result.message, 800),
      ...(Number.isSafeInteger(result.sequence) ? { sequence: result.sequence } : {}) };
    this.send({ type: 'receipt', operationId: rpc.params.operationId, ...safe });
    if (FINAL.has(safe.status)) rpc.finish(null, safe);
  }

  end(message) {
    if (this.closed) return;
    this.cancelJob(); this.closed = true; clearInterval(this.watchdog); clearTimeout(this.startTimer);
    clearTimeout(this.transcriptTimer);
    for (const rpc of [...this.rpc.values()]) {
      if (rpc.method === 'execute') rpc.finish(null, { status: 'uncertain', message: 'Connection ended before acknowledgement. Do not retry automatically.' });
      else rpc.finish(abortError());
    }
    this.pending = null; this.history = []; this.transcripts = []; this.contexts = [];
    this.send({ type: 'audio.clear' }); this.state(message ? 'error' : 'ended', message);
    this.live?.close(); this.client.close(1000, 'Voice session ended');
  }
}

export function createVoiceGateway({ token, router, liveFactory, allowedOrigins = [], maxSessions = 1, ...sessionOptions }) {
  if (typeof token !== 'string' || token.length < 24) throw new Error('Set a commander access token of at least 24 characters');
  const metrics = {}, sessions = new Set();
  const server = createServer((req, res) => {
    res.setHeader('Cache-Control', 'no-store');
    if (req.url !== '/health') { res.writeHead(404); return res.end(); }
    res.writeHead(200, { 'Content-Type': 'application/json' }); res.end(JSON.stringify({ ok: true, service: 'cinderline-voice', protocol: 1 }));
  });
  const wss = new WebSocketServer({ noServer: true, maxPayload: 256 * 1024, perMessageDeflate: false });
  server.on('upgrade', (req, socket, head) => {
    const auth = typeof req.headers.authorization === 'string' ? req.headers.authorization : '';
    const authorized = timingSafeEqual(tokenHash(auth), tokenHash(`Bearer ${token}`));
    const origin = req.headers.origin;
    const status = !authorized ? 401 : req.url !== '/voice' ? 404 : (origin && !allowedOrigins.includes(origin)) ? 403 : sessions.size >= maxSessions ? 429 : 0;
    if (status) { socket.write(`HTTP/1.1 ${status} Rejected\r\nConnection: close\r\nContent-Length: 0\r\n\r\n`); socket.destroy(); return; }
    wss.handleUpgrade(req, socket, head, client => {
      const session = new VoiceSession(client, { router, liveFactory, metrics, ...sessionOptions });
      sessions.add(session); client.on('close', () => sessions.delete(session));
    });
  });
  return { server, metrics, sessions, async close() {
    for (const session of sessions) session.end();
    for (const client of wss.clients) client.terminate();
    wss.close(); await new Promise(resolve => server.close(resolve));
  } };
}
