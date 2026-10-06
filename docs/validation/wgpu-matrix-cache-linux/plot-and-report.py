#!/usr/bin/env python3
"""Generate a Portuguese report and Matplotlib figure from archived JSON.

Example:
  python3 /tmp/coin-render-matrix-report.py \
    --offscreen EVIDENCE/offscreen/report-summary.json \
    --stress EVIDENCE/stress/report-summary.json \
    --analysis EVIDENCE/analysis.json --metadata EVIDENCE/stage-metadata.json \
    --output /tmp/coin-render-matrix-report

Required metadata: source_content_revision, baseline_source_content_revision,
coingl_source_content_revision. Optional flat fields match the preceding stage:
timing_valid_processes, timing_measured_frames, timing_warmup_frames,
core_cpu_test_executions, gpu_test_executions, rgb_comparisons, all_rgb_identical,
abi_cpp_rust, scene_sha256. Narrative arrays implementation_notes, scope_notes,
limitations, reviews and gates are supplied by the author; no passed gate,
source correspondence or visual-validation result is inferred from timing.

No campaign, GPU, build or Git command is invoked. Numbers come from JSON.
"""

import argparse
import hashlib
import json
import math
from pathlib import Path


LABELS = {"static": "Estático", "camera": "Câmera", "transforms-10": "Transformações 10%",
          "materials-10": "Materiais 10%", "geometry-10": "Geometria 10%",
          "transforms-100": "Transformações 100%", "geometry-100": "Geometria 100%"}
ORDER = ("transforms-10", "materials-10", "geometry-10", "static", "camera", "transforms-100", "geometry-100")
SOURCE_FIELDS = ("source_content_revision", "baseline_source_content_revision", "coingl_source_content_revision")


def read_json(path):
    data = path.read_bytes()
    return json.loads(data), {"path": str(path.resolve()), "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def case_order(case):
    return (ORDER.index(case) if case in ORDER else len(ORDER), case)


def number(value, digits=2):
    if value is None:
        return "n/d"
    if not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError(f"Expected a finite numeric JSON value, got {value!r}")
    return f"{value:.{digits}f}"


def integer(value):
    if value is None:
        return "n/d"
    if isinstance(value, (int, float)) and math.isfinite(value):
        return f"{value:,.0f}".replace(",", ".") if value == int(value) else number(value)
    raise ValueError(f"Expected a finite counter, got {value!r}")


def metric(rows, case, variant, key):
    return rows.get((case, variant), {}).get("metrics", {}).get(key)


def pair_text(value, digits=2, counters=False):
    if not value:
        return "n/d"
    formatter = integer if counters else lambda value: number(value, digits)
    return f"{formatter(value.get('before'))} → {formatter(value.get('after'))}"


def change_text(value):
    if not value or value.get("before") in (None, 0) or value.get("after") is None:
        return "n/d"
    return f"{(value['after'] / value['before'] - 1) * 100:+.2f}%"


def extract_rows(summary):
    rows = {}
    for row in summary["timing"]["comparisons"]:
        key = row["case"], row["variant"]
        if key in rows:
            raise ValueError(f"Duplicate summary case/variant: {key}")
        rows[key] = row
    return rows


def diagnostics(analysis):
    runs = []
    for directory, diagnostic in analysis.get("diagnostics", {}).items():
        raw = diagnostic["metadata_and_raw_trace"]
        for label, comparison in diagnostic["on_off_comparisons"].items():
            on = raw["runs"][comparison["on"]]
            off = raw["runs"][comparison["off"]]
            runs.append({"label": label, "directory": directory, "on": on, "off": off,
                         "comparison": comparison})
    return sorted(runs, key=lambda item: item["label"])


def diagnostic_pair(item, category, key):
    if not item["comparison"].get("complete_and_comparable", False):
        return None
    on = item["on"].get(category, {}).get(key)
    off = item["off"].get(category, {}).get(key)
    return {"before": off, "after": on} if on is not None and off is not None else None


def timing_table(rows, cases):
    lines = ["| Caso | wgpu antes → depois (ms) | Variação | Coin/OpenGL compartilhado (ms) |",
             "|---|---:|---:|---:|"]
    for case in cases:
        value = metric(rows, case, "wgpu-vulkan", "total_median_ms")
        control = metric(rows, case, "coingl", "total_median_ms")
        lines.append(f"| {LABELS.get(case, case)} | {pair_text(value)} | {change_text(value)} | {number(control.get('before')) if control else 'n/d'} |")
    return "\n".join(lines)


def resources_table(rows, cases):
    lines = ["| Caso | RSS wgpu antes → depois (MiB) | Primeiro total wgpu antes → depois (ms) | Primeiro Coin/OpenGL (ms) |",
             "|---|---:|---:|---:|"]
    for case in cases:
        control = metric(rows, case, "coingl", "first_total_ms_from_csv_median")
        lines.append(f"| {LABELS.get(case, case)} | {pair_text(metric(rows, case, 'wgpu-vulkan', 'peak_rss_median_mib'))} | "
                     f"{pair_text(metric(rows, case, 'wgpu-vulkan', 'first_total_ms_from_csv_median'))} | {number(control.get('before')) if control else 'n/d'} |")
    return "\n".join(lines)


def diagnostic_tables(items):
    timing = ["| Perfil | Pack literal → cache (ms) | Total literal → cache (ms) |",
              "|---|---:|---:|"]
    counters = ["| Perfil | Hits literal → cache | Matrizes calculadas literal → cache | Bytes literal → cache | Alocações literal → cache | Bypass literal → cache |",
                "|---|---:|---:|---:|---:|---:|"]
    for item in items:
        parts = item["label"].split("|")
        label = "wgpu/Vulkan · " + LABELS.get(parts[-1], parts[-1]) if len(parts) == 3 else item["label"].replace("|", "/")
        timing.append(f"| {label} | {pair_text(diagnostic_pair(item, 'phase_measured_medians_ms', 'bridge.pack_ms'))} | "
                      f"{pair_text(diagnostic_pair(item, 'csv_measured_medians_ms', 'total_ms'))} |")
        values = [pair_text(diagnostic_pair(item, "counter_measured_medians", "wgpu_opaque_instancing." + key), counters=True)
                  for key in ("matrix_cache_hits", "matrix_calculated", "matrix_cache_bytes", "matrix_cache_allocations", "matrix_cache_bypass")]
        counters.append("| " + label + " | " + " | ".join(values) + " |")
    return "\n".join(timing), "\n".join(counters)


def metadata_notes(metadata, name):
    value = metadata.get(name, [])
    if isinstance(value, str):
        return [value]
    return [json.dumps(item, ensure_ascii=False, sort_keys=True) if isinstance(item, dict) else str(item) for item in value]


def derive_counts(analysis):
    timing = {key: sum(campaign.get("unique_counts", {}).get(key, 0) for campaign in analysis.get("campaigns", {}).values())
              for key in ("processes", "measured_frames", "warmup_frames")}
    diagnostic = {"processes": 0, "measured_frames": 0, "warmup_frames": 0}
    for entry in analysis.get("diagnostics", {}).values():
        for run in entry["metadata_and_raw_trace"]["runs"].values():
            samples = run["samples"]
            if samples["selection_valid"]:
                diagnostic["processes"] += 1
                diagnostic["measured_frames"] += len(samples["measured_row_indices"])
                diagnostic["warmup_frames"] += len(samples["warmup_row_indices"])
    return timing, diagnostic


def validate_inputs(metadata, analysis, summaries):
    for key in SOURCE_FIELDS:
        if not metadata.get(key):
            raise ValueError(f"Required stage metadata missing: {key}")
    timing, diagnostic = derive_counts(analysis)
    for key, actual in (("timing_valid_processes", timing["processes"]),
                        ("timing_measured_frames", timing["measured_frames"]),
                        ("timing_warmup_frames", timing["warmup_frames"]),
                        ("diagnostic_processes", diagnostic["processes"])):
        if key in metadata and metadata[key] != actual:
            raise ValueError(f"Metadata {key}={metadata[key]} disagrees with analysis={actual}")
    for campaign in analysis.get("campaigns", {}).values():
        for role, key in (("before", "baseline_source_content_revision"), ("after", "source_content_revision")):
            source = campaign[role]["metadata"].get("source_content_revision")
            if source and source != metadata[key]:
                raise ValueError(f"{role} source differs from stage metadata: {source} / {metadata[key]}")
    for summary in summaries:
        for row in summary["timing"]["comparisons"]:
            if row["variant"] == "coingl":
                for name, value in row["metrics"].items():
                    if value.get("before") != value.get("after"):
                        raise ValueError(f"Shared CoinGL control differs in {row['case']}/{name}")


def protocol_lines(analysis):
    lines = []
    for name, campaign in analysis.get("campaigns", {}).items():
        parameters = campaign["before"]["metadata"].get("parameters", {})
        counts = campaign["unique_counts"]
        lines.append(f"- **{name}:** casos `{parameters.get('cases', 'não informado')}`, {parameters.get('rounds', 'n/d')} rodadas, "
                     f"{parameters.get('warmup', 'n/d')} warmups e {parameters.get('frames', 'n/d')} quadros medidos por processo; "
                     f"{integer(counts['processes'])} processos únicos, {integer(counts['measured_frames'])} quadros medidos, "
                     f"{integer(counts['warmup_frames'])} warmups.")
    return lines


def write_report(metadata, main, stress, analysis, figure_reference):
    main_rows, stress_rows = extract_rows(main), extract_rows(stress)
    rows = dict(main_rows)
    for key, value in stress_rows.items():
        if key in rows:
            raise ValueError(f"Main/stress summary overlap: {key}")
        rows[key] = value
    main_cases = sorted({case for case, variant in main_rows if variant == "wgpu-vulkan"}, key=case_order)
    stress_cases = sorted({case for case, variant in stress_rows if variant == "wgpu-vulkan"}, key=case_order)
    all_cases = main_cases + stress_cases
    timing_counts, diagnostic_counts = derive_counts(analysis)
    items = diagnostics(analysis)
    diagnostic_timing, diagnostic_counters = diagnostic_tables(items)
    title = metadata.get("title", "CoinRender: cache de matrizes wgpu — Linux")
    if metadata.get("date"):
        title += ", " + metadata["date"]
    lines = [f"# {title}", "", "Referência de render: Coin3D/Coin/OpenGL clássico (`SoGLRenderAction`).", "",
             "## Mudança e escopo", ""]
    notes = metadata_notes(metadata, "implementation_notes")
    for note in notes or ["Cache específico de matrizes no pack wgpu. As fontes, os limites de admissão e a revisão estão registrados nos metadados desta etapa."]:
        lines.extend([note, ""])
    lines.extend(["", "Optout: `COIN_WGPU_DISABLE_INSTANCE_MATRIX_CACHE=1`.", ""])
    for note in metadata_notes(metadata, "scope_notes"):
        lines.extend([note, ""])
    lines.extend(["", "## Tempos totais observados", "",
                  "Cada valor é a mediana das medianas por processo. O total é tempo de parede de update + render/readback + publication. "
                  "Diferenças positivas indicam aumento do tempo. A alternância reduz a dependência da ordem; não prova igualdade de clocks, temperatura ou estado do driver.", "",
                  timing_table(rows, main_cases), "", "### Stress", "", timing_table(rows, stress_cases), "",
                  f"![Tempos, recursos e primeiro quadro]({figure_reference})", "", "### Recursos e primeiro quadro", "",
                  resources_table(rows, all_cases), "",
                  "RSS é o pico do processo em MiB, não memória GPU. O primeiro total vem da primeira linha CSV, durante warmup, "
                  "incluindo update; é mostrado separadamente dos quadros medidos e não inclui toda a inicialização do aplicativo.", "",
                  "### Variações registradas", ""])
    for case in all_cases:
        value = metric(rows, case, "wgpu-vulkan", "total_median_ms")
        if value:
            delta = value["after"] - value["before"]
            lines.append(f"- {LABELS.get(case, case)}: {pair_text(value)} ms; {delta:+.2f} ms ({change_text(value)}).")
    lines.extend(["", "Os deltas descrevem a amostra desta campanha. A ablação abaixo isola o custo local do pack com o mesmo binário; "
                  "ela não atribui automaticamente toda variação do quadro completo ao cache.", "", "## Ablação e contadores", "",
                  f"Diagnóstico separado: {integer(diagnostic_counts['processes'])} processos, {integer(diagnostic_counts['measured_frames'])} "
                  f"quadros medidos e {integer(diagnostic_counts['warmup_frames'])} warmups. As medianas usam somente os índices CSV marcados como medidos, "
                  "e somente séries com a mesma cardinalidade do CSV. Intervalos de fases podem ser aninhados e não devem ser somados.", "",
                  diagnostic_timing, "", diagnostic_counters, "",
                  "Contadores são medianas por quadro medido quando há correspondência de contagem. `n/d` preserva ausência de campo, "
                  "divergência de cardinalidade ou protocolo não comparável; o trace bruto permanece em `analysis.json`. "
                  "Bytes representam a contabilização privada do cache, não a memória total do processo; bypass é o código emitido pelo trace.", "",
                  "## Protocolo, fontes e validação", "",
                  f"Código atual: `{metadata['source_content_revision']}`. Antes wgpu: `{metadata['baseline_source_content_revision']}`. "
                  f"Coin/OpenGL: `{metadata['coingl_source_content_revision']}`.", ""])
    if metadata.get("scene_sha256"):
        lines.extend([f"Cena SHA-256: `{metadata['scene_sha256']}`.", ""])
    lines.extend(protocol_lines(analysis))
    lines.extend([f"- Total das campanhas: **{integer(timing_counts['processes'])} processos únicos, "
                  f"{integer(timing_counts['measured_frames'])} quadros medidos e {integer(timing_counts['warmup_frames'])} warmups**.",
                  "- Coin/OpenGL usa uma execução compartilhada por caso/rodada. Suas cópias são deduplicadas mediante metadados e SHA-256 de CSV/log.",
                  "- Warmups são excluídos pela coluna CSV; percentis por processo usam nearest rank. As amostras curtas não caracterizam caudas de latência.", ""])
    for field, label in (("core_cpu_test_executions", "Execuções CPU/Core"), ("gpu_test_executions", "Execuções GPU"),
                         ("rgb_comparisons", "Comparações RGB"), ("abi_cpp_rust", "ABI C++/Rust")):
        if field in metadata:
            lines.append(f"- {label}: {integer(metadata[field])} (metadados da etapa).")
    if "all_rgb_identical" in metadata:
        lines.append(f"- Todas as comparações RGB idênticas por bytes: {'sim' if metadata['all_rgb_identical'] else 'não'} (metadados da etapa).")
    lines.extend(["", "### Gates e revisão", ""])
    lines.extend("- " + item for item in metadata_notes(metadata, "gates"))
    lines.extend("- " + item for item in metadata_notes(metadata, "reviews"))
    if not metadata.get("gates") and not metadata.get("reviews"):
        lines.append("Detalhes individuais dos gates e da revisão não informados nos metadados fornecidos.")
    limits = metadata_notes(metadata, "limitations")
    issues = []
    for name, campaign in analysis.get("campaigns", {}).items():
        issues.extend(f"{name}: {item}" for item in campaign.get("comparability_issues", []))
    for directory, diagnostic in analysis.get("diagnostics", {}).items():
        issues.extend(f"{directory}: {item}" for item in diagnostic.get("pairing_issues", []))
        for label, comparison in diagnostic.get("on_off_comparisons", {}).items():
            issues.extend(f"{label}: {item}" for item in comparison.get("comparability_issues", []))
    lines.extend(["", "## Limites da evidência", "",
                  "Esta campanha é offscreen. Tempo total inclui espera GPU/readback; não mede duração GPU isolada, latência de exibição ou fluidez em janela.", ""])
    lines.extend("- " + item for item in limits)
    lines.extend("- " + item for item in issues)
    if metadata.get("evidence_reference"):
        lines.extend(["", f"[Evidência e reprodução]({metadata['evidence_reference']})."])
    lines.extend(["", "Os JSONs de entrada e os hashes do gerador são registrados em `report-inputs.json`. Nenhum número de desempenho é embutido no gerador.", ""])
    return "\n".join(lines), rows, main_cases, stress_cases


def create_figure(rows, main_cases, stress_cases, output, metadata):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.patches import Patch

    colors = {"before": "#8796a9", "after": "#2768a6", "control": "#555c65"}
    fig, axes = plt.subplots(2, 2, figsize=(15, 10.5))

    def panel(ax, cases, key, title, units, include_control=True):
        maximum = 0
        roles = (("before", -.24), ("after", 0), ("control", .24)) if include_control else (("before", -.16), ("after", .16))
        for index, case in enumerate(cases):
            for role, offset in roles:
                value = metric(rows, case, "coingl" if role == "control" else "wgpu-vulkan", key)
                if not value:
                    continue
                amount = value.get("before" if role == "control" else role)
                if amount is None:
                    continue
                ax.barh(index + offset, amount, height=.20 if include_control else .28, color=colors[role])
                ax.annotate(f"{amount:.1f}", (amount, index + offset), xytext=(4, 0), textcoords="offset points", va="center", fontsize=8)
                maximum = max(maximum, amount)
        ax.set_yticks(range(len(cases)))
        ax.set_yticklabels([LABELS.get(case, case) for case in cases])
        ax.invert_yaxis(); ax.set_xlim(0, maximum * 1.20 if maximum else 1)
        ax.set_title(title); ax.set_xlabel(units)
        ax.grid(axis="x", alpha=.2); ax.set_axisbelow(True)
        for spine in ("top", "right"):
            ax.spines[spine].set_visible(False)

    panel(axes[0, 0], main_cases, "total_median_ms", "Principal · total por quadro", "ms · menor é melhor")
    panel(axes[0, 1], stress_cases, "total_median_ms", "Stress · total por quadro", "ms · menor é melhor")
    panel(axes[1, 0], main_cases + stress_cases, "peak_rss_median_mib", "Pico RSS do processo", "MiB")
    first_cases = ["static"] if "static" in main_cases else main_cases[:1]
    panel(axes[1, 1], first_cases, "first_total_ms_from_csv_median", "Primeiro quadro · total durante warmup", "ms · inclui update")
    fig.suptitle(metadata.get("plot_title", "CoinRender · cache de matrizes wgpu · offscreen"), fontsize=15, y=.985)
    fig.legend(handles=[Patch(color=colors["before"], label="wgpu antes"), Patch(color=colors["after"], label="wgpu depois (observado)"),
                        Patch(color=colors["control"], label="Coin/OpenGL compartilhado")], loc="upper center", bbox_to_anchor=(.5, .957), ncol=3, frameon=False)
    fig.subplots_adjust(top=.88, bottom=.065, left=.16, right=.97, hspace=.35, wspace=.49)
    fig.savefig(output / "wgpu-matrix-cache.png", dpi=150)
    fig.savefig(output / "wgpu-matrix-cache.svg")
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    for name in ("offscreen", "stress", "analysis", "metadata"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True, help="New output directory for Markdown, PNG, SVG and input provenance")
    parser.add_argument("--figure-reference", default="validation/wgpu-matrix-cache-linux/wgpu-matrix-cache.png",
                        help="Figure path embedded in the Markdown intended for docs/")
    args = parser.parse_args()
    main_summary, main_input = read_json(args.offscreen)
    stress_summary, stress_input = read_json(args.stress)
    analysis, analysis_input = read_json(args.analysis)
    metadata, metadata_input = read_json(args.metadata)
    validate_inputs(metadata, analysis, [main_summary, stress_summary])
    report, rows, main_cases, stress_cases = write_report(metadata, main_summary, stress_summary, analysis, args.figure_reference)
    output = args.output.resolve()
    if output.exists():
        parser.error("Output must be a new directory; preserve prior evidence")
    output.mkdir(parents=True)
    create_figure(rows, main_cases, stress_cases, output, metadata)
    report_path = output / "coin-render-wgpu-matrix-cache-linux.md"
    report_path.write_text(report, encoding="utf-8")
    script = Path(__file__).resolve()
    provenance = {"inputs": {"offscreen": main_input, "stress": stress_input, "analysis": analysis_input, "metadata": metadata_input},
                  "generator": {"path": str(script), "sha256": hashlib.sha256(script.read_bytes()).hexdigest()},
                  "outputs": {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in output.iterdir() if path.is_file()}}
    (output / "report-inputs.json").write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {report_path} and Matplotlib PNG/SVG; numbers read from archived JSON")


if __name__ == "__main__":
    main()
