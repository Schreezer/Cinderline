# Voice commander v1 implementation contract

The native game connects to a separately authenticated commander gateway over WebSocket. The gateway alone holds provider keys. V1 relays mono PCM16 little-endian 24 kHz audio through the gateway to GPT Live's documented primary WebSocket. Native WebRTC remains a later transport optimization, not a prerequisite for the command boundary.

## Wire messages (JSON, protocol 1)

Client authenticates with `Authorization: Bearer <commander access token>` at `/voice`. This is a local development gateway with one configured access token, not a public multiplayer identity service. Default bind is loopback; remote connections require TLS and explicit origin configuration. The existing game server remains authoritative for online orders.

Client messages:
- `session.open`: `{type, protocol:1, observation}`.
- `context.capture`: `{type, observation}` at the beginning of a detected speech burst, before its audio. Capture selection once, retain it throughout that utterance.
- `audio.append`: `{type, audio}` base64 PCM (bounded chunks).
- `text.submit`: `{type, text, observation}` for development/testing using exactly the same command path.
- `session.mute`: `{type, muted:boolean}`. Stop capture locally as well.
- `session.end`: `{type}`. Cancel unsubmitted work, close provider session.
- `rpc.result`: `{type, id, result}` for gateway reads or execution. Execution result can first be pending, then a terminal result with the same RPC id.

Server messages:
- `session.ready`: `{type}` after GPT Live has started.
- `session.state`: `{type, state, message?}`; connecting/listening/working/muted/error/ended.
- `caption`: `{type, role:'user'|'assistant', text}`.
- `audio.delta`: `{type, audio}` PCM output; never replay after disconnect.
- `audio.clear`: `{type}` clear queued output on interruption/end.
- `rpc.request`: `{type, id, method:'get_observation'|'execute', params}`.
- `operation.cancel`: `{type, operationId}` invalidates any unsubmitted game work for that operation.
- `receipt`: `{type, operationId, status, message}`; pending/accepted/rejected/cancelled/uncertain.

Observation:
```
{contextId, generation, revision, capturedAt,
 selected_group:'selected',
 unit_groups:[{id,label,units:[number],commandable:true}],
 locations:[{id,label,commandable:true,explored:true}],
 targets:[{id,label,visible:true,relationship:'enemy'|'friendly'}],
 summary:{...player-visible facts only}}
```
The game retains candidate IDs to concrete positions/entities privately, in a bounded expiring capture cache. Neither provider can invent coordinates or substitute the current selection for the captured one. Offline observations must be projected through fog rules. Online snapshots already have viewer-scoped handles. Generation changes on match/world change; the adapter rejects old captures.

Proposal: `{action, unitGroup?, destination?, target?, queueMode:'replace'|'append'}`.
Supported initial actions: `select_units`, `focus_location`, `move`, `attack_move`, `attack`, `hold`, `stop`, `defend`, `patrol`, `escort`.

Execute params: `{operationId, contextId, generation, proposal}`. Game adapter revalidates active gameplay, captured candidate membership, ownership, visibility, age, queue support and match generation. It uses the existing player-controller command gate. No raw UI action strings. Return `{status,message,sequence?}`; online submission is pending until server acknowledgement. Cache terminal results by operation ID; unknown outcomes never receive a new sequence automatically.

## Backend ownership

`router.js` exports `createCommanderRouter({jev,luna,minimumConfidence?})`. Returned router has `run({text,observation,history?,pending?,signal,operationId}, {getObservation,execute})`. Providers are injected async functions. Return `{status:'completed'|'needs_clarification'|'answered',message,pending?,route}`. Pending includes the original request, captured observation and bounded conversation. Replies resume Luna directly. Jev route values are only handle/handoff; missing arguments/confidence means handoff without a generated question. Luna chooses questions and tools. `execute(proposal, observation)` returns an authoritative receipt and is abort-aware. All mutation calls are sequential and bounded. A pending clarification cannot mutate anything.

`runtime-providers.js` implements Jev and Luna provider adapters, with cancellation/timeouts and safe errors. Preserve the old proposal-only A/B harness as an experiment. Root owns `gateway.js`, `voice-server.js`, shared wire integration and package scripts.

While resuming a clarification, Luna also has the conditional `start_new_request {}` tool. Before any mutation, and at most once, it can abandon an explicitly superseded request and adopt the latest utterance's capture. Ordinary clarification replies keep their original capture. A fresh read never substitutes its selection.

Delegations start a bounded transcript assembly window (350ms settling, 2s maximum). Fragments are sorted by audio time; approximate timestamps are not treated as a completed-turn signal. New speech invalidates delegations older than its audio boundary and cancels unsubmitted work. These timing defaults require real provider/device evaluation.

Native ownership: `FCinderCommanderBridge` (new files) owns Capture/Execute/Poll/Cancel/Reset. `UCinderVoiceSubsystem` owns the connection, audio and lifecycle. The bridge APIs are agreed in implementation messages before integration. Audio is an independent `CinderVoiceAudio` runtime plugin; platform implementations expose capture callback, playback, mute, stop and permission/error state. HUD routes Voice/Mute/End to the subsystem.

No live provider call or microphone capture starts before an explicit user Voice click. Limits close idle sessions. Credentials, transcripts and audio are not written to logs by default.
