# Voice commander local setup

The native Voice control, game bridge, Jev/Luna router and GPT Live gateway are implemented. The gateway is a separate Node service; the normal `/play` match server is unchanged. Provider access and microphone/device verification are separate from mocked tests and a successful build.

## Configure locally

1. Populate the ignored `Server/.env.local` using the variable names in `Server/commander/voice.env.example`. Provider keys belong here, on the gateway machine only. `CINDER_VOICE_ACCESS_TOKEN` is a separate random secret of at least 24 characters used by the game to access this gateway.
2. Jev-first routing requires `TYPESAFE_API_KEY`. To deliberately evaluate direct Luna instead, set `CINDER_VOICE_ROUTING=luna`; the service never silently substitutes this for missing TypeSafe configuration.
3. Create `Saved/Config/Voice.ini` (already ignored) on the game machine:

```ini
[CinderVoice]
Endpoint=ws://127.0.0.1:8789/voice
AccessToken=<the separate commander access token>
```

4. From `Server`, run `npm run start:voice`. It reads `.env.local` with Node 22. `GET /health` is a key-free readiness endpoint; connecting to `/voice` requires the bearer token. A provider session is only created after the authenticated game sends `session.open` following a Voice click.
5. Start a match and tap **Voice** in the lower-right dock. Grant microphone access. The control changes to Live / Mute / End, with captions and order results above it. End stops capture and closes the billable Live session. Muting keeps the session connected.

For an iPhone, loopback refers to the phone. Use a `wss://` gateway endpoint with a valid certificate, provision `Voice.ini` into the app's Saved directory, and keep provider credentials on the host. The CLI permits `-CinderVoiceEndpoint=...` for the endpoint only; secrets never belong in launch arguments. Remote gateway binding also requires explicit `CINDER_VOICE_TLS_PROXY=1` behind a TLS proxy. Native clients reject plain remote WebSockets. No public deployment or provisioning system is included in this local implementation.

## Current capabilities

- Select owned groups; focus known locations; move, attack-move, attack a visible target, hold, stop, defend, patrol, escort. Move/attack-move/patrol may append to the current queue.
- Candidates include the captured selection, squads, owned unit kinds, army, home/base locations, explored ramps, camera center and the last pointed ground location. Unsupported counts, locations, generated explanations and compound workflows go to Luna.
- Jev supplies only `handle` or `handoff`. Luna can read current state, use bounded tools, answer, or ask a question. A clarification reply retains the original selection. Luna can explicitly replace a pending request once through `start_new_request`, binding the replacement to the latest utterance's captured selection.
- The game resolves candidate IDs, validates ownership, visibility, expiry, generation and queue support, and submits through the existing controller gate. Online `pending` means sent; only the match server acknowledgement means accepted. Unknown outcomes are never automatically replayed.
- Mac and iOS audio use AVAudioEngine with native voice processing and PCM16 mono24k. Other platforms explicitly report unsupported. The v1 transport relays PCM over the gateway, rather than native WebRTC.

## Limits and lifecycle

The local gateway allows one concurrent session, 30 delegated requests/minute, 120 seconds without speech/activity, and 20 minutes total. A routing job is bounded to 45 seconds; native receipt waiting expires after 20 seconds and gateway RPC waiting after 25 seconds. Luna has bounded tool rounds and mutations. Captures expire after 120 seconds (24 retained); a clarification expires after 60 seconds. No transcripts, audio or provider error bodies are logged or persisted by the runtime.

Live transcripts have approximate timestamps and no completed-turn event. Only a delegation starts command interpretation. The gateway sorts fragments and waits for a 350ms post-delegation settling window (maximum 2 seconds); another fragment restarts that window. Interruptions invalidate earlier unsubmitted delegations. Late transcript changes cancel active unsubmitted work; they cannot undo an accepted game order. Real provider timing, speech corrections, accents and background game audio still require live evaluation before tuning this window or considering speculative execution.

The gateway token is a local development access credential, not a player account or public multiplayer session service. The native bridge and existing online authority enforce game legality. A production service still needs per-player gateway identity, issuance/revocation, quotas, deployment and device provisioning.

## Verify

```sh
cd Server
npm run test:voice
```

Tests inject providers and transports: no paid requests or microphone access. Build the game with `./scripts/unreal.sh build`. Focused Unreal tests are `Cinderline.Integration.VoiceCommanderBoundary`, `Cinderline.Voice.EndpointBoundary`, and `Cinderline.UI.MobileHUDLayout`; the shared test manifest contains a `voice` suite. Their checks cover fog privacy, captured selection, duplicate/cancelled/stale orders, lost acknowledgements, endpoint security and touch layout.

After credentials are deliberately configured, `npm run test:voice:live` makes real provider calls. Add `-- --luna-only` to explicitly check Live and Luna without Jev. This checks a synthetic hold proposal through a dry-run adapter and starts/closes a real GPT Live session. It may incur provider charges, opens no microphone and changes no game state; it is not a substitute for a spoken in-game order test.

Remaining device/live work and evidence are tracked in [VOICE_COMMANDER_PLAN.md](VOICE_COMMANDER_PLAN.md). Build/train/research tools, Android audio, native WebRTC, measured routing comparisons and production rollout remain later increments.
