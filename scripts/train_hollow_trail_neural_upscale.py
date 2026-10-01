#!/usr/bin/env python3
"""Train/export Hollow Trail's tiny integer 2x neural scenery upscaler.

The deployable model predicts three residuals for the interpolated samples in the
legacy 2x bilinear block while preserving the source anchor exactly. Inputs come
from one 3x3 source patch. It is intentionally small enough to run directly in
Hollow Trail without TensorFlow Lite or a runtime ML dependency.
"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path

import cv2
import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F

SEED = 20260928
EDGE_THRESHOLD = 80
TRAIN_SCENES = 220
TRAIN_SAMPLES_PER_SCENE = 500
VAL_SCENES = 40
VAL_SAMPLES_PER_SCENE = 700
EPOCHS = 60
BATCH = 1024


def synth_scene(rng: np.random.Generator, h: int = 64, w: int = 96) -> np.ndarray:
    """Procedural grayscale teacher shaped like Hollow Trail's scenery language."""
    y = np.linspace(0, 1, h, dtype=np.float32)[:, None]
    x = np.linspace(0, 1, w, dtype=np.float32)[None, :]
    base = float(rng.integers(0, 90))
    gx, gy = float(rng.uniform(-35, 35)), float(rng.uniform(-35, 35))
    image = np.clip(base + gx * (x - 0.5) + gy * (y - 0.5), 0, 255).astype(np.float32)

    # Wide atmospheric layers and soft occlusion fields.
    for _ in range(int(rng.integers(1, 4))):
        mask = np.zeros((h, w), np.uint8)
        kind = int(rng.integers(0, 3))
        if kind == 0:
            x1, x2 = sorted(rng.integers(-w // 4, 5 * w // 4, size=2))
            y1, y2 = sorted(rng.integers(-h // 4, 5 * h // 4, size=2))
            cv2.rectangle(mask, (int(x1), int(y1)), (int(x2), int(y2)), 255, -1)
        elif kind == 1:
            center = (int(rng.integers(-w // 4, 5 * w // 4)), int(rng.integers(-h // 4, 5 * h // 4)))
            axes = (int(rng.integers(5, w // 2)), int(rng.integers(4, h // 2)))
            cv2.ellipse(mask, center, axes, float(rng.integers(0, 180)), 0, 360, 255, -1)
        else:
            pts = np.stack([rng.integers(-10, w + 10, size=5), rng.integers(-10, h + 10, size=5)], axis=1).astype(np.int32)
            cv2.fillPoly(mask, [pts], 255)
        sigma = float(rng.uniform(1.0, 7.0))
        soft = cv2.GaussianBlur(mask.astype(np.float32), (0, 0), sigmaX=sigma, sigmaY=sigma)
        amplitude = float(rng.uniform(10, 90)) * (1 if rng.random() < 0.75 else -1)
        image = np.clip(image + amplitude * (soft / 255.0), 0, 255)

    # Hard silhouettes/linework plus offset soft shadows.
    for _ in range(int(rng.integers(4, 12))):
        shape = np.zeros((h, w), np.uint8)
        kind = int(rng.integers(0, 4))
        if kind == 0:
            x1, y1 = int(rng.integers(-10, w)), int(rng.integers(-10, h))
            x2 = int(rng.integers(x1 + 2, min(w + 15, x1 + 30)))
            y2 = int(rng.integers(y1 + 2, min(h + 15, y1 + 35)))
            cv2.rectangle(shape, (x1, y1), (x2, y2), 255, -1)
        elif kind == 1:
            center = (int(rng.integers(0, w)), int(rng.integers(0, h)))
            cv2.circle(shape, center, int(rng.integers(2, 18)), 255, -1)
        elif kind == 2:
            p1 = (int(rng.integers(-5, w + 5)), int(rng.integers(-5, h + 5)))
            p2 = (int(rng.integers(-5, w + 5)), int(rng.integers(-5, h + 5)))
            cv2.line(shape, p1, p2, 255, int(rng.integers(1, 6)), cv2.LINE_8)
        else:
            pts = np.stack([rng.integers(-5, w + 5, size=3), rng.integers(-5, h + 5, size=3)], axis=1).astype(np.int32)
            cv2.fillPoly(shape, [pts], 255)

        if rng.random() < 0.6:
            dx, dy = int(rng.integers(-5, 6)), int(rng.integers(-3, 5))
            shadow = np.roll(shape, (dy, dx), axis=(0, 1)).astype(np.float32)
            sigma = float(rng.uniform(0.8, 4.0))
            shadow = cv2.GaussianBlur(shadow, (0, 0), sigmaX=sigma, sigmaY=sigma)
            image = np.clip(image + float(rng.uniform(12, 75)) * (shadow / 255.0), 0, 255)

        tone = float(rng.integers(55, 256))
        image = np.maximum(image, tone * (shape.astype(np.float32) / 255.0))
    return np.clip(np.rint(image), 0, 255).astype(np.uint8)


def down2_sample(high: np.ndarray) -> np.ndarray:
    """Match runtime: quarter-res composition samples the even target lattice."""
    return high[0::2, 0::2].copy()


def baseline2(low: np.ndarray) -> np.ndarray:
    right = np.concatenate([low[:, 1:], low[:, -1:]], axis=1).astype(np.uint16)
    down = np.concatenate([low[1:, :], low[-1:, :]], axis=0).astype(np.uint16)
    down_right = np.concatenate([down[:, 1:], down[:, -1:]], axis=1).astype(np.uint16)
    a = low.astype(np.uint16)
    horizontal = (a + right) // 2
    vertical = (a + down) // 2
    diagonal = (horizontal + ((down + down_right) // 2)) // 2
    out = np.empty((low.shape[0] * 2, low.shape[1] * 2), np.uint8)
    out[0::2, 0::2] = a
    out[0::2, 1::2] = horizontal
    out[1::2, 0::2] = vertical
    out[1::2, 1::2] = diagonal
    return out


def make_patches(scene_count: int, per_scene: int, seed: int):
    rng = np.random.default_rng(seed)
    patches, residuals, baselines, local_ranges = [], [], [], []
    for _ in range(scene_count):
        high = synth_scene(rng)
        low = down2_sample(high)
        base = baseline2(low)
        padded = np.pad(low, 1, mode="edge")
        h, w = low.shape
        ids = rng.integers(0, h * w, size=per_scene)
        for index in ids:
            sy, sx = divmod(int(index), w)
            patch = padded[sy : sy + 3, sx : sx + 3].reshape(-1)
            target = high[2 * sy : 2 * sy + 2, 2 * sx : 2 * sx + 2].reshape(-1)
            baseline = base[2 * sy : 2 * sy + 2, 2 * sx : 2 * sx + 2].reshape(-1)
            patches.append(patch)
            residuals.append(target.astype(np.int16) - baseline.astype(np.int16))
            baselines.append(baseline)
            local_ranges.append(int(patch.max()) - int(patch.min()))
    return (
        np.asarray(patches, np.uint8),
        np.asarray(residuals, np.int16),
        np.asarray(baselines, np.uint8),
        np.asarray(local_ranges, np.uint8),
    )


class TinyUpscale(nn.Module):
    def __init__(self):
        super().__init__()
        self.fc1 = nn.Linear(9, 5)
        self.fc2 = nn.Linear(5, 3)

    def forward(self, x):
        return self.fc2(F.relu(self.fc1(x)))


def init_model() -> TinyUpscale:
    torch.manual_seed(SEED)
    model = TinyUpscale()
    for layer in model.modules():
        if isinstance(layer, nn.Linear):
            nn.init.kaiming_uniform_(layer.weight, a=math.sqrt(5))
            fan_in, _ = nn.init._calculate_fan_in_and_fan_out(layer.weight)
            bound = 1 / math.sqrt(fan_in)
            nn.init.uniform_(layer.bias, -bound, bound)
    return model


def signed_round_shift(values: np.ndarray, shift: int) -> np.ndarray:
    half = 1 << (shift - 1)
    pos = (values + half) >> shift
    neg = -(((-values) + half) >> shift)
    return np.where(values >= 0, pos, neg)


def quantize(model: TinyUpscale):
    w1 = np.clip(np.rint(model.fc1.weight.detach().numpy() * 64), -128, 127).astype(np.int8)
    b1 = np.rint(model.fc1.bias.detach().numpy() * 8192).astype(np.int32)
    w2 = np.clip(np.rint(model.fc2.weight.detach().numpy() * 128), -128, 127).astype(np.int8)
    b2 = np.rint(model.fc2.bias.detach().numpy() * 32768).astype(np.int32)
    return w1, b1, w2, b2


def integer_infer(patches: np.ndarray, q):
    w1, b1, w2, b2 = q
    centered = patches.astype(np.int32) - 128
    a1 = centered @ w1.astype(np.int32).T + b1
    hidden = np.maximum(0, (a1 + 16) >> 5)
    a2 = hidden @ w2.astype(np.int32).T + b2
    residual = signed_round_shift(a2, 9)
    return np.clip(residual, -127, 127).astype(np.int16)


def psnr(reference: np.ndarray, candidate: np.ndarray) -> float:
    mse = np.mean((reference.astype(np.float32) - candidate.astype(np.float32)) ** 2)
    return 99.0 if mse == 0 else float(20 * np.log10(255.0 / np.sqrt(mse)))


def c_rows(matrix: np.ndarray) -> str:
    rows = []
    for row in matrix:
        rows.append("    { " + ", ".join(f"{int(v):4d}" for v in row) + " },")
    return "\n".join(rows)


def export_include(path: Path, q, metrics):
    w1, b1, w2, b2 = q
    text = f'''/* Auto-generated by scripts/train_hollow_trail_neural_upscale.py.
 * Teacher: deterministic synthetic Hollow-Trail-style silhouettes, blurred
 * shadows, fog gradients and linework. Model: 3x3 patch -> 5 ReLU hidden ->
 * three residuals for the interpolated pixels of a 2x2 output block; the source
 * anchor is preserved exactly. Integer inference is bit-exact with
 * the trainer export. Seed {SEED}; central 2x2 edge gate {EDGE_THRESHOLD} gray levels.
 *
 * Held-out validation ({metrics["validation_patches"]:,} source pixels):
 *   baseline bilinear MAE {metrics["baseline_mae"]:.4f}, PSNR {metrics["baseline_psnr_db"]:.4f} dB
 *   quantized gated model MAE {metrics["model_mae"]:.4f}, PSNR {metrics["model_psnr_db"]:.4f} dB
 *
 * Quantization:
 *   input x = uint8 - 128
 *   W1 scale 64, B1 scale 8192, hidden Q8 via >>5
 *   W2 scale 128, B2 scale 32768, residual via signed rounded >>9
 */
#define HT_NN_EDGE_THRESHOLD {EDGE_THRESHOLD}
#define HT_NN_INPUTS 9
#define HT_NN_HIDDEN 5
#define HT_NN_OUTPUTS 3

static const int8_t ht_nn_w1[HT_NN_HIDDEN][HT_NN_INPUTS] = {{
{c_rows(w1)}
}};
static const int32_t ht_nn_b1[HT_NN_HIDDEN] = {{ {", ".join(str(int(v)) for v in b1)} }};
static const int8_t ht_nn_w2[HT_NN_OUTPUTS][HT_NN_HIDDEN] = {{
{c_rows(w2)}
}};
static const int32_t ht_nn_b2[HT_NN_OUTPUTS] = {{ {", ".join(str(int(v)) for v in b2)} }};

static inline __attribute__((always_inline)) int ht_nn_round_shift(int value,unsigned shift) {{
    int half=1<<(shift-1);
    return value>=0 ? (value+half)>>shift : -(((-value)+half)>>shift);
}}
static inline __attribute__((always_inline)) void ht_nn_residual(const uint8_t patch[HT_NN_INPUTS],int16_t out[HT_NN_OUTPUTS]) {{
    int hidden[HT_NN_HIDDEN];
    for(int h=0;h<HT_NN_HIDDEN;++h) {{
        int acc=ht_nn_b1[h];
        for(int i=0;i<HT_NN_INPUTS;++i)
            acc+=((int)patch[i]-128)*(int)ht_nn_w1[h][i];
        int q=(acc+16)>>5;
        hidden[h]=q>0?q:0;
    }}
    for(int o=0;o<HT_NN_OUTPUTS;++o) {{
        int acc=ht_nn_b2[o];
        for(int h=0;h<HT_NN_HIDDEN;++h) acc+=hidden[h]*(int)ht_nn_w2[o][h];
        int q=ht_nn_round_shift(acc,9);
        if(q<-127) q=-127; else if(q>127) q=127;
        out[o]=(int16_t)q;
    }}
}}
'''
    path.write_text(text)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--epochs", type=int, default=EPOCHS)
    parser.add_argument("--export", type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()

    torch.set_num_threads(1)
    train = make_patches(TRAIN_SCENES, TRAIN_SAMPLES_PER_SCENE, 123)
    valid = make_patches(VAL_SCENES, VAL_SAMPLES_PER_SCENE, 456)
    x, y, _, local_range = train
    xv, yv, bv, rangev = valid

    model = init_model()
    train_gate_values = x[:, [4, 5, 7, 8]]
    train_gate_range = train_gate_values.max(axis=1).astype(np.int16) - train_gate_values.min(axis=1).astype(np.int16)
    train_gate = train_gate_range >= EDGE_THRESHOLD
    x_t = torch.from_numpy((x[train_gate].astype(np.float32) - 128.0) / 128.0)
    y_t = torch.from_numpy(y[train_gate, 1:].astype(np.float32) / 64.0)
    edge_t = torch.from_numpy(train_gate_range[train_gate].astype(np.float32))
    optimizer = torch.optim.AdamW(model.parameters(), lr=2e-3, weight_decay=1e-5)

    for epoch in range(args.epochs):
        order = torch.randperm(len(x_t))
        total = 0.0
        for start in range(0, len(x_t), BATCH):
            idx = order[start : start + BATCH]
            predicted = model(x_t[idx])
            weight = (1.0 + 0.75 * torch.clamp((edge_t[idx] - EDGE_THRESHOLD) / 128.0, 0, 1)).unsqueeze(1)
            loss = (F.smooth_l1_loss(predicted, y_t[idx], reduction="none", beta=0.15) * weight).mean()
            optimizer.zero_grad()
            loss.backward()
            optimizer.step()
            total += loss.detach().item() * len(idx)
        if epoch in {0, args.epochs - 1} or (epoch + 1) % 10 == 0:
            print(f"epoch {epoch + 1:02d}/{args.epochs}: gated_weighted_smooth_l1={total / len(x_t):.6f}")

    q = quantize(model)
    residual = integer_infer(xv, q)
    reference = np.clip(bv.astype(np.int16) + yv, 0, 255).astype(np.int16)
    baseline = bv.astype(np.int16)
    candidate = baseline.copy()
    gate_values = xv[:, [4, 5, 7, 8]]
    gate_range = gate_values.max(axis=1).astype(np.int16) - gate_values.min(axis=1).astype(np.int16)
    gated = gate_range >= EDGE_THRESHOLD
    neural = baseline.copy()
    neural[:, 1:] = np.clip(baseline[:, 1:] + residual, 0, 255)
    candidate[gated] = neural[gated]

    metrics = {
        "seed": SEED,
        "architecture": "9-5-ReLU-3 anchored residual 2x2",
        "edge_threshold": EDGE_THRESHOLD,
        "generated_training_patches": int(len(x)),
        "training_patches": int(train_gate.sum()),
        "validation_patches": int(len(xv)),
        "gated_validation_fraction": float(gated.mean()),
        "baseline_mae": float(np.mean(np.abs(reference - baseline))),
        "baseline_psnr_db": psnr(reference, baseline),
        "model_mae": float(np.mean(np.abs(reference - candidate))),
        "model_psnr_db": psnr(reference, candidate),
        "weights": {
            "w1": q[0].tolist(), "b1": q[1].tolist(),
            "w2": q[2].tolist(), "b2": q[3].tolist(),
        },
    }
    print(json.dumps(metrics, indent=2))
    if args.export:
        export_include(args.export, q, metrics)
    if args.report:
        args.report.write_text(json.dumps(metrics, indent=2) + "\n")


if __name__ == "__main__":
    main()
