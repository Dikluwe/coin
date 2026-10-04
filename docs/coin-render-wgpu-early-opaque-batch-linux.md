# Agrupamento opaco antecipado no wgpu — Linux, 2026-10-04

Branch: `codex/coin-render-transform-performance`. Código: `f95e99f8da`.
Referência preservada: `2574c776a3` (arquitetura organizada, antes desta otimização).

## Custo encontrado

O packer wgpu construía um `CoinWgpuRenderState` de 2.280 bytes para cada estado capturado, antes de verificar se os draws poderiam virar um único batch. A cidade tem 40.001 estados: **91.202.280 bytes** nessa tabela. Mesmo após reduzir seu tamanho lógico a um, o vetor mantinha a capacidade da tabela inteira.

O novo caminho qualifica primeiro os perfis opacos sem texturas, sombras, strokes, fog, clipping ou offsets. O mesmo packer de estado valida os valores capturados e gera as mesmas chaves de comparação, incluindo estados sem draws. São preservadas apenas duas matrizes de 16 floats por estado durante a montagem: **5.120.128 bytes temporários**, mais um estado GPU. A geometria continua sendo expandida por ocorrência; não foi introduzido instancing nem modificado o layout da FFI.

Os estados gerais passam pelo caminho anterior. Uma checagem conservadora evita o empacotamento duplicado nos perfis heterogêneos mais comuns. O switch interno `COIN_WGPU_DISABLE_EARLY_OPAQUE_BATCHING=1` permite qualificar o caminho anterior no mesmo binário. `COIN_WGPU_DISABLE_OPAQUE_BATCHING=1` continua desativando ambos.

A alteração ficou no adaptador wgpu. O binário da biblioteca BGFX é byte a byte idêntico à referência. O experimento inicial com transformações de vértices não mostrou ganho consistente e foi descartado.

## Método

Builds Release/Ninja, GCC 13.3.0, `-O3 -DNDEBUG`. Cidade determinística, 40 mil prédios, seed 136, 480.012 triângulos. Offscreen 1024 × 1024, readback RGBA síncrono, publicação por cópia. Três pares alternados antes/depois por variante; processos novos, quatro quadros de aquecimento e oito medidos.

Os binários da referência foram copiados antes das alterações, junto com `libCoinRender.so` e `libCoin.so.80`, e carregados com `LD_LIBRARY_PATH` específico. Seus hashes e os hashes atuais estão nas evidências. Caches do driver/sistema foram preservados. Nenhuma compilação ou outro teste GPU concorreu com as medições finais.

NVIDIA RTX 3060 Laptop, driver 610.57.04: BGFX/Vulkan, BGFX/OpenGL, wgpu/Vulkan. AMD Renoir: wgpu/OpenGL. Cada comparação usa a mesma GPU e API.

## Resultado final

Tempos em ms. Primeiro quadro: mediana dos três processos. Aquecido: mediana das três medianas de oito quadros.

| Caminho | Primeiro antes | Primeiro depois | Variação | Aquecido antes | Aquecido depois | Variação |
|---|---:|---:|---:|---:|---:|---:|
| bgfx-vulkan | 690.10 | 683.18 | -1.00% | 3.18 | 3.15 | -1.01% |
| bgfx-opengl | 649.56 | 654.19 | +0.71% | 3.22 | 3.17 | -1.39% |
| wgpu-vulkan | 459.77 | 418.88 | -8.89% | 8.32 | 8.26 | -0.74% |
| wgpu-opengl | 468.71 | 434.97 | -7.20% | 11.44 | 11.52 | +0.77% |

Os três pares wgpu melhoraram o primeiro quadro em ambas as APIs. A redução da mediana foi **8,89% em Vulkan** e **7,20% em OpenGL**. Os tempos aquecidos permaneceram próximos: −0,74% e +0,77%. Os controles BGFX ficaram em torno de ±1% no primeiro quadro; não houve alteração de produção nesse backend.

### Memória do processo

Mediana do pico de RSS coletado por `/usr/bin/time`, em MiB. Inclui memória de CPU e do driver mapeada no processo; não é uma medida de VRAM.

| Caminho | Pico antes | Pico depois | Redução |
|---|---:|---:|---:|
| wgpu-vulkan | 578.17 | 494.63 | 83.54 MiB |
| wgpu-opengl | 502.91 | 419.70 | 83.21 MiB |

Uma execução diagnóstica separada em Vulkan mediu empacotamento de **135,44 → 97,42 ms**. Captura, número de vértices, geometria enviada à GPU e trabalho Rust conservaram o mecanismo anterior. O ganho vem da preparação CPU e da tabela de estados evitada.

## Validação

- Quatro testes CPU: Action, FrameCore, FrameReuseCore e FfiFrame.
- Onze execuções GPU wgpu, todas com código zero e sem skips: Texture, DrawStyle, Composition, ShadowReference com GPU obrigatório, DepthContract, ClipPlane, Lighting, Multitexture e Transparency em Vulkan; DepthContract `--gpu` em Vulkan/NVIDIA e OpenGL/AMD.
- FfiFrame compara bytes dos vértices, índices, materiais, estados e draws entre o caminho antecipado e o anterior, com geometria compartilhada ou contígua, escala não uniforme, rotação da câmera, iluminação e viewport parcial.
- Cobertura de fallback com fog, escrita de profundidade diferente e matriz projetiva; estado sem draw associado com iluminação inválida mantém a rejeição e o diagnóstico. Um owner que antes transportava textura/sampler remove esses dados no novo batch.
- Os 24 processos do benchmark final conservaram os checksums esperados nas quatro variantes. Os quatro pares PPM da primeira execução conservaram o SHA-256 antes/depois.
- Câmera e material animados: controles wgpu de 10 mil prédios, 1/2 quadros, com os checksums anteriores preservados. Não qualificam desempenho dinâmico.
- Cena de fallback: 10 mil prédios e um Cube final com `DepthBuffer { write FALSE }`, três pares Vulkan, 1/2 quadros. Todos os checksums e o PPM antes/depois coincidiram. Medianas primeiro quadro **147,41 → 148,18 ms**, aquecido **46,68 → 47,01 ms**; amostras não indicam regressão clara nesse controle.

## Limites e próximos alvos

O resultado qualifica este perfil de cena e estes caminhos offscreen Linux. Janelas nativas, outras GPUs, Direct3D12 e ganho em animações não foram medidos. Cenas fora da qualificação usam o caminho geral e ainda podem pagar uma sondagem extra de elegibilidade; o controle de fallback acima cobre um caso grande de estado heterogêneo.

Continuam sendo candidatos a medições específicas: captura e cópias de estados no Core comum, inicialização de recursos BGFX e expansão da geometria em cada backend. A geometria CPU/GPU permanece expandida no packer; seu volume não caiu nesta mudança.

[Logs, resultados e scripts](validation/wgpu-early-opaque-batch-linux/README.md).
