# Production workflow

## Workspace

```text
videos/health-channel/
  CLAUDE.md, WORKFLOW.md, config.json, status.json
  docs/                  tool and workflow guides
  queue/                 episode manifests and publication receipts
  references/            study material
  episodes/YYYY-MM-DD-topic/
    BRIEF.md, SCRIPT.md, STORYBOARD.md, SOURCES.md, frame.md
    episode.json, caption.txt, README.md
    scripts/
    public/art/, public/flow/, public/audio/, public/fonts/
    compositions/frames/, index.html, package.json
    snapshots/, renders/
```

Use a new episode folder or clearly versioned revision. Preserve earlier exports and unrelated workspace work. The existing `scripts/build.py`, `narrate.py`, and `verify.py` are episode-specific examples, not generic commands that accept arbitrary topics.

## Ordered production steps

1. **Reserve:** inspect configuration, today's run, queue, and incomplete episodes. Resume unfinished work before starting another topic. Record the generation date and episode ID separately from any publication date.
2. **Research and write:** follow [editorial guidance](01-editorial-and-research.md). Done when the complete spoken script and all claim support are recorded.
3. **Storyboard:** record visual beats and art direction. Match the requested duration; the latest experiment is 30 seconds, while the older general workflow suggests 35–60. Describe exact generated lettering and reserve caption space.
4. **Narrate:** follow [local voice](04-local-voice.md). Save raw audio and generation metadata. Done when the full performance is audible, natural, complete, and measured.
5. **Align:** transcribe, compare every word, and derive phrase captions and visual-reveal timings from speech. Estimated storyboard timings are replaced by measured timings.
6. **Illustrate:** follow [Flow](03-google-flow-cli.md). Preserve prompts, raw candidates, metadata, and final selections. Review every text-bearing image for spelling, scientific meaning, units, and legibility.
7. **Compose:** follow [HTML](05-html-and-hyperframes.md). Build deterministic scenes and a single narration timeline. Inspect mounted scenes at actual event times.
8. **Verify:** follow [verification](06-render-and-verification.md). Done when the delivery MP4, complete narration audit, encoded visual inspection, sources, and hash exist.
9. **Queue:** save an episode manifest and matching queue record with held platform states. Report the local draft path when newly completed. No external upload is part of this workflow.

## Art direction

The current reference uses cream `#efe7d4`, forest `#243a21`, and coral `#e89cb1`; bold ink illustrations; Source Serif 4 and JetBrains Mono. Adapt composition to 1080×1920 directly. Use a consistent character/prop description and a master reference when the tool supports it. Integrated Nano Banana 2 lettering is welcome; HTML is useful for independently moving elements, precise timing, changing data, and localization.

The narration is the emotional thread. Keep optional AI footage silent, and use restrained sound effects or music below speech. No talking AI presenter or lip-sync is part of the current format.
