#!/usr/bin/env python3
"""Compare a captured screenshot against its accepted visual baseline.

macOS system Python has no imaging library and this repository adds no runtime
dependency, so the decoder here handles exactly the shape Unreal's screenshot
writer produces: non-interlaced, 8-bit RGB or RGBA. Every other PNG shape is
refused by name instead of being decoded approximately, because a silently
wrong pixel number is worse than no comparison: it would certify a visual
regression as unchanged.

Exit status: 0 when the candidate is within the threshold or was accepted as
the new baseline, 1 when the difference exceeds the threshold or no baseline
exists yet, and 2 when the images cannot be compared at all.
"""

import argparse
from pathlib import Path
import shutil
import sys
import zlib


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
# PNG colour type -> samples per pixel. Only the two truecolour forms are read.
SUPPORTED_COLOUR_TYPES = {2: 3, 6: 4}
UNSUPPORTED_COLOUR_TYPES = {0: "grayscale", 3: "palette", 4: "grayscale with alpha"}
# A whole-image mean this large is a deliberate art change, not encoder noise:
# MetalFX at 80% linear plus FXAA already moves single pixels between runs.
DEFAULT_THRESHOLD = 0.02


class CompareError(ValueError):
    """The images cannot be compared, so no difference number is reported."""


def _chunks(data, path):
    """Walk the PNG chunk stream. CRCs are skipped; zlib catches real damage."""
    if data[:8] != PNG_SIGNATURE:
        raise CompareError(f"{path} is not a PNG file")
    offset = 8
    while offset + 12 <= len(data):
        length = int.from_bytes(data[offset:offset + 4], "big")
        kind = data[offset + 4:offset + 8]
        body = data[offset + 8:offset + 8 + length]
        if len(body) != length:
            raise CompareError(f"{path} has a truncated {kind.decode('ascii', 'replace')} chunk")
        yield kind, body
        offset += 12 + length


def _unfilter(raw, width, height, channels, path):
    """Reverse the five per-scanline PNG filters into flat 8-bit samples."""
    stride = width * channels
    if len(raw) != (stride + 1) * height:
        raise CompareError(
            f"{path} decompressed to {len(raw)} bytes, expected {(stride + 1) * height}"
        )
    pixels = bytearray(stride * height)
    previous = bytearray(stride)
    read = 0
    for row in range(height):
        method = raw[read]
        line = bytearray(raw[read + 1:read + 1 + stride])
        read += stride + 1
        if method == 1:  # Sub
            for index in range(channels, stride):
                line[index] = (line[index] + line[index - channels]) & 0xFF
        elif method == 2:  # Up
            for index in range(stride):
                line[index] = (line[index] + previous[index]) & 0xFF
        elif method == 3:  # Average
            for index in range(stride):
                left = line[index - channels] if index >= channels else 0
                line[index] = (line[index] + ((left + previous[index]) >> 1)) & 0xFF
        elif method == 4:  # Paeth
            for index in range(stride):
                left = line[index - channels] if index >= channels else 0
                up = previous[index]
                up_left = previous[index - channels] if index >= channels else 0
                estimate = left + up - up_left
                from_left, from_up = abs(estimate - left), abs(estimate - up)
                from_up_left = abs(estimate - up_left)
                if from_left <= from_up and from_left <= from_up_left:
                    predictor = left
                elif from_up <= from_up_left:
                    predictor = up
                else:
                    predictor = up_left
                line[index] = (line[index] + predictor) & 0xFF
        elif method != 0:  # None
            raise CompareError(f"{path} row {row} uses unknown filter method {method}")
        pixels[row * stride:(row + 1) * stride] = line
        previous = line
    return pixels


def decode(path):
    """Return (width, height, channels, samples) for an 8-bit RGB/RGBA PNG."""
    try:
        data = path.read_bytes()
    except OSError as error:
        raise CompareError(f"cannot read {path}: {error}") from error
    header = None
    compressed = bytearray()
    for kind, body in _chunks(data, path):
        if kind == b"IHDR":
            if len(body) != 13:
                raise CompareError(f"{path} has a malformed IHDR chunk")
            header = body
        elif kind == b"IDAT":
            compressed += body
        elif kind == b"IEND":
            break
    if header is None or not compressed:
        raise CompareError(f"{path} has no IHDR header or no image data")
    width = int.from_bytes(header[0:4], "big")
    height = int.from_bytes(header[4:8], "big")
    depth, colour, compression, filtering, interlace = header[8:13]
    if not width or not height:
        raise CompareError(f"{path} declares an empty {width}x{height} image")
    if compression != 0 or filtering != 0:
        raise CompareError(
            f"{path} uses compression method {compression} and filter method {filtering}; "
            "only the standard deflate/adaptive pair is supported"
        )
    if interlace != 0:
        raise CompareError(
            f"{path} is Adam7 interlaced; this comparator reads progressive PNGs only. "
            "Re-export it non-interlaced rather than trusting a partial comparison"
        )
    if depth != 8:
        raise CompareError(
            f"{path} stores {depth}-bit samples; only 8-bit samples are supported. "
            "Unreal's screenshot writer emits 8-bit, so a different depth means a different tool"
        )
    if colour not in SUPPORTED_COLOUR_TYPES:
        name = UNSUPPORTED_COLOUR_TYPES.get(colour, f"colour type {colour}")
        raise CompareError(
            f"{path} is {name}; only 8-bit RGB and RGBA are supported"
        )
    channels = SUPPORTED_COLOUR_TYPES[colour]
    try:
        raw = zlib.decompress(bytes(compressed))
    except zlib.error as error:
        raise CompareError(f"{path} has undecodable image data: {error}") from error
    return width, height, channels, _unfilter(raw, width, height, channels, path)


def compare(baseline_path, candidate_path):
    """Return the mean absolute and worst single-channel difference."""
    base_width, base_height, base_channels, base_samples = decode(baseline_path)
    width, height, channels, samples = decode(candidate_path)
    if (base_width, base_height, base_channels) != (width, height, channels):
        raise CompareError(
            f"baseline is {base_width}x{base_height}x{base_channels} but candidate is "
            f"{width}x{height}x{channels}; capture at the baseline's framebuffer or re-baseline"
        )
    # A 1440x900 RGBA frame is 5.2M samples, so this loop costs a second or two.
    # That is cheap against the ~20 s editor launch it is validating.
    total = 0
    worst = 0
    for left, right in zip(base_samples, samples):
        delta = left - right if left >= right else right - left
        total += delta
        if delta > worst:
            worst = delta
    return {
        "width": width, "height": height, "channels": channels,
        "samples": len(samples),
        "mean_abs_diff": total / (255.0 * len(samples)),
        "max_channel_diff": worst,
    }


def _report(label, result, **fields):
    parts = [f"PNG_COMPARE label={label}", f"result={result}"]
    parts.extend(f"{key}={value}" for key, value in fields.items())
    print(" ".join(parts))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True, help="accepted reference PNG")
    parser.add_argument("--candidate", type=Path, required=True, help="freshly captured PNG")
    parser.add_argument("--label", help="name used in the report line; defaults to the baseline stem")
    parser.add_argument(
        "--threshold", type=float, default=DEFAULT_THRESHOLD,
        help=f"maximum accepted mean absolute difference, 0..1 (default {DEFAULT_THRESHOLD})",
    )
    parser.add_argument(
        "--accept", action="store_true",
        help="replace the baseline with the candidate; required to record any new look",
    )
    args = parser.parse_args(argv)
    label = args.label or args.baseline.stem
    if not (0 <= args.threshold <= 1):
        print("Visual comparison failed: --threshold must be between 0 and 1.", file=sys.stderr)
        return 2
    try:
        if not args.candidate.is_file():
            raise CompareError(f"candidate {args.candidate} does not exist")
        if not args.baseline.is_file():
            if not args.accept:
                _report(label, "no-baseline", baseline=args.baseline, candidate=args.candidate)
                print(
                    f"Visual comparison failed: no baseline at {args.baseline}. "
                    "Inspect the capture, then re-run with --accept to record it as the baseline.",
                    file=sys.stderr,
                )
                return 1
            decode(args.candidate)  # Never record a baseline this tool cannot read back.
            args.baseline.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(args.candidate, args.baseline)
            _report(label, "baseline-created", baseline=args.baseline, candidate=args.candidate)
            return 0
        measurement = compare(args.baseline, args.candidate)
    except CompareError as error:
        print(f"Visual comparison failed: {error}.", file=sys.stderr)
        return 2
    exceeded = measurement["mean_abs_diff"] > args.threshold
    fields = {
        "width": measurement["width"], "height": measurement["height"],
        "channels": measurement["channels"], "samples": measurement["samples"],
        "mean_abs_diff": f"{measurement['mean_abs_diff']:.6f}",
        "max_channel_diff": f"{measurement['max_channel_diff']}/255",
        "threshold": f"{args.threshold:.6f}",
        "baseline": args.baseline, "candidate": args.candidate,
    }
    if args.accept:
        shutil.copyfile(args.candidate, args.baseline)
        _report(label, "accepted", **fields)
        return 0
    _report(label, "exceeded" if exceeded else "within", **fields)
    if exceeded:
        print(
            f"Visual comparison failed: {label} mean absolute difference "
            f"{measurement['mean_abs_diff']:.6f} exceeds {args.threshold:.6f}. "
            f"Compare {args.candidate} against {args.baseline} by eye; if the new look is "
            "intended, re-run with --accept to move the baseline.",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
