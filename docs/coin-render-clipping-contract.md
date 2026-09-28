# P01 / F03 — contrato de clipping CoinRender

Implementado em 2026-09-28. Perfil: até oito planos ativos por estado/draw,
triângulos, linhas e pontos capturados pela action. BGFX/OpenGL, BGFX/Vulkan e
Rust/wgpu executam o mesmo contrato; o protótipo Dawn/native rejeita triângulos
com clipping antes da submissão.

## Contrato Coin e referência GL estudada

- `SoClipPlane::doAction` acrescenta o plano quando `on` está ativo ou ignorado.
  `SoClipPlane::callback` usa esse caminho na action de captura.
- `SoClipPlaneElement::add` usa o model matrix vigente no nó do plano.
  `get(i, TRUE)` fornece o plano em mundo. A transformação posterior do shape
  não redefine esse plano.
- Os planos são acumulados por estado; push/pop de SoSeparator restaura o conjunto.
- `SoGLClipPlaneElement::addToElt` envia `(normal.xyz, -distance)` a glClipPlane
  e habilita seu índice; pop desabilita os planos acrescentados naquele escopo.
  Mantém-se o lado com `normal.dot(position) - distance >= 0`.

Arquivos: `src/nodes/SoClipPlane.cpp`, `src/elements/SoClipPlaneElement.cpp` e
`src/elements/GL/SoGLClipPlaneElement.cpp`. Nenhuma alteração em libCoin foi necessária.

## Responsabilidades e execução

**Wiring:** o builder captura `clipPlanesWorld` do elemento Coin em cada estado.
A deduplicação e a comparação de payload incluem o conteúdo dos planos;
modificações invalidam o reuso de câmera. O diagnóstico Recording mostra as
quatro componentes de cada equação quando há planos ativos.

**Core:** `CoinRenderClipCore.h` transforma SbPlane de mundo para espaço da câmera,
produzindo equações uniformes para os executores. Não recebe action/SoState nem
comanda a GPU. O mesmo Core calcula o intervalo visível de segmentos e testa o
centro de pontos, antes da expansão em quads; os atributos dos extremos recortados
são interpolados. Os estados expandidos não repetem o recorte dos strokes.
`CoinRenderStateCore.h` compara estados semanticamente, incluindo os planos.

**Infra BGFX:** recebe equações resolvidas por draw e aplica descarte no shader
comum de superfície, usado nos passes object, weighted OIT e sorted layers,
inclusive iluminação, textura e profundidade opaca auxiliar. Triângulos conservam
os vértices e a interpolação de iluminação/textura. Camera patch com planos ativos
faz reconstrução: os vértices BGFX contêm posições de câmera, que também precisam
ser atualizadas. Isso evita reutilizar posições antigas com uma nova câmera.

**Infra wgpu:** ABI privada **22**, estado de **1088 bytes**, draw ainda com 56 bytes.
`clip_plane_count` fica no offset 956, `clip_planes` no 960. O packer C++ usa o
mesmo Core; Rust valida o transporte e entrega uniformes ao WGSL. Os shaders
standard/line/point usam a mesma estrutura de uniformes. Rust não descobre planos
Coin nem decide novamente os espaços de coordenadas. C++ e Rust precisam ser
recompilados juntos.

**Publicação:** nove ou mais planos ativos na captura retornam UNSUPPORTED,
sem truncamento e sem alterar a imagem ou serial anterior. Um pedido válido
posterior pode renderizar normalmente. Planos não finitos, normais nulas ou
view matrix singular são inválidos. `COIN_RENDER_FEATURE_CLIP_PLANES` descreve
esse perfil implementado; não certifica outras plataformas/dispositivos.

## Evidências de fechamento

`CoinRenderClipPlaneTest` cobre captura transformada com escala/translação,
`on=FALSE`, restauração por Separator, cópia/reuso, equações em espaço da câmera,
segmentos/pontos antes da expansão, pixels opacos/transparentes, múltiplos planos
(incluindo o oitavo), câmera transladada/rotacionada, shape espelhado, perspectiva,
iluminação/textura com transparência, fundo/depth e limite/recovery. Exercita a
action com fast path indexed ligado e desligado.

No BGFX a mesma fixture tem células explícitas para OpenGL/Vulkan ×
object/weighted_oit/sorted_layers. No Rust há validação de payload excessivo,
normal nula e NaN, além da validação de shader existente. Regressões de composição,
anotações, depth, materiais, texturas, iluminação, RTT, readback e reuso acompanham
a entrega. Rodada final: **41 casos CTest passaram** (18 Rust/wgpu, 23 BGFX),
sem skip de caso CTest; **8 testes unitários Rust e 1 integração de shader passaram**,
offline. Compilações completas Debug/wgpu e Release/BGFX passaram. Comparações
opcionais Coin/GL indisponíveis continuam registradas como skips internos.

A comparação opcional Coin/GL não criou contexto offscreen neste ambiente,
inclusive sob Xvfb. Seu skip é registrado na saída; não alegamos paridade visual
GL completa. Ensaios GPU locais: wgpu/Vulkan em NVIDIA GeForce RTX 3060 Laptop GPU;
BGFX/Vulkan no mesmo dispositivo e BGFX/OpenGL sob Xvfb. Cor offscreen RGBA8,
tolerância de 6 níveis por canal nos pontos amostrados. Nenhum caso CTest de
clipping depende apenas da referência CPU quando há adaptador GPU disponível.

## Limites que permanecem abertos

- Qualificação visual GL de bordas, strokes/padrões, MSAA e cenas FreeCAD reais.
- Matriz de outras GPUs, plataformas e superfícies; RTT com clipping ainda exige
  a qualificação de dependências/publicação prevista em F14.
- Suporte além de oito planos e transforms projetivos arbitrários não faz parte
  deste perfil; testar range/clamp/precisão continua em F12.
- Clipping não cria tampas de seção, nem implementa SoDrawStyle ou SoText2.

P01 fica fechado nesse perfil de implementação e ensaio GPU. F03 conserva as
pendências de qualificação acima; próxima implementação: P02 / SoDrawStyle.
