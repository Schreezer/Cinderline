#!/usr/bin/env python3
"""Reproducible original Cinderline cues. Python standard library only; no playback."""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import math
from pathlib import Path
import random
import struct
import wave

RATE = 48_000
SEED = 20260911
TAU = math.tau
ROOT = Path(__file__).resolve().parents[1]

# name, duration seconds, peak target, role
CUES = (
    ("UI_Click", 0.065, 0.42, "Soft mechanical interface tap"),
    ("Order_Ack", 0.180, 0.49, "Two ascending confirmation tones"),
    ("Order_Invalid", 0.240, 0.45, "Muted descending rejection cue"),
    ("Unit_Ready", 0.320, 0.54, "Three light rising readiness notes"),
    ("Building_Ready", 0.560, 0.60, "Lower, wider three-note structure completion cue"),
    ("Weapon_Pulse", 0.110, 0.64, "Compact electronic pulse with a soft mechanical transient"),
    ("Impact", 0.170, 0.63, "Short low thud with damped metallic resonance"),
    ("Explosion", 0.580, 0.79, "Low expanding noise burst and falling bass body"),
)


def low_pass(values: list[float], cutoff: float) -> list[float]:
    coefficient = 1.0 - math.exp(-TAU * cutoff / RATE)
    state = 0.0
    output = []
    for value in values:
        state += coefficient * (value - state)
        output.append(state)
    return output


def envelope(t: float, duration: float, attack: float = 0.006, decay: float = 4) -> float:
    if t < 0 or t >= duration:
        return 0.0
    onset = math.sin(min(1.0, t / attack) * math.pi / 2) ** 2
    release = min(1.0, (duration - t) / min(0.028, duration * 0.25)) ** 2
    return onset * release * math.exp(-decay * t / duration)


def add_tone(output: list[float], start: float, duration: float, frequency: float,
             amplitude: float, *, end_frequency: float | None = None,
             attack: float = 0.005, decay: float = 3.0, shimmer: float = 0.0) -> None:
    phase = 0.0
    first = round(start * RATE)
    count = round(duration * RATE)
    target = end_frequency if end_frequency is not None else frequency
    for j in range(min(count, len(output) - first)):
        t = j / RATE
        fraction = j / max(1, count - 1)
        instantaneous = frequency * ((target / frequency) ** fraction)
        phase += TAU * instantaneous / RATE
        signal = math.sin(phase) + shimmer * math.sin(phase * 2.01)
        output[first + j] += amplitude * signal * envelope(t, duration, attack, decay)


def add_noise(output: list[float], rng: random.Random, duration: float, amplitude: float,
              cutoff: float, *, decay: float = 5, attack: float = 0.003,
              end_cutoff: float | None = None) -> None:
    state = 0.0
    for i in range(min(len(output), round(duration * RATE))):
        t = i / RATE
        current_cutoff = cutoff if end_cutoff is None else cutoff * ((end_cutoff / cutoff) ** (t / duration))
        coefficient = 1.0 - math.exp(-TAU * current_cutoff / RATE)
        state += coefficient * (rng.uniform(-1, 1) - state)
        output[i] += amplitude * state * envelope(t, duration, attack, decay)


def synthesize(name: str, duration: float, peak: float, cue_seed: int) -> list[int]:
    samples = [0.0] * round(duration * RATE)
    rng = random.Random(cue_seed)
    if name == "UI_Click":
        add_tone(samples, 0, 0.055, 460, 0.30, end_frequency=290, attack=0.0018, decay=5)
        add_noise(samples, rng, 0.042, 0.52, 2600, decay=7, attack=0.0015)
    elif name == "Order_Ack":
        add_tone(samples, 0, 0.105, 430, 0.54, shimmer=0.10)
        add_tone(samples, 0.064, 0.116, 645, 0.45, shimmer=0.08)
        add_noise(samples, rng, 0.04, 0.12, 1800)
    elif name == "Order_Invalid":
        add_tone(samples, 0, 0.125, 310, 0.46, end_frequency=265, shimmer=0.11)
        add_tone(samples, 0.09, 0.15, 205, 0.44, end_frequency=185, shimmer=0.10)
        add_noise(samples, rng, 0.06, 0.12, 1300)
    elif name == "Unit_Ready":
        for start, frequency, amplitude in ((0, 392, 0.42), (0.07, 523.25, 0.40), (0.15, 659.25, 0.37)):
            add_tone(samples, start, 0.17, frequency, amplitude, shimmer=0.12, decay=2.7)
        add_noise(samples, rng, 0.045, 0.09, 1800)
    elif name == "Building_Ready":
        for start, frequency, amplitude in ((0, 196, 0.44), (0.11, 261.63, 0.40), (0.24, 392, 0.34)):
            add_tone(samples, start, 0.32, frequency, amplitude, attack=0.013, decay=2.8, shimmer=0.17)
        add_tone(samples, 0.24, 0.30, 130.81, 0.17, attack=0.014, decay=3)
        add_noise(samples, rng, 0.10, 0.16, 1400, decay=4)
    elif name == "Weapon_Pulse":
        add_tone(samples, 0, 0.11, 950, 0.44, end_frequency=240, attack=0.002, decay=4)
        add_tone(samples, 0, 0.105, 155, 0.25, end_frequency=90, attack=0.003, decay=4.5)
        add_noise(samples, rng, 0.068, 0.39, 3300, decay=5.5, attack=0.002)
    elif name == "Impact":
        add_noise(samples, rng, 0.16, 0.62, 2200, end_cutoff=620, decay=5, attack=0.002)
        add_tone(samples, 0, 0.17, 112, 0.34, end_frequency=61, attack=0.003, decay=4.5)
        add_tone(samples, 0.003, 0.12, 337, 0.11, end_frequency=306, attack=0.002, decay=5)
    elif name == "Explosion":
        add_noise(samples, rng, 0.58, 1.10, 1850, end_cutoff=210, decay=3.7, attack=0.007)
        add_tone(samples, 0, 0.56, 81, 0.51, end_frequency=34, attack=0.012, decay=3.5)
        add_tone(samples, 0.023, 0.49, 132, 0.12, end_frequency=52, attack=0.014, decay=4)
    else:
        raise ValueError(name)

    # Restrained treble and silent endpoints. Remove DC using a smooth window
    # rather than subtracting a constant, which would undo endpoint silence.
    samples = low_pass(samples, 5400)
    fade_in = max(2, round(min(0.003, duration * 0.05) * RATE))
    fade_out = max(2, round(min(0.022, duration * 0.15) * RATE))
    for i in range(len(samples)):
        onset = math.sin(min(1.0, i / fade_in) * math.pi / 2) ** 2
        release = math.sin(min(1.0, (len(samples) - 1 - i) / fade_out) * math.pi / 2) ** 2
        samples[i] *= onset * release
    dc_window = [math.sin(math.pi * i / (len(samples) - 1)) ** 2 for i in range(len(samples))]
    dc = sum(samples) / sum(dc_window)
    samples = [sample - dc * weight for sample, weight in zip(samples, dc_window)]
    maximum = max(abs(sample) for sample in samples)
    normalized = [sample * peak / maximum for sample in samples]
    pcm = [round(sample * 32767) for sample in normalized]
    pcm[0] = pcm[-1] = 0
    return pcm


def encode_wav(pcm: list[int]) -> bytes:
    buffer = io.BytesIO()
    with wave.open(buffer, "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(RATE)
        wav.writeframes(struct.pack(f"<{len(pcm)}h", *pcm))
    return buffer.getvalue()


def metrics(data: bytes) -> dict:
    with wave.open(io.BytesIO(data), "rb") as wav:
        channels, width, rate, frames = wav.getnchannels(), wav.getsampwidth(), wav.getframerate(), wav.getnframes()
        assert (channels, width, rate) == (1, 2, RATE), "Unexpected WAV format"
        samples = struct.unpack(f"<{frames}h", wav.readframes(frames))
    amplitude = [sample / 32768 for sample in samples]
    peak = max(abs(sample) for sample in amplitude)
    dc = sum(amplitude) / frames
    assert peak <= 0.85, "Peak exceeds headroom requirement"
    assert abs(dc) < 1 / 32768, "DC exceeds one PCM quantization step"
    assert samples[0] == samples[-1] == 0, "Endpoints must be silent"
    assert max(samples) < 32767 and min(samples) > -32768, "Clipped PCM sample"
    return {
        "duration_seconds": frames / rate,
        "sample_rate_hz": rate,
        "channels": channels,
        "bits_per_sample": width * 8,
        "frames": frames,
        "peak_linear": round(peak, 9),
        "rms_linear": round(math.sqrt(sum(sample * sample for sample in amplitude) / frames), 9),
        "dc_offset_linear": round(dc, 12),
        "silent_endpoints": True,
        "clipped_samples": 0,
        "sha256": hashlib.sha256(data).hexdigest(),
        "bytes": len(data),
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "RawAssets" / "Audio")
    parser.add_argument("--verify-only", action="store_true", help="Read existing files and compare with deterministic synthesis; write nothing")
    args = parser.parse_args()
    if not args.verify_only:
        args.output.mkdir(parents=True, exist_ok=True)
    entries = []
    for index, (name, duration, peak, role) in enumerate(CUES):
        cue_seed = SEED + index * 101
        data = encode_wav(synthesize(name, duration, peak, cue_seed))
        destination = args.output / f"{name}.wav"
        if args.verify_only:
            assert destination.read_bytes() == data, f"Reproducibility mismatch: {destination.name}"
        else:
            destination.write_bytes(data)
        entries.append({"name": name, "file": destination.name, "role": role, "seed": cue_seed, **metrics(destination.read_bytes())})
    manifest = {
        "schema_version": 1,
        "project": "Cinderline",
        "provenance": "Original deterministic procedural synthesis; no source recordings or downloaded audio",
        "generator": "scripts/create_audio_assets.py",
        "generator_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "base_seed": SEED,
        "validation_scope": "File format, numerical signal checks, and byte-for-byte regeneration; no listening or in-game mixing validation",
        "assets": entries,
    }
    serialized = json.dumps(manifest, indent=2, sort_keys=True) + "\n"
    if args.verify_only:
        assert (args.output / "manifest.json").read_text() == serialized, "Manifest differs from regenerated values"
    else:
        (args.output / "manifest.json").write_text(serialized)
    for entry in entries:
        print(f"{entry['file']}: {entry['duration_seconds']:.3f}s, peak {entry['peak_linear']:.4f}, RMS {entry['rms_linear']:.4f}")
    print(f"{'Verified' if args.verify_only else 'Generated'} {len(entries)} original mono 48 kHz / 16-bit WAV cues. No playback performed.")


if __name__ == "__main__":
    main()
