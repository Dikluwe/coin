# Alocações na captura comum — Linux, 2026-10-04

Branch: `codex/coin-render-transform-performance`. Código: `52e1a9475c`.
Referência: `4725913e4b`, que já inclui o agrupamento opaco antecipado wgpu.

## Mudanças

1. **Core: índice de estados.** Cada chave de transformação guarda o primeiro slot diretamente; um vetor armazena apenas candidatos adicionais. Isso evita uma alocação de vetor por transformação única. A comparação completa de estado e a ordem dos slots foram preservadas; colisões continuam passando pela igualdade completa. O hash lê `SbMatrix::getValue()` uma vez, mantendo o algoritmo FNV e a equivalência entre zero positivo e negativo.
2. **Wiring: armazenamento temporário de luzes.** O builder reaproveita a capacidade de um vetor durante a captura. Luzes, transformações e ambiente continuam sendo lidos do estado Coin em cada shape. O Core copia cada novo snapshot para o plano; o scratch não transporta semântica entre shapes ou quadros.

As mudanças beneficiam a captura compartilhada por BGFX e wgpu e preservam as fronteiras da arquitetura. API pública e layout FFI permanecem iguais. Não há instancing ou mudança de shader.

## Confirmação das alocações

Uma medição CPU carregou as bibliotecas anteriores e atuais no mesmo executável, usando apenas API pública. A cidade de 40 mil prédios foi capturada sem target GPU, com uma action nova em cada amostra. Foram quatro pares de processos, invertendo a ordem nos pares pares; dois aquecimentos e oito amostras por processo. O contador instrumenta `new/new[]` durante `apply()`, sem incluir parsing, construção da action ou serialização posterior do recording.

| Durante `apply()` | Antes | Depois | Redução |
|---|---:|---:|---:|
| Alocações C++ contadas | 400.202 | 280.201 | 120.001 (29,99%) |
| Soma de bytes pedidos | 504.146.090 | 487.986.094 | 16.159.996 (15,41 MiB) |

Os números foram exatos em todas as 32 amostras de cada versão. A redução corresponde aos 40.001 vetores de candidatos e às duas alocações de luzes em cada ocorrência, conservando duas alocações iniciais do scratch. A soma dos bytes pedidos não é pico de memória residente. O contador não inclui chamadas diretas a `malloc` nem entry points de alocação alinhada.

Os 64 recordings conservaram o hash `fe1709e6108d5402`. A mediana CPU instrumentada foi 1.059,16 → 1.052,53 ms, mas dois dos quatro pares ficaram mais lentos. Esse caminho recording também expande geometria e não equivale à captura com target GPU. O resultado firme deste controle é a redução de alocações; não indica ganho consistente de tempo.

## Benchmark GPU

Release/Ninja, GCC 13.3.0, `-O3 -DNDEBUG`. Cena determinística, grid 200, seed 136, 480.012 triângulos. Offscreen 1024 × 1024 com readback síncrono e publicação por cópia. Três pares antes/depois por variante, processos novos, quatro quadros de aquecimento e oito medidos. Binários anteriores preservados antes da edição e bibliotecas selecionadas por `LD_LIBRARY_PATH`.

NVIDIA RTX 3060 Laptop, driver 610.57.04: BGFX/Vulkan, BGFX/OpenGL, wgpu/Vulkan. AMD Renoir: wgpu/OpenGL. Não houve build nem outro teste concorrendo com essas medições. Caches do driver/sistema foram mantidos.

Tempos em ms. Primeiro quadro: mediana dos três processos. Aquecido: mediana das três medianas de oito quadros.

| Caminho | Primeiro antes | Primeiro depois | Variação | Aquecido antes | Aquecido depois | Variação |
|---|---:|---:|---:|---:|---:|---:|
| bgfx-vulkan | 680.36 | 670.82 | -1.40% | 3.10 | 3.01 | -2.67% |
| bgfx-opengl | 638.33 | 636.20 | -0.33% | 3.17 | 3.25 | +2.69% |
| wgpu-vulkan | 411.61 | 400.13 | -2.79% | 8.16 | 8.29 | +1.53% |
| wgpu-opengl | 416.91 | 403.19 | -3.29% | 11.42 | 11.23 | -1.64% |

As medianas do primeiro quadro caíram entre 0,33% e 3,29%. Os quadros aquecidos variaram de −2,67% a +2,69%; nesse caminho o plano é reutilizado e não há captura completa. As amostras são pequenas e incluem pares que pioraram. Os resultados não justificam afirmar ganho consistente de FPS ou uma melhoria geral de tempo de captura. O pico RSS caiu menos de 1 MiB em cada variante, dentro de uma diferença pequena para este processo.

Um par diagnóstico separado com `COIN_RENDER_TRACE_PHASES=1` mediu traversal BGFX de 212,90 → 198,91 ms e wgpu de 204,64 → 207,48 ms. O contraste reforça a limitação da estimativa temporal. Esse par não foi incluído nas medianas do benchmark.

## Validação

- Builds completos BGFX, wgpu e recording concluídos.
- Onze execuções CPU selecionadas: BGFX Action/PlanAssemblyCore/FrameCore/IndexedGeometryCore; wgpu os mesmos quatro e FfiFrame; recording Action/PlanAssemblyCore.
- Vinte e duas execuções GPU, todas aprovadas sem skips. Por backend: Texture, DrawStyle, Composition, ShadowReference com GPU obrigatório, DepthContract, ClipPlane, Lighting, Multitexture e Transparency em Vulkan; DepthContract `--gpu` em Vulkan e OpenGL.
- O teste de estados recaptura 2.048 transformações × quatro aparências em ordem inversa, cobrindo slots adicionais, zero negativo e limpeza entre planos.
- O teste de iluminação cobre luzes e ambientes em separadores, pop de estado, luz desligada e ambiente modificado no quadro seguinte, além da independência do plano anterior.
- Os 24 checksums do benchmark coincidiram com os valores conhecidos. Os quatro pares PPM e os dois pares do trace conservaram SHA-256.
- Quatro controles dinâmicos de 10 mil prédios em Vulkan, câmera/material para BGFX/wgpu, conservaram os checksums anteriores. Eles verificam correção e não qualificam performance dinâmica.

São **33 execuções de testes selecionados**, mais quatro controles dinâmicos. Não é uma execução da suíte inteira.

## Próximos custos a investigar

No trace atual, a primeira montagem BGFX ainda paga aproximadamente 136,56 ms na conversão para o backend, 134,10 ms em preparação e 156,37 ms na espera do readback. O packer wgpu levou aproximadamente 99,16 ms. A expansão da geometria e o ciclo de inicialização/leitura merecem medições específicas antes de outra alteração. Os valores são diagnósticos de uma execução, não estimativas universais.

[Logs, scripts e hashes](validation/render-capture-allocation-linux/README.md).
