"""Compare sampled animation RGB against CoinGL, without image correction.

Requires numpy and Pillow. Reads capture metadata from the complete logs,
compares matching logical frames and scene-state digests, and records all RGB
errors. It also checks that dynamic images and state actually change over time.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re

import numpy as np
from PIL import Image


def captures(directory, row):
    text = (directory / row["log"]).read_text()
    frames = {}
    for line in text.splitlines():
        if line.startswith("capture "):
            fields = dict(re.findall(r"(\w+)=(\S+)", line))
            frame = int(fields["logical_frame"])
            image = Path(fields["image"])
            if not image.exists():
                image = directory / "images" / image.name
            assert image.exists(), image
            frames[frame] = {"image": image, "state": fields["state_fnv64"],
                             "rgb": fields["rgb_fnv64"], "rgba": fields["rgba_fnv64"]}
    assert len(frames) == 7, (row, len(frames))
    return frames


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--reference", default="coingl")
    args = parser.parse_args()
    directory = args.directory.resolve()
    rows = json.loads((directory / "results.json").read_text())
    cases = sorted({row["case"] for row in rows})
    results, images, motion = [], [], []
    for case in cases:
        group = [row for row in rows if row["case"] == case]
        reference = next(row for row in group if row["variant"] == args.reference)
        ref_frames = captures(directory, reference)
        for row in group:
            sample = captures(directory, row)
            assert sample.keys() == ref_frames.keys()
            if case != "static":
                assert len({v["state"] for v in sample.values()}) > 1, (case, row["variant"], "unchanged scene state")
                assert len({v["rgb"] for v in sample.values()}) > 1, (case, row["variant"], "unchanged RGB")
            else:
                assert len({v["state"] for v in sample.values()}) == 1
                assert len({v["rgb"] for v in sample.values()}) == 1
            motion.append({"case": case, "variant": row["variant"],
                           "distinct_states": len({v["state"] for v in sample.values()}),
                           "distinct_rgb": len({v["rgb"] for v in sample.values()})})
            for frame, item in sample.items():
                assert item["state"] == ref_frames[frame]["state"], (case, frame, row["variant"], "scene-state mismatch")
                pixels = np.asarray(Image.open(item["image"]), dtype=np.int16)
                ref_pixels = np.asarray(Image.open(ref_frames[frame]["image"]), dtype=np.int16)
                assert pixels.shape == ref_pixels.shape
                delta = np.abs(pixels - ref_pixels)
                results.append({"case": case, "variant": row["variant"], "reference": args.reference,
                                "logical_frame": frame, "state": item["state"],
                                "rgb_mae": float(delta.mean()), "max_channel_error": int(delta.max()),
                                "pixels_different": int(np.any(delta != 0, axis=2).sum()),
                                "pixels_over3": int(np.any(delta > 3, axis=2).sum())})
                images.append({"case": case, "variant": row["variant"], "logical_frame": frame,
                               "filename": item["image"].name,
                               "ppm_sha256": hashlib.sha256(item["image"].read_bytes()).hexdigest(),
                               "rgb_fnv64": item["rgb"], "rgba_fnv64": item["rgba"]})
    (directory / "rgb.json").write_text(json.dumps(results, indent=2) + "\n")
    (directory / "motion.json").write_text(json.dumps(motion, indent=2) + "\n")
    (directory / "image-manifest.json").write_text(json.dumps(images, indent=2) + "\n")
    for case in cases:
        for variant in sorted({r["variant"] for r in rows}):
            group = [r for r in results if r["case"] == case and r["variant"] == variant]
            print(f'{case}/{variant}: MAE max={max(r["rgb_mae"] for r in group):.6f}; '
                  f'max channel={max(r["max_channel_error"] for r in group)}; '
                  f'max pixels>3={max(r["pixels_over3"] for r in group)}')
    print(f'{len(results)} RGB comparisons; matching scene-state digests; motion verified')


if __name__ == "__main__":
    main()
