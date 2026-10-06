#!/usr/bin/env python3
"""Render figures from archived CSV and recomputed campaign summaries."""
import csv
import json
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

root = Path(__file__).resolve().parent
summary = json.loads((root / "offscreen/report-summary.json").read_text())
cases = ["transforms-10", "transforms-100", "materials-10", "geometry-10"]
labels = ["Transforms 10%", "Transforms 100%", "Materiais 10%", "Geometria 10%"]
fig, ax = plt.subplots(figsize=(10, 4.7), constrained_layout=True)
for offset, (campaign, variant, label, color) in enumerate([
        ("before", "coingl", "CoinGL", "#64748b"),
        ("before", "wgpu-vulkan", "wgpu anterior", "#d97706"),
        ("after", "wgpu-vulkan", "wgpu corrigido", "#0284c7")]):
    data = summary["campaigns"][campaign]
    groups = {g["case"]: g for g in data["groups"] if g["variant"] == variant}
    values = [groups[c]["stats"]["total_ms"]["median_ms"] for c in cases]
    runs = [[p["stats"]["total_ms"]["median_ms"] for p in data["processes"]
             if p["case"] == c and p["variant"] == variant] for c in cases]
    errors = [[v - min(r) for v, r in zip(values, runs)],
              [max(r) - v for v, r in zip(values, runs)]]
    x = np.arange(len(cases)) + (offset - 1) * 0.25
    ax.bar(x, values, width=0.23, label=label, color=color,
           yerr=errors, capsize=3, error_kw={"linewidth": 1})
    for xi, value in zip(x, values):
        ax.text(xi, value + 9, f"{value:.0f}", ha="center", fontsize=9)
ax.set_xticks(np.arange(len(cases)), labels)
ax.set_ylabel("Tempo total offscreen (ms)")
ax.set_title("Mediana entre três rodadas; traços mostram a faixa das medianas")
ax.set_ylim(0, 590)
ax.grid(axis="y", alpha=0.2)
ax.set_axisbelow(True)
ax.legend(frameon=False)
fig.savefig(root / "motion-before-after.png", dpi=160)
plt.close(fig)

fig, ax = plt.subplots(figsize=(10, 4.7), constrained_layout=True)
for side, variant, label, color in [
        ("before", "coingl", "CoinGL", "#64748b"),
        ("before", "wgpu-vulkan", "wgpu anterior", "#d97706"),
        ("after", "wgpu-vulkan", "wgpu corrigido", "#0284c7")]:
    path = root / "window" / side / "samples" / f"transforms-10-{variant}-1.csv"
    with path.open(newline="") as stream:
        rows = [r for r in csv.DictReader(stream) if r["warmup"] in ("0", "false")]
    x = [int(r["logical_frame"]) for r in rows]
    y = [float(r["total_ms"]) for r in rows]
    ax.plot(x, y, marker="o", markersize=3, linewidth=1.2, label=label, color=color)
ax.set_xlabel("Quadro lógico — primeira rodada, sem warmup")
ax.set_ylabel("Atualização + chamada render/present (ms)")
ax.set_title("Janela: espera alternada permanece após reduzir o trabalho CPU")
ax.set_ylim(0, 1100)
ax.grid(alpha=0.2)
ax.legend(frameon=False)
fig.savefig(root / "window-frame-times.png", dpi=160)
plt.close(fig)
