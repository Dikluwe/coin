# Fechamento portátil de geometria e viewport — 2026-10-06

Branch `codex/coin-render`, base de implementação `50ac49e02a`; o registro do
estudo foi publicado separadamente em `ec97ceaa1c`. Os hashes finais das fontes
estão em `source-sha256.json`. `summary.json` registra a qualificação consolidada,
com comandos, ambiente, contagens e limites.

Builds Release: RUST_BRIDGE, BGFX e RECORDING com renderer GL legado habilitado.
GPU Linux: NVIDIA RTX 3060 Laptop, driver 610.57.04. As execuções GPU são
sequenciais, com CoinGL e sombras obrigatórios quando pertinentes. Tempos CTest
não constituem campanha de desempenho. Linhas vazias ao fim dos logs são
normalizadas; conteúdo diagnóstico e contagens são preservados.

## Histórico e qualificação

- `wgpu-initial-full.log` e `wgpu-initial-last-test.log` preservam as falhas que
  conduziram às correções de cache, reserva de captura, POINTS e bounding boxes.
- `wgpu-full-final-ownership.log` executa a implementação final de raster em
  toda a suíte. Duas verificações estruturais antigas ainda ignoravam o passo
  subpixel nas posições; foram ajustadas para recuperar a linha original antes
  de testar clipping e atributos. `wgpu-final-coverage-retest.log` e seu LastTest
  confirmam os dois passes, com as tolerâncias de atributos preservadas.
- `bgfx-full-before-constant-depth.log` e seu LastTest registram os dois primeiros
  erros da matriz ampliada; são diagnósticos anteriores à política comum final.
  O relatório final BGFX conserva os gates Vulkan/OpenGL completos de 1.290
  células. A delimitação OpenGL experimental foi descartada e não integra a
  configuração publicada. O único timeout final de sombras passou isolado em
  18,74 s com o limite de 60 s mantido; os dois logs permanecem no registro.
- `rust-final.log`: 35 testes de biblioteca, dois de Gouraud e três de shaders.
- Recording: dez CTests passam; ClipPlane/DrawStyle executam seus controles
  CPU/GL, mas retornam skip na parte GPU por construção desse perfil.
- Os logs de builds e Recording completam a evidência da mesma fonte.

O [estudo de raster](../../coin-render-raster-junctions-study.md) conserva
observações CoinGL e experimentos descartados. Resultados antigos de falha
não são passes, nem dispensam falhas portáteis da qualificação final.

O [contrato ampliado](../../coin-render-geometry-viewport-contract.md) delimita
shapes, bindings pareados, estilos, ranges, viewport, RTT e sombras. Outros
cruzamentos, drivers, formatos e plataformas continuam nas campanhas próprias.
