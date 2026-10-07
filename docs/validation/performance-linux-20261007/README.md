# Evidência local de P17/P18/P19 — 2026-10-07

[Relatório, resultados e limites](../../coin-render-performance-continuation-linux.md).
Host Linux/Ryzen 7 5800H/RTX 3060 Laptop `10de:2560`, NVIDIA 610.57.04,
512², city com 40.000 prédios e chão. O fechamento não qualifica outras GPUs,
Windows, latência de exibição ou throughput GPU equivalente entre APIs.

## Campanha qualificada

- `measure/offscreen` e `measure/window`: 126 processos por alvo, sete variantes,
  seis casos, três repetições intercaladas; 30 warmups e 120 amostras por processo.
  Total: 252 processos, 30.240 quadros medidos e 7.560 warmups.
- Cada diretório preserva `manifest.json`, `results.json`, `medians.json`, logs e
  CSVs. Primeiro quadro/desde `main`, p95/p99, máximos, contagens acima dos
  orçamentos, RSS e drain final são mantidos nos registros originais.
- `baseline-binaries.json`: controle CoinGL da revisão
  `f89dfcbc548b9fb927527f5af1dc4b6a125029ca`. `final-binaries.json`: a mesma
  base com os quatro hashes de fontes medidos e as bibliotecas candidatas.
  Os pares literal/reserva usam o mesmo binário; só o flag A/B difere.
- `measured-sources`: cópia dos quatro inputs de fonte, verificados por SHA-256.
  `city.iv`: entrada exata, SHA-256 `bb9ccc612cd38ffb749ee599d9350a80974649eab5bf477d2f344538a42b2576`.
- `measure/coingl-physical-proof.log`: confirmação NVIDIA do CoinGL em execução
  diagnóstica separada. Janela confirma o adapter também em cada log.
- `measure/dpms-forced-on.log` e `measure/dpms-audit.json`: apresentação ativa;
  DPMS temporariamente desabilitado, auditado antes/depois de cada processo.
  `dpms-original.log`/`dpms-restored.log`: estado original restaurado ao final.
- `comparison.csv`, `coin-perf-analysis.json`, `geometry-table.md` e
  `geometry-100.png`: derivados de CSV, sem executar renderização. Estatística:
  mediana dos valores por processo; p95 nearest-rank dentro de cada processo.
  Pontos do gráfico mostram cada mediana individual, sem intervalo de confiança.
- `measurement-log-times.json`: horário de gravação dos logs de origem após
  cada processo; não são timestamps de quadros nem início exato dos processos.

## Conteúdo, perfil e gates

- `visual`: 42 processos offscreen separados, sete estados por caso, 294
  comparações RGB com CoinGL e 126 pares PPM A/B byte a byte idênticos.
  `motion.json` verifica alteração efetiva de estado e imagem. Os PPMs completos
  permanecem em `/tmp/coin-perf-verify/images`; os hashes de todos e quatro PNGs
  representativos ficam aqui. A verificação arquivada não reabre os PPMs ausentes.
- `window-visual`: 72 processos nativos, estados lógicos 0 e 600, 36 pares com
  digest RGBA/cena iguais A/B; movimento verificado. O executável exporta digest
  final FNV64, não pixels de janela: não equivale à prova byte a byte offscreen.
- `baseline-profile`: 36 diagnósticos originais, dois warmups/quatro quadros,
  sem timestamps GPU. `warm-spans.json` alinha registros no delimitador action
  e separa `capture_camera_basis` pós-commit. O agregado antigo em `results.json`
  não deve ser usado para atribuir primeira captura a uma amostra aquecida.
- `final-diagnostics`: 24 processos intrusivos, cinco warmups/15 quadros,
  posições/capacidades, spans de preparação e recursos/tempos GPU. As seis
  células reservadas por caso não cresceram os vetores durante a preparação;
  a capacidade final continua igual ao controle literal. Staging de janela zero.
  `unavailable`/`null` em memória total e timestamps não significa zero.
- `coin-perf-final-*-cpu.log` e `coin-perf-literal-*-cpu.log`: 12 gates de
  Action/FrameCore/FrameReuseCore em dois backends e duas opções, todos passados.
- `coin-perf-gates.log`: 8 gates BGFX e 7 wgpu, todos passados, sem skips:
  lowering/instancing, cache/aposentadoria, readback/backpressure, múltiplos alvos,
  RTT e publicação. Tempos CTest não são benchmark.
- `coin-perf-python.log`: primeiro gate de 12 testes. `coin-perf-python-final.log`:
  16 testes passados, incluindo interrupção/restauração e exclusão por perda de
  energia da sessão no novo helper. Testes de DPMS usam fixture, não alteram o host.

## Diagnósticos excluídos

- `measure/window-monitor-off-diagnostic`: primeira tentativa interrompida,
  118 processos completos e arquivos do processo interrompido. DPMS reportou
  monitor desligado; controles estáticos sem updates chegaram a 499–1.001 ms.
  `exclusion.json` registra o motivo. Nenhum desses dados entra nos 252 processos
  qualificados; não combinar suas amostras com a repetição ativa.
- `failed-device-diagnostic`/`failed-device-session`: tentativa privada em que
  BGFX/OpenGL selecionou llvmpipe. Falha de seleção de GPU, sem qualificação.
- `initial-candidate-profile`: 36 processos com a primeira reserva de capacidade
  exata. `interrupted-candidate`: 18 processos de medição antes da correção de
  crescimento. A versão final usa potências de dois e mantém o limite anterior
  em aliases subestimados. Dados desses candidatos não sustentam ganhos finais.
- `coin-perf-main.log` guarda o coordenador original com a tentativa excluída;
  `coin-perf-active-window.log` guarda a repetição ativa e os gates posteriores.

## Verificação e reprodução

O verificador lê arquivos, recalcula cada estatística e valida hashes; não faz
build, acesso à GPU nem rede. Na raiz do checkout:

```sh
python3 scripts/coinrender/verify_performance_evidence.py \
  docs/validation/performance-linux-20261007 --source-tree "$PWD" \
  --check-local-binaries
```

O último argumento exige os controles congelados nos caminhos `/tmp` originais.
Omiti-lo permite auditar a evidência em outra máquina. `--source-tree` compara
os quatro inputs medidos; omiti-lo permite auditar a campanha após modificações
posteriores do checkout. Os hashes de binários e imagens são observações
arquivadas, não novas execuções nem prova independente de pixels ausentes.

`coin-perf-main-campaign.py`, `coin-perf-active-window.py`, `coin-perf-after.py`,
`coin-perf-gates.py`, `coin-perf-analyze.py` e os coletores de perfil preservam os
comandos originais. São receitas locais com caminhos do host, não o verificador;
alguns fazem GPU/build. A reprodução atual com o helper de energia está no
relatório. Fontes, binários congelados e parâmetros devem permanecer fixos;
nunca medir durante outros builds/testes GPU.
