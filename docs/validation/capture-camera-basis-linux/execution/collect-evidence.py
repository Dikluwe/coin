#!/usr/bin/env python3
"""Collect completed capture evidence; pixels and binaries stay outside Git."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
sys.dont_write_bytecode = True

root = Path('/tmp/coin-render-first-frame')
stage = root / 'docs/validation/capture-camera-basis-linux'
stage.mkdir(parents=True, exist_ok=False)
def read(path): return json.loads(Path(path).read_text())
def write(path, value): Path(path).write_text(json.dumps(value, indent=2, ensure_ascii=False)+'\n')
def copy(source, relative):
    target = stage / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)
for name in ('cold', 'steady'):
    shutil.copytree('/tmp/coin-render-capture-'+name+'-summary', stage/name)
for name in ('diagnostic', 'gates', 'gates-initial'):
    source = '/tmp/coin-render-capture-'+('ablation' if name == 'diagnostic' else name)
    shutil.copytree(source, stage/name)
shutil.copytree('/tmp/coin-render-capture-ablation-cli-initial', stage/'diagnostic-cli-initial')
for source, target in (
    ('/tmp/coin-render-capture-analysis/results.json', 'analysis.json'),
    ('/tmp/coin-render-capture-analysis.py', 'analyze-capture.py'),
    ('/tmp/coin-render-capture-diagnostics.py', 'diagnostic/derive-diagnostic.py'),
    ('/tmp/coin-render-capture-README.md', 'README.md'),
    ('/tmp/coin-render-capture-report.py', 'plot-and-report.py'),
    ('/tmp/coin-render-capture-validate-archive.py', 'validate-archive.py'),
    ('/tmp/coin-render-capture-baseline-binaries.json', 'binaries-before.json'),
    ('/tmp/coin-render-capture-final-binaries.json', 'binaries-after.json'),
    ('/tmp/coin-render-state-coingl-binaries.json', 'binaries-coingl.json'),
    ('/tmp/coin-render-capture-binary-hashes-post.json', 'binary-hashes-post-campaign.json'),
    ('/tmp/coin-render-capture-hardware-before.json', 'hardware-before.json'),
    ('/tmp/coin-render-capture-hardware-after.json', 'hardware-after.json'),
    ('/tmp/coin-render-capture-campaign-commands.json', 'campaign-commands.json'),
    ('/tmp/coin-render-capture-matched.py', 'execution/matched.py'),
    ('/tmp/coin-render-run-capture-matched.py', 'execution/run-matched.py'),
    ('/tmp/coin-render-capture-ablation.py', 'execution/run-ablation.py'),
    ('/tmp/coin-render-capture-gates.py', 'execution/run-gates.py'),
    ('/tmp/coin-render-freeze-capture.py', 'execution/freeze-baseline.py'),
    ('/tmp/coin-render-capture-collect.py', 'execution/collect-evidence.py'),
    ('/tmp/coin-render-capture-post.py', 'execution/post-campaign.py'),
    ('/tmp/coin-render-wgpu-motion-matched.py', 'execution/matched-rows.py'),
    (str(root/'scripts/coinrender/run_animation_benchmark.py'), 'execution/benchmark-runner.py'),
): copy(source, target)
for path in sorted(Path('/tmp').glob('coin-render-capture-*-run.log')):
    copy(path, 'orchestration/'+path.name)
for path in sorted(Path('/tmp').glob('coin-render-capture-build-*.log')):
    copy(path, 'builds/'+path.name)
revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
analysis = read(stage/'analysis.json')
assert all(x['complete_and_comparable'] for x in analysis['campaigns'].values())
assert all(x['complete_and_comparable'] for x in analysis['diagnostics'].values())
gates = read(stage/'gates/commands.json')
assert len(gates['commands']) == 8 and sum(len(c['ctest_definitions']) for c in gates['commands']) == 18
assert all(c['exit_code'] == 0 and not c['skip_observed'] for c in gates['commands'])
rgb = read(stage/'steady/rgb-before-after.json')
assert rgb['comparison_count'] == 196 and rgb['all_rgb_identical']
sources = {'bgfx-vulkan':'6182410f5789bbdc30d5ccd06fa341e1810aef8e',
    'bgfx-opengl':'6182410f5789bbdc30d5ccd06fa341e1810aef8e',
    'wgpu-vulkan':'199b0e02b4f0b74ffb5d042d98e2837aafc31ad8',
    'coingl':'4d63bb993022ee8d40802558b0871a4803002b8d'}
metadata = {
    'date':'2026-10-05', 'source_content_revision':revision,
    'production_change_revision':'89fbdd7b0474953d5c5134c8dfec1ac12b2497d3',
    'baseline_source_content_revision':None,
    'baseline_variant_source_content_revisions':sources,
    'coingl_source_content_revision':sources['coingl'],
    'scene_sha256':'bb9ccc612cd38ffb749ee599d9350a80974649eab5bf477d2f344538a42b2576',
    'expected_counts':{'cold':{'processes':63,'measured_frames':63,'warmup_frames':0},
       'steady':{'processes':105,'measured_frames':1575,'warmup_frames':525},
       'ablation':{'processes':18,'measured_frames':18,'warmup_frames':0}},
    'ablation_rounds':3, 'gate_counts':{'Core puro':2,'Action/Reuse misto':4,'GPU':12},
    'rgb_comparisons':196,'verification_ppm_files':392,'verification_processes':56,'all_rgb_identical':True,
    'scope_notes':['Cena de 40.000 objetos, 40.001 estados, 480.012 triângulos, PHONG e duas luzes direcionais; 1024 × 1024. Ryzen 5800H, RTX 3060 Laptop, driver NVIDIA 610.57.04, Linux 6.17.0-42; Release, GCC 13.3, C++11. Governador de CPU powersave. Snapshots de hardware antes/depois estão arquivados.'],
    'gates':[
       'Core: CoinRenderFrameCoreTest e CoinRenderPlanAssemblyCoreTest — 2 execuções.',
       'CoinRenderActionTest e CoinRenderFrameReuseCoreTest em wgpu e BGFX/Vulkan — 4 execuções mistas, incluindo integração com Target/GPU.',
       'CoinRenderCameraReuseReferenceTest e CoinRenderTransparencyTest em wgpu/Vulkan, BGFX/Vulkan e BGFX/OpenGL — 6 execuções GPU com referência GL exigida.',
       'wgpu: SceneTexture, SceneTextureDirect, MultiDevice e AsyncAction; BGFX: RttOwnership Vulkan/OpenGL — 6 execuções GPU.',
       'Histórico preservado: dois builds iniciais falharam no tipo SbPimplPtr do oracle. A primeira rodada de gates teve 4 execuções (3 passaram, 1 falhou): o oracle exigia revisão atual para a prova ancorada. Ambos os problemas foram corrigidos somente no teste; os 18 gates finais passaram sem skips.'],
    'reviews':[
       'Revisão independente do código: lente local à mesma chamada, dono/revisão/contagens/luzes conferidos, admissão estrita de objetos preservada, câmera lazy em recaptura apenas de objetos e rollback mantidos.',
       'Oracle com optout compara payloads, status, diagnóstico, admissão e pixels do backend mock; cobre perspectiva/ortográfica × PHONG/BASE_COLOR × recording/mock, roots A/B/A, campos ignorados/conectados, câmeras inválidas, recaptura, falha/retry, rollback, callbacks, path e planOnly.',
       'SHA-256 dos 20 arquivos de controle/binaries antes/depois/CoinGL foi conferido após toda a campanha.'],
    'history_notes':['A primeira invocação do diagnóstico usou animated-percent=0, valor que o CLI rejeita antes da renderização. O script foi ajustado para 10, como o static da campanha principal. Esta invocação sem quadro/CSV não integra os 18 processos finais de ablação; log e comando rejeitados permanecem em diagnostic-cli-initial.'],
    'limitations':[
       'Os 392 PPMs foram produzidos em 56 processos novos e os 196 pares RGB foram reabertos nesta etapa. O arquivo Git preserva métricas e hashes; não inclui os PPMs nem as bibliotecas.',
       'Outliers do primeiro quadro permanecem nas amostras. Nove processos por lado, na mesma sessão, não equivalem a nove reinicializações do driver ou do computador.',
       'O próximo custo de captura a investigar é a descoberta de cena repetida nos perfis de câmera e objetos. Estes dois perfis continuam executados conforme seus guards atuais; esta mudança remove apenas uma preparação duplicada da base.'],
    'evidence_reference':'validation/capture-camera-basis-linux/README.md'
}
write(stage/'stage-metadata.json',metadata)
print('Collected',stage)
