# FitSnap production handoff

Start here when working on the health-video series. These guides describe the local workflow verified on 23 September 2026. Re-check installed CLI help and live configuration before execution; paths assume Chirag's Mac.

## Read by task

| Task | Read |
| --- | --- |
| Write or improve a script | [01 Editorial and research](01-editorial-and-research.md) |
| Produce an episode end to end | [02 Production workflow](02-production-workflow.md), then the relevant tool guides |
| Generate illustrations, infographics, optional footage | [03 Google Flow CLI](03-google-flow-cli.md) |
| Generate narration and keep voice identity | [04 Local voice](04-local-voice.md) |
| Improve emotion, pauses, and TTS prompts | [08 Audio prompting](08-audio-prompting.md) |
| Build HTML scenes and synchronize captions | [05 HTML and HyperFrames](05-html-and-hyperframes.md) |
| Render, inspect, and mark a draft ready | [06 Verification](06-render-and-verification.md) |
| Run the daily generator or inspect publishing state | [07 Queue and automation](07-queue-and-automation.md) |

Before production, read [WORKFLOW.md](../WORKFLOW.md), [config.json](../config.json), [status.json](../status.json), and the manifests in [queue](../queue/). Those files contain operational state; this folder explains how to work with it. Publishing is currently disabled. Creating a draft does not authorize any external upload or share-link publication.

## References and their limits

- [Label-reading project](../../supermarket-label-detective/README.md): established voice and technical reference. Preserve its published export and receipts.
- [Detox project](../episodes/2026-09-23-detox-job-interview/README.md): working 30-second Flow + Qwen + HTML implementation. **The user disliked its writing and wants Claude to improve it.** Reuse the mechanics, not the script as an editorial gold standard. Its ready status records technical completion, not approval of the writing.
- [Illustrated-story study](../references/PhrcWPJEpqs/STYLE-STUDY.md): observed staging, continuity, and visual storytelling techniques. Original reference footage is study material, not an export asset.

Current generation is English. Hindi and long-form are ambitions; do not imply a verified Hindi voice or completed Hindi pipeline exists. Follow the user's requested scope and duration.

## Prompt to hand Claude

> Read `/Users/chirag13/Documents/ChatGPT/starCraft/videos/health-channel/CLAUDE.md`, then `docs/README.md` and `docs/01-editorial-and-research.md`. Help improve the writing for our health explainers. The existing detox film is a technical reference whose writing I disliked. Propose three distinct hooks and one polished 30-second script with visual beats and source support. Work on the script first; this request does not ask you to generate or publish a video.

For a production request, replace the last two sentences with the exact episode, duration, and requested output. Claude Code can read these paths locally. In Claude web, attach the relevant Markdown files; local paths alone do not grant access.
