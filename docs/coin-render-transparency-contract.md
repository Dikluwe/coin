# P09 — modalidades de transparência Coin

O perfil funcional compartilha as onze modalidades entre CPU, CoinBgfx e o
bridge Rust de CoinWgpu. A decisão fica em `CoinRenderComposition.h`: classifica
alpha após a cascata de textura/combine, resolve ordem, adiamento, anotações,
stipple e profundidade efetiva; `coin_render_composition_schedule` expande o
sorting de triângulos em intervalos ordenados. Nenhum executor refaz a
interpretação do Coin.

## Referência e comportamento comum

O estudo local usa `SoGLRenderAction::handleTransparency`, `renderSingle`,
`doPathSort`, `SoPrimitiveVertexCache::depthSortTriangles`, `SoGLLazyElement`,
`SoAnnotation` e `SoDepthBuffer`. Wiring captura estado, centro de sorting e
camadas. Core trabalha sobre snapshots, sem travessia nem comandos GPU.

| Modalidade | Ordem e operação |
|---|---|
| `NONE` | Travessia imediata, sem blend; conserva alpha da superfície. |
| `SCREEN_DOOR` | Travessia imediata, sem blend; stipple de polígonos pelo alpha do material. |
| `ADD` | Imediata, `SRC_ALPHA, ONE`. |
| `BLEND` | Imediata, source-over. |
| `DELAYED_ADD` / `DELAYED_BLEND` | Opacos primeiro; transparentes na ordem de travessia. |
| `SORTED_OBJECT_ADD` / `SORTED_OBJECT_BLEND` | Transparentes ordenados pelo centro capturado, de trás para frente. |
| `SORTED_OBJECT_SORTED_TRIANGLE_ADD` / `SORTED_OBJECT_SORTED_TRIANGLE_BLEND` | Ordem de objetos, depois profundidade média dos vértices de cada triângulo; inclui packets de materiais da mesma shape/instância. |
| `SORTED_LAYERS_BLEND` | Seleção de camadas por profundidade por pixel e composição de trás para frente. |

Empates de sorting são estáveis. Triângulos artificiais da expansão de strokes
não são reordenados. Anotações preservam travessia na própria camada, com a
barreira comum de profundidade; não participam do peeling da camada base.

O passe transparente adiado usa test=true, write=false, LEQUAL e range [0,1],
salvo campos explícitos capturados de `SoDepthBuffer`. Transparência imediata
usa o estado capturado: alpha zero ainda pode escrever profundidade. Pedidos
fora das capacidades concretas devem ser rejeitados antes da publicação.

Source-over usa RGB `Cs*As + Cd*(1-As)` e alpha separado
`As + Ad*(1-As)`. Aditivo usa RGB `Cs*As + Cd` e alpha `As*As + Ad`, com clamp
no RGBA8, como `glBlendFunc(SRC_ALPHA, ONE)`. A equação separada de alpha do
source-over é o perfil CoinRender já existente; não promete alpha bit a bit
igual ao blend clássico do GL.

Screen door usa a matriz Bayer 32×32 do Coin `{0,2,3,1}`, origem global inferior
esquerda, com 65 níveis `int(transparency*64)`. Alpha primário torna-se 1 antes
da textura; alpha produzido por textura/combine permanece. Stipple não se
aplica a linhas/pontos, inclusive strokes expandidos.

## Execução concreta e transporte

CPU coleta fragmentos por pixel para a referência de camadas. BGFX reutiliza
seus quatro passes de peeling e compositor. wgpu dispõe de quatro attachments
de cor/profundidade e máscaras de escrita, snapshot da profundidade opaca e
compositor GPU. O mecanismo de camadas não é substituído por sorting de objetos
ou weighted OIT. No BGFX, a qualificação usa `COIN_BGFX_TRANSPARENCY=auto`.

A revisão privada wgpu é **27**: vértice de 100 bytes, estado de 2280 e draw de
56, com os mesmos tamanhos/offsets da revisão 26. `composition_flags` transporta
blend (bit 0), aditivo (1), screen door (2), peeling (3) e nível de stipple
(bits 8–14). C++ e Rust precisam ser reconstruídos juntos. Não muda a ABI
pública do Coin. O bridge valida combinações de flags; não reclassifica alpha.
Packets de triângulos ordenados não reutilizam uma única entrada de geometria
identificada pela shape. Camera patch recalcula ordem e invalida o hint quando
a sequência muda.

## Qualificação e limites

`CoinRenderTransparencyTest` atravessa cenas pela action, alternando fast path,
os onze modos, alpha 0/.25/.5/1, polígonos, LINES/POINTS e linhas/pontos nativos.
Verifica RGB com expectativas numéricas (até 5 níveis de byte), os 1024 pixels
do padrão de stipple, sorting entre materiais, superfícies que se cruzam,
alpha do framebuffer, alpha substituído por combine na unidade 7, anotações e
override NEVER nos onze modos. Overrides de escrita imediata também são
executados. Os testes de composição/depth completam defaults, máscara explícita,
empates, viewport/barreiras e rejeição com preservação de imagem/serial.

Com `COIN_RENDER_REQUIRE_GL_REFERENCE=1`, o GL deve renderizar o modo solicitado
sem fallback. Os onze modos básicos, sorting entre materiais e interseções
comparam RGB ao Coin GL. Há duas exceções explícitas às comparações de pixels:
peeling com combine/multitextura e peeling com anotações. O programa ARB de
`SoGLRenderAction.cpp` trata apenas textura 0, ignora suas configurações e
substitui combine; a cena de anotações dessa rota também não reproduz a camada
comum. Nessas duas fixtures o GL continua obrigatório, mas a verificação dos
executores usa expectativas numéricas independentes. O alpha separado do
framebuffer também usa expectativa numérica, não o blend clássico do GL.

O perfil de camadas é fixo em **quatro**. A CPU e wgpu selecionam quatro
profundidades transparentes distintas sobre o fundo opaco; o BGFX mantém seu
mecanismo existente, com epsilon de peeling e interrupção por alpha zero.
A matriz P09 cobre até duas superfícies transparentes sobre fundo opaco,
incluindo cruzamentos. Não certifica equivalência entre esses mecanismos em
empates, fragmentos quase coplanares, cadeias interrompidas por alpha zero ou
mais camadas. Essa qualificação ampliada, luzes, orçamento/configuração de
passes e weighted OIT continuam em **P10/F16**.

O compositor BGFX publica cor sobre a profundidade opaca: peeling da camada
base exige depth test LESS/LEQUAL/NEVER e write=false. Outros estados efetivos
são rejeitados antes da submissão, inclusive escrita transparente explícita.
CPU/wgpu possuem mecanismo de máscara de escrita; sua matriz ampliada de
overrides nas camadas também fica em P10. Isso não restringe os overrides dos
outros dez modos nem os estados capturados de anotações.

Há outra diferença de raster já delimitada: GL pode repetir um vértice
compartilhado da tesselação em POINTS com sorting de triângulos. O Core conserva
um ponto por vértice original da face; as amostras comparativas usam um canto
sem duplicação. P09 não encerra a qualificação integral P02, FreeCAD, MSAA,
outros drivers/plataformas ou o spike native/Dawn. Camadas com polygon offset,
clamp e viewports parcialmente externos exigem a qualificação própria P04/P10/P13.

## Execução local — 2026-09-28

- wgpu/Rust Debug: campanha de 22 casos CTest, todos aprovados. A primeira
  execução encontrou uma expectativa antiga de rejeição de SCREEN_DOOR em
  `CoinRenderTextureTest`; após atualizá-la para a capacidade P09, o caso passou.
  Os outros 21 casos, incluindo a matriz P09, passaram na execução inicial.
- BGFX Release: 47/47 casos CTest aprovados em Vulkan/OpenGL, incluindo
  readback, superfícies, OIT/layers, depth, clipping, raster, multitextura e P09.
  Nenhum caso CTest das duas campanhas retornou skip.
- Rust offline: 12 testes unitários e dois testes de shaders aprovados.
- Matriz P09 com GL obrigatório: CPU/wgpu e BGFX Vulkan/OpenGL aprovados,
  sem fallback de SORTED_LAYERS_BLEND e com as exceções de pixels declaradas acima.

As execuções offscreen usam RGBA8 sob Xvfb. A rota OpenGL local de referência
usa Mesa; isso não qualifica outros drivers ou a matriz física. Os avisos EGL
de DRI3 sob Xvfb não impediram as renderizações obrigatórias do GL.
