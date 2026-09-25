# Voice commander architecture and tracker

Updated: 2026-09-24. Status: local implementation complete for the initial command set. Mac editor build, 103 Node tests (including 57 voice tests), and three focused Unreal tests pass. The idle Voice HUD was rendered and checked at 1170x540. The iOS arm64 Development build also passes; no packaging, installation or microphone test is implied. The OpenAI key is now saved locally in ignored `Server/.env.local` with owner-only permissions, after explicit confirmation. It has not been used for provider inference; microphone/device behavior is not yet verified. Setup: [VOICE_COMMANDER_SETUP.md](VOICE_COMMANDER_SETUP.md).

## Recommended design

Use GPT-Live-1 for continuous speech and client delegation to a Cinderline commander gateway. The gateway owns a Jev-first bounded command interpreter, GPT-6 Luna fallback, operation state and receipts. Code owns legal game actions. Keep all inference and audio work off the simulation tick/game thread.

The HUD gains a Voice button in the lower-right safe area, paired vertically with the existing Workers control. Tap connects after microphone permission. A compact strip shows connecting, listening, working, muted or disconnected, the latest caption and the latest order result. Provide visible mute and end controls. Gameplay remains interactive. Ending voice cancels unsubmitted commander work; it does not reverse already accepted orders.

### Voice transport and orchestration

1. Authenticate the player to the commander gateway; bind the voice session to their match, player and match generation. Keep OpenAI and TypeSafe credentials server-side.
2. Original preferred media design: native WebRTC microphone/speaker tracks between the game and GPT-Live; the gateway negotiates the session using `POST /v1/live/sessions`. Add a gateway sideband at `/v1/live/sessions/{session_id}/attach` for transcript/delegation processing. Native Unreal/iOS/Android media integration needs a focused compatibility spike: the official quickstart is a browser example, not proof of a ready Unreal plugin.
   Implementation decision: v1 uses native Mac/iOS AVAudioEngine voice processing and relays PCM16 mono24k through the authenticated gateway to the documented primary GPT Live WebSocket. This avoids introducing an unverified Unreal WebRTC dependency; native WebRTC remains an optimization to evaluate. Android audio is explicitly unavailable in this implementation.
3. Set session `model: gpt-live-1` and `delegation: { type: client }`. Client delegation means application-managed backend execution, not that API keys or model routing run on the phone.
4. The gateway accumulates `session.input_transcript.delta` and relevant assistant context with their timing. `session.delegation.created` contains delegation metadata, not the request text. Preserve its delegation ID and derive the request from accumulated speech and verified application state.
5. Define application-owned utterance/task revisions and a command commit boundary. Transcript fragments have no completed-turn event; a network pause must not be treated as a finished order. Start with delegation-led processing; validate self-corrections and interruption behavior before speculative command interpretation. Fragment processing may prefetch observations, but must not submit gameplay mutations.
6. Only the gateway owns interpretation dispatch and operation IDs, even though both the client data channel and sideband can receive events. No duplicate execution from transcript and delegation paths.
7. Return short verified outcomes through `session.commentary.append` with the original delegation ID. Use `session.thinking.append` for quiet context. For an ambiguous request, Luna decides what needs clarification and supplies the question plus relevant context; GPT-Live delivers it. Successful simple commands can still return their verified receipt directly without a Luna rewrite call.

### Jev chooses handle or handoff

User decision, 2026-09-24: Jev has only two routing outcomes, `handle` and `handoff`. It does not decide whether clarification is needed, diagnose what the player must clarify, or generate a question. Its capability judgment is narrowly defined: can the entire request be fulfilled by a registered operation using available typed argument candidates, without generating new content or planning beyond that handler?

Batch that capability choice with speculative action/handler and argument choices in one request. Consume the handler and required arguments only for `handle`. This preserves the fast path without a separate routing request followed by another extraction request. Code checks completeness and confidence on used fields; if the proposal fails those gates, it hands off to Luna without assigning a clarification verdict.

Code supplies the candidate vocabulary: selected units, squads, named owned bases, known ramps/locations, the last pointed ground position, and currently visible targets. Jev chooses from these candidates, with explicit no-match outcomes. Code handles counts, coordinate geometry, resource arithmetic, legal placement and pathfinding. Candidate extraction must preserve explicit numbers and relationships; do not silently replace an unsupported count with a default.

Jev routing contract:

- `handle`: select a registered operation and its typed arguments. Code validates current game legality, executes it through the shared command boundary and returns the verified receipt.
- `handoff`: pass the original request, conversation and player-visible state to GPT-6 Luna. Optional raw candidate choices/scores can accompany it, but no Jev-authored clarification diagnosis or question is required. Luna decides whether it can act, needs more tool information, should explain something, or must ask the player.

A game-rule rejection after `handle`, such as insufficient resources, remains an execution receipt. It is not evidence that the player was unclear, and neither model may bypass that rule. Generated explanations and unsupported requests belong to Luna; ordinary successful receipts can go directly to GPT-Live.

Confidence is distribution concentration, not a probability that the whole command is correct. Evaluate thresholds per action and per used argument on Cinderline utterances. The existing harness default of 0.72 is an experimental value, not a production acceptance criterion. Include compound-request cases so a one-command interpreter cannot confidently discard half an instruction.

### Luna owns clarification

User decision, 2026-09-24: Luna owns both deciding whether gameplay clarification is needed and deciding what to ask. The earlier Jev ambiguity/clarification branch is superseded.

`Jev handoff -> Luna decides next step -> tools/resolved proposal OR clarification through GPT-Live -> player reply resumes Luna's pending request -> shared validator -> game acknowledgement`

The gateway supplies Luna with the original accumulated speech, relevant conversation, captured selection/pointing context, and a fresh player-visible observation. Jev's optional raw judgments are supplementary evidence, not a decision about missing intent. Luna may request a bounded read-only lookup before asking the player; missing context that code can retrieve should not become an unnecessary question. A handoff does not imply ambiguity: it may simply require generated output or a workflow outside Jev's registered handlers.

For example, when Jev cannot produce a complete supported command for “Send them to the ramp,” it returns `handoff`. Luna inspects the captured selection, conversation and known ramps. If two ramps remain equally plausible, Luna can return a proposed structured decision such as `status: needs_clarification`, `missing_fields: [destination]`, the two candidate IDs, and `question: Which ramp—the one by your main base or the western expansion?` If prior conversation already identifies the ramp, Luna returns the resolved proposal instead. The gateway retains the structured state and sends GPT-Live concise, speakable context identifying what is missing and the question to ask. This decision object belongs to Luna's application contract, not Jev's routing contract or a provider event schema.

Retain the pending operation and its task revision while waiting. Bind “the western one” to that operation and the offered candidates, not to a new standalone command or the latest UI selection. On reply, Luna resolves the pending request; code refreshes relevant state and validates before execution. A new Live delegation may have a new ID, so correlate it to the application operation explicitly rather than assuming the delegation ID persists across replies. Cancel or expire superseded clarifications, discard late results, and do not execute while required information remains unresolved. If Luna is unavailable, report that the request could not be resolved; do not guess or route around this clarification boundary.

### Luna and tool execution

Use the standalone Responses API with `gpt-6-luna`, initially low reasoning effort, concise output and a small explicit tool catalog. Compare none/low/medium on the actual command suite. Retain Sol as a separately evaluated option for difficult strategy questions; do not add automatic Sol escalation by default.

Both Jev and Luna reach the same typed commander adapter. The model requests bounded actions; the adapter performs execution. Proposed first tools:

- `get_observation`: compact, player-visible current state.
- `select_units` and `focus_location`: local UI effects with owned/known candidates.
- `issue_order`: explicit friendly handles, supported order, known destination or currently visible target, and queue mode.
- Later: `train_units`, `preview_build`, `place_building` and `research`, using the existing simulation status/assignment helpers.

Do not expose arbitrary `ExecuteAction` strings or shell/computer-use tools. Preview ambiguous placements with a game ghost or ask for a location. Unambiguous ordinary orders should execute without a confirmation dialog on every command. For multi-step requests, code owns dependencies, bounded job lifetime and cancellation; do not let a conversational model invent an indefinite autonomous match loop.

### Shared observation and command boundary

Capture match generation, operation/utterance IDs, transcript revision, snapshot tick, selection revision, explicit selected handles, pointed target/position and relevant context as speech arrives. Resolve “these” against the captured selection, then revalidate those same units; never replace them with whichever selection happens to be active after inference.

Build AI context from `net::snapshotFor` semantics. Offline simulation state is omniscient, so raw `Sim.entities()`/`players()` must not be serialized into the AI request. Preserve the existing per-player opaque handles and visible/remembered distinctions. Online observations must come from the authenticated viewer projection. Treat player-supplied names and chat as data, not instructions.

Revalidate ownership, visibility, current legality, target existence, resources, queue constraints, match generation, task revision and expiry immediately before submission. A changing simulation tick alone should not invalidate every request: refresh state and test the relevant preconditions. A target leaving vision invalidates an attack on its entity; unrelated movement elsewhere does not invalidate holding the captured squad.

Reuse this existing path on the game thread:

`typed commander adapter -> player controller command gate -> Battlefield::SubmitCommand -> local Simulation::command OR online SendCommand -> match worker -> acknowledgement`

Keep AI calls outside the authoritative match worker. For online play, bind every proposal to the authenticated player and let the existing server translate and validate the command. For local skirmish/campaign, execute through the local command gate; cloud voice still requires connectivity. Manual controls keep working when voice is unavailable.

Receipts must distinguish proposed, submitted/pending, accepted, rejected, cancelled and uncertain. Online `SubmitCommand.accepted` currently means sent, so await the actual command acknowledgement. “Three Drudges queued” means queue acceptance; it must not become “three Drudges built.” Correlate a stable voice operation ID to one gameplay command sequence, use existing duplicate acknowledgement handling, and do not silently retry uncertain actions with a new sequence.

On interruption/correction, cancel unsubmitted work and reject late results from older task revisions. Accepted actions cannot be undone by cancelling speech. Apply a new hold/move order only when the player requests that correction. A later non-conflicting request need not cancel every existing task.

## Existing code to reuse

- `Server/commander/README.md`: proposal-only A/B experiment, explicitly disconnected from execution.
- `Server/commander/pipeline.js`: typed candidate judgments and deterministic proposal validation. Current branches are TypeSafe-only and LLM-then-TypeSafe, not Jev-then-Luna fallback.
- `Server/commander/providers.js`: HTTP TypeSafe evaluator and Responses clarification provider; the current LLM provider does not call tools.
- `Source/Cinderline/Private/Presentation/CinderPlayerController.cpp`, `Issue`: active-game gate, selection, tutorial/audio handling. Expose a narrow typed entry rather than bypassing its behavior. Existing public action helpers may need receipt returns.
- `Source/Cinderline/Private/Presentation/CinderBattlefield.cpp`, `SubmitCommand`: offline/online dispatch.
- `Source/Cinderline/Private/Presentation/CinderOnlineSubsystem.cpp`, `ConsumeCommandAcknowledgement` / `ReportUncertainOrders`: server receipt and disconnect behavior.
- `Source/Cinderline/Private/Sim/Network.cpp`, `snapshotFor` / `translateCommand`: player-view privacy and command handle translation.
- `Source/Cinderline/Private/Sim/Simulation.cpp`, `command`, plus status APIs in its header: authoritative rules.
- `Server/MatchWorker.cpp` and `Server/server.js`: execution and duplicate command-sequence acknowledgements.
- `Source/Cinderline/Private/Presentation/CinderHUD.cpp`: existing bottom-right Workers control and shared HUD layout/hit testing.

## Latency and cost decisions

Luna is a reasonable first backend for bounded tool tasks; a smaller model does not prove lower end-to-end latency when errors cause retries or clarification. Measure speech-to-authoritative-acknowledgement separately from time to first spoken response.

Compare direct Live-to-Luna, Live-to-Jev, and Live-to-Jev-with-Luna-fallback using identical utterances/state. In a simplified model excluding shared costs, the cascade pays `T_jev + (1-p) * T_luna`, where p is the fraction Jev resolves correctly without fallback. It improves mean routing latency only when `T_jev < p * T_luna`; tail latency and error rate still matter. Keep the routing choice configurable, not hardwired.

Track p50/p95, valid intended command rate, wrong actions, clarification rate, fallback rate, stale/duplicate outcomes, per-session voice seconds and backend token spend. Test accents, Hinglish, unit-name recognition (Drudge/Ember/Needle/Skim/Anvil/Mend), background battle audio and corrections.

Current published GPT-Live rate: $0.05/minute, billed per second, with backend model/tool costs separate. Thirty running minutes is $1.50 for voice alone, before Jev/Luna. The WebRTC docs also describe a 15-second initialization charge credited against running duration. Close idle sessions with a visible reconnect affordance; muting is not ending a billable session. Stop microphone capture locally when muted/end is requested, not just model input processing. Observe final usage on graceful close and record uncertainty on transport loss.

## Delivery sequence

- [x] Read the user-supplied GPT-Live announcements and current official API docs.
- [x] Read the TypeSafe skill, live API, function-calling cookbook and confidence guidance.
- [x] Inspect the current commander prototype, game command path, privacy projection and HUD location.
- [x] Record architecture, routing tradeoffs, integration boundaries and remaining work here.
- [x] Revise routing ownership: Jev selects `handle` or `handoff`; Luna alone decides whether clarification is needed and what to ask; GPT-Live delivers it and the reply resumes the pending operation.
- [x] Create [standalone visual architecture](voice-commander-architecture.html): system routes, role boundaries, clarification loop, execution, fog filtering, receipts, captured selection, cancellation, transport and delivery sequence. Interactive examples are illustrative; they make no live API calls.
- [ ] V1: native audio/WebRTC feasibility spike on Mac and iPhone; authenticate session creation, connect, interrupt, mute and end; no gameplay mutation yet.
- [ ] V2: player-view observations, captured selection, typed command adapter, stable operation IDs and actual receipt plumbing, exercised first with typed inputs.
- [ ] V3: Jev `handle`/`handoff` capability route with typed handler arguments and code-owned completeness/confidence gates; Luna-owned reasoning, clarification decisions and reply correlation; synthetic provider tests plus an explicitly configured live evaluation. Cover direct handling, generated-output handoff, incomplete/compound requests without partial execution, context-resolvable ambiguity, two candidate destinations, changed selection, expired candidates, interrupted questions and Luna failure without execution.
- [ ] V4: bottom-right HUD control and full voice-to-command flow for select/focus/move/attack-move/hold, with captions and concise verified acknowledgements.
- [ ] V5: compare Jev cascade versus direct Luna; tune from measured task success, p50/p95 and cost; test noisy audio, corrections, stale targets and reconnects.
- [ ] V6: add production/build/research tools and bounded multi-step requests using the same validation/receipt boundary.
- [ ] V7: device audio routing, game-audio echo, battery/frame cost, network loss, online fog privacy, duplicate/cancellation cases and controlled rollout.

## Implementation ledger — 2026-09-24

- [x] Separate authenticated gateway, server-only providers, loopback default, explicit remote TLS configuration, bounded sessions/rates and safe errors.
- [x] Native voice plugin: Mac/iOS capture, AEC, resampling, playback, mute/end, permission and interruption handling. Other platforms report unsupported.
- [x] Frozen player-view observations, stable private candidate mapping, generation/expiry guards and shared typed game command adapter.
- [x] Native/online receipts, duplicate suppression, cancellation and uncertain-outcome handling without automatic replay.
- [x] Jev handle/handoff batch, Luna tools/clarification, pending replies and explicit Luna-owned request replacement.
- [x] Voice/Mute/End HUD controls, captions, status, background/match shutdown and endpoint/token configuration.
- [x] Mocked router/gateway tests; Mac editor and iOS arm64 compilation.
- [x] Focused native automation: 3/3 pass (boundary, endpoint, mobile HUD layout). Rendered 1170x540 authored-map HUD checked; Voice is visible above Workers.
- [x] Full Node suite: 103/103 pass after rebuilding the stale protocol-11 worker against current protocol-12 source.
- [x] iOS arm64 Development binary and target receipt build successfully. Physical microphone and device testing remain separate.
- [x] Save the provided OpenAI key as `OPENAI_API_KEY` in ignored `Server/.env.local`, with owner-only permissions, and verify Node loads it without revealing it.
- [ ] Configure the separate gateway access token and TypeSafe credential, or explicitly choose direct Luna for an initial live check.
- [ ] Verify real GPT Live/Luna/Jev access and transcript/delegation timing; no synthetic test proves provider access or perceived latency.
- [ ] iPhone/Android/platform capture work, live echo/noise/route/reconnect checks, physical-device proof, latency/cost measurements.
- [ ] Production identity/provisioning/quotas/deployment and train/build/research tools.

Evidence: `artifacts/voice-commander/verification/` and `artifacts/voice-commander/hud-authored/run-20260924T163036Z-IEQQPE/sectorbase.png`. The old art-preview capture fixture refused the authored-map contract; the current authored terrain fixture supplied the verified HUD frame.

The delivery sequence above remains a roadmap: implemented source is tracked here separately from live and physical-device validation. No provider charge, deployment or physical-device installation has been performed. The OpenAI credential was subsequently saved locally after explicit user confirmation; its value is not recorded here.

## Sources checked on 2026-09-24

- [GPT-Live API announcement](https://openai.com/index/introducing-gpt-live-1-in-the-api/)
- [Introducing GPT-Live](https://openai.com/index/introducing-gpt-live/)
- [Live overview](https://developers.openai.com/api/docs/guides/live)
- [Live delegation and tools](https://developers.openai.com/api/docs/guides/live-delegation)
- [Live session lifecycle and transcripts](https://developers.openai.com/api/docs/guides/live-conversations)
- [Live WebRTC setup](https://developers.openai.com/api/docs/guides/voice-webrtc?api=live)
- [Server-side session controls](https://developers.openai.com/api/docs/guides/voice-server-controls?api=live)
- [GPT-6 Luna API model](https://developers.openai.com/api/docs/models/gpt-6-luna)
- [TypeSafe function calling](https://docs.typesafe.ai/cookbooks/function_calling)
- [TypeSafe confidence](https://docs.typesafe.ai/confidence)
- [TypeSafe API](https://docs.typesafe.ai/api)
