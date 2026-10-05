# Evidência: cache temporal de matrizes wgpu

[Relatório](../../coin-render-wgpu-matrix-cache-linux.md).
`stage-metadata.json` identifica fontes, contagens e limites. São 63 processos
únicos de tempo, 801 quadros medidos, 279 warmups e oito processos de ablação.
As 98 comparações RGB antes/depois foram idênticas. Os 12 hashes de binários e
bibliotecas permaneceram iguais após a campanha.

## Recalcular os arquivos arquivados

Execute na raiz do checkout. Estes comandos leem CSVs e logs, sem builds ou GPU.
Os PPMs ficam fora do Git; RGB reutiliza a comparação registrada, explicitamente
marcada como reutilização pelo summarizer.

```bash
PYTHONDONTWRITEBYTECODE=1 python3 docs/validation/wgpu-matrix-cache-linux/offscreen/summarize.py \
  --repository . \
  --before docs/validation/wgpu-matrix-cache-linux/offscreen/before \
  --after docs/validation/wgpu-matrix-cache-linux/offscreen/after \
  --recorded-rgb docs/validation/wgpu-matrix-cache-linux/offscreen/rgb-before-after.json \
  --require-identical-rgb \
  --control docs/validation/wgpu-matrix-cache-linux/offscreen/controls/coin-render-matrix-baseline-binaries.json \
  --control docs/validation/wgpu-matrix-cache-linux/offscreen/controls/coin-render-matrix-final-binaries.json \
  --control docs/validation/wgpu-matrix-cache-linux/offscreen/controls/coin-render-state-coingl-binaries.json \
  --validate-only

PYTHONDONTWRITEBYTECODE=1 python3 docs/validation/wgpu-matrix-cache-linux/stress/summarize.py \
  --repository . \
  --before docs/validation/wgpu-matrix-cache-linux/stress/before \
  --after docs/validation/wgpu-matrix-cache-linux/stress/after \
  --control docs/validation/wgpu-matrix-cache-linux/stress/controls/coin-render-matrix-baseline-binaries.json \
  --control docs/validation/wgpu-matrix-cache-linux/stress/controls/coin-render-matrix-final-binaries.json \
  --control docs/validation/wgpu-matrix-cache-linux/stress/controls/coin-render-state-coingl-binaries.json \
  --validate-only

python3 docs/validation/wgpu-matrix-cache-linux/analyze-matrix.py \
  --trace-helper docs/validation/wgpu-matrix-cache-linux/diagnostic/derive-diagnostic.py \
  --pair offscreen docs/validation/wgpu-matrix-cache-linux/offscreen/before docs/validation/wgpu-matrix-cache-linux/offscreen/after \
  --pair stress docs/validation/wgpu-matrix-cache-linux/stress/before docs/validation/wgpu-matrix-cache-linux/stress/after \
  --diagnostic docs/validation/wgpu-matrix-cache-linux/diagnostic \
  --output /tmp/coin-render-matrix-analysis-recomputed.json
```

Resultados esperados: dez comparações de tempo e 98 RGB registradas na principal;
quatro comparações de tempo no stress. Coin/OpenGL executa uma vez por caso/rodada;
seus CSVs/logs compartilhados só são deduplicados após conferência SHA-256.
O analisador conserva traces, todas as linhas CSV e hashes. Só calcula mediana de
fase/contador por quadro quando a cardinalidade coincide com a do CSV, excluindo
warmups pelos índices da coluna. A ablação de transformação 100% tem zero hits e
+1,60 ms no pack; a queda do total entre revisões não demonstra ganho do cache
nesse caso.

## Reproduzir relatório e figura

Use uma pasta de saída nova:

```bash
MPLCONFIGDIR=/tmp/coin-render-matrix-mpl python3 docs/validation/wgpu-matrix-cache-linux/plot-and-report.py \
  --offscreen docs/validation/wgpu-matrix-cache-linux/offscreen/report-summary.json \
  --stress docs/validation/wgpu-matrix-cache-linux/stress/report-summary.json \
  --analysis docs/validation/wgpu-matrix-cache-linux/analysis.json \
  --metadata docs/validation/wgpu-matrix-cache-linux/stage-metadata.json \
  --output /tmp/coin-render-matrix-report-recomputed

python3 docs/validation/wgpu-matrix-cache-linux/validate-archive.py
```

O gerador lê os números dos JSONs; a figura PNG foi inspecionada visualmente.
`report-inputs.json` registra hashes das entradas, do gerador e das saídas.
`validate-archive.py` confere inventários e recalcula os dados arquivados.

## Execuções originais

`run-campaigns.py`, `matched-runner.py`, `campaign-commands.json` e manifests
preservam os caminhos originais em `/tmp`, protocolo, ordem alternada, retornos,
fontes e hashes. A ablação ligada/desligada usa o mesmo binário, cinco warmups e
três medidos; `diagnostic/run-ablation.py` também valida adapter NVIDIA e CSV.
Scripts de execução mantêm seus caminhos originais; uma nova campanha precisa
dos controles e da cena com os hashes registrados.

`tests/initial/` preserva cinco gates CPU passados e a tentativa FFI que falhou
por uma expectativa errada do teste sobre admissão de NaN/Inf. `tests/final/`
preserva o oracle reparado e os sete gates GPU passados sem skips. A produção
não mudou entre essas rodadas; a correção foi somente no teste. O driver final
`tests/run-gates.py` executa o oracle reparado e os gates GPU. Comandos e definições
CTest dos seis gates CPU estão no registro inicial. As referências Coin/OpenGL
foram exigidas em câmera, transparência e multitextura. Os dois logs de build
estão em `tests/`.

Verificação antes e depois foi feita novamente: 14 processos em cada conjunto,
sete casos, duas variantes, sete estados lógicos por processo. Os manifests
separam revisão do runner e revisão real dos binários. `binary-hashes-post-campaign.json`
verifica os três controles após tempo, ablação e RGB. `machine.json` é uma
observação posterior, sem histórico de clocks, carga ou temperatura por amostra.

Cada `archive-manifest.json` guarda SHA-256 dos arquivos copiados. `stage-files.json`
inventaria os arquivos finais e o relatório, excluindo o próprio inventário.
Bibliotecas e PPMs permanecem fora do Git. Esta evidência é offscreen.
