# Benchmark após a separação do CoinRender — 2026-10-04

Código medido: `24eee81929`, branch `codex/coin-render-isolation`.
Referência: `ad82572bd3`, após a correção de geometria compartilhada e antes das quatro fases de organização.

## Método

Repetição do benchmark offscreen anterior: builds Release/Ninja (`-O3 -DNDEBUG`), cidade determinística de 40 mil prédios (seed 136, 480.012 triângulos), 1024 × 1024, readback RGBA síncrono e publicação por cópia. Três processos novos por variante, quatro quadros de aquecimento e oito medidos, executados em sequência e sem compilação ou outros testes GPU concorrentes.

BGFX/Vulkan, BGFX/OpenGL e wgpu/Vulkan usam NVIDIA RTX 3060 Laptop; wgpu/OpenGL usa AMD Renoir. As linhas comparam o mesmo caminho e GPU. Primeiro quadro inclui captura e readback, excluindo parsing, enquadramento e sondagem inicial.

A referência vem dos logs preservados da medição anterior. Ela não foi recompilada nem medida novamente nesta sessão. Caches do sistema/driver foram preservados.

## Resultados

Tempos em milissegundos. Primeiro quadro: mediana de três processos. Aquecido: mediana das três medianas de oito quadros.

| Caminho | Primeiro anterior | Primeiro atual | Variação | Aquecido anterior | Aquecido atual | Variação |
|---|---:|---:|---:|---:|---:|---:|
| bgfx-vulkan | 688.82 | 681.35 | -1.08% | 3.03 | 3.14 | +3.64% |
| bgfx-opengl | 649.87 | 639.63 | -1.58% | 3.16 | 3.13 | -0.96% |
| wgpu-vulkan | 447.38 | 448.62 | +0.28% | 8.22 | 8.20 | -0.33% |
| wgpu-opengl | 460.55 | 454.08 | -1.40% | 11.62 | 11.35 | -2.32% |

Os tempos se mantiveram próximos da referência: primeiro quadro entre −1,58% e +0,28%; aquecido entre −2,32% e +3,64%. A maior alta aquecida, BGFX/Vulkan, foi de 0,11 ms. As faixas das medianas aquecidas por processo se sobrepõem em todas as variantes. Estas amostras não indicam regressão clara e não estabelecem uma garantia estatística de equivalência.

Os 12 processos conservaram os checksums RGBA esperados. A imagem PPM da primeira execução de cada variante também conservou o SHA-256 da referência. Não foi feita alteração de produção nesta medição.

O resultado cobre os quatro caminhos offscreen da referência; não qualifica janelas nativas, desempenho de animação, OpenGL legado, Direct3D12 ou outras plataformas.

## Evidências

- [Resultados, logs e script](validation/render-isolation-benchmark-linux/README.md).
- [Medição de referência](coin-render-first-frame-linux.md).
