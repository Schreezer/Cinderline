# Daily health explainers

For tool-specific instructions and Claude handoff, start at [docs/README.md](docs/README.md).

## Current user hold

The user first said "hey dont post yet", then "emm you know what just upload". The current label-reading episode was authorized for immediate publication. Future automatic posting remains disabled; generate local review artifacts only until the user explicitly resumes recurring publication. Preserve published episodes and their receipts. This hold overrides the recurring publication steps below.

The user authorized one English video per day on their Instagram Reels and YouTube Shorts accounts at 19:00 Asia/Kolkata. Generate at 18:00; publishing is a separate scheduled step. Verified destinations: Instagram @fitsnap.fun (public at the user's explicit request) and YouTube Chirag Aggarwal, channel UCeL6Mq1UW4eW8pscVuYCVXw. Use the Codex in-app browser (`iab`) and verify the same account identities before any future authorized upload.

## Production

Read `config.json` and the queue first. Use the oldest ready episode for the next posting date. The initial label-reading film is already published; preserve its export and receipts. Keep at most three wholly unpublished ready episodes while review is pending. Reserve each date in its episode manifest before starting; a resumed run continues that episode.

For a new episode, use `episodes/YYYY-MM-DD-topic/`. Aim for 35–60 seconds, 1080×1920, 30fps, H.264/AAC. Reuse the approved forest-green, cream, coral editorial style, legible captions, HTML diagrams, and occasional still images. Use HyperFrames and its applicable skills. Generate stills through the existing authenticated Google Flow CLI at `/Users/chirag13/src/gflow-cli` where useful; limited silent AI footage may illustrate a concept. Keep narration separate; no speaking AI presenter. Existing fictional assets and HTML illustrations are valid fallbacks when Flow is unavailable. Do not buy credits or add paid services.

Before storyboarding a new draft, read `references/PhrcWPJEpqs/STYLE-STUDY.md` for the user's illustrated-story reference and its concrete FitSnap adaptation. Build a visible situation, one question, a visual reveal, and a practical resolution using consistent original characters and props. Reuse scenes with meaningful changes in expression, framing, and evidence. Reference footage and artwork stay outside production exports.

Use Nano Banana 2 through Flow for complete infographic images with integrated text when useful. Prompt exact approved wording and values, then inspect the rendered text and diagram for accuracy and mobile legibility. Use HTML/SVG for independent animation, changing data, and timed captions; static labels can be part of the generated artwork.

Choose one useful, specific misconception with an actionable takeaway. Rotate label reading, everyday movement, sleep, and general nutrition. Check every substantive health claim against current primary research, systematic reviews, or official clinical/public-health guidance; record source URLs and claim support in `SOURCES.md`. Use uncertainty when warranted. Exclude personalized treatment, medication changes, unsupported cure claims, and fear-based food claims. Avoid repeating a recent episode.

For voice, follow the working script and model parameters in the approved reference project recorded in config. Qwen Base BF16 must receive `public/audio/voice-examples-v3/qwen-designed-raw.wav` and its exact reference transcript to preserve the approved synthetic voice. Keep short conversational sentences, questions, breath pauses, and varied emphasis. Generate the full script continuously. Normalize gain while preserving pauses and pitch; retime visuals to measured audio. Fish is an approved alternative only with the previously successful reference and sparse emotion cues. Do not reuse the original unconditioned or slowed-down narration.

Transcribe the entire narration, compare spoken words with the script, and time captions and semantic visual reveals to the measured words. Run HyperFrames checks and inspect all mounted scenes. Render delivery quality, decode the entire MP4, verify streams and duration, inspect encoded frames including opening/middle/end, and check the final mixed audio contains the complete script. Fix actual defects before marking ready. Keep all evidence inside the episode.

Write a queue manifest with stable id, topic, absolute video path, SHA-256, caption, source/evidence paths, planned date, and independent platform states. A ready episode must have an existing verified video and supported claims. Captions should add useful context and cite sources. Disclose generated imagery/narration and use applicable platform AI labels. Fictional product labels and illustrative numbers must stay clearly identified. The generation step does not upload.

## Publication

The active destinations are Instagram Reels and YouTube Shorts only. TikTok is excluded by the user. For YouTube, prepare a concise title and source-linked description, verify the exact channel identity, and set the audience accurately for this general adult educational series. Follow the platform's current altered/synthetic-content disclosure requirements.

This section is dormant while publishing is disabled. Only after explicit user resumption, follow the newly authorized scope and schedule using available authorized connectors or normal browser upload interfaces. Historical scheduling instructions do not resume recurring posting. Missing handles, login, account mismatch, unresolved verification failures, or unavailable upload access require holding that platform and reporting the exact action needed. Never choose a different account or work around access restrictions.

Select an episode already partially published today before a new one. Otherwise select the oldest ready pending episode. Check each platform ledger and visible account history before retrying. Record an attempt before upload, and record returned post ID/URL plus visible published confirmation immediately after success. An uploaded draft or processing screen is not a published post. If a response is ambiguous, mark `unknown`, reconcile account history, and avoid blind retries. If one platform succeeds, preserve that success and retry only the missing platform. At most one new post per platform per local calendar day; missed dates never trigger bulk catch-up.

Check the MP4 hash against its manifest before upload. Preserve source-backed caption meaning if platform limits require shortening. Use public post visibility for the user's named channel and the platform's applicable AI-content disclosure. Keep existing account settings unchanged. Verify the final published URL/account and persist evidence. Retain artifacts when publishing is blocked.

## Status

Report completion with output paths or actual published URLs. Report a new failure or required user action specifically. Unchanged blockers should stay quiet after the first report; record their fingerprint locally. These local scheduled runs require this computer and the needed services to be available; a scheduled wake-up is not proof of a completed post.
