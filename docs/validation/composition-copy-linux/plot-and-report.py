#!/usr/bin/env python3
"""Generate the composition-copy report and scientific PNG/SVG from JSON.

Reads evidence only: no benchmark, GPU, build, network or Git command.

Usage:
  python3 /tmp/coin-render-composition-copy-report.py --write-config-template /tmp/config.json
  python3 /tmp/coin-render-composition-copy-report.py \
    --analysis /tmp/coin-render-composition-copy-analysis/results.json \
    --cold /tmp/coin-render-composition-copy-cold-summary/summary.json \
    --steady /tmp/coin-render-composition-copy-steady-summary/summary.json \
    --metadata /tmp/config.json --output /tmp/coin-render-composition-copy-report

The analysis schema is coin-render-composition-copy-analysis.py: campaigns cold/steady
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
REPORT_NAME = "coin-render-composition-copy-linux.md"
FIGURE_NAME = "composition-copy"
OPTOUT = "COIN_RENDER_DISABLE_COMPOSITION_BORROW"


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
    contract = metadata.get("diagnostic_contract", {})
    require(contract.get("configured") is True, "Composition trace contract is pending")
    require(contract.get("optout") == OPTOUT, "Unexpected composition optout")
    require(isinstance(contract.get("rounds"), int) and contract["rounds"] > 0, "Diagnostic rounds missing")
    require(contract.get("metrics") and contract.get("per_frame_assertions"), "Trace metrics/proof must be configured")
    require(sum(metric.get("primary") is True for metric in contract["metrics"]) == 1, "Exactly one primary plot metric is required")
    require(notes(metadata, "implementation_notes"), "Supply source-reviewed implementation notes")
    groups = diagnostic_groups(analysis)
    require({(item["variant"], item["case"]) for item in groups} ==
            {(variant, case) for variant in contract["variants"] for case in contract["cases"]},
            "Diagnostic API/case groups differ from contract")
    diagnostic_counts = {key: sum(value["unique_counts"][key] for value in analysis["diagnostics"].values()) for key in COUNTS}
    expected = metadata.get("expected_counts", {}).get("ablation")
    require(expected and diagnostic_counts == expected, f"Ablation counts differ: {diagnostic_counts} / {expected}")
    for value in analysis["diagnostics"].values():
        require(value["metadata_and_raw_trace"]["command_metadata"].get("source_content_revision") == metadata["source_content_revision"],
                "Diagnostic source differs from current rendering source")
        require(value.get("contract") == contract, "Reporter contract differs from analyzed contract")
        require(value.get("complete_and_comparable") is True, f"Incomparable diagnostic: {value.get('pairing_issues')}")
        require(value.get("unique_counts") == value.get("expected_unique_counts"), "Diagnostic planned/actual counts differ")
        for stem, run in value["metadata_and_raw_trace"]["runs"].items():
            require(run.get("composition_contract_passed") is True, "Per-process trace proof failed: " + stem)
            require(run.get("composition_contract_checks") and all(check["passed"] for check in run["composition_contract_checks"]),
                    "Per-frame proof absent or failed: " + stem)
    for group in groups:
        for mode in ("on", "off"):
            require(group[mode].get("processes") == contract["rounds"], f"Ablation N differs: {group['label']}/{mode}")
        for metric in contract["metrics"]:
            require(diagnostic_pair(group, metric["category"], metric["key"]) is not None,
                    "Missing requested trace metric: " + group["label"] + "/" + metric["key"])
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


def ablation_table(groups, contract):
    sections = []
    tables = list(dict.fromkeys(metric.get("table", "Métricas") for metric in contract["metrics"]))
    for table in tables:
        metrics = [metric for metric in contract["metrics"] if metric.get("table", "Métricas") == table]
        header = ["Variante", "Caso"] + [metric["label"] + " (" + metric["unit"] + ")" for metric in metrics] + ["N literal/empréstimo"]
        lines = ["### " + str(table), "", "| " + " | ".join(map(escape, header)) + " |", "|" + "---|" * len(header)]
        for group in groups:
            cells = [VARIANT_LABELS[group["variant"]], CASE_LABELS.get(group["case"], group["case"])]
            for metric in metrics:
                cells.append(pair_text(diagnostic_pair(group, metric["category"], metric["key"]),
                                       digits=metric.get("digits", 3), counter=metric.get("counter", False)))
            cells.append(integer(group["off"]["processes"]) + "/" + integer(group["on"]["processes"]))
            lines.append("| " + " | ".join(map(escape, cells)) + " |")
        sections.append("\n".join(lines))
    return "\n\n".join(sections)


def descriptive_tables(groups, contract):
    sections = []
    for scope in contract.get("descriptive_scopes", []):
        lines = ["### Classificação existente: " + scope, "",
                 "Medianas por processo da quantidade de eventos e da soma de qualify_ms de **todos os eventos, incluindo warmups**. Uma chamada de ordenação é um evento; estes valores não são medianas de quadros medidos nem overhead puro da mudança.", "",
                 "| Variante | Caso | Eventos literal → empréstimo por processo | Soma qualify_ms literal → empréstimo (ms) |", "|---|---|---:|---:|"]
        for group in groups:
            before, after = (group[mode].get("descriptive_scope_summaries", {}).get(scope, {}) for mode in ("off", "on"))
            count = {"before": before.get("median_event_count_per_process"), "after": after.get("median_event_count_per_process")}
            total = {"before": before.get("median_all_events_qualify_ms_total_per_process"), "after": after.get("median_all_events_qualify_ms_total_per_process")}
            lines.append(f"| {VARIANT_LABELS[group['variant']]} | {CASE_LABELS.get(group['case'], group['case'])} | {pair_text(count, counter=True)} | {pair_text(total, 3)} |")
        sections.append("\n".join(lines))
    return "\n\n".join(sections)


def report(metadata, analysis, groups, diagnostic_counts, rgb, figure_reference):
    cold, steady = (analysis["campaigns"][name] for name in ("cold", "steady"))
    contract = metadata["diagnostic_contract"]
    timing, resources, ranges = cold_tables(cold)
    title = metadata.get("title", "CoinRender: cópias de composição — Linux")
    if metadata.get("date"):
        title += ", " + metadata["date"]
    lines = ["# " + title, "", "Referência de render: Coin3D/Coin/OpenGL clássico (`SoGLRenderAction`).", "",
             "## Mudança e organização", ""]
    paragraphs(lines, notes(metadata, "implementation_notes"))
    lines.extend([f"Optout: `{OPTOUT}=1`.", ""])
    paragraphs(lines, notes(metadata, "scope_notes"))
    lines.extend(["## Primeiro quadro em processo novo", "",
                  "Mediana de estatísticas por processo. Cold tem zero warmups e um quadro medido: total inclui update + render/readback + publication e representa o primeiro apply completo. A inicialização anterior ao apply aparece separadamente no log main → primeira imagem.", "",
                  timing, "", ranges, "", "Os intervalos preservam mínimo e máximo por processo, incluindo outliers; não são intervalos de confiança.", "", resources, "",
                  "Pico RSS é memória residente máxima do processo, não memória GPU. Coin/OpenGL é controle compartilhado: CSV e log antes/depois têm hashes iguais e a execução é contada uma vez.", "",
                  f"![Primeiro quadro, composição e controle steady]({figure_reference})", "",
                  "## Quadros após warmup", "", "Mediana das medianas por processo, usando exclusivamente índices CSV com warmup falso. Δ% positivo significa mais tempo.", "",
                  steady_table(steady), "", "### Aumentos observados", ""])
    increases = []
    for case in ordered_cases(steady):
        for variant in RENDER_VARIANTS:
            pair = statistic(row_at(steady, case, variant))
            if change(pair) > 0:
                increases.append(f"- {VARIANT_LABELS[variant]} · {CASE_LABELS.get(case, case)}: {pair_text(pair)} ms; {delta_text(pair)} ms ({percent(pair)}).")
    lines.extend(increases or ["Nenhuma mediana steady dos três backends aumentou nesta amostra."])
    lines.extend(["", "As diferenças descrevem esta amostra; a ablação abaixo isola a opção no mesmo binário e não atribui automaticamente toda variação do quadro à mudança.", "",
                  "## Ablação no mesmo binário", "",
                  f"{integer(diagnostic_counts['processes'])} processos, {integer(diagnostic_counts['measured_frames'])} quadros medidos e {integer(diagnostic_counts['warmup_frames'])} warmups. N={integer(contract['rounds'])} processos por opção/API/caso; a agregação usa medianas por processo.", "",
                  ablation_table(groups, contract), "", descriptive_tables(groups, contract), ""])
    lines.extend(["### Aumentos totais na ablação", ""])
    increases = []
    for group in groups:
        pair = diagnostic_pair(group, "csv_measured_medians_ms", "total_ms")
        if change(pair) > 0:
            increases.append(f"- {VARIANT_LABELS[group['variant']]} · {CASE_LABELS.get(group['case'], group['case'])}: {pair_text(pair)} ms; {delta_text(pair)} ms ({percent(pair)}).")
    lines.extend(increases or ["Nenhuma mediana de total on/off aumentou nesta amostra."])
    lines.extend(["", "A métrica primária mede transferência/ativação e lookup O(1), excluindo o predicado adicional acumulado na classificação. A comparação de total_ms on/off inclui o classificador e é o resultado conjunto. Redução da métrica primária não é ganho líquido após todo overhead.", "",
                  "copy_ms do lowering mede a realização literal completa da schedule, incluindo seus passos e verificações; não representa memcpy puro.", ""])
    paragraphs(lines, notes(metadata, "ablation_notes"))
    lines.extend(["Séries brutas, eventos, valores por quadro e correspondência de cardinalidade com CSV estão preservados. Só séries com uma observação por linha CSV fornecem estatísticas de quadros medidos. Tempos de fases podem se sobrepor e não devem ser somados.", "",
                  "## Protocolo, fontes e gates", "",
                  f"Código atual CoinRender: `{metadata['source_content_revision']}`. Coin/OpenGL: `{metadata['coingl_source_content_revision']}`.", "",
                  "Fontes dos binários congelados por variante:", ""])
    for variant in RENDER_VARIANTS:
        lines.append(f"- Antes {VARIANT_LABELS[variant]}: `{metadata['baseline_variant_source_content_revisions'][variant]}`.")
    lines.append("")
    if metadata.get("scene_sha256"):
        lines.extend([f"Cena SHA-256: `{metadata['scene_sha256']}`.", ""])
    for name, campaign in (("Cold", cold), ("Steady", steady)):
        params, count = campaign["before"]["metadata"]["parameters"], campaign["unique_counts"]
        lines.append(f"- **{name}:** {integer(int(params['rounds']))} rodadas, {integer(int(params['warmup']))} warmups e {integer(int(params['frames']))} quadros medidos por processo; {integer(count['processes'])} processos únicos, {integer(count['measured_frames'])} medidos e {integer(count['warmup_frames'])} warmups. Casos: `{escape(params['cases'])}`.")
    lines.extend(["- As três variantes CoinRender têm antes/depois próprios; Coin/OpenGL é compartilhado por caso/rodada mediante igualdade de hashes e metadados.",
                  "- Percentis usam nearest rank por processo; entre processos usa-se mediana, inclusive média dos dois centrais quando N é par. Amostras curtas não caracterizam caudas de latência.", ""])
    gates = metadata["gate_counts"]
    lines.extend([f"Gates registrados: **{integer(sum(gates.values()))} execuções** — " + ", ".join(f"{escape(key)}: {integer(value)}" for key, value in gates.items()) + ". Resultados vêm dos metadados preservados, sem inferência a partir dos tempos.", "",
                  f"Verificação RGB: **{integer(rgb['comparisons'])} comparações**, {integer(rgb['ppm_files'])} PPMs antes/depois; " +
                  ("todas idênticas por bytes" if rgb["identical"] else "há diferenças registradas") + ". Cada par compara a mesma variante/caso/quadro entre revisões, sem afirmar igualdade entre backends.", ""])
    if "verification_processes" in metadata:
        lines.extend([f"Processos de verificação antes + depois: {integer(metadata['verification_processes'])}.", ""])
    if not rgb["recomputed"]:
        lines.extend(["Os registros RGB são reutilizados do arquivo fornecido; este gerador não reabre os PPMs nem apresenta uma nova comparação visual.", ""])
    lines.extend(["### Gates e revisão", ""])
    for key in ("gates", "history_notes", "reviews"):
        lines.extend("- " + value for value in notes(metadata, key))
    lines.extend(["", "## Limites da evidência", "",
                  "A campanha é offscreen: total inclui espera GPU e readback, sem medir duração GPU isolada, latência de exibição ou fluidez em janela.", "",
                  f"- Cold N={integer(int(cold['before']['metadata']['parameters']['rounds']))} e ablação N={integer(contract['rounds'])} por opção descrevem as amostras preservadas, sem estabelecer variância populacional ou ganho universal.",
                  "- A alternância e o controle compartilhado reduzem dependência da ordem; não comprovam igualdade de clocks, temperatura ou estado do driver.",
                  "- Aumentos de tempo permanecem registrados. A prova de contadores mostra o trabalho de cópia executado/evitado; não é medição de ganho GPU."])
    lines.extend("- " + value for value in notes(metadata, "limitations"))
    if metadata.get("evidence_reference"):
        lines.extend(["", f"[Evidência e reprodução]({metadata['evidence_reference']})."])
    lines.extend(["", "Entradas, configuração e SHA-256 do gerador estão em `report-inputs.json`. Valores de desempenho são lidos da evidência.", ""])
    return "\n".join(lines)


def create_figure(metadata, analysis, groups, output):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import numpy as np
    from matplotlib.colors import TwoSlopeNorm
    from matplotlib.patches import Patch
    contract = metadata["diagnostic_contract"]
    primary = next((metric for metric in contract["metrics"] if metric.get("primary") is True), None)
    require(primary is not None, "Supply one primary diagnostic metric for the figure")
    plt.rcParams.update({"font.size": 10, "svg.fonttype": "none"})
    fig, axes = plt.subplots(2, 2, figsize=(16, 10.5), gridspec_kw={"width_ratios": [1.06, 1]})
    colors = {"before": "#8b97a8", "after": "#2566a1"}
    cold, steady = (analysis["campaigns"][name] for name in ("cold", "steady"))

    def bars(ax, labels, pairs, title, unit, digits=1, ranges=None):
        maximum = 0
        for index, pair in enumerate(pairs):
            require(pair and all(finite(pair.get(role)) for role in ("before", "after")), "Cannot chart missing/nonfinite value")
            for role, offset in (("before", -.18), ("after", .18)):
                value = pair[role]
                ax.barh(index + offset, value, height=.31, color=colors[role])
                if ranges is not None:
                    minimum, maximum_value = ranges[index][role]
                    ax.errorbar(value, index + offset, xerr=[[value-minimum], [maximum_value-value]], fmt="none", ecolor="#444444", capsize=3, elinewidth=.8)
                    maximum = max(maximum, maximum_value)
                ax.annotate(f"{value:.{digits}f}", (value, index+offset), xytext=(4, 0), textcoords="offset points", va="center", fontsize=9)
                maximum = max(maximum, value)
        ax.set_yticks(range(len(labels)), labels)
        ax.invert_yaxis()
        ax.set_xlim(0, maximum*1.22 if maximum else 1)
        ax.set_title(title, pad=10)
        ax.set_xlabel(unit)
        ax.grid(axis="x", alpha=.22)
        ax.set_axisbelow(True)
        for spine in ("top", "right"):
            ax.spines[spine].set_visible(False)
    bars(axes[0, 0], [VARIANT_LABELS[v] for v in VARIANTS], [statistic(row_at(cold, "static", v)) for v in VARIANTS],
         f"Primeiro quadro · total · N={cold['before']['metadata']['parameters']['rounds']}", "ms · zero warmups · hastes = mínimo .. máximo",
         ranges=[{side: process_total_range(cold, side, "static", variant) for side in ("before", "after")} for variant in VARIANTS])
    bars(axes[0, 1], [VARIANT_LABELS[g["variant"]] + " · " + CASE_LABELS.get(g["case"], g["case"]) for g in groups],
         [diagnostic_pair(g, primary["category"], primary["key"]) for g in groups],
         f"Ablação · {primary['label']} · N={contract['rounds']} por opção", primary["unit"] + " · literal → empréstimo", digits=primary.get("digits", 2))
    bars(axes[1, 0], [VARIANT_LABELS[v] for v in VARIANTS], [scaled_pair(row_at(cold, "static", v)["peak_rss_kib"], 1024) for v in VARIANTS],
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
        for col in range(len(VARIANTS)):
            ax.text(col, row, f"{values[row, col]:+.2f}%", va="center", ha="center", color="white" if abs(values[row, col]) > limit*.58 else "#151515", fontsize=9)
    ax.set_title(f"Steady · variação do total · N={steady['before']['metadata']['parameters']['rounds']}", pad=10)
    fig.colorbar(graphic, ax=ax, fraction=.038, pad=.035, label="% · positivo = aumento do tempo")
    fig.suptitle(metadata.get("plot_title", "CoinRender · cópias de composição · offscreen"), fontsize=16, y=.985)
    fig.legend(handles=[Patch(color=colors["before"], label="Antes / literal na ablação"), Patch(color=colors["after"], label="Depois / empréstimo na ablação")], loc="upper center", bbox_to_anchor=(.5, .953), ncol=2, frameon=False)
    fig.text(.5, .015, "Medianas por processo; Coin/OpenGL compartilhado. Steady exclui warmup; fases não são aditivas.", ha="center", fontsize=9)
    fig.subplots_adjust(top=.865, bottom=.14, left=.125, right=.965, hspace=.45, wspace=.55)
    fig.savefig(output/(FIGURE_NAME+".png"), dpi=160)
    fig.savefig(output/(FIGURE_NAME+".svg"))
    plt.close(fig)


def config_template():
    # Planned protocol and expected proof only; no performance/GPU result is embedded.
    return {'source_content_revision': 'REQUIRED_CURRENT_SOURCE_SHA',
     'baseline_source_content_revision': '24bc92d8d60d3a1ce782e7787d2060a6b08261fb',
     'baseline_variant_source_content_revisions': {'coingl': '4d63bb993022ee8d40802558b0871a4803002b8d',
                                                   'bgfx-vulkan': '24bc92d8d60d3a1ce782e7787d2060a6b08261fb',
                                                   'bgfx-opengl': '24bc92d8d60d3a1ce782e7787d2060a6b08261fb',
                                                   'wgpu-vulkan': '24bc92d8d60d3a1ce782e7787d2060a6b08261fb'},
     'coingl_source_content_revision': '4d63bb993022ee8d40802558b0871a4803002b8d',
     'date': '2026-10-05',
     'scene_sha256': 'REQUIRED_SCENE_SHA256',
     'expected_counts': {'cold': {'processes': 63, 'measured_frames': 63, 'warmup_frames': 0},
                         'steady': {'processes': 105, 'measured_frames': 1575, 'warmup_frames': 525},
                         'ablation': {'processes': 36, 'measured_frames': 36, 'warmup_frames': 90}},
     'diagnostic_contract': {'configured': False,
                             'optout': 'COIN_RENDER_DISABLE_COMPOSITION_BORROW',
                             'rounds': 3,
                             'profiles': {'static': {'frames': 1, 'warmup': 0},
                                          'transforms-10': {'frames': 1, 'warmup': 5}},
                             'variants': ['bgfx-vulkan', 'bgfx-opengl', 'wgpu-vulkan'],
                             'cases': ['static', 'transforms-10'],
                             'scopes': ['target_composition_copy', 'composition_schedule_copy'],
                             'metrics': [{'category': 'csv_measured_medians_ms',
                                          'key': 'total_ms',
                                          'label': 'Total',
                                          'unit': 'ms',
                                          'digits': 3,
                                          'table': 'timing'},
                                         {'category': 'phase_measured_medians_ms',
                                          'key': 'composition_transfer.copy_lookup_ms',
                                          'label': 'Transferência + lookup O(1)',
                                          'unit': 'ms',
                                          'digits': 3,
                                          'table': 'timing',
                                          'primary': True},
                                         {'category': 'phase_measured_medians_ms',
                                          'key': 'target_composition_copy.copy_ms',
                                          'label': 'Target: cópia/ativação',
                                          'unit': 'ms',
                                          'digits': 3,
                                          'table': 'timing'},
                                         {'category': 'phase_measured_medians_ms',
                                          'key': 'composition_schedule_copy.copy_ms',
                                          'label': 'Lowering: schedule literal',
                                          'unit': 'ms',
                                          'digits': 3,
                                          'table': 'timing'},
                                         {'category': 'phase_measured_medians_ms',
                                          'key': 'target_composition_copy.qualify_ms',
                                          'label': 'Target: lookup',
                                          'unit': 'ms',
                                          'digits': 6,
                                          'table': 'lookup'},
                                         {'category': 'phase_measured_medians_ms',
                                          'key': 'composition_schedule_copy.qualify_ms',
                                          'label': 'Lowering: lookup',
                                          'unit': 'ms',
                                          'digits': 6,
                                          'table': 'lookup'},
                                         {'category': 'counter_measured_medians',
                                          'key': 'target_composition_copy.copied_items',
                                          'label': 'Target: copied_items',
                                          'unit': 'itens',
                                          'counter': True,
                                          'table': 'Target'},
                                         {'category': 'counter_measured_medians',
                                          'key': 'target_composition_copy.copied_bytes',
                                          'label': 'Target: copied_bytes',
                                          'unit': 'bytes lógicos',
                                          'counter': True,
                                          'table': 'Target'},
                                         {'category': 'counter_measured_medians',
                                          'key': 'target_composition_copy.borrowed_items',
                                          'label': 'Target: borrowed_items',
                                          'unit': 'itens',
                                          'counter': True,
                                          'table': 'Target'},
                                         {'category': 'counter_measured_medians',
                                          'key': 'target_composition_copy.borrowed_bytes',
                                          'label': 'Target: borrowed_bytes',
                                          'unit': 'bytes lógicos',
                                          'counter': True,
                                          'table': 'Target'},
                                         {'category': 'counter_measured_medians',
                                          'key': 'target_composition_copy.computed_items',
                                          'label': 'Target: computed_items',
                                          'unit': 'itens',
                                          'counter': True,
                                          'table': 'Target'},
                                         {'category': 'counter_measured_medians',
                                          'key': 'composition_schedule_copy.copied_items',
                                          'label': 'Lowering: copied_items',
                                          'unit': 'itens',
                                          'counter': True,
                                          'table': 'Lowering'},
                                         {'category': 'counter_measured_medians',
                                          'key': 'composition_schedule_copy.copied_bytes',
                                          'label': 'Lowering: copied_bytes',
                                          'unit': 'bytes lógicos',
                                          'counter': True,
                                          'table': 'Lowering'},
                                         {'category': 'counter_measured_medians',
                                          'key': 'composition_schedule_copy.borrowed_items',
                                          'label': 'Lowering: borrowed_items',
                                          'unit': 'itens',
                                          'counter': True,
                                          'table': 'Lowering'},
                                         {'category': 'counter_measured_medians',
                                          'key': 'composition_schedule_copy.borrowed_bytes',
                                          'label': 'Lowering: borrowed_bytes',
                                          'unit': 'bytes lógicos',
                                          'counter': True,
                                          'table': 'Lowering'},
                                         {'category': 'counter_measured_medians',
                                          'key': 'composition_schedule_copy.computed_items',
                                          'label': 'Lowering: computed_items',
                                          'unit': 'itens',
                                          'counter': True,
                                          'table': 'Lowering'}],
                             'per_frame_assertions': [{'scope': 'target_composition_copy',
                                                       'key': 'copied_items',
                                                       'expected': {'on': 0, 'off': 40001},
                                                       'sample_selection': 'all',
                                                       'groups': ['bgfx-vulkan|static',
                                                                  'bgfx-opengl|static',
                                                                  'wgpu-vulkan|static']},
                                                      {'scope': 'target_composition_copy',
                                                       'key': 'copied_bytes',
                                                       'expected': {'on': 0, 'off': 2240056},
                                                       'sample_selection': 'all',
                                                       'groups': ['bgfx-vulkan|static',
                                                                  'bgfx-opengl|static',
                                                                  'wgpu-vulkan|static']},
                                                      {'scope': 'target_composition_copy',
                                                       'key': 'borrowed_items',
                                                       'expected': {'on': 40001, 'off': 0},
                                                       'sample_selection': 'all',
                                                       'groups': ['bgfx-vulkan|static',
                                                                  'bgfx-opengl|static',
                                                                  'wgpu-vulkan|static']},
                                                      {'scope': 'target_composition_copy',
                                                       'key': 'borrowed_bytes',
                                                       'expected': {'on': 2240056, 'off': 0},
                                                       'sample_selection': 'all',
                                                       'groups': ['bgfx-vulkan|static',
                                                                  'bgfx-opengl|static',
                                                                  'wgpu-vulkan|static']},
                                                      {'scope': 'target_composition_copy',
                                                       'key': 'computed_items',
                                                       'expected': {'on': 0, 'off': 0},
                                                       'sample_selection': 'all',
                                                       'groups': ['bgfx-vulkan|static',
                                                                  'bgfx-opengl|static',
                                                                  'wgpu-vulkan|static']},
                                                      {'scope': 'composition_schedule_copy',
                                                       'key': 'copied_items',
                                                       'expected': {'on': 0, 'off': 40001},
                                                       'sample_selection': 'all'},
                                                      {'scope': 'composition_schedule_copy',
                                                       'key': 'copied_bytes',
                                                       'expected': {'on': 0, 'off': 2240056},
                                                       'sample_selection': 'all'},
                                                      {'scope': 'composition_schedule_copy',
                                                       'key': 'borrowed_items',
                                                       'expected': {'on': 40001, 'off': 0},
                                                       'sample_selection': 'all'},
                                                      {'scope': 'composition_schedule_copy',
                                                       'key': 'borrowed_bytes',
                                                       'expected': {'on': 2240056, 'off': 0},
                                                       'sample_selection': 'all'},
                                                      {'scope': 'composition_schedule_copy',
                                                       'key': 'computed_items',
                                                       'expected': {'on': 0, 'off': 0},
                                                       'sample_selection': 'all'},
                                                      {'scope': 'target_composition_copy',
                                                       'key': 'copied_items',
                                                       'expected': {'on': 0, 'off': 0},
                                                       'sample_selection': 'measured',
                                                       'groups': ['bgfx-vulkan|transforms-10',
                                                                  'bgfx-opengl|transforms-10',
                                                                  'wgpu-vulkan|transforms-10']},
                                                      {'scope': 'target_composition_copy',
                                                       'key': 'copied_bytes',
                                                       'expected': {'on': 0, 'off': 0},
                                                       'sample_selection': 'measured',
                                                       'groups': ['bgfx-vulkan|transforms-10',
                                                                  'bgfx-opengl|transforms-10',
                                                                  'wgpu-vulkan|transforms-10']},
                                                      {'scope': 'target_composition_copy',
                                                       'key': 'borrowed_items',
                                                       'expected': {'on': 0, 'off': 0},
                                                       'sample_selection': 'measured',
                                                       'groups': ['bgfx-vulkan|transforms-10',
                                                                  'bgfx-opengl|transforms-10',
                                                                  'wgpu-vulkan|transforms-10']},
                                                      {'scope': 'target_composition_copy',
                                                       'key': 'borrowed_bytes',
                                                       'expected': {'on': 0, 'off': 0},
                                                       'sample_selection': 'measured',
                                                       'groups': ['bgfx-vulkan|transforms-10',
                                                                  'bgfx-opengl|transforms-10',
                                                                  'wgpu-vulkan|transforms-10']},
                                                      {'scope': 'target_composition_copy',
                                                       'key': 'computed_items',
                                                       'expected': {'on': 40001, 'off': 40001},
                                                       'sample_selection': 'measured',
                                                       'groups': ['bgfx-vulkan|transforms-10',
                                                                  'bgfx-opengl|transforms-10',
                                                                  'wgpu-vulkan|transforms-10']},
                                                      {'scope': 'target_composition_copy',
                                                       'key': 'consumer',
                                                       'expected': {'on': 'target', 'off': 'target'},
                                                       'sample_selection': 'all'},
                                                      {'scope': 'composition_schedule_copy',
                                                       'key': 'consumer',
                                                       'expected': {'on': 'bgfx_instancing',
                                                                    'off': 'bgfx_instancing'},
                                                       'sample_selection': 'all',
                                                       'groups': ['bgfx-vulkan|static',
                                                                  'bgfx-vulkan|transforms-10']},
                                                      {'scope': 'composition_schedule_copy',
                                                       'key': 'consumer',
                                                       'expected': {'on': 'bgfx_instancing',
                                                                    'off': 'bgfx_instancing'},
                                                       'sample_selection': 'all',
                                                       'groups': ['bgfx-opengl|static',
                                                                  'bgfx-opengl|transforms-10']},
                                                      {'scope': 'composition_schedule_copy',
                                                       'key': 'consumer',
                                                       'expected': {'on': 'wgpu_instancing',
                                                                    'off': 'wgpu_instancing'},
                                                       'sample_selection': 'all',
                                                       'groups': ['wgpu-vulkan|static',
                                                                  'wgpu-vulkan|transforms-10']}],
                             'description': 'City profile only: Target1 and schedule1 per CSV row; static cold '
                                            'two logical copies, RESOURCE measured frame Target moves computed '
                                            'order and lowering alone copies. composition_identity is '
                                            'descriptive per order call, not universal CSV frame or isolated '
                                            'overhead.',
                             'descriptive_scopes': ['composition_identity'],
                             'derived_metrics': [{'key': 'composition_transfer.copy_lookup_ms',
                                                  'source_keys': ['target_composition_copy.copy_ms',
                                                                  'target_composition_copy.qualify_ms',
                                                                  'composition_schedule_copy.copy_ms',
                                                                  'composition_schedule_copy.qualify_ms'],
                                                  'description': 'Row-wise sum of these four separately '
                                                                 'instrumented transfer/activation/lookup '
                                                                 'intervals only; excludes '
                                                                 'composition_identity.qualify_ms. Median after '
                                                                 'CSV measured-row selection.'}]},
     'gate_counts': {'Core puro': 7, 'Action/Reuse misto': 4, 'GPU': 18},
     'gate_commands': {'core': {'tests': ['CoinRenderFrameCoreTest', 'CoinRenderPlanAssemblyCoreTest'],
                                'category': 'Core puro',
                                'source_category': 'core_cpu',
                                'variant': 'wgpu-vulkan',
                                'direct_cli_override': False},
                       'wgpu-action-reuse': {'tests': ['CoinRenderActionTest', 'CoinRenderFrameReuseCoreTest'],
                                             'category': 'Action/Reuse misto',
                                             'source_category': 'action_reuse',
                                             'variant': 'wgpu-vulkan',
                                             'direct_cli_override': False},
                       'bgfx-action-reuse': {'tests': ['CoinRenderActionTest', 'CoinRenderFrameReuseCoreTest'],
                                             'category': 'Action/Reuse misto',
                                             'source_category': 'action_reuse',
                                             'variant': 'bgfx-vulkan',
                                             'direct_cli_override': False},
                       'wgpu-vulkan-camera-transparency': {'tests': ['CoinRenderCameraReuseReferenceTest',
                                                                     'CoinRenderTransparencyTest'],
                                                           'category': 'GPU',
                                                           'source_category': 'gpu',
                                                           'variant': 'wgpu-vulkan',
                                                           'direct_cli_override': False},
                       'bgfx-vulkan-camera-transparency': {'tests': ['CoinRenderCameraReuseReferenceTest',
                                                                     'CoinRenderTransparencyTest'],
                                                           'category': 'GPU',
                                                           'source_category': 'gpu',
                                                           'variant': 'bgfx-vulkan',
                                                           'direct_cli_override': False},
                       'bgfx-opengl-camera-transparency': {'tests': ['CoinRenderCameraReuseReferenceTest',
                                                                     'CoinRenderTransparencyTest'],
                                                           'category': 'GPU',
                                                           'source_category': 'gpu',
                                                           'variant': 'bgfx-opengl',
                                                           'direct_cli_override': False},
                       'wgpu-rtt-runtime': {'tests': ['CoinRenderSceneTextureTest',
                                                      'CoinRenderSceneTextureDirectTest',
                                                      'CoinWgpuMultiDeviceTest',
                                                      'CoinRenderAsyncActionTest'],
                                            'category': 'GPU',
                                            'source_category': 'gpu',
                                            'variant': 'wgpu-vulkan',
                                            'direct_cli_override': False},
                       'bgfx-rtt': {'tests': ['CoinRenderRttOwnership_vulkan', 'CoinRenderRttOwnership_opengl'],
                                    'category': 'GPU',
                                    'source_category': 'gpu',
                                    'variant': 'bgfx-vulkan',
                                    'direct_cli_override': False},
                       'composition-range-memo-cpu': {'tests': ['CoinRenderCompositionTest'],
                                                      'category': 'Core puro',
                                                      'source_category': 'core_cpu',
                                                      'variant': 'wgpu-vulkan',
                                                      'direct_cli_override': True,
                                                      'arguments': ['--range-memo'],
                                                      'required_output_marker': None},
                       'composition-borrow-cpu': {'tests': ['CoinRenderCompositionTest'],
                                                  'category': 'Core puro',
                                                  'source_category': 'core_cpu',
                                                  'variant': 'wgpu-vulkan',
                                                  'direct_cli_override': True,
                                                  'arguments': ['--borrow'],
                                                  'required_output_marker': 'CoinRenderCompositionTest borrow '
                                                                            'passed'},
                       'wgpu-ffi-frame-cpu': {'tests': ['CoinWgpuFfiFrameTest'],
                                              'category': 'Core puro',
                                              'source_category': 'core_cpu',
                                              'variant': 'wgpu-vulkan',
                                              'direct_cli_override': False},
                       'depth-contract-cpu': {'tests': ['CoinRenderDepthContractTest'],
                                              'category': 'Core puro',
                                              'source_category': 'core_cpu',
                                              'variant': 'wgpu-vulkan',
                                              'direct_cli_override': False},
                       'bgfx-core-cpu': {'tests': ['CoinBgfxCoreTest'],
                                         'category': 'Core puro',
                                         'source_category': 'core_cpu',
                                         'variant': 'bgfx-vulkan',
                                         'direct_cli_override': False},
                       'wgpu-vulkan-composition-gpu': {'tests': ['CoinRenderCompositionTest'],
                                                       'category': 'GPU',
                                                       'source_category': 'gpu',
                                                       'variant': 'wgpu-vulkan',
                                                       'direct_cli_override': True,
                                                       'arguments': ['--gpu'],
                                                       'required_output_marker': 'CoinRenderCompositionTest GPU '
                                                                                 'passed'},
                       'bgfx-vulkan-composition-gpu': {'tests': ['CoinRenderCompositionTest'],
                                                       'category': 'GPU',
                                                       'source_category': 'gpu',
                                                       'variant': 'bgfx-vulkan',
                                                       'direct_cli_override': True,
                                                       'arguments': ['--gpu'],
                                                       'required_output_marker': 'CoinRenderCompositionTest GPU '
                                                                                 'passed'},
                       'bgfx-opengl-composition-gpu': {'tests': ['CoinRenderCompositionTest'],
                                                       'category': 'GPU',
                                                       'source_category': 'gpu',
                                                       'variant': 'bgfx-opengl',
                                                       'direct_cli_override': True,
                                                       'arguments': ['--gpu'],
                                                       'required_output_marker': 'CoinRenderCompositionTest GPU '
                                                                                 'passed'},
                       'wgpu-vulkan-depth-gpu': {'tests': ['CoinRenderDepthContractTest'],
                                                 'category': 'GPU',
                                                 'source_category': 'gpu',
                                                 'variant': 'wgpu-vulkan',
                                                 'direct_cli_override': True,
                                                 'arguments': ['--gpu']},
                       'bgfx-vulkan-depth-gpu': {'tests': ['CoinRenderDepthContractVulkanTest'],
                                                 'category': 'GPU',
                                                 'source_category': 'gpu',
                                                 'variant': 'bgfx-vulkan',
                                                 'direct_cli_override': False},
                       'bgfx-opengl-depth-gpu': {'tests': ['CoinRenderDepthContractOpenGLTest'],
                                                 'category': 'GPU',
                                                 'source_category': 'gpu',
                                                 'variant': 'bgfx-opengl',
                                                 'direct_cli_override': False}},
     'historical_gate_runs': [],
     'rgb_comparisons': 196,
     'verification_ppm_files': 392,
     'verification_processes': 56,
     'all_rgb_identical': None,
     'implementation_notes': ['A ordem opaca já qualificada é emprestada em escopos locais de captura/submission '
                              'e lowering. Target deixa de copiar a ordem da captura elegível; o lowering usa '
                              'uma view imutável em vez de copiar a schedule. A prova fica vinculada ao frame, à '
                              'revisão e às opções de transparência da chamada atual.',
                              'O predicado de identidade é acumulado dentro da ordenação/classificação '
                              'existente. O perfil preserva o modo opaco com sortObject e ordem de traversal; '
                              'compositing fora do perfil mantém os vetores owned, validação e diagnósticos '
                              'literais. RTT/shadow/expansão e estados que exigem outra ordem continuam fora do '
                              'empréstimo.',
                              'computed_items descreve a ordem calculada localmente; mover esse vetor no Target '
                              'não conta como cópia. As views não cruzam a submissão backend/Rust nem tickets '
                              'assíncronos.'],
     'scope_notes': [],
     'ablation_notes': ['Na captura fria, o caminho literal copia a ordem no Target e no lowering; a opção ativa '
                        'empresta a mesma ordem admitida a ambos. Em RESOURCE_REBUILD, Target calcula a ordem e '
                        'move o vetor; somente o lowering faz a cópia literal. computed_items não representa uma '
                        'cópia.',
                        'copied_bytes e borrowed_bytes representam itens × sizeof(CoinRenderCompositionItem), '
                        'como transporte lógico. Não medem capacidade dos vetores, tráfego do alocador, RSS ou '
                        'memória GPU.',
                        'copy_ms do Target inclui copiar ou ativar/mover a ordem; copy_ms do lowering engloba a '
                        'chamada literal de preparação de schedule. qualify_ms desses dois scopes mede lookup. '
                        'composition_identity.qualify_ms é a classificação/sort existente inteiro quando a prova '
                        'é solicitada, não overhead puro da otimização.',
                        'A prova de uma observação Target e uma schedule por linha vale apenas para '
                        'static/transforms-10 elegíveis nesta cidade. Tentativas extras são retidas e tornam a '
                        'ablação incomparável. Em REUSE/CAMERA_PATCH, não se presume identity ou schedule por '
                        'quadro.',
                        'A métrica primária soma, por linha CSV antes de selecionar os quadros medidos, copy_ms '
                        'e qualify_ms dos scopes Target e schedule. São os quatro intervalos separados de '
                        'transferência/ativação/lookup instrumentados; composition_identity.qualify_ms fica '
                        'excluído. Ela não é a soma de medianas nem inclui a classificação inteira.'],
     'gates': [],
     'history_notes': [],
     'reviews': [],
     'limitations': ['O resumo descritivo de composition_identity agrega eventos da chamada de ordenação, '
                     'incluindo warmups; o timer marca o passe original inteiro quando solicitado. Não mede '
                     'overhead novo separado nem duração GPU.',
                     'Execuções diretas dos gates usam a definição CTest registrada e override CLI exato. '
                     'Composition --borrow/--gpu exigem markers; range memo e Depth --gpu não imprimem marker '
                     'próprio em sua versão atual, e são validados por modo/SHA/exit/ausência de skip.'],
     'evidence_reference': 'validation/composition-copy-linux/README.md',
     'composition_item_bytes': 56,
     'diagnostic_draws_expected': 40001}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--write-config-template", type=Path, help="Create metadata skeleton and exit; no result or gate pass is assumed")
    for name in ("analysis", "cold", "steady", "metadata", "output"):
        parser.add_argument("--" + name, type=Path)
    parser.add_argument("--figure-reference", default="validation/composition-copy-linux/composition-copy.png")
    args = parser.parse_args()
    if args.write_config_template:
        require(not args.write_config_template.exists(), "Config template destination already exists")
        args.write_config_template.write_text(json.dumps(config_template(), indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        print("Wrote configuration skeleton; replace sources/hashes and admit diagnostic contract only after observed trace/cardinality checks")
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
