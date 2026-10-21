# Etapa 2: B12 — recusa explícita de bump não suportado

Entrega de 16/10/2026 em `codex/coin-render`, sobre `24ddf557af4c8956b008d51a53446f4671f33a3b`, com alterações locais identificadas por SHA-256 no [manifesto](validation/coin-render-b12-20261016/manifest.json). Estado inicial: **corrigido no perfil captura/CPU**. Continuação: [qualificação GPU BGFX AMD/NVIDIA × Vulkan/OpenGL, 11 grupos por célula](coin-render-stage2-b12-b02-20261016.md), com B12 aprovado nas quatro células. A [matriz](coin-render-feature-matrix.md) conserva R26/C17 abertos para implementação visual e qualificação GPU.

## Problema e comportamento corrigido

O callback legado de `SoBumpMap` é inerte. Antes desta mudança, CoinRender aceitava uma cena com mapa ativo e publicava uma imagem sem o bump. O teste novo reproduziu o problema antes da alteração de produção: [retorno 1 na exigência de recusa](validation/coin-render-b12-20261016/baseline-red.log), [recibo](validation/coin-render-b12-20261016/baseline-red.json).

A [ação comum](../src/actions/CoinRenderAction.cpp) agora registra `SoBumpMap` na guarda dos efeitos sem executor. Um nó percorrido com imagem de tamanho não vazio ou `filename` não vazio retorna `UNSUPPORTED`, com diagnóstico contendo `SoBumpMap`, antes da submissão. Não há carregamento de arquivo nem consulta GL nessa guarda. Filename pendente também é recusado conservadoramente. O caminho é compartilhado pelos executores; isso não certifica sua execução GPU.

Um mapa sem imagem e sem filename continua inerte. Nós não percorridos por `SoSwitch` não causam recusa. `SoBumpMapCoordinate` e `SoBumpMapTransform` sozinhos permanecem aceitos. O campo `image` marcado ignored continua sujeito à guarda, pois o GLRender legado também lê essa imagem.

A política é conservadora por nó visitado, como as guardas existentes de textura 3D/cubo: também recusa um mapa posteriormente substituído, desativado ou sem shape consumidor. Descobrir se o estado realmente chega a uma primitiva exigirá o contrato de captura R26. Esta mudança não implementa bump/normal mapping, conversão height-to-normal, UVs, tangentes, iluminação nem suas combinações visuais.

## Validação

Build Release com `COIN_RENDER_BACKEND=RECORDING`, Ninja e dois jobs em RAM. `DISPLAY` e `WAYLAND_DISPLAY` removidos na execução. O teste de bump injeta explicitamente `CoinRenderCpuReferenceBackend` para produzir cor e profundidade reais; os outros dois testes conservam seus controles próprios. [Comandos](validation/coin-render-b12-20261016/commands.txt), [configuração](validation/coin-render-b12-20261016/configure.log), [rebuild corrigido](validation/coin-render-b12-20261016/build-fixed.log).

| Controle | Resultado e escopo |
| --- | --- |
| CoinRenderBumpRejectionTest | PASS, 11 grupos no executor CPU de referência |
| CoinRenderNodeInventoryTest | PASS, controles existentes de efeitos/subclasses |
| CoinRenderActionTest | PASS, controles existentes da ação |

[Log final completo](validation/coin-render-b12-20261016/green-tests.log), [JUnit](validation/coin-render-b12-20261016/tests.xml). Nenhuma campanha GPU, Android ou Metal foi executada nesta primeira entrega CPU. A continuação ligada acima adicionou a execução BGFX GPU. CMake registra agora `CoinRenderBumpRejectionVulkanTest` e `CoinRenderBumpRejectionOpenGLTest` para BGFX, e `CoinRenderBumpRejectionGpuTest --gpu` para RUST_BRIDGE, serializados por `coinrender_gpu`; ausência de executor compilado produz SKIP explícito.

O [teste novo](../testsuite/coinrender/CoinRenderBumpRejectionTest.cpp) cobre:

1. Imagem de um componente, recusa e recuperação.
2. Dois componentes, recusa e recuperação.
3. Três componentes, recusa e recuperação.
4. Quatro componentes, recusa e recuperação.
5. Campo image ignored com imagem ativa.
6. SoSwitch NONE aceito e ALL recusado, seguido de recuperação.
7. Coordenadas e transformação de bump isoladas aceitas.
8. Subclasse registrada após a criação da ação recusada.
9. Filename pendente sem pixels recusado, sem disparar leitura de arquivo.
10. Bump no filho RTT recusado, preservando a publicação do pai.
11. Repetição da cena inválida recusada, sem reaproveitar quadro válido como sucesso.

Cada recusa exige simultaneamente status/diagnóstico, serial sem avanço e igualdade integral dos buffers de cor e profundidade com o último quadro válido. Remover o pedido permite renderizar novamente com a mesma ação e o mesmo alvo. O quadro inicial contém cubo e fundo distintos, evitando um controle que verificasse apenas a limpeza do alvo.

O fixture de filename desliga sua notificação para não disparar o sensor de arquivo e chama `bump.touch()` para invalidar o percurso retido. A tentativa intermediária sem esse touch suprimiu a invalidação do próprio teste e falhou; [log preservado](validation/coin-render-b12-20261016/test-fixture-invalidation.log). Alterações sem notificação exigem invalidação pelo chamador, como os demais campos Coin.

## Acompanhamento

B12 encerra o defeito de aceitação silenciosa no perfil CPU inicial e nas quatro células BGFX GPU da continuação; C17.1 registra os cruzamentos de recusa/RTT/subclasse/recuperação. R26 continua ausente. Os outros B e C mantêm suas pendências e evidências próprias. O inventário da etapa 1 é um retrato anterior à correção e permanece preservado.
