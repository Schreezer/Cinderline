import test from 'node:test';
import assert from 'node:assert/strict';
import { EventEmitter, once } from 'node:events';
import { setTimeout as delay } from 'node:timers/promises';
import WebSocket from 'ws';
import { createLiveConnection, createVoiceGateway, validateObservation, VoiceSession } from '../commander/gateway.js';

const TOKEN = 'test-only-commander-access-token-0001';
const pcm = (milliseconds = 20) => Buffer.alloc(milliseconds * 48).toString('base64');
const observation = (overrides = {}) => ({
  contextId: 'capture-a', generation: 'match-a', revision: 10, capturedAt: 100,
  selected_group: 'selected',
  unit_groups: [{ id: 'selected', label: 'Selected army', units: [11, 12], commandable: true }],
  locations: [{ id: 'home', label: 'Home', commandable: true, explored: true }],
  targets: [{ id: 'enemy-1', label: 'Visible enemy', visible: true, relationship: 'enemy' }],
  summary: { ore: 100 }, ...overrides,
});
const proposal = { action: 'move', unitGroup: 'selected', destination: 'home', queueMode: 'replace' };

class FakeClient extends EventEmitter {
  readyState = 1;
  bufferedAmount = 0;
  sent = [];
  send(data) { this.sent.push(JSON.parse(data)); }
  input(event) { this.emit('message', Buffer.from(JSON.stringify(event)), false); }
  close(code = 1000, reason = '') {
    if (this.readyState === 3) return;
    this.readyState = 3; this.closeCode = code; this.closeReason = reason; this.emit('close');
  }
}
class FakeLive extends EventEmitter {
  sent = [];
  closeCount = 0;
  send(event) { this.sent.push(event); return true; }
  close() { this.closeCount++; }
  event(event) { this.emit('event', event); }
}
function fixture(t, run = async () => ({ status: 'answered', message: 'Ready.', route: 'luna' }), options = {}) {
  const client = new FakeClient(), live = new FakeLive(), calls = [];
  let connections = 0;
  const session = new VoiceSession(client, {
    router: { run: (...args) => { calls.push(args); return run(...args); } },
    liveFactory: () => { connections++; return live; }, transcriptSettleMs: 1, ...options,
  });
  t.after(() => session.end());
  const open = () => client.input({ type: 'session.open', protocol: 1, observation: observation() });
  const start = () => { open(); live.event({ type: 'session.started' }); };
  const transcript = (text, start = 0, end = 100) => live.event({ type: 'session.input_transcript.delta', delta: text, start_ms: start, end_ms: end });
  const delegate = (id = 'delegation-1', offset_ms = 100, extra = {}) => live.event({ type: 'session.delegation.created', offset_ms, delegation: { id, target: 'client' }, ...extra });
  const submit = (text, view = observation()) => client.input({ type: 'text.submit', text, observation: view });
  return { client, live, calls, session, open, start, transcript, delegate, submit, connections: () => connections };
}
async function until(predicate, message = 'condition', timeout = 1000) {
  const end = Date.now() + timeout;
  while (!predicate()) { if (Date.now() > end) assert.fail(`Timed out waiting for ${message}`); await delay(2); }
}
const messages = (client, type) => client.sent.filter(event => event.type === type);
const commentary = live => live.sent.filter(event => event.type === 'session.commentary.append');
function deferred() {
  let resolve, reject;
  const promise = new Promise((yes, no) => { resolve = yes; reject = no; });
  return { promise, resolve, reject };
}
async function gatewayFixture(t, options = {}) {
  const lives = [];
  const gateway = createVoiceGateway({ token: TOKEN,
    router: { run: async () => ({ status: 'answered', message: 'Ready.', route: 'luna' }) },
    liveFactory: () => { const live = new FakeLive(); lives.push(live); return live; }, ...options });
  gateway.server.listen(0, '127.0.0.1'); await once(gateway.server, 'listening');
  t.after(() => gateway.close());
  return { gateway, lives, url: `ws://127.0.0.1:${gateway.server.address().port}` };
}
async function connect(url, headers = { Authorization: `Bearer ${TOKEN}` }) {
  const socket = new WebSocket(url, { headers });
  await once(socket, 'open');
  // Oversized frames and deliberate server closure are expected in several tests.
  socket.on('error', () => {});
  return socket;
}
async function rejected(url, headers = {}) {
  return new Promise((resolve, reject) => {
    const socket = new WebSocket(url, { headers });
    socket.on('error', () => {});
    socket.once('open', () => { socket.terminate(); reject(new Error('Unauthorized socket opened')); });
    socket.once('unexpected-response', (_req, response) => {
      const status = response.statusCode; response.resume(); socket.terminate(); resolve(status);
    });
  });
}

test('observation boundary drops arbitrary simulation fields and rejects ambiguous candidate IDs', () => {
  const view = observation({ omniscientSimulation: { enemyOre: 5000 } });
  view.unit_groups[0].privateOrders = ['secret']; view.unit_groups[0].units.push(-1, 1.5, '13');
  const safe = validateObservation(view);
  assert.equal(safe.omniscientSimulation, undefined);
  assert.equal(safe.unit_groups[0].privateOrders, undefined);
  assert.deepEqual(safe.unit_groups[0].units, [11, 12]);
  view.summary.ore = 999; assert.equal(safe.summary.ore, 100);
  assert.throws(() => validateObservation(observation({ locations: [{ id: 'same' }, { id: 'same' }] })), /Duplicate/);
  assert.throws(() => validateObservation(observation({ targets: Array.from({ length: 241 }, (_, i) => ({ id: `${i}` })) })), /Invalid candidates/);
  assert.throws(() => validateObservation(observation({ generation: '' })), /Invalid observation/);
});

test('HTTP upgrade enforces auth, exact path and explicit browser origins before opening any provider', async t => {
  const { url, lives } = await gatewayFixture(t, { allowedOrigins: ['https://game.example'] });
  assert.equal(await rejected(`${url}/voice`), 401);
  assert.equal(await rejected(`${url}/voice`, { Authorization: 'Bearer incorrect-token' }), 401);
  assert.equal(await rejected(`${url}/voice?token=anything`, { Authorization: `Bearer ${TOKEN}` }), 404);
  assert.equal(await rejected(`${url}/voice`, { Authorization: `Bearer ${TOKEN}`, Origin: 'https://untrusted.example' }), 403);
  const socket = await connect(`${url}/voice`, { Authorization: `Bearer ${TOKEN}`, Origin: 'https://game.example' });
  assert.equal(lives.length, 0, 'an authenticated handshake must not start paid inference');
  socket.send(JSON.stringify({ type: 'session.open', protocol: 1, observation: observation() }));
  await until(() => lives.length === 1, 'explicit session opening');
  socket.close();
});

test('gateway caps concurrent sessions and bounds WebSocket payloads', async t => {
  const { url, lives } = await gatewayFixture(t);
  const socket = await connect(`${url}/voice`);
  assert.equal(await rejected(`${url}/voice`, { Authorization: `Bearer ${TOKEN}` }), 429);
  const closed = once(socket, 'close');
  socket.send('x'.repeat(256 * 1024 + 1));
  const [code] = await closed;
  assert.equal(code, 1009);
  assert.equal(lives.length, 0);
});

test('invalid, binary and pre-open audio messages never create a provider session', t => {
  for (const input of [client => client.input({ type: 'audio.append', audio: pcm() }),
    client => client.emit('message', Buffer.from('{bad json'), false),
    client => client.emit('message', Buffer.from('binary'), true),
    client => client.input({ type: 'session.open', protocol: 2, observation: observation() })]) {
    const f = fixture(t); input(f.client);
    assert.equal(f.connections(), 0); assert.equal(f.client.readyState, 3);
  }
});

test('audio waits for Live session.started, respects mute and rejects malformed PCM', t => {
  const f = fixture(t); f.open();
  f.client.input({ type: 'audio.append', audio: pcm() });
  assert.equal(f.live.sent.length, 0);
  f.live.event({ type: 'session.started' });
  f.client.input({ type: 'audio.append', audio: pcm() });
  assert.equal(f.live.sent.filter(e => e.type === 'session.input_audio.append').length, 1);
  f.client.input({ type: 'session.mute', muted: true });
  f.client.input({ type: 'audio.append', audio: pcm() });
  assert.equal(f.live.sent.filter(e => e.type === 'session.input_audio.append').length, 1);
  f.client.input({ type: 'session.mute', muted: false });
  f.client.input({ type: 'audio.append', audio: Buffer.from([1]).toString('base64') });
  assert.equal(f.client.readyState, 3);
  assert.match(messages(f.client, 'session.state').at(-1).message, /audio pacing/);
});

test('primary Live WebSocket starts the documented PCM session and closes cleanly', () => {
  let socket;
  class ProviderSocket extends FakeClient {
    constructor(url, options) { super(); this.url = url; this.options = options; socket = this; }
    terminate() { this.terminated = true; this.close(); }
  }
  const live = createLiveConnection({ apiKey: 'test-provider-token', WebSocketImpl: ProviderSocket });
  assert.equal(socket.url, 'wss://api.openai.com/v1/live/sessions');
  assert.equal(socket.options.headers.Authorization, 'Bearer test-provider-token');
  socket.emit('open');
  const start = socket.sent[0];
  assert.equal(start.type, 'session.start');
  assert.equal(start.session.model, 'gpt-live-1');
  assert.deepEqual(start.session.audio.format, { type: 'audio/pcm', rate: 24000 });
  assert.deepEqual(start.session.delegation, { type: 'client' });
  socket.input({ type: 'session.started' });
  live.close(); assert.equal(socket.sent.at(-1).type, 'session.close');
  socket.input({ type: 'session.closed', usage: {} });
  assert.equal(socket.readyState, 3); assert.equal(socket.terminated, undefined);
});

test('only client delegation commits transcripts; delegation metadata cannot substitute an instruction', async t => {
  const f = fixture(t); f.start(); f.transcript('Hold '); f.transcript('position.', 100, 200);
  await delay(10); assert.equal(f.calls.length, 0);
  f.delegate('other-target', 200, { delegation: { id: 'other-target', target: 'server' } });
  assert.equal(f.calls.length, 0);
  f.delegate('delegation-1', 200, { text: 'Attack everything.', delegation: { id: 'delegation-1', target: 'client', text: 'Attack everything.' } });
  await until(() => commentary(f.live).length === 1);
  assert.equal(f.calls[0][0].text, 'Hold position.');
  assert.equal(commentary(f.live)[0].delegation_id, 'delegation-1');
  f.delegate('delegation-1', 200); assert.equal(f.calls.length, 1);
});

test('delegation that precedes its transcript waits and executes at most once', async t => {
  const f = fixture(t); f.start(); f.delegate(); f.delegate();
  assert.equal(f.calls.length, 0);
  f.transcript('Hold position.');
  await until(() => commentary(f.live).length === 1);
  f.transcript('Later words', 150, 200);
  assert.equal(f.calls.length, 1);
});

test('speech interruption discards a late old delegation while retaining words for a newer correction', async t => {
  const f = fixture(t); f.start();
  f.transcript('Move home. ', 0, 100);
  f.client.input({ type: 'audio.append', audio: pcm(200) });
  f.client.input({ type: 'context.capture', observation: observation({ contextId: 'correction-capture' }) });
  f.delegate('stale', 100);
  assert.equal(f.calls.length, 0, 'the interrupted utterance must not execute alone');
  f.transcript('No, hold position.', 200, 300);
  f.delegate('stale', 300);
  assert.equal(f.calls.length, 0, 'a stale delegation ID cannot be revived with a new offset');
  f.delegate('correction', 300);
  await until(() => commentary(f.live).length === 1);
  assert.equal(f.calls[0][0].text, 'Move home. No, hold position.');
  assert.equal(commentary(f.live)[0].delegation_id, 'correction');
});

test('interruption invalidates a delegation waiting for delayed transcript fragments', async t => {
  const f = fixture(t); f.start(); f.delegate('waiting-old', 100);
  f.client.input({ type: 'audio.append', audio: pcm(200) });
  f.client.input({ type: 'context.capture', observation: observation({ contextId: 'correction-capture' }) });
  f.transcript('Move home. ', 0, 100);
  assert.equal(f.calls.length, 0);
  f.transcript('No, hold position.', 200, 300);
  assert.equal(f.calls.length, 0, 'transcript-only input cannot revive cancelled delegation');
  f.delegate('correction', 300);
  await until(() => commentary(f.live).length === 1);
  assert.equal(f.calls.length, 1);
  assert.equal(commentary(f.live)[0].delegation_id, 'correction');
});

test('delegation requires its documented finite audio offset', t => {
  const f = fixture(t); f.start(); f.transcript('Hold position.');
  for (const [i, offset] of [undefined, null, '100', NaN, Infinity, -Infinity].entries()) {
    f.delegate(`invalid-offset-${i}`, offset, { offset_ms: offset });
  }
  assert.equal(f.calls.length, 0);
  assert.equal(f.client.readyState, 1);
});

test('transcript fragments are assembled by audio time rather than network arrival order', async t => {
  const f = fixture(t); f.start();
  f.client.input({ type: 'audio.append', audio: pcm(200) });
  f.client.input({ type: 'context.capture', observation: observation({ contextId: 'later-capture' }) });
  f.transcript('home.', 400, 700);
  f.transcript('Move them ', 0, 400);
  f.delegate('reordered', 700);
  await until(() => commentary(f.live).length === 1);
  assert.equal(f.calls[0][0].text, 'Move them home.');
  assert.equal(f.calls[0][0].observation.contextId, 'capture-a');
});

test('delegation waits for a delayed conditional transcript tail before routing', async t => {
  const f = fixture(t, undefined, { transcriptSettleMs: 20, transcriptMaxWaitMs: 100 });
  f.start(); f.transcript('Hold the squad', 0, 400); f.delegate('conditional', 1200);
  await delay(5);
  assert.equal(f.calls.length, 0);
  f.transcript(' only if they are at home.', 400, 1200);
  await until(() => commentary(f.live).length === 1);
  assert.equal(f.calls[0][0].text, 'Hold the squad only if they are at home.');
});

test('a transcript tail arriving during inference cancels the incomplete request before mutation', async t => {
  const resume = deferred();
  const f = fixture(t, async (input, tools) => {
    await resume.promise; await tools.execute(proposal, input.observation);
    return { status: 'completed', message: 'Unconditional order.', route: 'jev' };
  }, { transcriptSettleMs: 20 });
  f.start(); f.transcript('Hold the squad', 0, 400); f.delegate('conditional', 1200);
  await until(() => f.calls.length === 1);
  f.transcript(' only if they are at home.', 400, 1200);
  assert.equal(f.calls[0][0].signal.aborted, true);
  resume.resolve(); await delay(5);
  assert.equal(messages(f.client, 'rpc.request').length, 0);
  assert.equal(commentary(f.live).length, 0);
  assert.ok(messages(f.client, 'caption').some(e => /Speech changed/.test(e.text)));
});

test('transcript maximum wait fails closed instead of shortening the required settling interval', async t => {
  let now = 0;
  const f = fixture(t, undefined, { transcriptSettleMs: 20, transcriptMaxWaitMs: 30, now: () => now });
  f.start(); f.transcript('Hold ', 0, 50); f.delegate('unstable', 100);
  now = 25; f.transcript('there.', 50, 100);
  now = 30;
  await delay(15);
  assert.equal(f.calls.length, 0, 'a deadline is not evidence that the partial transcript settled');
  assert.ok(messages(f.client, 'caption').some(e => /did not settle/.test(e.text)));
});

test('utterance uses its earliest captured selection even when selection changes before delegation', async t => {
  const f = fixture(t); f.start();
  f.client.input({ type: 'audio.append', audio: pcm(200) });
  const later = observation({ contextId: 'capture-b', revision: 11,
    unit_groups: [{ id: 'selected', label: 'New selection', units: [30], commandable: true }] });
  f.client.input({ type: 'context.capture', observation: later });
  f.transcript('Move them home.', 10, 190); f.delegate('first', 200);
  await until(() => commentary(f.live).length === 1);
  assert.equal(f.calls[0][0].observation.contextId, 'capture-a');
  assert.deepEqual(f.calls[0][0].observation.unit_groups[0].units, [11, 12]);
});

test('a capture shortly after speech begins cannot replace the frozen utterance context', async t => {
  const f = fixture(t); f.start();
  f.client.input({ type: 'audio.append', audio: pcm(20) });
  f.client.input({ type: 'context.capture', observation: observation({ contextId: 'later-capture', revision: 11 }) });
  f.transcript('Hold them.', 0, 100); f.delegate();
  await until(() => commentary(f.live).length === 1);
  assert.equal(f.calls[0][0].observation.contextId, 'capture-a');
});

test('sent order remains pending until the same RPC receives an authoritative terminal acknowledgement', async t => {
  const f = fixture(t, async (input, tools) => {
    const receipt = await tools.execute(proposal, input.observation);
    return { status: 'completed', message: receipt.message, route: 'jev' };
  });
  f.start(); f.transcript('Move home.'); f.delegate();
  await until(() => messages(f.client, 'rpc.request').length === 1);
  const request = messages(f.client, 'rpc.request')[0];
  f.client.input({ type: 'rpc.result', id: 'unknown-rpc', result: { status: 'accepted', message: 'Spoofed' } });
  f.client.input({ type: 'rpc.result', id: request.id, result: { status: 'pending', sequence: 42, message: 'Sent to server.' } });
  await delay(10);
  assert.equal(commentary(f.live).length, 0);
  assert.equal(messages(f.client, 'receipt').at(-1).status, 'pending');
  f.client.input({ type: 'rpc.result', id: request.id, result: { status: 'accepted', sequence: 42, message: 'Units moving.' } });
  await until(() => commentary(f.live).length === 1);
  assert.equal(commentary(f.live)[0].content, 'Units moving.');
  f.client.input({ type: 'rpc.result', id: request.id, result: { status: 'accepted', message: 'Duplicate' } });
  assert.equal(messages(f.client, 'receipt').length, 2);
  assert.equal(messages(f.client, 'rpc.request').length, 1);
});

test('new speech cancels unsubmitted work and suppresses late mutations and stale spoken results', async t => {
  const resume = deferred();
  const f = fixture(t, async (input, tools) => {
    await resume.promise;
    await tools.execute(proposal, input.observation);
    return { status: 'completed', message: 'Stale completion.', route: 'luna' };
  });
  f.start(); f.transcript('Move home.'); f.delegate();
  await until(() => f.calls.length === 1);
  f.client.input({ type: 'context.capture', observation: observation({ contextId: 'interrupt' }) });
  assert.equal(f.calls[0][0].signal.aborted, true);
  resume.resolve(); await delay(10);
  assert.equal(messages(f.client, 'rpc.request').length, 0);
  assert.equal(commentary(f.live).length, 0);
  assert.ok(messages(f.client, 'audio.clear').length > 0);
});

test('cancelling a submitted order retains its final receipt without replay or stale commentary', async t => {
  let receipt;
  const f = fixture(t, async (input, tools) => {
    receipt = await tools.execute(proposal, input.observation);
    return { status: 'completed', message: receipt.message, route: 'jev' };
  });
  f.start(); f.transcript('Move home.'); f.delegate();
  await until(() => messages(f.client, 'rpc.request').length === 1);
  const request = messages(f.client, 'rpc.request')[0];
  f.client.input({ type: 'context.capture', observation: observation({ contextId: 'interrupt' }) });
  assert.ok(messages(f.client, 'operation.cancel').some(e => e.operationId === request.params.operationId));
  f.client.input({ type: 'rpc.result', id: request.id, result: { status: 'accepted', sequence: 17, message: 'Accepted before cancellation.' } });
  await until(() => receipt);
  assert.equal(receipt.status, 'accepted');
  assert.equal(messages(f.client, 'receipt').at(-1).status, 'accepted');
  assert.equal(messages(f.client, 'rpc.request').length, 1);
  assert.equal(commentary(f.live).length, 0);
});

test('disconnect makes an outstanding order uncertain and never replays it', async t => {
  let receipt;
  const f = fixture(t, async (input, tools) => {
    receipt = await tools.execute(proposal, input.observation);
    return { status: 'completed', message: receipt.message, route: 'jev' };
  });
  f.start(); f.transcript('Move home.'); f.delegate();
  await until(() => messages(f.client, 'rpc.request').length === 1);
  f.client.close(); await until(() => receipt);
  assert.equal(receipt.status, 'uncertain'); assert.match(receipt.message, /Do not retry/);
  assert.equal(messages(f.client, 'rpc.request').length, 1);
  assert.equal(f.connections(), 1); assert.equal(commentary(f.live).length, 0);
});

test('order acknowledgement timeout reports uncertain and never synthesizes acceptance', async t => {
  const f = fixture(t, async (input, tools) => {
    const receipt = await tools.execute(proposal, input.observation);
    return { status: 'completed', message: receipt.message, route: 'jev' };
  }, { rpcTimeoutMs: 15 });
  f.start(); f.transcript('Move home.'); f.delegate();
  await until(() => commentary(f.live).length === 1);
  assert.equal(messages(f.client, 'receipt').at(-1).status, 'uncertain');
  assert.match(commentary(f.live)[0].content, /unknown/);
  assert.equal(messages(f.client, 'rpc.request').length, 1);
});

test('clarification preserves original operation and context across a later selection change', async t => {
  const histories = [];
  const f = fixture(t, async input => {
    histories.push(structuredClone(input.history));
    return input.pending
      ? { status: 'answered', message: 'Resumed original request.', route: 'luna' }
      : { status: 'needs_clarification', message: 'Which location?', route: 'luna',
        pending: { originalRequest: input.text, observation: input.observation, history: [] } };
  }, { allowText: true });
  f.start(); f.submit('Move them there.');
  await until(() => messages(f.client, 'caption').length === 1);
  assert.equal(messages(f.client, 'rpc.request').length, 0);
  f.submit('Home.', observation({ contextId: 'changed-selection', revision: 11 }));
  await until(() => f.calls.length === 2 && messages(f.client, 'caption').length === 2);
  assert.equal(f.calls[1][0].pending.originalRequest, 'Move them there.');
  assert.equal(f.calls[1][0].pending.observation.contextId, 'capture-a');
  assert.equal(f.calls[1][0].operationId, f.calls[0][0].operationId);
  assert.deepEqual(histories[1].map(e => e.text), ['Move them there.', 'Which location?']);
});

test('match generation changes end the session and prevent late commands', async t => {
  const resume = deferred();
  const f = fixture(t, async (input, tools) => {
    await resume.promise; await tools.execute(proposal, input.observation);
    return { status: 'completed', message: 'Old match order.', route: 'jev' };
  });
  f.start(); f.transcript('Move home.'); f.delegate();
  await until(() => f.calls.length === 1);
  f.client.input({ type: 'context.capture', observation: observation({ generation: 'match-b' }) });
  resume.resolve(); await delay(10);
  assert.equal(f.client.readyState, 3);
  assert.match(messages(f.client, 'session.state').at(-1).message, /Match changed/);
  assert.equal(messages(f.client, 'rpc.request').length, 0);
});

test('fresh observation RPC rejects another match generation before any action', async t => {
  const f = fixture(t, async (input, tools) => {
    const fresh = await tools.getObservation(); await tools.execute(proposal, fresh);
    return { status: 'completed', message: 'Moved.', route: 'luna' };
  });
  f.start(); f.transcript('Move home.'); f.delegate();
  await until(() => messages(f.client, 'rpc.request').length === 1);
  const request = messages(f.client, 'rpc.request')[0];
  assert.equal(request.method, 'get_observation');
  f.client.input({ type: 'rpc.result', id: request.id, result: observation({ generation: 'match-b' }) });
  await until(() => commentary(f.live).length === 1);
  assert.match(commentary(f.live)[0].content, /could not be resolved/);
  assert.equal(messages(f.client, 'rpc.request').filter(e => e.method === 'execute').length, 0);
});

test('message, audio pacing, idle and maximum lifetime limits close bounded sessions', async t => {
  const flood = fixture(t); flood.start();
  for (let i = 0; i < 161; i++) flood.client.input({ type: 'rpc.result', id: `unknown-${i}`, result: {} });
  assert.equal(flood.client.readyState, 3);
  assert.match(messages(flood.client, 'session.state').at(-1).message, /rate exceeded/);
  const audio = fixture(t); audio.start();
  for (let i = 0; i < 31; i++) audio.client.input({ type: 'audio.append', audio: pcm(100) });
  assert.equal(audio.client.readyState, 3);
  assert.match(messages(audio.client, 'session.state').at(-1).message, /audio pacing/);
  const idle = fixture(t, undefined, { idleMs: 10 }); idle.start();
  await until(() => idle.client.readyState === 3, 'idle session closure');
  assert.equal(idle.live.closeCount, 1);
  let now = 0;
  const lifetime = fixture(t, undefined, { idleMs: 10, maxSessionMs: 5, now: () => now }); lifetime.start();
  now = 6;
  await until(() => lifetime.client.readyState === 3, 'maximum session lifetime');
});

test('typed input is disabled by default and no unknown RPC result triggers work', t => {
  const f = fixture(t); f.start();
  f.client.input({ type: 'rpc.result', id: 'invented', result: { status: 'accepted', message: 'Fake result.' } });
  assert.equal(f.calls.length, 0); assert.equal(messages(f.client, 'receipt').length, 0);
  f.submit('Move home.');
  assert.equal(f.calls.length, 0); assert.equal(f.client.readyState, 3);
});

test('a timed-out router cannot submit a delayed mutation', async t => {
  const f = fixture(t, async (input, tools) => {
    await delay(25); await tools.execute(proposal, input.observation);
    return { status: 'completed', message: 'Late order.', route: 'luna' };
  }, { jobTimeoutMs: 10 });
  f.start(); f.transcript('Move home.'); f.delegate();
  await until(() => commentary(f.live).length === 1);
  assert.match(commentary(f.live)[0].content, /stopped/);
  assert.equal(messages(f.client, 'rpc.request').length, 0);
});

test('command rate limit prevents a thirty-first request from reaching the router', async t => {
  const f = fixture(t, undefined, { allowText: true }); f.start();
  for (let i = 0; i < 31; i++) {
    f.submit(`Request ${i}`);
    await delay(0);
  }
  assert.equal(f.calls.length, 30);
  assert.equal(f.client.readyState, 3);
  assert.match(messages(f.client, 'session.state').at(-1).message, /command rate/);
});

test('mute selected while connecting remains effective after Live starts', t => {
  const f = fixture(t); f.open();
  f.client.input({ type: 'session.mute', muted: true });
  f.live.event({ type: 'session.started' });
  assert.equal(messages(f.client, 'session.state').at(-1).state, 'muted');
  assert.ok(f.live.sent.some(event => event.type === 'session.input_audio.mute'));
  f.client.input({ type: 'audio.append', audio: pcm() });
  assert.equal(f.live.sent.filter(event => event.type === 'session.input_audio.append').length, 0);
});

test('large provider audio deltas are split into bounded native chunks without losing sample order', t => {
  const f = fixture(t); f.start();
  const source = Buffer.alloc(24000);
  for (let i = 0; i < source.length; i += 2) source.writeInt16LE((i % 64000) - 32000, i);
  f.live.event({ type: 'session.output_audio.delta', delta: source.toString('base64') });
  const chunks = messages(f.client, 'audio.delta').map(event => Buffer.from(event.audio, 'base64'));
  assert.ok(chunks.length > 1);
  assert.ok(chunks.every(chunk => chunk.length <= 9600 && chunk.length % 2 === 0));
  assert.deepEqual(Buffer.concat(chunks), source);
  f.live.event({ type: 'session.output_audio.delta', delta: Buffer.from([1]).toString('base64') });
  assert.equal(f.client.readyState, 3);
  assert.match(messages(f.client, 'session.state').at(-1).message, /invalid audio/);
});

test('provider closure distinguishes finalized usage from an interrupted connection', t => {
  const metrics = {};
  const clean = fixture(t, undefined, { metrics }); clean.start();
  clean.live.event({ type: 'session.closed', usage: { input_audio_seconds: 2 } });
  assert.equal(metrics.finalized, 1);
  assert.equal(messages(clean.client, 'session.state').at(-1).state, 'ended');
  const interrupted = fixture(t); interrupted.start(); interrupted.live.emit('closed');
  assert.equal(messages(interrupted.client, 'session.state').at(-1).state, 'error');
  assert.match(messages(interrupted.client, 'session.state').at(-1).message, /usage is unconfirmed/);
});

test('client backpressure ends a session instead of queuing unlimited audio', t => {
  const f = fixture(t); f.start();
  f.client.bufferedAmount = 512 * 1024 + 1;
  f.live.event({ type: 'session.output_audio.delta', delta: pcm() });
  assert.equal(f.client.readyState, 3);
  assert.equal(f.live.closeCount, 1);
  assert.equal(messages(f.client, 'audio.delta').length, 0);
});
