# Evidência: composição e estado comum wgpu

[Relatório](../../coin-render-state-composition-linux.md). `stage-metadata.json`
identifica revisões, cena e totais. `machine.json` registra versões e uma
observação da GPU depois da campanha; não é uma série temporal das amostras.

## Recalcular o pacote arquivado

Execute na raiz do checkout. Os comandos não compilam nem usam GPU. Recalculam
os tempos a partir dos CSVs e conferem protocolo/proveniência. A opção
`--recorded-rgb` reutiliza a comparação original de pixels, explicitamente
marcada como reutilização: PPMs não estão no Git. Digests, hashes e métricas
RGB originais permanecem arquivados.

```bash
PYTHONDONTWRITEBYTECODE=1 python3 docs/validation/state-composition-linux/offscreen/summarize.py \
  --repository . \
  --before docs/validation/state-composition-linux/offscreen/before \
  --after docs/validation/state-composition-linux/offscreen/after \
  --recorded-rgb docs/validation/state-composition-linux/offscreen/rgb-before-after.json \
  --require-identical-rgb \
  --control docs/validation/state-composition-linux/offscreen/controls/coin-render-state-baseline-binaries.json \
  --control docs/validation/state-composition-linux/offscreen/controls/coin-render-state-final-binaries.json \
  --control docs/validation/state-composition-linux/offscreen/controls/coin-render-state-coingl-binaries.json \
  --validate-only

PYTHONDONTWRITEBYTECODE=1 python3 docs/validation/state-composition-linux/stress/summarize.py \
  --repository . \
  --before docs/validation/state-composition-linux/stress/before \
  --after docs/validation/state-composition-linux/stress/after \
  --control docs/validation/state-composition-linux/stress/controls/coin-render-state-baseline-binaries.json \
  --control docs/validation/state-composition-linux/stress/controls/coin-render-state-final-binaries.json \
  --control docs/validation/state-composition-linux/stress/controls/coin-render-state-coingl-binaries.json \
  --validate-only
```

Saídas: 20 comparações de tempo e 168 comparações RGB registradas na campanha
principal; quatro comparações de tempo no stress. Coin/OpenGL tem uma execução
por caso/rodada compartilhada entre os dois conjuntos.

## Reproduzir as execuções

`run-campaigns.py` preserva caminhos originais em `/tmp`, warmups, casos,
alternância e verificação de tela ativa. Para outros caminhos, use
`offscreen/run-all-matched.py --help`. Compile as revisões indicadas nos
controles de binários separadamente; não reconstrua o controle com código novo.
Os manifests registram cada comando, ambiente selecionado, ordem e parâmetros.

`diagnostic/` conserva os dois scripts de perfil/ablação, comandos e séries
completas dos traces. `derived-profile.json` usa apenas os índices CSV medidos
quando a série tem uma entrada por quadro; também conserva as séries completas.

`tests/commands.json` foi gravado durante a execução dos gates, incluindo
ambientes selecionados, retorno e definições CTest. Os logs e os dois logs
finais de build estão em `tests/`. `tests/run-gates.py` preserva o script usado.
O teste FFI precisou de uma correção no oráculo local de imutabilidade para
evitar chamar um método não exportado; os builds/gates finais passaram.

O clipping passou inicialmente com a subchecagem opcional Coin/OpenGL omitida.
`tests/run-clipping-reference.py` repetiu esse gate com pixmap GLX direto,
exigindo a confirmação da referência e nenhuma mensagem de skip. A execução
passou; `clipping-reference-command.json` e `clipping-reference.log` conservam
comando, ambiente, resultado e saída. Essa repetição complementa os 23 gates.

`window-excluded/` conserva o checkpoint que impediu iniciar a campanha em
janela. Não existem amostras em janela desta etapa.

Para regenerar relatório e figuras, com Matplotlib:

```bash
MPLCONFIGDIR=/tmp/coin-render-state-mpl python3 docs/validation/state-composition-linux/plot-and-report.py \
  --evidence docs/validation/state-composition-linux \
  --output docs/coin-render-state-composition-linux.md
```

`stage-files.json` lista tamanhos e SHA-256 dos arquivos finais e do relatório,
excluindo o próprio inventário. `archive-manifest.json` em cada campanha mantém
a origem e hashes dos dados copiados. Não foram copiados PPMs ou bibliotecas.
