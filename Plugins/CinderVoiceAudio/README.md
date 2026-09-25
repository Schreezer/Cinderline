# Native voice audio

`FCinderVoiceAudio` supplies mono PCM16LE at 24 kHz to the game voice subsystem and plays the same format returned by its gateway. It has no provider credentials or network client.

## Platforms and lifecycle

- Mac and iOS: AVAudioEngine with native voice processing (echo cancellation and automatic gain control), an AVAudioConverter for microphone sample rates, and AVAudioPlayerNode for response audio.
- Other platforms: `Start` returns an explicit unsupported error. No microphone permission is requested there.
- Microphone permission is requested only by `Start`, which the caller invokes after a Voice-button gesture. A denied permission, missing usage description, unavailable input, route change, or interrupted audio session returns a visible error.
- All controls are game-thread calls. Native audio graph mutation and format conversion use one serial processing queue shared across session restarts. Capture callbacks use that queue; state callbacks use the game thread. The caller must apply its own session-generation guard to callbacks.
- Mute immediately blocks outgoing PCM, mutes the native voice-processing input, and clears converter history before unmuting. The OS audio session remains active for incoming speech; `Stop` releases microphone resources.
- Stop invalidates capture immediately and tears down the engine asynchronously. iOS restores the previous category/mode if this plugin still owns the voice-chat category; it does not deactivate Unreal's app-wide audio session.

## Bounded audio

At most four capture buffers may await conversion. Capture discontinuity ends the session with a retry error so missing words cannot silently change a game command. Playback packets are limited to two seconds and total queued playback to three seconds. Invalid or overloaded playback ends with a retry error. `ClearPlayback` invalidates old queued packets and stops scheduled speech for interruption handling.

The consumer must also bound any game-thread/network queue it creates after the capture callback.

## Packaging and validation

iOS microphone usage text is appended to the existing plist metadata. Mac uses the plugin's `Info-Mac.Template.plist` and sandbox entitlements with audio-input and the existing game's client/server networking. Unreal Editor uses its engine-provided microphone description.

Local source checks validate the plugin descriptor and Apple metadata. Building/linking the Mac and iOS targets, prompting for microphone permission, echo cancellation, Bluetooth routing, interruption recovery, and coexistence with Unreal game audio require separate runtime verification. No device or live-provider result is implied by source checks.
