# Command allocation source review

Reviewed the automatic build, train, research, rally, stable queue identity,
save-v7 migration, protocol-v4 codec, recipient filtering, and match-worker
application paths without building or running the app.

## Resolved follow-up

- The selected-producer queue now stores the producer ID and stable job ID on
  every cancellation button in `CinderHUD.cpp:1610-1619`. The shared HUD
  dispatch calls `CancelProduction(EntityId, JobId)` at `CinderHUD.cpp:420`,
  which submits that explicit producer and job through
  `CinderPlayerController.cpp:903-908`. Queue advancement can no longer retarget
  a stale rendered row by index.
- `MatchWorker.cpp:117-125` replaces successful AutoBuild and AutoResearch
  messages before the acknowledgement leaves the authoritative worker. The
  public strings name only the structure/unit roles; authoritative worker,
  foundation, and laboratory IDs remain available only to local simulation
  feedback. The server fixture checks the AutoBuild public message and absence
  of digits.
- The public production methods preserve the current world selection. Global
  build and production rally clear mutually exclusive destination modes;
  successful rally submission clears the mode, failed submission keeps it for
  retry, and escape cancels it without clearing selection. Producer pins are
  validated when rally mode is armed and revalidated by the simulation when
  submitted.

## Reviewed invariants

- Batch training plans all cost, supply, queue capacity, and producer job IDs
  before mutation. Assignment uses total remaining time and producer ID for an
  exact tie.
- Research checks duplicates across every live owned queue before charging.
- Protocol translation preserves cancellation job IDs while translating only
  the owned producer handle; visible foreign and unknown pins remain subject to
  authoritative rejection.
- Version 7 preserves queue IDs and next sequences. Versions 1-6 synthesize
  ordered IDs and keep legacy index cancellation semantics for old recordings.
- Completing or canceling a queue entry does not reuse its ID. A stale stable ID
  is rejected instead of falling through to a queue position.

No additional source correctness issue was found in this follow-up. This is a
source review only; it does not claim compilation or runtime verification.
