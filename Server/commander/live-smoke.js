// Explicit opt-in provider connectivity check. Never connects to a game or microphone.
import { createLiveConnection } from './gateway.js';
import { createCommanderRouter } from './router.js';
import { createJevProvider, createLunaProvider } from './runtime-providers.js';

const lunaOnly = process.argv.includes('--luna-only');
const env = process.env;
if (!env.OPENAI_API_KEY || (!lunaOnly && !env.TYPESAFE_API_KEY)) {
  console.error('Configure server-side credentials before this explicit live check. Use --luna-only only for a deliberate direct-Luna check.');
  process.exitCode = 1;
} else {
  const results = { mode: lunaOnly ? 'direct_luna' : 'jev_then_luna', realProviders: true,
    microphoneOpened: false, gameConnected: false };
  const observation = { contextId: 'smoke-capture', generation: 'smoke-match', revision: 1,
    selected_group: 'selected', capturedAt: Date.now(),
    unit_groups: [{ id: 'selected', label: 'Two selected friendly Ember units', units: [1, 2], commandable: true }],
    locations: [], targets: [], summary: { gameplayActive: true } };
  const proposed = [];
  const router = createCommanderRouter({
    jev: lunaOnly ? null : createJevProvider({ apiKey: env.TYPESAFE_API_KEY, model: env.CINDER_JEV_MODEL || 'jev-latest' }),
    luna: createLunaProvider({ apiKey: env.OPENAI_API_KEY, model: env.CINDER_LUNA_MODEL || 'gpt-6-luna' }),
  });
  const start = performance.now();
  try {
    const result = await router.run({ text: 'Hold the selected units.', observation, operationId: 'provider-smoke', signal: AbortSignal.timeout(35000) }, {
      getObservation: async () => observation,
      execute: async proposal => {
        proposed.push(proposal);
        return { status: 'accepted', message: 'Dry-run adapter accepted the proposal; no real game was changed.' };
      },
    });
    results.routing = { ok: proposed.length === 1 && proposed[0].action === 'hold' && proposed[0].unitGroup === 'selected',
      route: result.route, elapsedMs: Math.round(performance.now() - start), proposals: proposed.length };
  } catch { results.routing = { ok: false, error: 'Provider routing did not complete.' }; }
  results.live = await new Promise(resolve => {
    const live = createLiveConnection({ apiKey: env.OPENAI_API_KEY });
    let ready = false, finalized = false, settled = false;
    const done = result => { if (settled) return; settled = true; clearTimeout(timeout); live.close(); resolve(result); };
    const timeout = setTimeout(() => done({ ok: false, ready, finalized, error: 'Live startup/finalization timed out.' }), 30000);
    live.on('event', event => {
      if (event.type === 'session.started') { ready = true; live.close(); }
      if (event.type === 'session.closed') { finalized = true; done({ ok: ready, ready, finalized, usageReceived: event.usage != null }); }
      if (event.type === 'error') done({ ok: false, ready, finalized, error: 'Live rejected the session.' });
    });
    live.on('fault', () => done({ ok: false, ready, finalized, error: 'Live connection failed.' }));
    live.on('closed', () => { if (!finalized) done({ ok: false, ready, finalized, error: 'Live closed without final usage.' }); });
  });
  console.log(JSON.stringify(results, null, 2));
  if (!results.routing.ok || !results.live.ok) process.exitCode = 1;
}
