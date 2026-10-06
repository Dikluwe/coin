# Evidências — alocações na captura comum

Referência `4725913e4b`; código medido `52e1a9475c`. Consulte [o relatório](../../coin-render-capture-allocation-linux.md).

- `benchmark/`: 24 logs e resultados, quatro variantes, três pares antes/depois.
- `trace/`: quatro logs diagnósticos separados das medidas principais.
- `cpu/`: oito processos, 64 capturas instrumentadas sem GPU; ordem invertida nos pares pares.
- `tests/`: logs de 11 execuções CPU, 22 GPU e quatro controles dinâmicos, com resultados estruturados.
- `metadata.json`, `baseline-binaries.json`, `comparison.json`: revisões, cenas, hashes dos binários e medianas.
- Scripts `.py` reproduzem os comandos no ambiente local original. Binários anteriores, cenas e PPMs estão em `/tmp`, fora do Git; SHA-256 dos PPMs consta dos resultados. Para reconstruir a referência, use a revisão registrada em `metadata.json` e a mesma configuração de build.

Para reconstruir o medidor CPU, use o código `coin-render-capture-cpu-measure.cpp` e linke as bibliotecas do build correspondente. O comando original foi:

```sh
g++ -std=c++17 -O3 -DNDEBUG /tmp/coin-render-capture-cpu-measure.cpp \
  -I/tmp/coin-render-first-frame/experimental/include \
  -I/tmp/coin-render-first-frame/include \
  -I/tmp/coin-render-first-frame-bgfx/include \
  /tmp/coin-render-capture-baseline-bgfx/lib/libCoinRender.so \
  /tmp/coin-render-capture-baseline-bgfx/lib/libCoin.so.80 \
  -o /tmp/coin-render-capture-cpu-measure
```

Depois execute `coin-render-capture-cpu-compare.py`, usando `LD_LIBRARY_PATH` de cada versão como faz o script. O medidor não acessa headers privados; a mesma cópia do executável foi usada nas duas versões.
