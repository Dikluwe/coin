# Transparência Coin e readback BGFX

`SoBGFXRenderAction` executa todos os valores de `SoGLRenderAction::TransparencyType`.
O padrão `COIN_BGFX_TRANSPARENCY=auto` mantém a modalidade Coin; não substitui
ordenamento por triângulo por weighted OIT, nem muda de algoritmo quando cresce
o número de draws.

| Modalidade | Ordem | Blend | Escrita de depth |
|---|---|---|---|
| `NONE` | imediata | desativado | estado capturado |
| `SCREEN_DOOR` | imediata | stipple de polígonos | estado capturado nos pixels cobertos |
| `ADD` | imediata | SRC_ALPHA, ONE | estado capturado |
| `BLEND` | imediata | source-over | estado capturado |
| `DELAYED_ADD` / `DELAYED_BLEND` | depois da passagem imediata, ordem de travessia | aditivo / source-over | não |
| `SORTED_OBJECT_ADD` / `SORTED_OBJECT_BLEND` | objetos de trás para frente | aditivo / source-over | não |
| `SORTED_OBJECT_SORTED_TRIANGLE_ADD` / `SORTED_OBJECT_SORTED_TRIANGLE_BLEND` | objetos e triângulos de trás para frente | aditivo / source-over | não |
| `SORTED_LAYERS_BLEND` | quatro camadas por depth peeling | source-over | não no depth de saída |

Screen-door reproduz a matriz Coin de 32×32 e sua quantização em 65 níveis.
Ela usa `SoMaterial.transparency[0]`, independentemente de `MaterialBinding`,
não o alpha da textura;
linhas/pontos não recebem polygon stipple. Texturas continuam sendo aplicadas.
Os modos ordenados usam o centro fornecido por `SoShape::computeBBox`, capturado
em espaço global para continuar válido com mudanças de câmera. Linhas/pontos
expandidos mantêm a modalidade do objeto, mas não têm os triângulos artificiais
reordenados. A ordenação por triângulo também cruza packets de materiais
pertencentes à mesma shape/instância. Os overlays mantêm sua ordem e estado
de depth explícito. Na passagem atrasada, o estado padrão é test=true,
write=false, `LEQUAL`, range `[0,1]`; campos de nós `SoDepthBuffer` no caminho
sobrescrevem esses padrões, com o escopo da pilha Coin preservado.

`weighted_oit` continua como extensão selecionável. Apenas transparência
source-over atrasada participa de OIT; modalidades imediatas/aditivas conservam
seu blend. Em uma mistura com OIT/peeling, draws aditivos atrasados são compostos
depois dessas passagens. Isso não promete equivalência de ordem com qualquer
mistura arbitrária de modalidades locais no Coin/GL.

## Profundidade

Offscreen publica cor RGBA8 e, por padrão, depth float32 normalizado em `[0,1]`,
em linhas top-left. A Infra amostra o attachment de profundidade real da GPU,
converte para `R32F` numa passagem fullscreen e faz blit/readback. Não há
re-renderização CPU, estimativa a partir da cor ou buffer artificial de 1.0.
Cor e depth pertencem à mesma submissão. Clear, clipping, range, writes de depth
e overlays são refletidos no resultado final. Transparência atrasada não escreve
no attachment de saída: um pixel transparente pode ter depth de um opaco atrás.

`setDepthReadbackEnabled(FALSE)` deixa depth vazio e elimina a passagem de
conversão/leitura. Trocar essa política drena o staging síncrono pendente sem
recriar os attachments. Resize drena destinos CPU ainda usados pelo staging
síncrono antes de liberá-los. O ambiente experimental de pipeline depth 2/3
continua podendo publicar frames anteriores; o padrão depth 1 é síncrono.

## Tickets assíncronos

`applyAsync`, `renderAsync`, `pollReadback` e `cancelReadback` funcionam no BGFX
offscreen. A submissão agenda as cópias e avança um frame BGFX, sem fazer o loop
de espera do readback síncrono. Polling avança um frame e devolve `NOT_READY`
ou entrega cor/depth juntos. Somente `READY` altera os vetores de saída e consome
o ticket. Metadados alterados, tickets consumidos e polling em thread diferente
da API são rejeitados. As row pitches BGFX são compactas (`width * 4`), não
o alinhamento de 256 bytes usado pelo bridge Rust.

Cada ticket retém staging, destinos CPU e uma referência ao runtime; permanece
válido após resize e destruição do alvo/action. Há backpressure em 16 tickets
ou 128 MiB de payload CPU pendente no runtime compartilhado. Isso não limita
a quantidade de viewports. Cada payload também tem staging GPU correspondente.
Cancelamento pode drenar frames para garantir que a GPU deixou de usar os
destinos CPU. Após device loss, os buffers são retidos até o shutdown seguro;
cancelar todos os tickets permite reconstruir o runtime.

## Validação e limites

`BgfxReadbackModesTest` verifica os onze valores, blending RGB, depth writes,
screen-door, sorting de triângulos, depth range/perspectiva/origem, API pública,
cor-only, resize/destruição, cancelamento, tickets adulterados, thread da API,
backpressure e device loss/recuperação em Vulkan e OpenGL.
`COIN_BGFX_COMPARE_GL=1` adiciona referência RGB Coin/GL de dez modalidades;
sorted layers tem o ensaio independente já existente e o oráculo GPU deste teste.
A matriz CTest opcional `COIN_TEST_WGPU_GL_REFERENCE` roda ambas as referências.

Isso não estabelece paridade bit-a-bit de RGBA com GL: o perfil existente de
source-over mantém alpha de composição separado. Também não implementa as
opções extras de `SoGLRenderAction` para custom sorting, passagem separada de
backfaces não sólidas ou quantidade configurável de camadas além das quatro
do perfil. Esses controles não devem ser confundidos com aceitar a enumeração
de modalidades. `SoTextureCombine` e transporte portátil de superfícies ainda
são pendências separadas.
