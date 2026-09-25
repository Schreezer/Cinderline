# Queue, automation, and publication state

## Current operating mode

Read `../config.json`, `../status.json`, `../queue/`, and the current request before each run. As of 23 September 2026, the automation is `generate-daily-health-explainer`, producing local English drafts at 18:00 Asia/Kolkata, with one video per day and at most three unpublished ready episodes while review is pending.

The stored 19:00 publication time is historical configuration, not authorization to publish. `publishing_enabled` is false. Do not upload, schedule an external post, create a public share link, change account visibility, or resume posting without explicit user instruction. The earlier “just upload” applied to the label-reading episode, whose receipts are already saved. TikTok is outside the current destination scope.

## Daily run

1. Inspect incomplete work and queue manifests. Resume the same episode when possible.
2. Check today's generation record and completed drafts. A verified episode already completed today satisfies the one-video daily target; a three-draft cap is not a target to fill in one run.
3. If today's target is satisfied or the unpublished-ready cap is reached, record the check without generating duplicate work.
4. Otherwise reserve today's generation against a stable episode ID, research a distinct topic, and follow the production guides.
5. Update status with the resulting draft or a specific blocker. Notify only on a newly completed draft, changed failure, or required user action. Unchanged state stays quiet.

These runs depend on the local machine, runtime, models, and needed sessions being available. A scheduled trigger is not proof that production finished.

## Manifest contents

Use the [detox queue entry](../queue/002-detox-job-interview.json) as a shape reference, with new values: stable ID, topic, language, creation timestamp, absolute video path, SHA-256, duration, caption, source file, verification file, and independent platform states. Keep publication date null for a held draft; record generation date separately. Current unpublished platform states are `held`, URLs null, and `publish_now_authorized` false.

Keep the episode manifest and queue record consistent. Preserve published IDs, URLs, hashes, timestamps, and receipts. A revision gets a distinct version/export so a prior published artifact remains traceable. Record script feedback separately from technical verification rather than treating a previous ready flag as approval of new writing.

If publication is explicitly resumed later, read the relevant section of WORKFLOW.md and verify the exact requested scope, account identities, current platform requirements, existing receipts, and artifact hash before acting. Browser login alone does not grant permission to publish.
