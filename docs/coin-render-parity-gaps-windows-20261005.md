# CoinRender: lacunas de paridade com Coin/OpenGL

Auditoria do checkout `codex/coin-render-transform-performance`, base
`dd8483b987`, em 2026-10-05. O escopo é CoinRender compartilhado, BGFX e
wgpu; a cidade animada testa somente geometria opaca, materiais, iluminação
e câmera. Uma imagem equivalente dessa cidade não certifica outros nós.

Atualização posterior em Linux: a captura comum de `SoText2` e `SoImage`
independente foi implementada e qualificada nos executores wgpu/Vulkan e
BGFX/Vulkan/OpenGL. Consulte o [perfil, limites e evidência](coin-render-node-inventory.md#texto-e-imagem-implementados-em-linux).
As duas primeiras linhas abaixo preservam o resultado desta auditoria Windows
anterior à implementação; a nova versão ainda exige qualificação Windows.

## Funcionalidades que faltam ou têm contrato limitado

| Prioridade | Recurso Coin | Comportamento atual verificado | Dono e próximo fechamento |
| --- | --- | --- | --- |
| 1 | `SoText2` | `generatePrimitives` não produz texto; a action pode retornar SUCCESS sem draws. Falta o texto visível, incluindo fonte, âncora e composição. | Captura/layout comum; atlas e recursos por executor. P03/F05. |
| 1 | `SoImage` independente | O callback produz um quad, mas a imagem não é capturada como textura habilitada. Não há paridade de conteúdo, alpha e alinhamento. | Captura e contrato de imagem comuns antes dos executores. F20/I02. |
| 1 | Nós customizados com semântica somente em `GLRender` | CoinRender deriva de `SoCallbackAction`; não executa o override GL para obter sua semântica. Um nó pode passar sem produzir sua parte visual. Não há introspecção genérica segura de overrides C++. | Adaptação explícita no nó/host. Inventário FreeCAD P15 e fechamento por workbench P16. |
| 2 | UV procedural/default | `captureTextureUnit` rejeita DEFAULT/FUNCTION em uma unidade de textura habilitada com imagem, inclusive unidade 0. O fast path também tem restrições próprias; falta matriz completa por shape/função. | Captura de coordenadas e interpretação comum P07/F06. |
| 2 | Qualidade/filtros de texturas | O builder aceita textura desligada (`quality <= 0`) ou qualidade próxima de 0,5, com filtro LINEAR. Outros valores são rejeitados. REPEAT/CLAMP e quatro modelos legados têm caminho; isso não cobre qualidade/filtros/mipmaps gerais do GL. | Contrato de sampler/formato e recursos dos executores. P07/F11. |
| 2 | `SoShaderProgram` ativo | Rejeição explícita antes da submissão: não há contrato de shader portátil. | Definir entrada/estado comuns e tradução/execução por backend. I07. |
| 2 | `SoTexture3`, `SoTextureCubeMap`, `SoSceneTextureCubeMap` ativos | Rejeição explícita: faltam planos e executores de textura volumétrica, faces de cubo e RTT de cubo. Nós vazios permanecem inertes. | Captura comum, formatos/UV/orientação e execução BGFX/wgpu. P28. |
| 2 | RTT fora do perfil `SoSceneTexture2` | Suporte atual limitado a unidade 0, RGBA8, MODULATE, REPEAT/CLAMP, NONE/ALPHA_BLEND/ALPHA_TEST, sem `sceneTransparencyType`, dimensões 1..2048 e limites de grafo/memória. RTT GPU direto exige alvo offscreen. | Ampliar contrato comum e formatos/recursos específicos. F14/P13. |
| 2 | Viewport parcialmente fora do alvo no wgpu | `resolved_viewport` rejeita origem negativa ou extensão além do alvo. BGFX tem clipping de viewport; a equivalência entre os dois não é geral nesse caso. | Projeção/scissor no Core e adaptação do executor. P04/F13. |
| 3 | Antialiasing/MSAA e multipass do Coin | Não há opção equivalente completa na action/target experimental; os attachments/pipelines Rust examinados usam uma amostra. Capability de MSAA do hardware não implementa a política da action. | Contrato de qualidade e resolve por executor. F21/P24. |

As três primeiras lacunas podem omitir conteúdo com status SUCCESS. As
rejeições explícitas nas demais linhas são diagnóstico de ausência de suporte,
não uma aproximação visual aprovada. Limites de oito luzes/planos/unidades,
oito camadas de peeling e os orçamentos de RTT também delimitam o perfil.

## Implementação existente que ainda exige qualificação ampliada

- Bindings de materiais/normais por shape, alpha heterogêneo, contornos não
  recuperáveis/convexos, precisão de depth/offset/clamp e bordas de raster
  têm perfis implementados; as matrizes gerais P02/P04/P05/P06 permanecem
  abertas. Isso não significa que todos esses recursos estejam ausentes.
- Sombras já têm executores BGFX e wgpu, incluindo perfis de até oito mapas,
  transparência e RTT. O oráculo CoinGL nativo com oito mapas, outras GPUs e
  APIs ainda precisam de evidência específica. O teste de inventário usa um
  executor testemunha sem sombras e sua rejeição não prova ausência do executor
  GPU real. Consulte o contrato P27 e as campanhas por plataforma.
- FreeCAD Part/BRep tem integração e evidência Linux; textos, imagens, nós
  especiais e outros workbenches precisam de fixtures no host. Inventário
  de classes não é certificação visual de toda a aplicação.
- Esta máquina permite qualificar Windows/NVIDIA/OpenGL/Vulkan/D3D12.
  Não fecha Intel física, macOS/Metal, Android ou todos os drivers.

## Otimizações e extensões não são lacunas de semântica Coin

Instancing já existe no BGFX e wgpu em perfis restritos. A cidade usa 40.001
instâncias, 24 vértices de transporte e um draw GPU no BGFX. RTT direto,
weighted OIT nos dois executores, buffers persistentes e readback assíncrono
offscreen também existem. Documentos históricos P25/P26 e tabelas antigas
não devem ser usados isoladamente para dizer que esses caminhos faltam.

Culling/compute/indirect ampliados e SSAO são otimizações/extensões futuras;
não substituem texto, imagem, shaders ou outros contratos Coin ausentes.
A atualização de vértices da cidade ainda refaz validação/composição e lowering
CPU, mesmo com instancing GPU; esse é um custo de desempenho separado.

## Evidência rastreável

- [Caracterização de texto, imagem, GL-only e rejeições](../testsuite/coinrender/CoinRenderNodeInventoryTest.cpp).
- [Guardas de efeitos e RTT na action](../src/actions/CoinRenderAction.cpp).
- [Captura de textura, qualidade, wrap e UV](../src/rendering/coinrender/CoinRenderFramePlanBuilder.cpp).
- [RTT direto exige offscreen](../src/rendering/coinrender/CoinRenderRttExecution.cpp).
- [Limites de luzes e readback async](../src/rendering/coinrender/CoinRenderTarget.cpp).
- [Viewport e sample_count no Rust](../src/rendering/coinwgpu/rust_bridge/src/lib.rs).
- [Inventário FreeCAD com escopo e classes](coin-render-node-inventory.md).
- [Perfil atual de sombras](coin-render-p27-shadows.md).
- [Contratos de draw style](coin-render-draw-style-contract.md),
  [multitextura](coin-render-multitexture-contract.md) e
  [RTT/publicação](coin-render-rtt-publication-contract.md).

A regressão desta otimização reexecuta `CoinRenderNodeInventoryTest` nos dois
builds. O teste caracteriza os bloqueios; seu sucesso significa que reproduziu
as omissões/rejeições declaradas, não que implementou os recursos faltantes.
O relatório de desempenho e os logs ficam em
[prédios animados no Windows](coin-render-animated-buildings-windows.md).
