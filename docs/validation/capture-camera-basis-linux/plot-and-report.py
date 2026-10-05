#!/usr/bin/env python3
"""Generate the capture-camera-basis report and scientific PNG/SVG from JSON.

Reads evidence only: no benchmark, GPU, build, network or Git command.

Usage:
  python3 /tmp/coin-render-capture-report.py --write-config-template /tmp/config.json
  python3 /tmp/coin-render-capture-report.py \
    --analysis /tmp/coin-render-capture-analysis/results.json \
    --cold /tmp/coin-render-capture-cold-summary/summary.json \
    --steady /tmp/coin-render-capture-steady-summary/summary.json \
    --metadata /tmp/config.json --output /tmp/coin-render-capture-report

The analysis schema is coin-render-capture-analysis.py: campaigns cold/steady
and diagnostics with group_summaries/on_off_comparisons. Summary JSON uses the
coin-render-wgpu-motion-summary.py schema. Source revisions and gate results
come from the supplied metadata, never inferred from timing. Missing or
incomparable data are rejected rather than replaced with planned results.
"""

import argparse
import hashlib
import json
import math
from pathlib import Path


VARIANTS = ("coingl", "bgfx-vulkan", "bgfx-opengl", "wgpu-vulkan")
RENDER_VARIANTS = VARIANTS[1:]
VARIANT_LABELS = {"coingl": "Coin/OpenGL", "bgfx-vulkan": "BGFX/Vulkan",
                  "bgfx-opengl": "BGFX/OpenGL", "wgpu-vulkan": "wgpu/Vulkan"}
CASE_LABELS = {"static": "Estático", "camera": "Câmera", "transforms-10": "Transformações 10%",
               "materials-10": "Materiais 10%", "geometry-10": "Geometria 10%"}
CASE_ORDER = ("static", "camera", "transforms-10", "materials-10", "geometry-10")
COUNTS = ("processes", "measured_frames", "warmup_frames")
REPORT_NAME = "coin-render-capture-camera-basis-linux.md"
FIGURE_NAME = "capture-camera-basis"
OPTOUT = "COIN_RENDER_DISABLE_CAPTURE_CAMERA_BASIS_REUSE"


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read_json(path):
    data = path.read_bytes()
    value = json.loads(data, parse_constant=lambda value: (_ for _ in ()).throw(ValueError("Nonfinite JSON: " + value)))
    return value, {"path": str(path.resolve()), "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def finite(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def number(value, digits=2):
    if value is None:
        return "n/d"
    require(finite(value), f"Expected finite number: {value!r}")
    return f"{value:.{digits}f}"


def integer(value):
    if value is None:
        return "n/d"
    require(finite(value) and value == int(value), f"Expected integer counter: {value!r}")
    return f"{value:,.0f}".replace(",", ".")


def escape(value):
    return str(value).replace("|", "\\|").replace("\n", " ")


def change(pair):
    if not pair or pair.get("before") in (None, 0) or pair.get("after") is None:
        return None
    return (pair["after"] / pair["before"] - 1) * 100


def percent(pair):
    value = change(pair)
    return "n/d" if value is None else f"{value:+.2f}%"


def pair_text(pair, digits=2, counter=False):
    if not pair:
        return "n/d"
    formatter = integer if counter else lambda value: number(value, digits)
    return f"{formatter(pair.get('before'))} → {formatter(pair.get('after'))}"


def delta_text(pair, digits=2):
    if not pair or pair.get("before") is None or pair.get("after") is None:
        return "n/d"
    return f"{pair['after'] - pair['before']:+.{digits}f}"


def scaled_pair(pair, divisor):
    return {key: pair.get(key) / divisor if pair.get(key) is not None else None
            for key in ("before", "after")} if pair else None


def statistic(row, metric="total_ms", name="median_ms"):
    return row.get("stats", {}).get(metric, {}).get(name)


def ordered_cases(campaign):
    cases = {row["case"] for row in campaign["comparisons"].values()}
    return sorted(cases, key=lambda item: (CASE_ORDER.index(item) if item in CASE_ORDER else len(CASE_ORDER), item))


def row_at(campaign, case, variant):
    return campaign["comparisons"].get(f"{case}|{variant}")


def process_total_range(campaign, side, case, variant):
    values = [process["recomputed_stats"]["total_ms"]["median_ms"]
              for process in campaign[side]["processes"].values()
              if process["case"] == case and process["variant"] == variant]
    expected = row_at(campaign, case, variant)[side + "_processes"]
    require(len(values) == expected and all(finite(value) for value in values),
            f"Missing/nonfinite process totals for {side}/{case}/{variant}")
    return min(values), max(values)


def notes(metadata, key):
    value = metadata.get(key, [])
    if isinstance(value, str):
        return [value]
    require(isinstance(value, list), f"Metadata {key} must be a string or list")
    return [json.dumps(item, ensure_ascii=False, sort_keys=True) if isinstance(item, dict) else str(item) for item in value]


def paragraphs(lines, values):
    for value in values:
        lines.extend([value, ""])


def diagnostic_groups(analysis):
    groups = []
    for directory, diagnostic in analysis.get("diagnostics", {}).items():
        for label, comparison in diagnostic.get("on_off_comparisons", {}).items():
            variant, separator, case = label.partition("|")
            require(separator and variant in RENDER_VARIANTS, f"Unexpected diagnostic group: {label}")
            modes = diagnostic.get("group_summaries", {}).get(label, {})
            groups.append({"variant": variant, "case": case, "label": label, "directory": directory,
                           "comparison": comparison, "on": modes.get("on", {}), "off": modes.get("off", {})})
    return sorted(groups, key=lambda group: (VARIANTS.index(group["variant"]), group["case"]))


def diagnostic_pair(group, category, key):
    if not group["comparison"].get("complete_and_comparable", False):
        return None
    return group["comparison"].get(category, {}).get(key)


def summary_rows(summary):
    rows = {}
    for row in summary["timing"]["comparisons"]:
        key = row["case"], row["variant"]
        require(key not in rows, f"Duplicate summary row: {key}")
        rows[key] = row
    return rows


def rgb_evidence(cold_summary, steady_summary):
    records = [summary["rgb_before_after"] for summary in (cold_summary, steady_summary)
               if summary.get("rgb_before_after") is not None]
    require(records, "RGB result absent; supply a summary containing rgb_before_after")
    # Both report summaries may contain the same verification campaign.
    seen = set()
    unique = []
    for record in records:
        key = hashlib.sha256(json.dumps(record, sort_keys=True, allow_nan=False).encode()).hexdigest()
        if key not in seen:
            seen.add(key)
            unique.append(record)
    results = [item for record in unique for item in record.get("results", [])]
    count = sum(record.get("comparison_count", 0) for record in unique)
    require(len(results) == count, "RGB declared count differs from individual comparisons")
    identities = [(item["case"], item["variant"], item.get("round"), item["logical_frame"]) for item in results]
    require(len(identities) == len(set(identities)), "Overlapping RGB summaries must refer to exactly the same record or disjoint comparisons")
    exact = all(record.get("all_rgb_identical") is True for record in unique)
    if exact:
        require(all(item.get("pixels_different") == 0 and item.get("max_channel_error") == 0 and item.get("rgb_mae") == 0
                    and item["before"].get("ppm_sha256") == item["after"].get("ppm_sha256") for item in results),
                "RGB identical claim disagrees with metrics or PPM hashes")
    return {"comparisons": count, "ppm_files": count * 2, "identical": exact,
            "recomputed": all(record.get("rgb_metrics_recomputed_this_invocation") is True for record in unique)}


def validate(metadata, analysis, cold_summary, steady_summary):
    for key in ("source_content_revision", "coingl_source_content_revision", "baseline_variant_source_content_revisions"):
        require(metadata.get(key), "Required metadata missing: " + key)
    baseline_sources = metadata["baseline_variant_source_content_revisions"]
    require(isinstance(baseline_sources, dict) and all(baseline_sources.get(variant) for variant in VARIANTS),
            "Supply complete baseline_variant_source_content_revisions, including CoinGL")
    require(baseline_sources["coingl"] == metadata["coingl_source_content_revision"], "Baseline CoinGL source differs from control")
    if metadata.get("baseline_source_content_revision"):
        require(all(baseline_sources[variant] == metadata["baseline_source_content_revision"] for variant in RENDER_VARIANTS),
                "A global baseline revision cannot represent mixed variant sources; use null with the per-variant map")
    require(set(analysis.get("campaigns", {})) == {"cold", "steady"}, "Analysis must contain exactly cold and steady campaigns")
    for name, summary in (("cold", cold_summary), ("steady", steady_summary)):
        campaign = analysis["campaigns"][name]
        require(campaign.get("complete_and_comparable") is True, f"Incomparable {name} campaign: {campaign.get('comparability_issues')}")
        expected_counts = metadata.get("expected_counts", {}).get(name)
        if expected_counts:
            require(campaign["unique_counts"] == expected_counts, f"{name} counts differ: {campaign['unique_counts']} / {expected_counts}")
        cases = ordered_cases(campaign)
        require(all(row_at(campaign, case, variant) is not None for case in cases for variant in VARIANTS), f"Missing four-variant coverage in {name}")
        other = summary_rows(summary)
        require(set(other) == {(row["case"], row["variant"]) for row in campaign["comparisons"].values()}, f"{name} summary coverage differs from analysis")
        for row in campaign["comparisons"].values():
            key = row["case"], row["variant"]
            require(row.get("rounds_match") is True and not row.get("limitations"), f"{name} incomplete row: {key}")
            require(other[key].get("same_recorded_protocol") is True, f"{name} summary protocol differs: {key}")
            for side in ("before", "after"):
                a = statistic(row)[side]
                b = other[key]["metrics"]["total_median_ms"][side]
                require(finite(a) and finite(b) and math.isclose(a, b, rel_tol=1e-10, abs_tol=1e-10), f"{name} total differs between independent summaries: {key}/{side}")
            if row["variant"] == "coingl":
                require(all(pair["before"] == pair["after"] for stats in row["stats"].values() for pair in stats.values()), f"Shared CoinGL control differs: {name}/{key}")
                for field in ("first_ms", "first_total_ms", "result_since_main_ms", "peak_rss_kib", "throughput_fps"):
                    if field in row:
                        require(row[field]["before"] == row[field]["after"], f"Shared CoinGL {field} differs: {name}/{key}")
                require(all(pair["before"] == pair["after"] for pair in other[key]["metrics"].values()),
                        f"Shared CoinGL summary metrics differ: {name}/{key}")
        require(all(item.get("counted_once") is True for item in campaign["shared_controls"]), f"CoinGL shared hashes not proven for {name}")
        for side in ("before", "after"):
            manifest = campaign[side]["metadata"]
            revisions = manifest.get("variant_source_content_revisions", {})
            for variant in RENDER_VARIANTS:
                expected_source = baseline_sources[variant] if side == "before" else metadata["source_content_revision"]
                require(revisions.get(variant) == expected_source, f"{name}/{side}/{variant} source differs from metadata")
            require(revisions.get("coingl") == metadata["coingl_source_content_revision"], f"{name}/{side} CoinGL source differs")
            if metadata.get("scene_sha256"):
                require(manifest.get("scene_sha256") == metadata["scene_sha256"], f"{name}/{side} scene hash differs")
    require(ordered_cases(analysis["campaigns"]["cold"]) == ["static"], "Cold campaign must contain static only")
    groups = diagnostic_groups(analysis)
    require({(item["variant"], item["case"]) for item in groups} == {(variant, "static") for variant in RENDER_VARIANTS}, "Diagnostic must cover each rendering API once")
    diagnostic_counts = {key: sum(value["unique_counts"][key] for value in analysis["diagnostics"].values()) for key in COUNTS}
    expected = metadata.get("expected_counts", {}).get("ablation")
    if expected:
        require(diagnostic_counts == expected, f"Ablation counts differ: {diagnostic_counts} / {expected}")
    for value in analysis["diagnostics"].values():
        require(value.get("complete_and_comparable") is True, f"Incomparable diagnostic: {value.get('pairing_issues')}")
    for group in groups:
        for mode in ("on", "off"):
            require(group[mode].get("processes") == metadata.get("ablation_rounds", 3), f"Ablation N differs: {group['label']}/{mode}")
        for key, before, after in (("calls", 1, 1), ("prepares", 2, 1), ("reuse", 0, 1)):
            pair = diagnostic_pair(group, "counter_measured_medians", "capture_camera_basis." + key)
            require(pair and pair["before"] == before and pair["after"] == after, f"Ablation counter proof differs: {group['label']}/{key}")
        require(diagnostic_pair(group, "phase_measured_medians_ms", "capture_camera_basis.prepare_ms"), "Missing prepare_ms in ablation: " + group["label"])
    rgb = rgb_evidence(cold_summary, steady_summary)
    for field, actual in (("rgb_comparisons", rgb["comparisons"]), ("verification_ppm_files", rgb["ppm_files"]), ("all_rgb_identical", rgb["identical"])):
        if field in metadata:
            require(metadata[field] == actual, f"Metadata {field} differs from RGB evidence")
    gate_counts = metadata.get("gate_counts", {})
    require(gate_counts and all(isinstance(value, int) and value >= 0 for value in gate_counts.values()), "Supply explicit gate_counts; passing gates are not inferred")
    return groups, diagnostic_counts, rgb


def cold_tables(campaign):
    timings = ["| Variante | Total antes → depois (ms) | Δ total (ms) | Variação | Render antes → depois (ms) | N antes/depois |",
               "|---|---:|---:|---:|---:|---:|"]
    resources = ["| Variante | Main → primeira imagem antes → depois (ms) | Pico RSS antes → depois (MiB) |",
                 "|---|---:|---:|"]
    ranges = ["| Variante | Intervalo dos totais por processo antes (ms) | Intervalo depois (ms) |",
              "|---|---:|---:|"]
    for variant in VARIANTS:
        row = row_at(campaign, "static", variant)
        total = statistic(row)
        timings.append(f"| {VARIANT_LABELS[variant]} | {pair_text(total)} | {delta_text(total)} | {percent(total)} | "
                       f"{pair_text(statistic(row, 'render_ms'))} | {integer(row['before_processes'])}/{integer(row['after_processes'])} |")
        resources.append(f"| {VARIANT_LABELS[variant]} | {pair_text(row.get('result_since_main_ms'))} | {pair_text(scaled_pair(row.get('peak_rss_kib'), 1024))} |")
        before_range = process_total_range(campaign, "before", "static", variant)
        after_range = process_total_range(campaign, "after", "static", variant)
        ranges.append(f"| {VARIANT_LABELS[variant]} | {number(before_range[0])} .. {number(before_range[1])} | {number(after_range[0])} .. {number(after_range[1])} |")
    return "\n".join(timings), "\n".join(resources), "\n".join(ranges)


def steady_table(campaign):
    lines = ["| Caso | Coin/OpenGL (ms) | BGFX/Vulkan antes → depois (ms; Δ%) | BGFX/OpenGL antes → depois (ms; Δ%) | wgpu/Vulkan antes → depois (ms; Δ%) |",
             "|---|---:|---:|---:|---:|"]
    for case in ordered_cases(campaign):
        control = statistic(row_at(campaign, case, "coingl"))
        cells = [CASE_LABELS.get(case, case), number(control["before"])]
        for variant in RENDER_VARIANTS:
            pair = statistic(row_at(campaign, case, variant))
            cells.append(f"{pair_text(pair)}; {percent(pair)}")
        lines.append("| " + " | ".join(map(escape, cells)) + " |")
    return "\n".join(lines)


def ablation_table(groups):
    lines = ["| Variante | Basis literal → lente (ms) | Total literal → lente (ms) | Δ total | Prepares | Reuse | N por opção |",
             "|---|---:|---:|---:|---:|---:|---:|"]
    for group in groups:
        pair = diagnostic_pair(group, "csv_measured_medians_ms", "total_ms")
        lines.append(f"| {VARIANT_LABELS[group['variant']]} | {pair_text(diagnostic_pair(group, 'phase_measured_medians_ms', 'capture_camera_basis.prepare_ms'), 3)} | "
                     f"{pair_text(pair)} | {delta_text(pair)} ms; {percent(pair)} | "
                     f"{pair_text(diagnostic_pair(group, 'counter_measured_medians', 'capture_camera_basis.prepares'), counter=True)} | "
                     f"{pair_text(diagnostic_pair(group, 'counter_measured_medians', 'capture_camera_basis.reuse'), counter=True)} | "
                     f"{integer(group['off']['processes'])}/{integer(group['on']['processes'])} |")
    return "\n".join(lines)


def report(metadata, analysis, groups, diagnostic_counts, rgb, figure_reference):
    cold, steady = (analysis["campaigns"][name] for name in ("cold", "steady"))
    cold_timing, cold_resources, cold_ranges = cold_tables(cold)
    title = metadata.get("title", "CoinRender: base de câmera compartilhada na captura — Linux")
    if metadata.get("date"):
        title += ", " + metadata["date"]
    lines = ["# " + title, "", "Referência de render: Coin3D/Coin/OpenGL clássico (`SoGLRenderAction`).", "",
             "## Mudança e organização", ""]
    paragraphs(lines, notes(metadata, "implementation_notes") or [
        "A Action empresta o resultado positivo de `prepareCameraOverlayBasis` à qualificação de objetos dentro da mesma chamada de `rememberFrameRoot`, após instalar a captura validada. A base continua pertencendo à Action; a lente expira ao retornar da chamada.",
        "A admissão de câmera e a de objetos continuam independentes. A travessia estrita dos objetos, ownership, aliases, materiais e geometria permanecem verificados. Uma captura apenas de objetos conserva a preparação local e a política lazy da câmera. A lente não autoriza capturas futuras por revisão, ponteiro ou identidade de nó.",
        "A mudança fica em Wiring/Action, que conecta a cena ao Core por snapshots e notificações. Core mantém a preparação da base e as transações por valores; Target conserva a admissão de submission; BGFX e wgpu mantêm seus próprios lowering/runtime. Não há nova prova de backend ou recurso GPU na lente."])
    lines.extend([f"Optout: `{OPTOUT}=1`.", ""])
    paragraphs(lines, notes(metadata, "scope_notes"))
    lines.extend(["## Primeiro quadro em processo novo", "",
                  "Cada valor é a mediana de estatísticas por processo. Cold tem zero warmups e um quadro medido: total inclui update + render/readback + publication e representa o primeiro apply completo, não toda a inicialização do aplicativo.", "",
                  cold_timing, "", cold_ranges, "",
                  "Os intervalos mostram mínimo e máximo das medianas por processo, preservando todos os processos válidos e os outliers. Não são intervalos de confiança.", "",
                  cold_resources, "",
                  "Main → primeira imagem vem do log e inclui custos anteriores ao render. Pico RSS é memória residente máxima do processo, não memória GPU. Coin/OpenGL é o controle compartilhado, com igualdade de CSV/log comprovada por hashes.", "",
                  f"![Primeiro quadro, qualificação e controles steady]({figure_reference})", "",
                  "## Quadros após warmup", "",
                  "Mediana das medianas por processo, excluindo linhas CSV de warmup. Todos os quatro caminhos de render são mostrados; valores positivos de Δ% indicam aumento do tempo.", "",
                  steady_table(steady), "", "### Aumentos observados", ""])
    regressions = []
    for case in ordered_cases(steady):
        for variant in RENDER_VARIANTS:
            pair = statistic(row_at(steady, case, variant))
            if change(pair) > 0:
                regressions.append(f"- {VARIANT_LABELS[variant]} · {CASE_LABELS.get(case, case)}: {pair_text(pair)} ms; {delta_text(pair)} ms ({percent(pair)}).")
    lines.extend(regressions or ["Nenhuma mediana steady dos três backends aumentou nesta amostra."])
    lines.extend(["", "Essas diferenças descrevem esta amostra. A mudança elimina trabalho duplicado na captura elegível; não promete ganho em REUSE/CAMERA_PATCH nem atribui toda variação steady à lente.", "",
                  "## Ablação no mesmo binário", "",
                  f"{integer(diagnostic_counts['processes'])} processos, {integer(diagnostic_counts['measured_frames'])} quadros medidos e {integer(diagnostic_counts['warmup_frames'])} warmups. Cada opção tem N={integer(metadata.get('ablation_rounds', 3))} processos por API; as medianas são de medianas por processo, sem juntar quadros.", "",
                  ablation_table(groups), "",
                  "`calls` permanece 1; `prepares` cai de 2 para 1 e `reuse` sobe de 0 para 1 em cada execução diagnosticada. `prepare_ms` mede as preparações Core feitas durante essa captura; exclui a qualificação lazy de câmera em quadros posteriores. O trace bruto e a correspondência de cardinalidade com o CSV ficam arquivados.", "",
                  "A qualificação ocorre após o ponto que encerra `action.backend_ms`; por isso esse campo e `action.frame_plan_ms` excluem esse custo. O total/render que envolve o apply completo inclui a qualificação. Intervalos de fases podem ser aninhados e não devem ser somados.", "",
                  "## Protocolo, fontes e gates", "",
                  f"Código atual CoinRender: `{metadata['source_content_revision']}`. Coin/OpenGL: `{metadata['coingl_source_content_revision']}`.", "",
                  "O baseline foi congelado por variante; uma revisão global não descreve esse conjunto:", ""])
    for variant in RENDER_VARIANTS:
        lines.append(f"- Antes {VARIANT_LABELS[variant]}: `{metadata['baseline_variant_source_content_revisions'][variant]}`.")
    lines.append("")
    if metadata.get("scene_sha256"):
        lines.extend([f"Cena SHA-256: `{metadata['scene_sha256']}`.", ""])
    for name, campaign in (("Cold", cold), ("Steady", steady)):
        params = campaign["before"]["metadata"]["parameters"]
        count = campaign["unique_counts"]
        lines.append(f"- **{name}:** {integer(int(params['rounds']))} rodadas, {integer(int(params['warmup']))} warmups e {integer(int(params['frames']))} quadros medidos por processo; "
                     f"{integer(count['processes'])} processos únicos, {integer(count['measured_frames'])} medidos e {integer(count['warmup_frames'])} warmups. Casos: `{escape(params['cases'])}`.")
    lines.extend(["- As três variantes CoinRender têm antes/depois próprios; Coin/OpenGL é compartilhado por caso/rodada e contado uma vez mediante hashes e metadados.",
                  "- Percentis usam nearest rank por processo; a agregação entre processos usa mediana, inclusive média dos dois centrais quando N é par. Amostras curtas não caracterizam caudas de latência.", ""])
    gates = metadata["gate_counts"]
    lines.extend([f"Gates registrados: **{integer(sum(gates.values()))} execuções** — " + ", ".join(f"{escape(key)}: {integer(value)}" for key, value in gates.items()) + ". Resultados vêm dos metadados da etapa, sem inferência a partir dos tempos.", "",
                  f"Verificação RGB: **{integer(rgb['comparisons'])} comparações**, {integer(rgb['ppm_files'])} PPMs antes/depois; "
                  + ("todas idênticas por bytes" if rgb["identical"] else "há diferenças registradas") + ". Comparações são da mesma variante/caso/quadro entre revisões, não igualdade entre backends.", ""])
    if "verification_processes" in metadata:
        lines.extend([f"Processos de verificação antes + depois: {integer(metadata['verification_processes'])} (metadados da etapa).", ""])
    if not rgb["recomputed"]:
        lines.extend(["Os registros RGB foram reutilizados do arquivo fornecido; este gerador não reabriu os PPMs. Essa condição não é apresentada como uma nova comparação visual.", ""])
    lines.extend(["### Gates e revisão", ""])
    for key in ("gates", "history_notes", "reviews"):
        lines.extend("- " + value for value in notes(metadata, key))
    if not metadata.get("gates") and not metadata.get("reviews"):
        lines.append("Detalhes individuais dos gates não foram fornecidos nos metadados.")
    lines.extend(["", "## Limites da evidência", "",
                  "A campanha é offscreen: total inclui espera GPU e readback, sem medir duração GPU isolada, latência de exibição ou fluidez em janela.", "",
                  f"- Cold N={integer(int(cold['before']['metadata']['parameters']['rounds']))} e ablação N={integer(metadata.get('ablation_rounds', 3))} por opção descrevem as amostras preservadas; não estabelecem variância populacional ou ganhos universais.",
                  "- A alternância e o controle compartilhado reduzem a dependência da ordem; não comprovam igualdade de clocks, temperatura ou estado do driver.",
                  "- Os aumentos steady foram mantidos na tabela e na lista. A ablação confirma o trabalho removido na captura, sem atribuir automaticamente todas as variações do quadro à mudança."])
    lines.extend("- " + value for value in notes(metadata, "limitations"))
    if metadata.get("evidence_reference"):
        lines.extend(["", f"[Evidência e reprodução]({metadata['evidence_reference']})."])
    lines.extend(["", "Entradas, configuração e SHA-256 do gerador estão em `report-inputs.json`. Nenhum resultado de desempenho é embutido no gerador.", ""])
    return "\n".join(lines)


def create_figure(metadata, analysis, groups, output):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import numpy as np
    from matplotlib.colors import TwoSlopeNorm
    from matplotlib.patches import Patch

    plt.rcParams.update({"font.size": 10, "svg.fonttype": "none"})
    fig, axes = plt.subplots(2, 2, figsize=(16, 10.5), gridspec_kw={"width_ratios": [1.06, 1]})
    colors = {"before": "#8b97a8", "after": "#2566a1"}
    cold, steady = (analysis["campaigns"][name] for name in ("cold", "steady"))

    def bars(ax, labels, pairs, title, unit, digits=1, ranges=None):
        maximum = 0
        for index, pair in enumerate(pairs):
            for role, offset in (("before", -.18), ("after", .18)):
                value = pair[role]
                ax.barh(index + offset, value, height=.31, color=colors[role])
                if ranges is not None:
                    minimum, maximum_value = ranges[index][role]
                    ax.errorbar(value, index + offset, xerr=[[value - minimum], [maximum_value - value]],
                                fmt="none", ecolor="#444444", capsize=3, elinewidth=.8)
                    maximum = max(maximum, maximum_value)
                ax.annotate(f"{value:.{digits}f}", (value, index + offset), xytext=(4, 0),
                            textcoords="offset points", va="center", fontsize=9)
                maximum = max(maximum, value)
        ax.set_yticks(range(len(labels)), labels)
        ax.invert_yaxis()
        ax.set_xlim(0, maximum * 1.22 if maximum else 1)
        ax.set_title(title, pad=10)
        ax.set_xlabel(unit)
        ax.grid(axis="x", alpha=.22)
        ax.set_axisbelow(True)
        for spine in ("top", "right"):
            ax.spines[spine].set_visible(False)

    bars(axes[0, 0], [VARIANT_LABELS[v] for v in VARIANTS],
         [statistic(row_at(cold, "static", v)) for v in VARIANTS],
         f"Primeiro quadro · total · N={cold['before']['metadata']['parameters']['rounds']}", "ms · zero warmups · hastes = mínimo .. máximo",
         ranges=[{side: process_total_range(cold, side, "static", variant) for side in ("before", "after")} for variant in VARIANTS])
    bars(axes[0, 1], [VARIANT_LABELS[g["variant"]] for g in groups],
         [diagnostic_pair(g, "phase_measured_medians_ms", "capture_camera_basis.prepare_ms") for g in groups],
         f"Ablação · preparação da base · N={metadata.get('ablation_rounds', 3)} por opção", "ms · literal → lente · prepara 2 → 1", digits=2)
    bars(axes[1, 0], [VARIANT_LABELS[v] for v in VARIANTS],
         [scaled_pair(row_at(cold, "static", v)["peak_rss_kib"], 1024) for v in VARIANTS],
         "Primeiro quadro · pico RSS do processo", "MiB · inclui bibliotecas e driver")
    cases = ordered_cases(steady)
    values = np.array([[change(statistic(row_at(steady, case, variant))) for variant in VARIANTS] for case in cases])
    require(np.isfinite(values).all(), "Cannot chart missing/nonfinite steady percentage")
    limit = max(float(np.abs(values).max()), .01)
    ax = axes[1, 1]
    graphic = ax.imshow(values, cmap="RdBu_r", norm=TwoSlopeNorm(vmin=-limit, vcenter=0, vmax=limit), aspect="auto")
    ax.set_yticks(range(len(cases)), [CASE_LABELS.get(case, case) for case in cases])
    ax.set_xticks(range(len(VARIANTS)), [VARIANT_LABELS[v] for v in VARIANTS], rotation=20, ha="right")
    for row in range(len(cases)):
        for column in range(len(VARIANTS)):
            ax.text(column, row, f"{values[row, column]:+.2f}%", va="center", ha="center",
                    color="white" if abs(values[row, column]) > limit * .58 else "#151515", fontsize=9)
    ax.set_title(f"Steady · variação do total · N={steady['before']['metadata']['parameters']['rounds']}", pad=10)
    fig.colorbar(graphic, ax=ax, fraction=.038, pad=.035, label="% · positivo = aumento do tempo")
    fig.suptitle(metadata.get("plot_title", "CoinRender · base de câmera na mesma captura · offscreen"), fontsize=16, y=.985)
    fig.legend(handles=[Patch(color=colors["before"], label="Antes / literal na ablação"),
                        Patch(color=colors["after"], label="Depois / lente na ablação")],
               loc="upper center", bbox_to_anchor=(.5, .953), ncol=2, frameon=False)
    fig.text(.5, .015, "Medianas por processo; Coin/OpenGL compartilhado. Steady exclui warmup. Qualificação não está em action.backend_ms.", ha="center", fontsize=9)
    fig.subplots_adjust(top=.865, bottom=.14, left=.125, right=.965, hspace=.45, wspace=.55)
    fig.savefig(output / (FIGURE_NAME + ".png"), dpi=160)
    fig.savefig(output / (FIGURE_NAME + ".svg"))
    plt.close(fig)


def config_template():
    return {"source_content_revision": "REQUIRED_CURRENT_SOURCE_SHA", "baseline_source_content_revision": None,
            "baseline_variant_source_content_revisions": {variant: "REQUIRED_BASELINE_" + variant + "_SOURCE_SHA" for variant in VARIANTS},
            "coingl_source_content_revision": "REQUIRED_COINGL_SOURCE_SHA", "date": "2026-10-05", "scene_sha256": "REQUIRED_SCENE_SHA256",
            "expected_counts": {"cold": {"processes": 63, "measured_frames": 63, "warmup_frames": 0},
                                "steady": {"processes": 105, "measured_frames": 1575, "warmup_frames": 525},
                                "ablation": {"processes": 18, "measured_frames": 18, "warmup_frames": 0}},
            "ablation_rounds": 3, "gate_counts": {"Core puro": 2, "Action/Reuse misto": 4, "GPU": 12},
            "rgb_comparisons": 196, "verification_ppm_files": 392, "verification_processes": 56,
            "all_rgb_identical": None, "scope_notes": [], "gates": [], "history_notes": [], "reviews": [], "limitations": [],
            "evidence_reference": "validation/capture-camera-basis-linux/README.md"}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--write-config-template", type=Path, help="Create metadata skeleton and exit; no result or gate pass is assumed")
    for name in ("analysis", "cold", "steady", "metadata", "output"):
        parser.add_argument("--" + name, type=Path)
    parser.add_argument("--figure-reference", default="validation/capture-camera-basis-linux/capture-camera-basis.png")
    args = parser.parse_args()
    if args.write_config_template:
        require(not args.write_config_template.exists(), "Config template destination already exists")
        args.write_config_template.write_text(json.dumps(config_template(), indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        print("Wrote configuration skeleton; replace required sources/hashes and supply actual gate/RGB evidence")
        return
    for name in ("analysis", "cold", "steady", "metadata", "output"):
        if getattr(args, name) is None:
            parser.error("Required argument: --" + name)
    loaded, inputs = {}, {}
    for name in ("analysis", "cold", "steady", "metadata"):
        loaded[name], inputs[name] = read_json(getattr(args, name))
    metadata, analysis = loaded["metadata"], loaded["analysis"]
    require(not any(str(metadata.get(key, "")).startswith("REQUIRED_") for key in
                    ("source_content_revision", "baseline_source_content_revision", "coingl_source_content_revision", "scene_sha256")),
            "Configuration still contains placeholder sources or hashes")
    require(not any(str(value).startswith("REQUIRED_") for value in metadata.get("baseline_variant_source_content_revisions", {}).values()),
            "Configuration still contains placeholder baseline variant sources")
    groups, diagnostic_counts, rgb = validate(metadata, analysis, loaded["cold"], loaded["steady"])
    text = report(metadata, analysis, groups, diagnostic_counts, rgb, args.figure_reference)
    output = args.output.resolve()
    require(not output.exists(), "Output must be a new directory; preserve previous evidence")
    require(all(not path.resolve().is_relative_to(output) for path in (args.analysis, args.cold, args.steady, args.metadata)), "Output would contain an input")
    output.mkdir(parents=True)
    create_figure(metadata, analysis, groups, output)
    (output / REPORT_NAME).write_text(text, encoding="utf-8")
    script = Path(__file__).resolve()
    provenance = {"inputs": inputs, "generator": {"path": str(script), "sha256": hashlib.sha256(script.read_bytes()).hexdigest()},
                  "outputs": {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in sorted(output.iterdir()) if path.is_file()}}
    (output / "report-inputs.json").write_text(json.dumps(provenance, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"Wrote {output / REPORT_NAME}, PNG/SVG and input provenance; performance values read from evidence")


if __name__ == "__main__":
    main()
