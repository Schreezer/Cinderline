# Cinderline commander experiment

This is a proposal-only A/B harness for spoken battlefield commands. It does not connect to the match worker and cannot execute an order.

The two paths are:

1. **TypeSafe only:** evaluate the raw transcript against bounded actions, friendly unit groups, known locations and visible targets.
2. **LLM then TypeSafe:** use an LLM only to ask for clarification or normalize references already present in conversation and battlefield state, then run the same TypeSafe judgment.

Both paths finish with the same deterministic validator. A future game integration should translate a validated proposal into `cinder::Command`, then submit it through the existing authoritative command path. It must re-check the battlefield revision immediately before submission.

## Offline tests

These tests use injected fake provider responses and make no network requests:

```sh
cd Server
npm run test:commander
```

## Live proposal comparison

Live evaluation requires provider keys plus explicit model selection. The script intentionally has no hardcoded model fallback.

```sh
cd Server
OPENAI_API_KEY=... \
TYPESAFE_API_KEY=... \
CINDERLINE_COMMANDER_LLM_MODEL=... \
CINDERLINE_TYPESAFE_MODEL=... \
npm run test:commander:live
```

The command prints JSON proposals and clarification decisions. It never sends a command to the game. Treat live evaluation as paid/provider-backed testing and do not commit credentials.
# Integrated voice runtime

The live runtime now lives in `gateway.js`, `voice-server.js`, `router.js`, and `runtime-providers.js`. See [native setup and limits](../../docs/VOICE_COMMANDER_SETUP.md) and [implementation tracker](../../docs/VOICE_COMMANDER_PLAN.md). Run `npm run test:voice` for injected-provider/gateway tests. The older A/B harness documented below remains isolated and does not describe the runtime's Jev-first routing.
