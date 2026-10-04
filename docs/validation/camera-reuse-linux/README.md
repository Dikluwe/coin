# Evidências de reutilização da câmera — Linux, 2026-10-04

Relatório: [coin-render-camera-reuse-linux.md](../../coin-render-camera-reuse-linux.md).
Protocolo: [coin-render-animation-benchmark.md](../../coin-render-animation-benchmark.md).
Referência: CoinGL, `SoGLRenderAction`, na RTX 3060 Laptop usada por BGFX/Vulkan,
BGFX/OpenGL e wgpu/Vulkan. O diagnóstico offscreen confirma NVIDIA, OpenGL 4.6,
driver 610.57.04 e pbuffer; os logs de janela também registram o adaptador.

## Fonte e contagens

Código medido: `4d63bb993022ee8d40802558b0871a4803002b8d`, branch
`codex/coin-render-transform-performance`. Os builds Release e testes GPU
foram serializados. O [report-summary.json](report-summary.json) recalcula as
estatísticas dos CSVs e identifica a fonte compilada separadamente da revisão
do checkout que executou o runner.

Foram **60 processos de timing**, **16.560 quadros medidos** e **1.740 warmup**:

| Campanha | Processos | Quadros medidos | Warmup |
|---|---:|---:|---:|
| Offscreen static/camera, 3 × 600 por variante/caso | 24 | 14.400 | 1.440 |
| Janela camera, 3 × 120 por variante | 12 | 1.440 | 180 |
| Transforms-10 anterior, 3 × 30 por variante | 12 | 360 | 60 |
| Transforms-10 atual, 3 × 30 por variante | 12 | 360 | 60 |

A verificação separada soma 12 processos e 84 imagens, sem warmup; as campanhas
finais totalizam 72 processos. Ablações, testes e pilotos de desenvolvimento são
controles separados. Os 72 processos históricos usados para comparação são
referenciados por hashes no arquivo anterior; seus CSVs não foram duplicados.

Os binários anteriores foram congelados antes das edições. O arquivo
`controls/coin-render-camera-release-controls/frozen-binaries.json` identifica
o conteúdo compilado `0cc3caffa7` e o HEAD na cópia `5bc466fe04`; os SHA-256
coincidem com a campanha histórica. O `source_revision` original do runner
transform-before identifica o checkout atual, pois o runner atual executou
esses binários anteriores. Esse valor foi preservado, com provenance explícita.

## Arquivos

- `offscreen/`, `window/`, `transform-before/`, `transform-after/`: comandos,
  ambiente, cena/binários, resultados, medians, logs e CSVs originais.
- `verify/`: logs, metadados de imagem, hashes, movimento e as 84 comparações
  RGB/estado. `report-image-evidence.json` recalcula a verificação.
- `verification-before-after-rgb.json`: 84 comparações entre o mesmo backend
  desta campanha e da anterior, nos mesmos estados lógicos.
- `camera-before-after.png`: medianas e p99 da câmera atual/histórica. Janela
  atual tem 120 quadros por processo e a histórica tem 600; os tempos medem a
  chamada CPU/render-present, com limites de apresentação registrados.
- `camera-rgb-comparison.png` e `images/`: montagem e quatro PNGs RGB sem perdas,
  1024², câmera no estado lógico 300.
- `controls/coin-render-camera-release-controls/`: builds, testes CPU/Rust/GPU,
  comandos GPU, prova por tracing dos 16 patches de cada variante, máquina e
  diagnóstico CoinGL. A consulta NVML inicial falhou dentro do sandbox; a
  consulta com permissão apropriada confirmou GPU/driver e foi preservada.
- `controls/coin-render-camera-release-ablation/`: nove processos com oito
  quadros e tracing, flags explícitas desligando overlay/patch, fora dos timings
  principais.
- `controls/coin-render-camera-transform-diagnostics/`: diagnóstico da regressão
  intermediária; seis processos curtos. `...transform-final-diagnostics/`
  compara fases wgpu depois das correções com os binários congelados.
- `controls/coin-render-transforms-*-pilot/`: pilotos antes, durante e depois
  da correção. Os atuais de desenvolvimento tinham edições ainda sem commit;
  seus `source_revision` não identificam todo esse conteúdo. Os resultados
  finais com fonte exata estão nas campanhas principais.
- `archive-manifest.json`: hashes dos arquivos copiados verbatim e referências
  ao histórico. `file-manifest.json` cobre todos os artefatos finais desta pasta.

Os 84 PPM completos permanecem em `/tmp/coin-render-camera-release-verify`.
O Git conserva seus hashes e métricas, mais os quatro PNGs representativos.
Recalcular todo o RGB requer os PPM originais ou regenerar a verificação com a
cena/revisão/ambiente registrados. Timers de verify incluem uma preparação sem
warmup e sofrem efeitos de captura/hashing/I/O; ficam fora das tabelas de timing.

## Recalcular estatísticas

As medianas, p95 e p99 são medianas das estatísticas por processo; os percentis
usam nearest rank. Máximos globais e contagens são calculados diretamente sobre
todos os quadros medidos. Os limites são estritos `>1000/60` e `>1000/30` ms.
Publication da janela é não aplicável. Warmup continua nos arquivos brutos.

O helper valida sequências, estatísticas do runner, somas dos timers, RSS e
digests, sem executar renderizadores. A validação dos timings versionados usa:

```sh
python3 docs/validation/camera-reuse-linux/summarize.py \
  --offscreen docs/validation/camera-reuse-linux/offscreen \
  --window docs/validation/camera-reuse-linux/window \
  --transform-before docs/validation/camera-reuse-linux/transform-before \
  --transform-after docs/validation/camera-reuse-linux/transform-after \
  --validate-only
```

Para gerar outro arquivo/figura, passe `--output /tmp/novo-resumo` em vez de
`--validate-only`. O helper consulta os dois baselines históricos em
`docs/validation/animation-linux` por padrão. NumPy/Pillow são necessários para
RGB e Matplotlib para a figura; validar somente timings usa a biblioteca padrão.
