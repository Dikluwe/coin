#!/usr/bin/env python3
"""Generate a compact geometry-overlay report and scientific PNG/SVG from evidence.

Read-only inputs: no benchmark, GPU, build, network or Git command is invoked.

CLI: --analysis RESULTS_JSON --summary REPORT_SUMMARY_JSON --metadata STAGE_JSON
     --output NEW_DIRECTORY_UNDER_TMP [--figure-reference RELATIVE_REFERENCE]

Library API, for archived validators:
  validate(metadata, analysis, summary) -> dict
  report(metadata, analysis, data, figure_ref) -> Markdown str

The analysis comes from coin-render-geometry-overlay-analysis.py; summary uses
coin-render-wgpu-motion-summary.py. Validation rejects missing or incomparable
results. Performance values and pass results are never manufactured from plans.
"""

import argparse
import hashlib
import json
import math
import statistics
from pathlib import Path


VARIANTS = ("coingl", "bgfx-vulkan", "bgfx-opengl", "wgpu-vulkan")
RENDER_VARIANTS = VARIANTS[1:]
VARIANT_LABELS = {"coingl": "Coin/OpenGL", "bgfx-vulkan": "BGFX/Vulkan",
                  "bgfx-opengl": "BGFX/OpenGL", "wgpu-vulkan": "wgpu/Vulkan"}
CASE_LABELS = {"transforms-10": "Transformações 10%", "materials-10": "Materiais 10%",
               "geometry-10": "Geometria 10%", "geometry-100": "Geometria 100%"}
CASE_ORDER = ("geometry-10", "geometry-100", "transforms-10", "materials-10")
COUNTS = ("processes", "measured_frames", "warmup_frames")
REPORT_NAME = "coin-render-geometry-overlay-linux.md"
FIGURE_NAME = "geometry-overlay"
OPTOUT = "COIN_RENDER_DISABLE_GEOMETRY_INTERVAL_VALIDATION"


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


EXPECTED_STEADY = dict(processes=84, measured_frames=1260, warmup_frames=420)
EXPECTED_ABLATION = dict(processes=36, measured_frames=252, warmup_frames=108)
SCOPE = "geometry_overlay_validation"
PHASE_KEY = SCOPE + ".validation_ms"
COUNTER_KEYS = tuple(SCOPE + "." + key for key in ("positions", "runs", "sorted_items", "scratch_bytes"))


def rgb_evidence(summary):
    record = summary.get("rgb_before_after")
    require(isinstance(record, dict), "Summary needs actual RGB verification evidence")
    results = record.get("results", [])
    count = record.get("comparison_count")
    require(type(count) is int and count == 112 and len(results) == count,
            "Expected 112 individual RGB comparisons")
    identities = [(item["case"], item["variant"], item.get("round"), item["logical_frame"]) for item in results]
    require(len(set(identities)) == len(identities), "Duplicate RGB identities")
    require({item["case"] for item in results} == set(CASE_ORDER) and
            {item["variant"] for item in results} == set(VARIANTS), "RGB case/API coverage differs")
    exact = all(item.get("pixels_different") == 0 and item.get("max_channel_error") == 0 and item.get("rgb_mae") == 0
                and item["before"].get("ppm_sha256") == item["after"].get("ppm_sha256") for item in results)
    require(type(record.get("all_rgb_identical")) is bool and record["all_rgb_identical"] == exact,
            "RGB identical claim differs from individual metrics/hashes")
    return dict(comparisons=count, ppm_files=count * 2, identical=exact,
                recomputed=record.get("rgb_metrics_recomputed_this_invocation") is True)


def process_values(campaign, side, case, variant):
    values = [process["recomputed_stats"]["total_ms"]["median_ms"]
              for process in campaign[side]["processes"].values()
              if process["case"] == case and process["variant"] == variant]
    require(len(values) == 3 and all(finite(value) for value in values),
            f"Expected three finite process medians: {side}/{case}/{variant}")
    return values


def validate_focused_probe(metadata):
    """Validate a separate same-build probe supplied by the collector."""
    probe = metadata.get("focused_probe")
    if probe is None:
        return None
    require(isinstance(probe, dict) and probe.get("complete_and_comparable") is True,
            "Focused probe must contain complete, comparable actual results")
    require(probe.get("source_content_revision") == metadata["source_content_revision"] and
            probe.get("scene_sha256") == metadata["scene_sha256"], "Focused probe source/scene differs")
    parameters = probe.get("parameters", {})
    require(parameters.get("variant") == "bgfx-vulkan" and parameters.get("case") == "geometry-10" and
            parameters.get("trace") is False and all(parameters.get(key) == value for key, value in
            (("rounds", 3), ("warmup", 5), ("frames", 15))), "Focused probe must be BGV geometry10, without trace, 3/5/15")
    require(probe.get("unique_counts") == dict(processes=6, measured_frames=90, warmup_frames=30),
            "Focused probe counts must be separate 6/90/30")
    summary = probe.get("summary", {})
    require(summary.get("complete_and_comparable") is True and set(summary.get("groups", {})) == {"off", "on"},
            "Focused probe summary incomplete")
    pair = summary.get("comparisons", {}).get("total_ms", {})
    for mode in ("off", "on"):
        group = summary["groups"][mode]
        values = group.get("process_median_values_ms", {}).get("total_ms", [])
        ranges = group.get("process_median_range_ms", {}).get("total_ms", {})
        require(group.get("processes") == 3 and group.get("rounds") == [1, 2, 3] and
                len(values) == 3 and all(finite(value) for value in values), "Focused probe needs three process medians per option")
        median = statistics.median(values)
        require(group.get("stats", {}).get("total_ms", {}).get("median_ms") == median and pair.get(mode) == median,
                "Focused probe median differs from individual process values")
        require(ranges == dict(minimum=min(values), maximum=max(values)), "Focused probe process ranges differ")
    expected_delta = pair["on"] - pair["off"]
    expected_percent = (pair["on"] / pair["off"] - 1) * 100 if pair["off"] else None
    require(finite(pair.get("delta_ms")) and math.isclose(pair["delta_ms"], expected_delta, rel_tol=1e-10, abs_tol=1e-10),
            "Focused probe delta differs")
    require(pair.get("change_percent") == expected_percent, "Focused probe percent differs")
    rounds = summary.get("pairs", [])
    require(len(rounds) == 3 and {item.get("round") for item in rounds} == {1, 2, 3} and
            all(item.get("complete_and_comparable") is True for item in rounds), "Focused probe rounds incomplete")
    for item in rounds:
        round_pair = item.get("stats", {}).get("total_ms", {}).get("median_ms", {})
        index = item["round"] - 1
        values = {mode: summary["groups"][mode]["process_median_values_ms"]["total_ms"][index] for mode in ("off", "on")}
        require(all(round_pair.get(mode) == values[mode] for mode in ("off", "on")), "Focused probe pair/process values differ")
        delta = values["on"] - values["off"]
        percent = (values["on"] / values["off"] - 1) * 100 if values["off"] else None
        require(round_pair.get("delta_ms") == delta and round_pair.get("change_percent") == percent, "Focused probe round delta differs")
    require(probe.get("evidence_reference"), "Focused probe needs an archived evidence reference")
    return probe


def validate(metadata, analysis, summary):
    """Check the single steady campaign, diagnostic alignment and RGB evidence.

    This does not rerun raw CSV/PPM analysis or gates. The archive validator
    independently recomputes those artifacts and checks logs/build identities.
    """
    for key in ("source_content_revision", "coingl_source_content_revision", "baseline_variant_source_content_revisions", "scene_sha256"):
        require(metadata.get(key), "Required metadata missing: " + key)
    sources = metadata["baseline_variant_source_content_revisions"]
    require(isinstance(sources, dict) and all(sources.get(variant) for variant in VARIANTS), "Supply a complete baseline source map")
    require(sources["coingl"] == metadata["coingl_source_content_revision"], "Baseline CoinGL source differs")
    if metadata.get("baseline_source_content_revision"):
        require(all(sources[variant] == metadata["baseline_source_content_revision"] for variant in RENDER_VARIANTS),
                "Global baseline revision differs from per-variant sources")
    require(set(analysis.get("campaigns", {})) == {"steady"}, "Expected exactly one steady campaign")
    campaign = analysis["campaigns"]["steady"]
    require(campaign.get("complete_and_comparable") is True, f"Incomparable campaign: {campaign.get('comparability_issues')}")
    expected = metadata.get("expected_counts", {})
    require(expected.get("steady") == EXPECTED_STEADY and campaign.get("unique_counts") == EXPECTED_STEADY,
            "Steady actual/metadata counts must be 84 processes, 1260 measured, 420 warmup")
    require(analysis.get("unique_counts") == EXPECTED_STEADY, "Analysis aggregate counts differ")
    require(set(campaign.get("comparisons", {})) == {case + "|" + variant for case in CASE_ORDER for variant in VARIANTS},
            "Expected four cases and four variants")
    other = summary_rows(summary)
    require(set(other) == {(case, variant) for case in CASE_ORDER for variant in VARIANTS}, "Independent summary coverage differs")
    for row in campaign["comparisons"].values():
        key = row["case"], row["variant"]
        require(row.get("rounds_match") is True and not row.get("limitations") and
                row.get("before_processes") == row.get("after_processes") == 3, "Incomplete row: " + str(key))
        require(other[key].get("same_recorded_protocol") is True, "Summary protocol differs: " + str(key))
        for side in ("before", "after"):
            value, independently = statistic(row)[side], other[key]["metrics"]["total_median_ms"][side]
            require(finite(value) and finite(independently) and math.isclose(value, independently, rel_tol=1e-10, abs_tol=1e-10),
                    "Independent median differs: " + str(key) + "/" + side)
            process_values(campaign, side, *key)
        if key[1] == "coingl":
            require(all(pair["before"] == pair["after"] for stats in row["stats"].values() for pair in stats.values()),
                    "Shared CoinGL statistics differ")
            require(all(pair["before"] == pair["after"] for pair in other[key]["metrics"].values()), "Shared CoinGL summary differs")
            for field in ("first_ms", "first_total_ms", "result_since_main_ms", "peak_rss_kib", "throughput_fps"):
                if field in row:
                    require(row[field]["before"] == row[field]["after"], "Shared CoinGL " + field + " differs")
    shared = campaign.get("shared_controls", [])
    require(len(shared) == 12 and all(item.get("counted_once") is True and item.get("csv_sha256_equal") is True
                                    and item.get("log_sha256_equal") is True for item in shared), "Twelve shared CoinGL controls are not proven")
    for side in ("before", "after"):
        manifest = campaign[side]["metadata"]
        parameters = manifest.get("parameters", {})
        require(all(int(parameters.get(key, -1)) == value for key, value in (("rounds", 3), ("warmup", 5), ("frames", 15))),
                "Steady protocol differs from 3 rounds, 5 warmups, 15 measured frames")
        require(set(str(parameters.get("cases", "")).split(",")) == set(CASE_ORDER), "Manifest cases differ")
        require(manifest.get("scene_sha256") == metadata["scene_sha256"], "Scene SHA256 differs")
        revisions = manifest.get("variant_source_content_revisions", {})
        for variant in VARIANTS:
            source = sources[variant] if side == "before" else (
                metadata["coingl_source_content_revision"] if variant == "coingl" else metadata["source_content_revision"])
            require(revisions.get(variant) == source, f"Source differs: {side}/{variant}")
    contract = metadata.get("diagnostic_contract", {})
    require(contract.get("configured") is True and contract.get("optout") == OPTOUT and contract.get("scope") == SCOPE,
            "Geometry diagnostic contract must be admitted after raw observations")
    require(set(contract.get("variants", [])) == set(RENDER_VARIANTS) and contract.get("cases") == ["geometry-10", "geometry-100"],
            "Diagnostic API/case coverage differs")
    require(all(contract.get(key) == value for key, value in (("rounds", 3), ("warmup", 3), ("frames", 7))) and
            contract.get("alignment") == "validation event belongs to next action marker", "Diagnostic protocol differs")
    groups = diagnostic_groups(analysis)
    require(len(groups) == 6 and {(item["variant"], item["case"]) for item in groups} ==
            {(variant, case) for variant in RENDER_VARIANTS for case in contract["cases"]}, "Diagnostic groups differ")
    diagnostic_counts = {key: sum(value["unique_counts"][key] for value in analysis["diagnostics"].values()) for key in COUNTS}
    require(expected.get("ablation") == EXPECTED_ABLATION and diagnostic_counts == EXPECTED_ABLATION,
            "Diagnostic counts differ from 36 processes, 252 measured, 108 warmup")
    for diagnostic in analysis["diagnostics"].values():
        require(diagnostic.get("contract") == contract and diagnostic.get("complete_and_comparable") is True and
                diagnostic.get("contract_observation_passed") is True, "Diagnostic proof/contract failed")
        require(diagnostic.get("unique_counts") == diagnostic.get("expected_unique_counts"), "Diagnostic actual counts differ")
        raw = diagnostic["metadata_and_raw_trace"]
        require(raw["command_metadata"].get("source_content_revision") == metadata["source_content_revision"] and
                raw["command_metadata"].get("scene_sha256") == metadata["scene_sha256"], "Diagnostic source/scene differs")
        for stem, run in raw["runs"].items():
            require(run.get("geometry_contract_passed") is True and run.get("geometry_validation_alignment", {}).get("passed") is True,
                    "Per-process geometry proof failed: " + stem)
            require(run["samples"].get("measured_row_indices") == list(range(3, 10)), "Diagnostic measured selection differs")
    for group in groups:
        require(group["comparison"].get("complete_and_comparable") is True, "Incomparable on/off group")
        for mode in ("on", "off"):
            require(group[mode].get("processes") == 3, "Ablation needs three processes per option")
            phase_values = group[mode].get("phase_measured_medians_ms_process_values", {}).get(PHASE_KEY, [])
            require(len(phase_values) == 3 and all(finite(value) for value in phase_values), "Missing diagnostic process phase medians")
        for category, keys in (("phase_measured_medians_ms", (PHASE_KEY,)), ("counter_measured_medians", COUNTER_KEYS),
                               ("csv_measured_medians_ms", ("total_ms",))):
            for key in keys:
                pair = diagnostic_pair(group, category, key)
                require(pair and all(finite(pair.get(side)) for side in ("before", "after")), "Missing diagnostic metric: " + key)
    rgb = rgb_evidence(summary)
    for key, actual in (("rgb_comparisons", rgb["comparisons"]), ("verification_ppm_files", rgb["ppm_files"]),
                        ("all_rgb_identical", rgb["identical"]), ("verification_processes", 32)):
        require(metadata.get(key) == actual, "Metadata differs from verification evidence: " + key)
    gates = metadata.get("gate_counts", {})
    require(gates and all(type(value) is int and value >= 0 for value in gates.values()) and sum(gates.values()) == 12,
            "Supply actual gate_counts totalling twelve executions")
    require(metadata.get("build_count") == 2, "Supply actual build_count=2")
    require(notes(metadata, "implementation_notes"), "Supply source-reviewed implementation notes")
    return dict(campaign=campaign, groups=groups, diagnostic_counts=diagnostic_counts, rgb=rgb,
                focused_probe=validate_focused_probe(metadata))


def range_text(campaign, side, case, variant):
    low, high = process_total_range(campaign, side, case, variant)
    return number(low) + " .. " + number(high)


def report(metadata, analysis, data, figure_ref):
    """Return Markdown; values come exclusively from validated evidence."""
    campaign, groups, rgb = (data[key] for key in ("campaign", "groups", "rgb"))
    title = metadata.get("title", "CoinRender: overlay de geometria — Linux")
    if metadata.get("date"):
        title += ", " + metadata["date"]
    lines = ["# " + title, "", "Referência principal: Coin3D/Coin/OpenGL clássico (`SoGLRenderAction`).", "",
             "## Mudança", ""]
    paragraphs(lines, notes(metadata, "implementation_notes"))
    lines.extend([f"Optout literal: `{OPTOUT}=1`.", ""])
    paragraphs(lines, notes(metadata, "scope_notes"))
    lines.extend(["## Quadros após warmup", "",
                  "Mediana das medianas de três processos/rodadas por variante e caso. Cada processo tem cinco warmups excluídos e quinze quadros medidos. Total inclui update, render/readback e publicação; Δ% positivo significa mais tempo.", "",
                  "| Caso | Coin/OpenGL (ms) | BGFX/Vulkan antes → depois (ms; Δ%) | BGFX/OpenGL antes → depois (ms; Δ%) | wgpu/Vulkan antes → depois (ms; Δ%) |",
                  "|---|---:|---:|---:|---:|"])
    for case in CASE_ORDER:
        cells = [CASE_LABELS[case], number(statistic(row_at(campaign, case, "coingl"))["before"])]
        cells += [pair_text(statistic(row_at(campaign, case, variant))) + "; " + percent(statistic(row_at(campaign, case, variant))) for variant in RENDER_VARIANTS]
        lines.append("| " + " | ".join(cells) + " |")
    lines.extend(["", "### Faixa das medianas por processo", "",
                  "Mínimo .. máximo das três medianas por processo, sem remover extremos. Estas faixas não são intervalos de confiança.", "",
                  "| Caso | Coin/OpenGL (ms) | BGFX/Vulkan antes → depois (ms) | BGFX/OpenGL antes → depois (ms) | wgpu/Vulkan antes → depois (ms) |",
                  "|---|---:|---:|---:|---:|"])
    for case in CASE_ORDER:
        cells = [CASE_LABELS[case], range_text(campaign, "before", case, "coingl")]
        cells += [range_text(campaign, "before", case, variant) + " → " + range_text(campaign, "after", case, variant) for variant in RENDER_VARIANTS]
        lines.append("| " + " | ".join(cells) + " |")
    lines.extend(["", "### Aumentos observados", ""])
    increases = [f"- {VARIANT_LABELS[variant]} · {CASE_LABELS[case]}: {pair_text(pair)} ms; {delta_text(pair)} ms ({percent(pair)})."
                 for case in CASE_ORDER for variant in RENDER_VARIANTS
                 for pair in (statistic(row_at(campaign, case, variant)),) if change(pair) > 0]
    lines.extend(increases or ["Nenhuma mediana total dos três backends aumentou nesta amostra."])
    lines.extend(["", f"![Totais, faixa das rodadas e ablação]({figure_ref})", "",
                  "## Ablação no mesmo binário", "",
                  "Opção desligada → ligada; três processos por API/caso/opção, três warmups e sete quadros medidos por processo. O controle é o caminho literal no mesmo binário.", "",
                  "| Variante | Caso | Validação off → on (ms; Δ%) | Total off → on (ms; Δ%) | Posições | Runs off → on | Itens ordenados off → on | Scratch off → on (bytes) |",
                  "|---|---|---:|---:|---:|---:|---:|---:|"])
    for group in groups:
        phase = diagnostic_pair(group, "phase_measured_medians_ms", PHASE_KEY)
        total = diagnostic_pair(group, "csv_measured_medians_ms", "total_ms")
        counters = [diagnostic_pair(group, "counter_measured_medians", key) for key in COUNTER_KEYS]
        cells = [VARIANT_LABELS[group["variant"]], CASE_LABELS[group["case"]], pair_text(phase, 3) + "; " + percent(phase),
                 pair_text(total) + "; " + percent(total), pair_text(counters[0], counter=True)]
        cells += [pair_text(pair, counter=True) for pair in counters[1:]]
        lines.append("| " + " | ".join(cells) + " |")
    lines.extend(["", "`validation_ms` inclui checagem de posições, preparação do undo, unicidade e liberação do scratch. Exclui verificações posteriores de draws/modelos e não mede o overlay inteiro, memcpy isolado ou sort puro. `scratch_bytes` mede capacidade do scratch de colisão; exclui undo obrigatório e overhead do alocador.", "",
                  "O vínculo com CSV usa eventos anteriores ao próximo marcador `action`: primeiro full rebuild sem evento e cada resource rebuild posterior com exatamente um evento. Os índices medidos 3..9 só são usados após esta prova. Não há deslocamento inferido apenas pela contagem.", ""])
    increases = [f"- {VARIANT_LABELS[group['variant']]} · {CASE_LABELS[group['case']]}: total {pair_text(pair)} ms ({percent(pair)})."
                 for group in groups for pair in (diagnostic_pair(group, "csv_measured_medians_ms", "total_ms"),) if change(pair) > 0]
    if increases:
        lines.extend(["Aumentos totais na ablação:", ""] + increases + [""])
    paragraphs(lines, notes(metadata, "ablation_notes"))
    phase_changes = [change(diagnostic_pair(group, "phase_measured_medians_ms", PHASE_KEY)) for group in groups]
    extremes = "; ".join(f"{VARIANT_LABELS[variant]} · {CASE_LABELS[case]} {percent(statistic(row_at(campaign, case, variant)))}"
                         for case, variant in (("geometry-10", "bgfx-vulkan"), ("geometry-10", "bgfx-opengl"), ("geometry-100", "wgpu-vulkan")))
    lines.extend([f"A fase local variou de {min(phase_changes):+.2f}% a {max(phase_changes):+.2f}% na ablação. As variações do total na campanha principal — {extremes} — permanecem registradas e não são atribuídas causalmente à otimização desta fase.", "",
                  "A redução da fase não explica automaticamente toda mudança no total. Fases são aninhadas e suas medianas não devem ser somadas.", ""])
    probe = data.get("focused_probe")
    if probe is not None:
        probe_summary = probe["summary"]
        pair = probe_summary["comparisons"]["total_ms"]
        lines.extend(["## Probe suplementar: BGFX/Vulkan · geometria 10%", "",
                      "Comparação on/off no mesmo binário, sem trace de fases, com três processos por opção. São seis processos adicionais, 90 quadros medidos e 30 warmups. A campanha principal mantém suas 84 execuções, contabilizadas separadamente.", "",
                      "| Opção | Mediana total (ms) | Mínimo .. máximo das três medianas (ms) |",
                      "|---|---:|---:|"])
        for mode, label in (("off", "Literal (off)"), ("on", "Ativa (on)")):
            limits = probe_summary["groups"][mode]["process_median_range_ms"]["total_ms"]
            lines.append(f"| {label} | {number(pair[mode])} | {number(limits['minimum'])} .. {number(limits['maximum'])} |")
        percent_text = "n/d" if pair["change_percent"] is None else f"{pair['change_percent']:+.2f}%"
        main = statistic(row_at(campaign, "geometry-10", "bgfx-vulkan"))
        lines.extend(["", f"Total on − off: {pair['delta_ms']:+.2f} ms ({percent_text}). A mudança {percent(main)} da campanha principal continua na tabela e figura. Este probe separado descreve somente esta amostra e não identifica a causa da discrepância nem mede validation_ms.", "",
                      "| Rodada | Total off → on (ms) | Δ (ms) | Variação |",
                      "|---|---:|---:|---:|"])
        for item in sorted(probe_summary["pairs"], key=lambda item: item["round"]):
            row = item["stats"]["total_ms"]["median_ms"]
            pct = "n/d" if row["change_percent"] is None else f"{row['change_percent']:+.2f}%"
            lines.append(f"| {item['round']} | {number(row['off'])} → {number(row['on'])} | {row['delta_ms']:+.2f} | {pct} |")
        lines.extend(["", "Os pares que aumentaram o tempo permanecem nesta tabela; a mediana agregada não estabelece ausência universal de regressão.", "",
                      f"[Comandos, CSVs, logs e resumo do probe]({probe['evidence_reference']}).", ""])
    lines.extend([
                  "## Protocolo e fontes", "",
                  "- Campanha pareada: **84 processos únicos, 1.260 quadros medidos e 420 warmups**. São quatro casos × três rodadas × sete papéis: três backends antes, três depois e um Coin/OpenGL compartilhado.",
                  "- Ablação: **36 processos, 252 quadros medidos e 108 warmups**.",
                  "- Verificação: **32 processos, 224 PPMs e 112 pares antes/depois**. Cada par usa a mesma variante, caso e quadro; não compara imagens entre backends.",
                  "- Dois builds e doze execuções de gates registrados. Categorias: " + ", ".join(f"{escape(key)}={value}" for key, value in metadata["gate_counts"].items()) + ". Os resultados vêm dos registros preservados; não se presume teste GPU a partir de saída CPU ou duração.", "",
                  f"CoinRender depois: `{metadata['source_content_revision']}`. Coin/OpenGL: `{metadata['coingl_source_content_revision']}`.", ""])
    for variant in RENDER_VARIANTS:
        lines.append(f"- Antes {VARIANT_LABELS[variant]}: `{metadata['baseline_variant_source_content_revisions'][variant]}`.")
    lines.append("")
    if metadata.get("baseline_source_snapshot_revision"):
        lines.extend([f"Snapshot da baseline: `{metadata['baseline_source_snapshot_revision']}`; o código de render medido é identificado pelas revisões dos binários acima.", ""])
    lines.extend([f"Cena SHA-256: `{metadata['scene_sha256']}`.", "",
                  "Os doze controles Coin/OpenGL têm CSV/log iguais por SHA-256 e são contados uma vez, embora estejam presentes nos dois diretórios. O primeiro quadro destes processos pertence ao warmup; esta etapa não é uma campanha de primeiro quadro frio.", "",
                  "RGB antes/depois: " + ("todos os 112 pares são idênticos por bytes." if rgb["identical"] else "há diferenças preservadas nos resultados individuais; não se declara equivalência visual."), "",
                  "Este gerador lê os resultados RGB fornecidos e não reabre os PPMs. A reprodução do analisador/validador é a evidência da comparação de pixels.", ""])
    for key in ("gates", "builds", "history_notes", "reviews"):
        values = notes(metadata, key)
        if values:
            lines.extend("- " + value for value in values)
            lines.append("")
    lines.extend(["## Limites", "",
                  "- A medição offscreen inclui espera GPU/readback; não isola tempo GPU, apresentação em janela ou fluidez percebida.",
                  "- N=3 processos e quinze quadros medidos por processo descrevem esta amostra. Percentis p95/p99 preservados usam nearest rank; amostras curtas não caracterizam caudas de latência ou variância populacional.",
                  "- A alternância e os controles compartilhados não garantem clocks, temperatura ou estado do driver iguais.",
                  "- Todos os aumentos das medianas totais permanecem no relatório. Ganhos locais de validação e ganhos totais têm escopos diferentes."])
    lines.extend("- " + value for value in notes(metadata, "limitations"))
    if metadata.get("evidence_reference"):
        lines.extend(["", f"[Evidência e reprodução]({metadata['evidence_reference']})."])
    lines.extend(["", "Entradas e SHA-256 do gerador estão em `report-inputs.json`; os valores são lidos da evidência.", ""])
    return "\n".join(lines)


def create_figure(metadata, analysis, data, output):
    """Plot process medians with min/max ranges; never pool measured frames."""
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import numpy as np
    from matplotlib.colors import TwoSlopeNorm
    from matplotlib.patches import Patch
    plt.rcParams.update({"font.size": 10, "svg.fonttype": "none"})
    figure, axes = plt.subplots(3, 2, figsize=(15.5, 13), gridspec_kw={"height_ratios": [1, 1, 1.24]})
    campaign = data["campaign"]
    colors = {"before": "#8995a5", "after": "#276baa", "control": "#27836d"}

    def bar(ax, index, value, values, role, offset, label=None):
        low, high = min(values), max(values)
        require(low <= value <= high, "Median outside process range")
        ax.barh(index + offset, value, height=.29, color=colors[role], label=label)
        ax.errorbar(value, index + offset, xerr=[[value-low], [high-value]], fmt="none",
                    color="#303844", capsize=3, elinewidth=.8)
        ax.plot(values, [index + offset] * len(values), ".", color="#303844", markersize=3, alpha=.7)
        ax.annotate(f"{value:.2f}", (high, index + offset), xytext=(4, 0), textcoords="offset points", va="center", fontsize=9)
        return high

    def finish(ax, title, labels, maximum, unit):
        ax.set_title(title, pad=9)
        ax.set_yticks(range(len(labels)), labels)
        ax.invert_yaxis()
        ax.set_xlim(0, maximum * 1.23 if maximum > 0 else 1)
        ax.set_xlabel(unit)
        ax.grid(axis="x", alpha=.2)
        ax.set_axisbelow(True)
        for side in ("top", "right"):
            ax.spines[side].set_visible(False)

    for case, ax in zip(CASE_ORDER, axes[:2].flatten()):
        maximum = 0
        for index, variant in enumerate(VARIANTS):
            pair = statistic(row_at(campaign, case, variant))
            if variant == "coingl":
                maximum = max(maximum, bar(ax, index, pair["before"], process_values(campaign, "before", case, variant), "control", 0))
            else:
                for side, offset in (("before", -.17), ("after", .17)):
                    maximum = max(maximum, bar(ax, index, pair[side], process_values(campaign, side, case, variant), side, offset))
        finish(ax, CASE_LABELS[case] + " · total após warmup", [VARIANT_LABELS[variant] for variant in VARIANTS], maximum,
               "ms · hastes: mínimo .. máximo das 3 medianas")
    groups = data["groups"]
    ax = axes[2, 0]
    maximum = 0
    for index, group in enumerate(groups):
        pair = diagnostic_pair(group, "phase_measured_medians_ms", PHASE_KEY)
        for side, mode, offset in (("before", "off", -.17), ("after", "on", .17)):
            values = group[mode]["phase_measured_medians_ms_process_values"][PHASE_KEY]
            maximum = max(maximum, bar(ax, index, pair[side], values, side, offset))
    finish(ax, "Ablação · validação de geometria · literal → ativa",
           [VARIANT_LABELS[group["variant"]] + " · " + CASE_LABELS[group["case"]] for group in groups], maximum,
           "ms · posições + undo + unicidade + cleanup")
    values = np.array([[change(statistic(row_at(campaign, case, variant))) for variant in RENDER_VARIANTS] for case in CASE_ORDER])
    span = max(.05, float(np.abs(values).max()))
    ax = axes[2, 1]
    image = ax.imshow(values, cmap="RdYlGn_r", norm=TwoSlopeNorm(vmin=-span, vcenter=0, vmax=span), aspect="auto")
    ax.set_xticks(range(len(RENDER_VARIANTS)), [VARIANT_LABELS[variant] for variant in RENDER_VARIANTS])
    ax.set_yticks(range(len(CASE_ORDER)), [CASE_LABELS[case] for case in CASE_ORDER])
    ax.set_title("Variação do total · depois / antes − 1", pad=9)
    for row in range(values.shape[0]):
        for column in range(values.shape[1]):
            ax.text(column, row, f"{values[row, column]:+.2f}%", ha="center", va="center", fontsize=11,
                    color="white" if abs(values[row, column]) > .65 * span else "#17212b")
    figure.colorbar(image, ax=ax, shrink=.7, label="% · positivo = mais tempo")
    figure.legend(handles=[Patch(color=colors["before"], label="Antes / literal na ablação"),
                           Patch(color=colors["after"], label="Depois / ativa na ablação"),
                           Patch(color=colors["control"], label="Coin/OpenGL compartilhado")],
                  loc="upper center", ncol=3, bbox_to_anchor=(.5, .959), frameon=False)
    figure.suptitle("CoinRender · overlay de geometria · Linux", y=.992, fontsize=16)
    figure.text(.5, .012, "Medianas por processo; N=3. Warmups excluídos. Faixas descritivas, sem inferência de caudas ou duração GPU isolada.",
                ha="center", fontsize=10)
    figure.tight_layout(rect=(0, .035, 1, .94), h_pad=2.3, w_pad=2.2)
    for extension in ("png", "svg"):
        figure.savefig(output / (FIGURE_NAME + "." + extension), dpi=180, bbox_inches="tight")
    plt.close(figure)


def config_template():
    """Provide source/protocol facts; results remain pending until observed."""
    baseline = "9a594fa39c7ca7924f9931863d4bdd7a37165cee"
    control = "4d63bb993022ee8d40802558b0871a4803002b8d"
    return {
        "title": "CoinRender: overlay de geometria — Linux", "date": None,
        "source_content_revision": "96e5ed80fa3ab3c9016cf10e6bbfc9b638335a33",
        "baseline_source_content_revision": baseline,
        "baseline_source_snapshot_revision": "a2e19d360d",
        "baseline_variant_source_content_revisions": {variant: control if variant == "coingl" else baseline for variant in VARIANTS},
        "coingl_source_content_revision": control, "scene_sha256": None,
        "expected_counts": {"steady": EXPECTED_STEADY.copy(), "ablation": EXPECTED_ABLATION.copy()},
        "diagnostic_contract": {
            "schema_version": 1, "configured": False, "optout": OPTOUT, "scope": SCOPE,
            "variants": ["wgpu-vulkan", "bgfx-vulkan", "bgfx-opengl"], "cases": ["geometry-10", "geometry-100"],
            "rounds": 3, "warmup": 3, "frames": 7, "alignment": "validation event belongs to next action marker",
            "limitations": ["validation_ms includes position validation, undo preparation, uniqueness and scratch cleanup; not isolated sort time",
                            "scratch_bytes is collision scratch capacity, excluding mandatory undo and allocator overhead",
                            "city profile: first full capture without event; exactly one validation before each later resource_rebuild action",
                            "on-path city requires ordered or intervals; literal fallback stays raw and blocks optimization-route proof"]},
        "gate_counts": {}, "build_count": None, "gates": [], "builds": [], "focused_probe": None,
        "rgb_comparisons": 112, "verification_ppm_files": 224, "verification_processes": 32, "all_rgb_identical": None,
        "implementation_notes": [
            "A validação de unicidade de posições reconhece ordem estritamente crescente sem scratch. Na primeira inversão, reúne runs inclusivos; ordena os intervalos e recusa interseções. A ordem dos updates e do undo permanece a mesma.",
            "O scratch opcional é limitado a min(65.536, N/2) intervalos e no máximo 4×N bytes. Saturação ou falha opcional de alocação libera o scratch e usa slots + sort literal. Não há inferência sobre tipo de nó, revisão ou geometria mutável."],
        "scope_notes": [], "ablation_notes": [], "history_notes": [], "reviews": [], "limitations": [],
        "evidence_reference": "validation/geometry-overlay-linux/README.md"}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--write-config-template", type=Path, help="Write a pending metadata skeleton only")
    for name in ("analysis", "summary", "metadata", "output"):
        parser.add_argument("--" + name, type=Path)
    parser.add_argument("--figure-reference", default="validation/geometry-overlay-linux/geometry-overlay.png")
    args = parser.parse_args()
    if args.write_config_template:
        destination = args.write_config_template.resolve()
        require(destination.is_relative_to(Path("/tmp")) and not destination.exists(), "Use a new template file under /tmp")
        destination.write_text(json.dumps(config_template(), indent=2, ensure_ascii=False, allow_nan=False) + "\n", encoding="utf-8")
        print("Wrote pending configuration; no gate/performance/RGB pass is assumed")
        return
    for name in ("analysis", "summary", "metadata", "output"):
        if getattr(args, name) is None:
            parser.error("Required argument: --" + name)
    loaded, inputs = {}, {}
    for name in ("analysis", "summary", "metadata"):
        loaded[name], inputs[name] = read_json(getattr(args, name))
    data = validate(loaded["metadata"], loaded["analysis"], loaded["summary"])
    markdown = report(loaded["metadata"], loaded["analysis"], data, args.figure_reference)
    output = args.output.resolve()
    require(output.is_relative_to(Path("/tmp")) and output != Path("/tmp") and not output.exists(), "Use a new directory under /tmp; preserve previous evidence")
    require(all(not getattr(args, name).resolve().is_relative_to(output) for name in ("analysis", "summary", "metadata")), "Output would contain an input")
    output.mkdir(parents=True)
    create_figure(loaded["metadata"], loaded["analysis"], data, output)
    (output / REPORT_NAME).write_text(markdown, encoding="utf-8")
    script = Path(__file__).resolve()
    provenance = {"inputs": inputs, "generator": {"path": str(script), "sha256": hashlib.sha256(script.read_bytes()).hexdigest()},
                  "outputs": {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in sorted(output.iterdir()) if path.is_file()}}
    (output / "report-inputs.json").write_text(json.dumps(provenance, indent=2, ensure_ascii=False, allow_nan=False) + "\n", encoding="utf-8")
    print(f"Wrote {output / REPORT_NAME}, PNG/SVG and input provenance; values read from evidence")


if __name__ == "__main__":
    main()
