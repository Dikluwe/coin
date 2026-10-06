# Evidência: cache de Cubes e qualificação CPU

Resultados e limites: [relatório](../../coin-render-cube-template-linux.md).
`stage-metadata.json` identifica as revisões e os totais válidos. `machine.json`
registra versões e uma observação da GPU feita depois da campanha; temperatura,
clock e P-state desse arquivo não descrevem cada amostra de tempo.

## Recalcular os tempos arquivados

Execute na raiz do checkout. Os comandos somente leem os arquivos; não usam GPU.
A comparação RGB abaixo reutiliza o resultado registrado da comparação original
dos pixels, identificando explicitamente essa reutilização. Os PPMs estão fora do
Git. CSVs, logs, digests e hashes das imagens permanecem neste pacote.

```bash
PYTHONDONTWRITEBYTECODE=1 python3 docs/validation/cube-template-linux/offscreen/summarize.py \
  --repository . \
  --before docs/validation/cube-template-linux/offscreen/before \
  --after docs/validation/cube-template-linux/offscreen/after \
  --recorded-rgb docs/validation/cube-template-linux/offscreen/rgb-before-after.json \
  --require-identical-rgb \
  --control docs/validation/cube-template-linux/offscreen/controls/coin-render-cube-baseline-binaries.json \
  --control docs/validation/cube-template-linux/offscreen/controls/coin-render-cube-final-binaries.json \
  --control docs/validation/cube-template-linux/offscreen/controls/coin-render-cube-coingl-binaries.json \
  --validate-only

PYTHONDONTWRITEBYTECODE=1 python3 docs/validation/cube-template-linux/stress/summarize.py \
  --repository . \
  --before docs/validation/cube-template-linux/stress/before \
  --after docs/validation/cube-template-linux/stress/after \
  --control docs/validation/cube-template-linux/stress/controls/coin-render-cube-baseline-binaries.json \
  --control docs/validation/cube-template-linux/stress/controls/coin-render-cube-final-binaries.json \
  --control docs/validation/cube-template-linux/stress/controls/coin-render-cube-coingl-binaries.json \
  --validate-only
```

As saídas esperadas são 20 comparações de tempo e 168 comparações RGB registradas
na campanha principal, e quatro comparações de tempo no stress. O controle
Coin/OpenGL usa a mesma execução por rodada nos conjuntos antes e depois.

## Reproduzir a execução

`run-campaigns.py` conserva a composição executada e os caminhos originais em
`/tmp`. Para outra máquina, use `offscreen/run-all-matched.py --help` e substitua
os caminhos de builds, cena, runner, row helper e saídas. Compile separadamente
as revisões indicadas em `stage-metadata.json`; não reconstrua o controle usando
o código novo. Os manifests de cada campanha conservam os comandos por processo,
o ambiente selecionado, a ordem e os parâmetros efetivos. Os controles de
binários conservam os SHA-256 de bibliotecas e executáveis usados.

`tests/commands.json` descreve os gates executados e seus ambientes selecionados;
os quatro logs preservam suas saídas. Os scripts e comandos dos diagnósticos
instrumentados estão em `diagnostic/`. `window-excluded/` conserva a campanha
incompleta em janela e o motivo de sua exclusão.

Para regenerar o relatório e as figuras, com Matplotlib instalado:

```bash
MPLCONFIGDIR=/tmp/coin-render-cube-mpl python3 docs/validation/cube-template-linux/plot-and-report.py \
  --evidence docs/validation/cube-template-linux \
  --output docs/coin-render-cube-template-linux.md
```

`stage-files.json` lista tamanho e SHA-256 dos arquivos finais deste pacote,
exceto o próprio inventário; inclui também o relatório. Os manifests individuais
descrevem a origem e os hashes dos arquivos copiados das campanhas.
