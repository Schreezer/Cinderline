import { createVoiceGateway, createLiveConnection } from './gateway.js';
import { createCommanderRouter } from './router.js';
import { createJevProvider, createLunaProvider } from './runtime-providers.js';

const env = process.env;
const host = env.CINDER_VOICE_HOST || '127.0.0.1';
const port = Number(env.CINDER_VOICE_PORT || 8789);
if (!Number.isInteger(port) || port < 1 || port > 65535) throw new Error('Invalid CINDER_VOICE_PORT');
if (!['127.0.0.1', '::1', 'localhost'].includes(host) && env.CINDER_VOICE_TLS_PROXY !== '1') {
  throw new Error('Remote voice binding requires an authenticated TLS reverse proxy and CINDER_VOICE_TLS_PROXY=1');
}
if (!env.OPENAI_API_KEY) throw new Error('OPENAI_API_KEY is required on the gateway');
if (!env.TYPESAFE_API_KEY && env.CINDER_VOICE_ROUTING !== 'luna') {
  throw new Error('Set TYPESAFE_API_KEY for Jev, or explicitly choose CINDER_VOICE_ROUTING=luna');
}
const router = createCommanderRouter({
  jev: env.CINDER_VOICE_ROUTING === 'luna' ? null : createJevProvider({ apiKey: env.TYPESAFE_API_KEY, model: env.CINDER_JEV_MODEL || 'jev-latest' }),
  luna: createLunaProvider({ apiKey: env.OPENAI_API_KEY, model: env.CINDER_LUNA_MODEL || 'gpt-6-luna' }),
});
const gateway = createVoiceGateway({ token: env.CINDER_VOICE_ACCESS_TOKEN, router,
  liveFactory: () => createLiveConnection({ apiKey: env.OPENAI_API_KEY, voice: env.CINDER_VOICE_NAME || 'marin' }),
  allowedOrigins: (env.CINDER_VOICE_ORIGINS || '').split(',').filter(Boolean),
  allowText: env.CINDER_VOICE_TYPED_INPUT === '1',
});
gateway.server.listen(port, host, () => console.log(`Cinderline voice gateway listening on ${host}:${port}. Routing: ${env.CINDER_VOICE_ROUTING === 'luna' ? 'Luna' : 'Jev -> Luna'}.`));
let closing = false;
for (const signal of ['SIGINT', 'SIGTERM']) process.on(signal, async () => {
  if (closing) return; closing = true; await gateway.close();
});
