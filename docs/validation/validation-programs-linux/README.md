# Evidência: validação Common de programas de textura

[Relatório](../../coin-render-validation-programs-linux.md).
`stage-metadata.json` identifica revisões e totais. Os PPMs e as bibliotecas
não estão no Git; o pacote mantém hashes, métricas e resultados originais.

## Recalcular os dados arquivados

Execute na raiz do checkout. Estes comandos leem CSVs e registros existentes;
não compilam nem executam GPU. RGB usa a comparação de pixels já registrada,
explicitamente marcada como reutilização pelo summarizer.

```bash
PYTHONDONTWRITEBYTECODE=1 python3 docs/validation/validation-programs-linux/offscreen/summarize.py \
  --repository . \
  --before docs/validation/validation-programs-linux/offscreen/before \
  --after docs/validation/validation-programs-linux/offscreen/after \
  --recorded-rgb docs/validation/validation-programs-linux/offscreen/rgb-before-after.json \
  --require-identical-rgb \
  --control docs/validation/validation-programs-linux/offscreen/controls/coin-render-validation-baseline-binaries.json \
  --control docs/validation/validation-programs-linux/offscreen/controls/coin-render-validation-final-binaries.json \
  --control docs/validation/validation-programs-linux/offscreen/controls/coin-render-state-coingl-binaries.json \
  --validate-only

PYTHONDONTWRITEBYTECODE=1 python3 docs/validation/validation-programs-linux/stress/summarize.py \
  --repository . \
  --before docs/validation/validation-programs-linux/stress/before \
  --after docs/validation/validation-programs-linux/stress/after \
  --control docs/validation/validation-programs-linux/stress/controls/coin-render-validation-baseline-binaries.json \
  --control docs/validation/validation-programs-linux/stress/controls/coin-render-validation-final-binaries.json \
  --control docs/validation/validation-programs-linux/stress/controls/coin-render-state-coingl-binaries.json \
  --validate-only
```

Resultados: 20 comparações de tempo e 168 RGB registradas na principal;
quatro comparações de tempo no stress. Coin/OpenGL tem uma execução
por caso/rodada compartilhada nos conjuntos antes/depois.

## Reproduzir fases, relatório e figura

```bash
python3 docs/validation/validation-programs-linux/diagnostic/derive-diagnostic.py \
  --input docs/validation/validation-programs-linux/diagnostic \
  --output /tmp/coin-render-validation-diagnostic-recomputed.json

MPLCONFIGDIR=/tmp/coin-render-validation-mpl python3 docs/validation/validation-programs-linux/plot-and-report.py \
  --evidence docs/validation/validation-programs-linux \
  --output docs/coin-render-validation-programs-linux.md
```

O script de diagnóstico conserva séries brutas, eventos e SHA-256 de entradas.
Só usa índices CSV medidos quando a contagem do trace corresponde à do CSV.
Séries com outra contagem ficam registradas sem mediana por quadro inferida.

## Execuções originais

`run-campaigns.py` mantém caminhos originais em `/tmp`, alternância e protocolo
offscreen. Cada manifest registra comandos, ambiente selecionado e hashes.
`campaign-commands.json` registra o lançamento e retorno das duas campanhas.
`diagnostic/run-ablation.py` conserva as oito execuções com optout ligado/desligado.
`tests/run-gates.py` e `tests/commands.json` conservam gates, ambientes, retornos,
definições CTest e exigências de saída para as referências Coin/OpenGL.
Os dois logs de build estão em `tests/`.

A verificação RGB antes é a da etapa anterior, fonte `ac28529a27`; seus hashes
registrados coincidem com os binários congelados desta campanha. A verificação
depois fez 24 processos novos. Os manifests preservam essa proveniência.

`machine.json` foi registrado depois da campanha. `load-observation.json`
conserva uma consulta de carga; ps usou um namespace restrito de processos.
Esses registros não são uma história de clocks, temperatura ou carga por amostra.

`archive-manifest.json` em cada campanha mantém caminhos originais e hashes
dos arquivos copiados. `stage-files.json` lista tamanhos e SHA-256 dos arquivos
finais e do relatório, excluindo o próprio inventário.
