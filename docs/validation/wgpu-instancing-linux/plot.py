#!/usr/bin/env python3
"""Plot the motion summary's six cases, using the three process medians.

Example:
  python3 /tmp/coin-render-instancing-plot.py \
    --summary /path/to/report-summary.json --output /path/to/figures

Reads the format emitted by coin-render-wgpu-motion-summary.py/campaign_core.
No CSV, benchmark, GPU or build command is invoked. CoinGL is the shared control
copied into the before and after campaigns; its round medians must agree.
"""

import argparse
import hashlib
import json
import math
from pathlib import Path
import statistics


CASES = ("transforms-10", "transforms-100", "materials-10", "geometry-10", "static", "camera")
LABELS = {
    "transforms-10": "Transformações\n10% dos objetos",
    "transforms-100": "Transformações\n100% dos objetos",
    "materials-10": "Materiais\n10% dos objetos",
    "geometry-10": "Geometria\n10% dos objetos",
    "static": "Cena estática",
    "camera": "Câmera em movimento",
}
SERIES = (
    ("before", "wgpu-vulkan", "wgpu antes", "#79848c"),
    ("after", "wgpu-vulkan", "wgpu depois", "#287bb8"),
    ("after", "coingl", "CoinGL", "#ce811f"),
)
METRICS = {
    "total_ms": ("Tempo total por quadro", "Atualização + renderização + publicação"),
    "render_ms": ("Tempo de renderização por quadro", "Renderização + leitura de pixels; exclui atualização e publicação"),
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def process_values(dataset, case, variant, metric):
    rows = sorted((p for p in dataset["processes"]
                   if p["case"] == case and p["variant"] == variant), key=lambda p: int(p["round"]))
    require([int(p["round"]) for p in rows] == [1, 2, 3],
            f"Expected exactly rounds 1,2,3 for {dataset['name']}/{case}/{variant}")
    values = [float(p["stats"][metric]["median_ms"]) for p in rows]
    require(all(math.isfinite(v) and v >= 0 for v in values),
            f"Invalid round medians for {dataset['name']}/{case}/{variant}/{metric}")
    groups = [g for g in dataset["groups"] if g["case"] == case and g["variant"] == variant]
    require(len(groups) == 1, f"Missing or duplicate aggregate: {dataset['name']}/{case}/{variant}")
    recorded = float(groups[0]["stats"][metric]["median_ms"])
    require(math.isclose(statistics.median(values), recorded, rel_tol=1e-12, abs_tol=1e-9),
            f"Process medians disagree with summary aggregate: {case}/{variant}/{metric}")
    return values


def plot_data(summary):
    campaigns = summary["campaigns"]
    require("before" in campaigns and "after" in campaigns, "Summary must contain campaigns.before and campaigns.after")
    old, new = campaigns["before"], campaigns["after"]
    for dataset in (old, new):
        require(dataset["manifest"]["parameters"].get("mode", "measure") == "measure",
                "Use measurement campaigns, not the sparse image verification runs")
    for key in ("scope", "gpu", "size", "warmup", "frames", "rounds"):
        require(old["manifest"]["parameters"].get(key) == new["manifest"]["parameters"].get(key),
                f"Before/after protocol differs: {key}")
    require(old["manifest"]["parameters"]["scope"] == "offscreen",
            "This figure's render/readback labels require the offscreen campaign")
    require(old["manifest"]["scene_sha256"] == new["manifest"]["scene_sha256"], "Before/after scenes differ")
    data = {}
    for metric in METRICS:
        rows = []
        for case in CASES:
            a = process_values(old, case, "coingl", metric)
            b = process_values(new, case, "coingl", metric)
            require(a == b, f"CoinGL control was not shared across campaigns: {case}/{metric}")
            for campaign, variant, label, color in SERIES:
                values = process_values(campaigns[campaign], case, variant, metric)
                rows.append({"case": case, "campaign": campaign, "variant": variant,
                             "label": label, "color": color, "round_medians_ms": values,
                             "median_ms": statistics.median(values), "minimum_ms": min(values),
                             "maximum_ms": max(values)})
        data[metric] = rows
    return data


def figure(plt, ticker, rows, metric, destination):
    plt.rcParams.update({"font.family": "DejaVu Sans", "font.size": 11,
                         "axes.spines.top": False, "axes.spines.right": False,
                         "svg.fonttype": "none", "savefig.facecolor": "white"})
    fig, axes = plt.subplots(1, 2, figsize=(14, 6.1), gridspec_kw={"width_ratios": [2, 1.2]})
    fig.subplots_adjust(left=.068, right=.985, top=.75, bottom=.23, wspace=.28)
    panels = ((CASES[:4], "Objetos alterados"), (CASES[4:], "Cena estática e câmera"))
    width = .23
    for ax, (cases, title) in zip(axes, panels):
        ax.set_axisbelow(True)
        ax.grid(axis="y", color="#dde3e8", linewidth=.8)
        ax.set_title(title, loc="left", fontweight="bold", pad=12)
        upper = max(r["maximum_ms"] for r in rows if r["case"] in cases)
        for series_index, (campaign, variant, label, color) in enumerate(SERIES):
            group = [next(r for r in rows if r["case"] == case and r["campaign"] == campaign
                          and r["variant"] == variant) for case in cases]
            positions = [i + (series_index - 1) * width for i in range(len(cases))]
            medians = [r["median_ms"] for r in group]
            errors = [[r["median_ms"] - r["minimum_ms"] for r in group],
                      [r["maximum_ms"] - r["median_ms"] for r in group]]
            ax.bar(positions, medians, width=width * .91, color=color, label=label,
                   yerr=errors, capsize=4, error_kw={"elinewidth": 1.25, "ecolor": "#29313b"})
            for x, row in zip(positions, group):
                ax.plot([x-.035, x, x+.035], row["round_medians_ms"], linestyle="none",
                        marker="o", markersize=3.3, markerfacecolor="white",
                        markeredgecolor="#29313b", markeredgewidth=.6, zorder=4)
                text = f"{row['median_ms']:.1f}".replace(".", ",")
                ax.annotate(text, (x, row["maximum_ms"]), xytext=(0, 7), textcoords="offset points",
                            ha="center", va="bottom", fontsize=9, color="#29313b")
        ax.set_xticks(range(len(cases)), [LABELS[case] for case in cases])
        ax.tick_params(axis="x", length=0, pad=12)
        ax.set_ylabel("Tempo por quadro (ms)")
        ax.set_ylim(0, upper * 1.23 if upper else 1)
        ax.yaxis.set_major_formatter(ticker.FuncFormatter(lambda v, _: f"{v:g}".replace(".", ",")))
    title, subtitle = METRICS[metric]
    fig.suptitle(title, x=.068, y=.96, ha="left", fontsize=19, fontweight="bold")
    fig.text(.068, .893, subtitle, fontsize=11, color="#56616c")
    handles, labels = axes[0].get_legend_handles_labels()
    fig.legend(handles, labels, loc="upper left", bbox_to_anchor=(.058, .862), ncol=3,
               frameon=False, fontsize=11)
    fig.text(.068, .08, "Barras: mediana das três rodadas. Hastes: mínimo–máximo das medianas por rodada.",
             fontsize=10, color="#56616c")
    fig.text(.068, .045, "Pontos: cada rodada. Os dois painéis usam escalas independentes. CoinGL: controle compartilhado.",
             fontsize=10, color="#56616c")
    stem = "mediana-tempo-total-ms" if metric == "total_ms" else "mediana-renderizacao-ms"
    files = []
    for extension in ("png", "svg", "pdf"):
        target = destination / (stem + "." + extension)
        fig.savefig(target, dpi=180, metadata={"Creator": "CoinRender instancing benchmark report"}
                    if extension == "pdf" else None)
        files.append(target.name)
    plt.close(fig)
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--summary", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path, help="Directory for PNG, SVG, PDF and plotted values")
    args = parser.parse_args()
    summary_path = args.summary.resolve()
    raw = summary_path.read_bytes()
    summary = json.loads(raw)
    data = plot_data(summary)
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib import ticker
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    files = [name for metric, rows in data.items() for name in figure(plt, ticker, rows, metric, output)]
    (output / "valores-das-figuras.json").write_text(json.dumps({
        "summary": str(summary_path), "summary_sha256": hashlib.sha256(raw).hexdigest(),
        "statistic": "median of three process medians; error bars are min/max of those medians, not confidence intervals",
        "CoinGL": "shared identical per-round control, taken from after; not counted twice",
        "cases": list(CASES), "panels_use_independent_scales": True,
        "metrics": data, "figures": files,
    }, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps({"output": str(output), "figures": files, "metrics": list(data)}, ensure_ascii=False))


if __name__ == "__main__":
    main()
