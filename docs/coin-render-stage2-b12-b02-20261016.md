# Etapa 2: B12 na GPU e correção de instancing B02

Entrega local de 16/10/2026 em `codex/coin-render`, sobre `24ddf557af4c8956b008d51a53446f4671f33a3b`. O [manifesto](validation/coin-render-stage2-b12-b02-20261016/manifest.json) identifica fontes, binários, dependências e logs por SHA-256. A [matriz de acompanhamento](coin-render-feature-matrix.md) registra os fechamentos delimitados e as pendências.

## Resultado

B12 validado em GPU: quatro células **AMD Renoir / NVIDIA RTX 3060 × BGFX Vulkan / OpenGL**, 11 grupos de casos em cada execução. A recusa de bump preserva cor, profundidade e serial e permite recuperação no mesmo alvo, inclusive com RTT, subclasses e filename pendente. O [relatório inicial CPU](coin-render-b12-bump-rejection.md) conserva a reprodução e o contrato da guarda. R26 continua ausente: validar a recusa não implementa bump visual.

B02 corrigido no perfil Linux exercitado. A transformação instanciada agora compõe `model * view` na CPU antes da avaliação das posições, como a rota geral. Malhas e índices continuam compartilhados em dois draws para 400 instâncias no teste; o patch de câmera atualiza o buffer de instâncias. O log prova `instancing=1 instances=400 world_space=0` e `camera_patch=1`, sem substituir a otimização pela rota geral.

## Reprodução e causa delimitada

A execução anterior à mudança reproduziu B02 em NVIDIA/OpenGL: pixel `(179,59)`, RGB máximo **138**, média `0.00104777`, um pixel acima do limite. A rota instanciada publicou fundo `(8,11,17)`, enquanto a geral publicou `(146,50,46)`. É o mesmo caso registrado no Windows; esse fato não substitui revalidação Windows. [Log anterior NVIDIA/OpenGL](validation/coin-render-stage2-b12-b02-20261016/baseline-nvidia-CoinBgfxInstancingOpenGLTest.log).

O caminho LESS guardava matrizes de modelo em espaço de mundo e aplicava modelo e câmera em etapas no shader. A rota geral usa `model * view` composto e posições em espaço de câmera. As operações em float dessas duas sequências podem produzir cobertura diferente perto de uma borda. Escolher a representação já existente em espaço de câmera eliminou a divergência no teste sem mudar tolerâncias ou geometria.

A primeira correção, restrita a OpenGL, passou na NVIDIA. O controle adicional AMD/Vulkan revelou outro subcaso da mesma representação separada: retorno de viewport fora do alvo, pixel `(61,187)`, RGB máximo **138**. [Log intermediário AMD/Vulkan](validation/coin-render-stage2-b12-b02-20261016/final-amd-CoinBgfxInstancingTest.log). A correção final seleciona a representação composta nos executores BGFX testados, sem condicionar comportamento ao fabricante. Ambos os casos passam agora com RGB zero. Este é um controle entre duas rotas da mesma API/GPU; a regra de cobertura entre GPUs/APIs e CoinGL em B01 permanece aberta.

## Validação final

Linux x86_64, kernel `6.17.0-42-generic`, Release/Ninja, dois jobs de compilação em RAM, processos GPU em série. NVIDIA RTX 3060 Laptop `10de:2560`, driver `615.71.09`; AMD Renoir `1002:1638`, Mesa `25.2.8-0ubuntu0.24.04.4`. O SDK BGFX instalado foi reutilizado; seus arquivos relevantes estão identificados no manifesto. [Configuração](validation/coin-render-stage2-b12-b02-20261016/configure.log), [build final](validation/coin-render-stage2-b12-b02-20261016/build-all-eye.log), [comandos](validation/coin-render-stage2-b12-b02-20261016/commands.txt), [runner](validation/coin-render-stage2-b12-b02-20261016/run_cells.py).

| GPU / API | B12 | Instancing × geral, maior erro RGB | Maior erro de depth nos controles com readback |
| --- | --- | --- | --- |
| AMD / Vulkan | PASS, 11 grupos | PASS, 0 | 8.9407e-08 |
| AMD / OpenGL | PASS, 11 grupos | PASS, 1 | 1.49012e-07 |
| NVIDIA / Vulkan | PASS, 11 grupos | PASS, 1 | 5.96046e-08 |
| NVIDIA / OpenGL | PASS, 11 grupos | PASS, 0 | 5.96046e-08 |

**8/8 processos GPU, nenhum skip.** [Resultados AMD](validation/coin-render-stage2-b12-b02-20261016/qualified-amd-results.json), [NVIDIA](validation/coin-render-stage2-b12-b02-20261016/qualified-nvidia-results.json). Os XMLs e logs `qualified-*` no mesmo diretório são os resultados finais. `baseline-*`, `eye-space-*` e `final-*` preservam a sequência anterior/intermediária, incluindo a falha AMD/Vulkan; os nomes desses arquivos históricos não indicam o fechamento atual.

Os limites originais permanecem: RGB máximo 3, média 0.15, alpha exato e erro de profundidade menor que 1e-5 nos controles que habilitam depth readback. As comparações incluem câmera, materiais, resize, fog/clipping com fallback e retorno ao instancing, LEQUAL, último material coplanar, viewport fora do alvo e retorno. Cada rota compara também repetição por cache e restauração após opt-out. Há 13 comparações de imagem por processo de instancing, duas com depth; os resultados não são uma campanha de paridade CoinGL.

**2/2 controles CPU**, sem DISPLAY/WAYLAND_DISPLAY: `CoinBgfxCoreTest` e `CoinRenderBumpRejectionTest`. [Log](validation/coin-render-stage2-b12-b02-20261016/qualified-cpu.log), [JUnit](validation/coin-render-stage2-b12-b02-20261016/qualified-cpu.xml). O teste core compara o patch LESS com um lowering completo byte a byte, em ambos os domínios de profundidade, com payload de instâncias presente e após sua liberação por orçamento de metadados. São quatro cruzamentos adicionais. O teste GPU qualifica a elegibilidade de batching com e sem a representação em espaço de mundo.

O Vulkan registra os IDs PCI físicos. Em OpenGL, BGFX pode informar vendor/device zero; a identificação utiliza o `GL_VENDOR` e o `GL_RENDERER` efetivos, registrados pelo contexto EGL, além do vendor/ICD fixado pelo runner. A classificação inicial que exigia IDs PCI também em GL está preservada nos arquivos `initial-strict-pci-results.json`; sua correção de recibo não altera nenhum resultado de execução.

## Custo e pendências

Na rota LESS, mover a câmera passa a recalcular matrizes e reenviar o buffer de instâncias, como já acontecia em LEQUAL. Isso troca o patch anterior de uniformes por trabalho proporcional às instâncias. Malhas, índices e batches permanecem retidos. Esta entrega valida correção e uso do patch; não mede mediana/p95 de cenas grandes nem declara ganho de desempenho. B10 acompanha esse custo antes de considerar o perfil de desempenho fechado. O lowering interno conserva a opção de espaço de mundo para estudos/controles; o executor de produção solicita a representação composta.

B02.1 (OpenGL/NVIDIA) e B02.2 (viewport AMD/Vulkan) estão corrigidos nos perfis exercitados; Windows e outras APIs/GPUs ainda exigem execução própria. B12/C17.1 têm agora qualificação BGFX nas quatro células. wgpu, Android e Metal não foram executados. B01, demais B/C e a implementação visual R26 permanecem abertos.
