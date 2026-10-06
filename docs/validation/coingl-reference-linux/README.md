# Evidências — CoinGL como referência

[Relatório](../../coin-render-coingl-reference-linux.md) e [padrão de comparação](../../coin-render-benchmark-standard.md).

- `results.json`: 18 execuções, três processos por renderer/GPU, 30 warmup e 120 quadros medidos.
- `medians.json`, `comparison.json`: medianas, razões contra CoinGL na mesma GPU e erros RGB dos PPM orientados.
- `metadata.json`: revisão, contratos, ordem, hardware, contexto GLX e hashes das bibliotecas/cena.
- `*-1.log`, `*-2.log`, `*-3.log`: logs das medições.
- `coingl-*-probe.log`: diagnósticos separados de GL_VENDOR/GL_RENDERER. O probe pbuffer AMD falhou; o probe pixmap AMD confirmou GPU e render1024. O probe NVIDIA usa a cena pequena em 64×64.
- `*-rgb-comparison.png`: painéis de inspeção visual reduzidos; as métricas foram calculadas dos PPM 1024×1024, sem redução.
- `coin-render-coingl-reference-measure.py` e `coin-render-coingl-reference-analyze.py`: comandos e análise reproduzíveis no ambiente local original. Cenas, binários e PPM ficam em `/tmp`, fora do Git; os hashes dos PPM constam de `results.json`.

Recrie a cena com `generate_large_scene.py` conforme `metadata.json`; adapte os caminhos de build dos scripts para reproduzir em outro host. Execute os benchmarks em sequência, sem builds ou testes GPU concorrentes. O checksum da referência não precisa igualar o do renderer experimental; a comparação cruzada usa RGB orientado.
