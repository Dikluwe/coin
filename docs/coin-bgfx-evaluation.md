# Avaliação BGFX sobre o `CoinRenderFramePlan` experimental

Esta branch adiciona um conector **BGFX (Vulkan ou OpenGL)** a
`CoinRender`. A ação BGFX é `CoinBgfxAction`, um tipo Coin próprio.
`CoinRenderAction` permanece como base compartilhada e API de compatibilidade;
BGFX não é wgpu-native,
nem transforma essa ação em uma API gráfica estável. `libCoin` e a ABI pública
do Coin 4 não são alteradas.

## Por que a arquitetura ajuda

O Wiring existente continua a capturar Open Inventor com `SoCallbackAction` e
entrega o mesmo `CoinRenderFramePlan`. `CoinBgfxLowering` valida um subconjunto e converte
cor/material, geometria e matrizes sem incluir headers BGFX. A Infra
`CoinBgfxBackend` usa somente o plano convertido para criar shaders,
buffers, apresentar na janela ou fazer readback offscreen. Shell e
diagnósticos existentes traduzem `UNSUPPORTED` e erros de backend. Assim a
comparação isola a troca do executor, sem duplicar um segundo scene graph.

## Perfil implementado

- Linux, BGFX com renderer Vulkan por padrão ou OpenGL com
  `COIN_BGFX_RENDERER=opengl`; alvos offscreen e janelas Xlib simultâneos
  compartilham o runtime na thread da API.
- Triângulos indexados com `BASE_COLOR` ou termos PHONG completos
  (ambiente, difusa, especular, emissão, `shininess`; luz direcional, pontual
  e spot). Materiais e alpha por vértice/face são preservados. Linhas,
  `SoIndexedLineSet` e pontos são expandidos deterministicamente para
  triângulos em espaço de tela, incluindo largura, tamanho e padrão.
- `SoTexture2` CPU-backed em RGBA8, UV explícita/procedural, matriz de textura,
  `MODULATE`, `REPLACE`, `DECAL`, `BLEND`, `REPEAT`/`CLAMP` e filtro
  nearest/linear. Alpha de textura participa de object blend, depth peeling e
  weighted OIT. Até oito unidades `SoTextureUnit` têm UVs, transformações e
  modelos independentes. Linhas e pontos preservam a textura durante a expansão.
  Fog `HAZE`, `FOG` e `SMOKE` é aplicado depois da cascata de texturas, sem alterar
  alpha. Veja [bgfx-surface-features.md](bgfx-surface-features.md).
  RTT direto e texturas GPU-token continuam fora do perfil.
- Viewports/scissors por draw, inclusive múltiplas subviewports. Todos os modos
  Coin de transparência têm caminhos próprios; modos imediatos preservam writes
  de depth e modos atrasados não escrevem. Offscreen publica RGBA e depth GPU,
  síncronos ou por tickets assíncronos; janela apresenta direto na swapchain.
  Veja [bgfx-transparency-readback.md](bgfx-transparency-readback.md).
- Ordem de draws preservada com `ViewMode::Sequential`; shaders SPIR-V e GLSL
  330 gerados por `shaderc` durante o build. O readback OpenGL é invertido
  por linhas para cumprir a mesma orientação RGBA do Vulkan.
- Um cache privado por target retém o plano convertido e os buffers de geometria
  quando a revisão não nula do `CoinRenderFramePlan`, dimensões e convenção de depth
  continuam iguais. O payload de geometria é limitado a 32 MiB; revisão zero
  nunca reutiliza. `COIN_RENDER_TRACE_PHASES=1` informa `resource_cache_hit`.
- `CAMERA_PATCH` preserva buffers GPU e atualiza somente MVPs para uma
  mudança de câmera validada; `COIN_BGFX_DISABLE_CAMERA_PATCH=1` permite
  medir o caminho completo no mesmo build.
- Profundidade offscreen é publicada por padrão; usar
  `CoinRenderTarget::setDepthReadbackEnabled(FALSE)` para solicitar somente cor.
- Coordenadas procedurais nas unidades adicionais,
  RTT direto, Wayland e texturas residentes externas retornam `UNSUPPORTED` em vez de aparentar paridade.
  O PHONG BGFX avalia materiais e até oito luzes direcionais, pontuais ou spot
  por vértice e interpola a cor iluminada (Gouraud), como o GL normal do Coin.
  O mesmo vertex shader é usado no caminho opaco, depth peeling e weighted OIT.
  Veja [bgfx-gouraud-parity.md](bgfx-gouraud-parity.md) para oráculos e limites.
- A query de capacidades v2 faz um `prepare` offscreen 1x1 temporário, avança
  um frame e libera sua referência ao runtime antes de retornar. Ela informa renderer, IDs de
  vendor/device, limites e formatos de framebuffer, MRT, independent blend,
  compute e timestamps reais. Alvos ativos na mesma thread compartilham o runtime;
  uma sonda em outra thread ou sem orçamento de views retorna
  `COIN_RENDER_PROBE_BUSY`, sem confundir ocupação com GPU ausente.
  Para `XLIB_WINDOW`, a sonda valida GPU/renderer; a apresentação só é
  comprovada ao preparar uma superfície Xlib real. Os bits `features` continuam
  descrevendo separadamente o perfil implementado.

### Política de múltiplos alvos

O runtime é compartilhado por referência contada, na mesma thread de API.
Cada alvo tem seus próprios recursos, cache e reserva de IDs de view;
cada janela possui uma swapchain independente. Resize e destruição de uma
janela não reinicializam o dispositivo nem encerram as outras viewports.
O último alvo preparado libera o runtime. O orçamento de views limita a
quantidade de alvos simultâneos, com rejeição explícita quando esgotado.
O conector não deve coexistir no processo com outro usuário direto de BGFX.
Veja `bgfx-multiple-viewports.md` para arquitetura, limites e validação.

Falhas fatais reportadas pelo callback BGFX, inclusive `DeviceLost`, são
convertidas em `DEVICE_LOST`/`BACKEND_ERROR` em todo o runtime compartilhado.
Cada alvo descarta seu backend; o último chama `bgfx::shutdown()` na thread
da API. Depois de liberar todos os alvos afetados, o frame seguinte
cria um runtime novo e reconstrói programas, uniforms, buffers, texturas e
framebuffers a partir do `CoinRenderFramePlan`. Os testes injetam perda durante resize e
durante submissão, além de falha parcial de `prepare`, e comprovam que uma nova
inicialização funciona em Vulkan e OpenGL.

## Build reproduzível

Instale um pacote CMake `bgfx::bgfx` e seu `shaderc` correspondente. O
adaptador foi desenvolvido com `bgfx.cmake` e BGFX em
`81d81fba72c42d348c589514c774bbfe01e110fa`. O build **não** baixa
dependências automaticamente. Exemplo após a instalação do BGFX em `$prefix`:

```sh
cmake -S . -B /tmp/coin-bgfx-release \
  -DCMAKE_BUILD_TYPE=Release -DCOIN_BUILD_WGPU=ON \
  -DCOIN_WGPU_BACKEND=BGFX -DCOIN_BUILD_TESTS=ON \
  -DCOIN_BUILD_WGPU_BENCHMARKS=ON -DCOIN_BUILD_WGPU_WINDOW_EXAMPLE=ON \
  -DCMAKE_PREFIX_PATH="$prefix" \
  -DCOIN_BGFX_SHADERC_EXECUTABLE="$prefix/bin/shaderc" \
  -DCOIN_BGFX_SHADER_INCLUDE_DIR="$prefix/include/bgfx"
cmake --build /tmp/coin-bgfx-release --target CoinBgfxCoreTest CoinBgfxOffscreenTest CoinBgfxTransparencyTest CoinBgfxWindowTest CoinRenderBackendContractTest coin_render_window_cone coin_render_viewer coin_render_gl_benchmark -j4
ctest --test-dir /tmp/coin-bgfx-release -R '^WgpuBgfx' --output-on-failure
xvfb-run -a -s '-screen 0 1024x768x24 +extension GLX +render -noreset' \
  ctest --test-dir /tmp/coin-bgfx-release -R '^CoinBgfxWindow' --output-on-failure
# Em uma sessão X11 real: COIN_BGFX_RENDERER=opengl /tmp/coin-bgfx-release/bin/coin_render_viewer
env EGL_PLATFORM=x11 \
  __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json \
  COIN_BGFX_RENDERER=opengl \
  /tmp/coin-bgfx-release/bin/CoinBgfxOffscreenTest
env VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/radeon_icd.json \
  COIN_GLX_PIXMAP_DIRECT_RENDERING=1 \
  /tmp/coin-bgfx-release/bin/coin_render_gl_benchmark \
  --backend both --size 512 --warmup 8 --frames 30 \
  --readback color --rgba-output copy --scene /caminho/Assembly.iv
```

O teste Core roda sem GPU. O teste offscreen desenha um triângulo vermelho,
confere orientação, cache e `CAMERA_PATCH` nos dois renderers em processos separados.
Os testes de paridade do perfil WebGPU amplo não são uma afirmação de suporte
BGFX e devem continuar rodando no backend Rust/Recording.

O teste X11 verifica os pixels da janela Vulkan, resize, suspensão/restauração e
ausência de readback. Variantes adicionais exercitam o adapter `SoRenderManager`
em Vulkan e OpenGL; OpenGL é validado pela submissão porque `XGetImage` não é
válido para esse swapchain, separadamente em Vulkan e OpenGL. Em 25/09/2026,
7/7 testes direcionados de BGFX e contrato compartilhado passaram no Xvfb;
`coin_render_viewer --frames 5` e `coin_render_window_cone --frames 5` também passaram
em ambos os renderizadores. Isso não mede latência de apresentação nem cobre
a viewport real do FreeCAD.

Com os quatro testes de transparência (objeto e camadas), 11/11 passaram no Xvfb sem forçar
um ICD Vulkan. Forçar RADV nesse servidor fez apenas o teste de apresentação
Vulkan falhar por ausência de DRI3; o teste offscreen de composição passou.

Xvfb continua útil para regressão funcional, mas não certifica hardware. A matriz
física AMD/RADV, AMD/radeonsi, Intel e NVIDIA, para Vulkan e OpenGL, está
descrita em `docs/coin-bgfx-gpu-matrix.md` e é executada pelo workflow manual
`BGFX physical GPU matrix`. Cada célula verifica o driver antes dos testes e
arquiva o inventário, as capacidades reais e o resultado CTest.

## Ensaio controlado de transparência — 25/09/2026

`CoinBgfxTransparencyTest` constrói a mesma cena Open Inventor para
Coin/OpenGL e BGFX (Vulkan ou OpenGL), a 128² e `BASE_COLOR`.
Há um quadrilátero azul opaco atrás de dois semitransparentes (vermelho e
verde, alpha 0,5), inseridos fora da ordem de profundidade. No caso
`layered`, as superfícies são paralelas; no `crossing`, vermelho e verde
se cruzam. A composição BGFX é validada contra cores analíticas em todo o
interior dos quadrados, incluindo o cruzamento. Coin/GL é uma comparação
diagnóstica: o teste conta as diferenças na imagem inteira, mas não usa a
saída Coin como oráculo para aprovar BGFX.

```sh
cmake --build /tmp/coin-bgfx-release --target CoinBgfxTransparencyTest -j4
xvfb-run -a -s '-screen 0 1024x768x24 +extension GLX +render -noreset' \
  env COIN_GLX_PIXMAP_DIRECT_RENDERING=1 COIN_WGPU_REQUIRE_GL_REFERENCE=1 \
  VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/radeon_icd.json \
  /tmp/coin-bgfx-release/bin/CoinBgfxTransparencyTest \
  --output-prefix /tmp/coin-transparency-vulkan
# Repetir com COIN_BGFX_RENDERER=opengl para BGFX/OpenGL.
```

Nesta execução em Xvfb, RGB nos pixels esquerdo/direito do cruzamento:

| Caminho e modo | Esquerda | Direita |
| --- | --- | --- |
| BGFX/Vulkan, objeto | (64, 128, 64) | (64, 128, 64) |
| BGFX/OpenGL, objeto | (64, 128, 63) | (64, 128, 63) |
| Coin/OpenGL, `SORTED_OBJECT_BLEND` | (64, 128, 63) | (64, 128, 63) |
| Coin/OpenGL, `SORTED_LAYERS_BLEND` | (128, 64, 63) | (64, 128, 63) |

A tabela amostra somente dois pixels, e por isso ocultava o defeito central.
Tomando BGFX `sorted_layers` como referência visual e tolerando até 2 níveis
por canal, Coin/GL por objeto divergiu em 5.202 dos 16.384 pixels.
Coin/GL `SORTED_LAYERS_BLEND` acertou os dois lados, mas divergiu em 612
pixels: uma faixa vertical de 6×102 em `x=61..66`, `y=13..114`, onde
aparece azul do quadrilátero de fundo em vez das duas camadas misturadas.
Nas camadas paralelas e no caso opaco à frente, a diferença foi zero.

O fragment program legado do Coin usa um limiar de profundidade fixo
`0.0040000002` em `src/actions/SoGLRenderAction.cpp`. Ele é uma causa
plausível para a faixa junto à interseção; ainda falta um A/B alterando esse
limiar para confirmar. O modo do Coin também pode cair para ordenação por
objeto quando faltam extensões ou formato depth/alpha; o teste mantém o
rótulo `layers-or-fallback` por esse motivo.

A falha ampla do modo por objeto é limite da técnica `SORTED_OBJECT_BLEND`,
compartilhado por Coin/GL, Rust/wgpu e BGFX. A faixa residual do Coin em
camadas é distinta e não deve ser normalizada como resultado correto. O
ensaio não mede desempenho nem testa a viewport real do FreeCAD; em Xvfb,
GL pode usar renderização por software.

### Seleção e mapeamento dos modos Coin

O padrão é `COIN_BGFX_TRANSPARENCY=auto`, preservando `NONE`, screen-door,
blend/aditivo imediato, atrasado, ordenado por objeto, ordenado por triângulo e
sorted layers. O número de draws não ativa OIT automaticamente. Veja os
contratos e limites em [bgfx-transparency-readback.md](bgfx-transparency-readback.md).

`SORTED_LAYERS_BLEND`, como no Coin/GL, é uma configuração global da action
que sobrescreve nós `SoTransparencyType` locais. `weighted_oit` permanece
uma extensão explícita, não um alias dos modos Coin de sorting por triângulo.

### Modo BGFX `sorted_layers` (experimental)

`COIN_BGFX_TRANSPARENCY=sorted_layers` ativa quatro passagens de depth peeling
por pixel no BGFX, tanto em Vulkan quanto em OpenGL. Em `auto`, essa técnica é
escolhida por `SORTED_LAYERS_BLEND`. A Infra mantém
alvos RGBA8/D32F por passagem, reutiliza o `CoinRenderFramePlan` e compõe as camadas
de trás para a frente sobre a cena opaca. Isso não muda a ABI pública de
`libCoin` nem solicita ao usuário outro tipo de nó Open Inventor.

No cruzamento do ensaio acima, os pixels BGFX/OpenGL passaram de
`(64,128,63)/(64,128,63)` no modo objeto para
`(128,64,63)/(64,128,63)` em `sorted_layers`. BGFX/Vulkan deu
`(64,128,64)/(64,128,64)` antes e a mesma ordem espacial depois, com
variação de arredondamento de até um nível RGB. Coin/GL em camadas igualou
apenas os dois pontos amostrados, não a imagem. O teste valida BGFX contra a
composição analítica. Um terceiro caso, com
quadrilátero opaco à frente, exige `(0,0,255)`; ele revelou que o alvo
offscreen BGFX anterior não tinha attachment de depth. A Infra agora cria
RGBA8 mais depth explícito: D24S8 quando anunciado pelo BGFX, ou D32F.
Na AMD/RADV desta máquina, D24S8 não é anunciado e D32F é usado. O teste
offscreen passou com o ICD RADV forçado, tanto em `object` quanto em
`sorted_layers`; forçar esse ICD no teste de janela Xvfb ainda exige DRI3.

```sh
cmake --build /tmp/coin-bgfx-evaluation-build --target CoinBgfxTransparencyTest coin_render_viewer -j4
xvfb-run -a -s '-screen 0 1024x768x24 +extension GLX +render -noreset' \
  env COIN_GLX_PIXMAP_DIRECT_RENDERING=1 \
  ctest --test-dir /tmp/coin-bgfx-evaluation-build \
  -R '^(WgpuBgfx|CoinRenderBackendContract)' --output-on-failure
# Janela A/B: repetir com COIN_BGFX_RENDERER=vulkan e opengl.
COIN_BGFX_RENDERER=opengl COIN_BGFX_TRANSPARENCY=object \
  /tmp/coin-bgfx-evaluation-build/bin/coin_render_viewer --transparency-demo
COIN_BGFX_RENDERER=opengl COIN_BGFX_TRANSPARENCY=sorted_layers \
  /tmp/coin-bgfx-evaluation-build/bin/coin_render_viewer --transparency-demo
COIN_BGFX_RENDERER=opengl COIN_BGFX_TRANSPARENCY=weighted_oit \
  /tmp/coin-bgfx-evaluation-build/bin/coin_render_viewer --transparency-demo
```

Quatro camadas são um limite fixo deste protótipo, não cobertura geral da
transparência do Coin. Texturas, iluminação PHONG, subviewports e
oclusão opaca compartilham agora o mesmo lowering validado. As quatro duplas
RGBA8/D32F acrescentam cerca de 8 MiB a 512² ou 253 MiB a 3840×2160,
sem contar framebuffer base, readback e overhead do driver. A emissão de
draws também cresce com as passagens. Não há benchmark Release do modo novo
nem teste da viewport real do FreeCAD; os números de desempenho anteriores
continuam sendo do modo objeto.

## Integração com o FreeCAD real — 26/09/2026

`CoinRenderManagerAdapter` liga diretamente um `SoRenderManager` ao alvo
nativo, encaminha resize em pixels físicos e preserva o mesmo contrato da
ação WGPU. O patch opt-in para o `QuarterWidget` está em
`examples/coinrender/freecad_coin_render.patch.gz` e é aplicado com:

```sh
gzip -dc /caminho/coin/examples/coinrender/freecad_coin_render.patch.gz | \
  git -C /caminho/freecad apply
```

O FreeCAD deve ser configurado com `FREECAD_COIN_WGPU_EXPERIMENTAL=ON` e o
pacote instalado `CoinRender` no `CMAKE_PREFIX_PATH`. Em execução,
`FREECAD_COIN_WGPU=1` ativa o caminho; sem a variável, o `QuarterWidget`
continua usando Coin/GL. O adaptador atual é deliberadamente restrito a Qt 6
com o plugin X11/xcb.

O teste real abriu `PartDesignExample.FCStd` no FreeCAD 26.3.0dev, criou BGFX
Vulkan na GPU NVIDIA (`vendor_id=0x10de`, `device_id=0x2560`) e entregou ao
backend 1.303 vértices, 2.264 índices e quatro draws por frame. O `QuarterWidget`
mantém seu `QOpenGLWidget` lógico para eventos e callbacks do FreeCAD, enquanto
a apresentação BGFX usa um filho X11 nativo isolado e recortado à viewport com
`WA_DontCreateNativeAncestors`. Uma região de entrada XFixes vazia encaminha
hit-test e hover ao widget Coin.

A preseleção de faces agora é nativa no frame BGFX: `SoFCSelection` prepara o
estado durante `SoCallbackAction`, `SoBrepFaceSet` remapeia o índice topológico
para materiais por face, e o fast path só poda os tipos Coin exatos para não
suprimir callbacks de subclasses. Não há mais troca para Coin/GL durante hover.
A validação mostrou a face `Face6` em ciano e a remoção do realce sem apagar a
peça. O painel inferior permaneceu visível. O filho não é marcado como
`OpenGLSurface` e não é elevado via X11 a cada frame; o Qt mantém o stacking.
Eventos de exibição solicitam novo desenho e a troca do viewport destrói o
adapter e o XID antigos antes de criar os novos. Os avisos
`QOpenGLContext::makeCurrent`/`QRhiGles2` não ocorreram na sessão final.

O NaviCube também percorre agora seu scene graph retido durante
`SoCallbackAction`. O adaptador aceita um root composto pelo superscene efetivo
do `SoRenderManager`, foreground e decorations, preservando câmera e headlight.
`SoAnnotation` cria camadas monotônicas; cada camada limpa somente o depth de seu
subviewport e é emitida depois da cena, inclusive após `weighted_oit` e
`sorted_layers`. No teste Vulkan com `weighted_oit`, o frame passou para 3.272
vértices, 4.235 índices e 36--38 draws, incluindo 7--9 mudanças de textura do
NaviCube. Restaurar e maximizar manteve a apresentação em 1.920x710 dentro da
janela 1.920x1.008, preservando 298 pixels para o painel inferior.

`SoDepthBuffer` e `SoPolygonOffset` são capturados por draw. BGFX aplica
depth test/write/function, depth range e offset restrito a fill/line/point,
inclusive nos overlays e nas passagens de transparência. O contrato e as
aproximações de precisão estão em [wgpu-depth-contract.md](wgpu-depth-contract.md).
A integração visual do NaviCube/axis cross/rubber-band no FreeCAD deve ser
validada no aplicativo; os testes do Coin cobrem o comportamento coplanar.

A geometria indexada preserva índices de material preexistentes quando uma ação
substitui a lista por um único material: a captura de estado limita a consulta
ao último material válido, como Coin/GL, em vez de rejeitar o frame. Um teste
de regressão cobre esse caso. O limite restante mais importante é arquitetural:
o runtime BGFX ainda aceita somente um alvo ativo por processo; múltiplas abas
exigem um runtime global com referência contada e uma swapchain por janela.

O ambiente local ainda contém uma instalação PySide 6.6 incompatível
com o Qt 6.4 do build; isso produz avisos de módulos Python, mas não impediu o
documento PartDesign nem o frame BGFX.

### Modo BGFX `weighted_oit` (experimental)

`COIN_BGFX_TRANSPARENCY=weighted_oit` ativa Weighted Blended Order-Independent
Transparency. A cena opaca continua na view base; uma passagem adicional
reconstrói somente a profundidade opaca e acumula toda a geometria transparente
em dois attachments MRT (`RGBA16F` para cor/peso e `R16F` para revelação). Uma
passagem fullscreen compõe o resultado sobre a cena opaca. O backend exige
`BGFX_CAPS_BLEND_INDEPENDENT`, três attachments totais e os formatos
float amostráveis; quando isso não existe, retorna `UNSUPPORTED` em vez de
alterar silenciosamente a técnica.

Esse modo não é uma versão aproximada do código do Coin/GL: é uma estratégia
de cobertura comum à Infra BGFX. Ela renderiza cada draw transparente uma vez,
independentemente da ordem de submissão, contra quatro repetições no protótipo
`sorted_layers`. Em contrapartida, mistura fragmentos por pesos de alpha e
profundidade e não reproduz exatamente a composição source-over ordenada.
`sorted_layers` permanece a referência de qualidade para cenas com até quatro
camadas; `weighted_oit` é o candidato de interação com profundidade complexa.

No ensaio de 128², BGFX/Vulkan produziu `(110,80,64)` nas camadas paralelas,
`(109,82,64)/(81,110,64)` no cruzamento e `(0,0,255)` com o objeto opaco à
frente. BGFX/OpenGL variou no máximo um nível nos pontos amostrados. O teste
percorre toda a região interna: exige dominância da superfície próxima em cada
lado do cruzamento, intervalo de composição transparente e ocultação opaca
exata. Ele não exige igualdade com depth peeling, pois isso invalidaria a
aproximação que está sendo avaliada.


O teste de estresse adicional usa 32 superfícies PHONG cruzadas, com alphas
`0.001`, `0.08`, `0.35` e `0.999`, e renderiza a mesma cena nas duas ordens de
submissão. Ele limita a diferença por canal, varre toda a região interna contra
preto/branco espúrio e varre a moldura externa contra halos. Casos isolados
validam a opacidade analítica de 32 camadas com alpha `0.001` e uma camada com
alpha `0.999`. Outra cena combina, na mesma superfície transparente, textura
RGBA em `MODULATE` com iluminação PHONG em duas intensidades. Um único
`SoIndexedFaceSet`, com `PER_FACE`, contém dois triângulos transparentes que se
intersectam e exige a inversão de dominância vermelho/verde nos dois lados do
cruzamento. Os casos básicos mantêm ainda objetos opacos à frente e atrás das
superfícies transparentes. A função de peso é limitada a 128 por fragmento
para manter dezenas de contribuições abaixo do máximo FP16.
Os attachments weighted acrescentam aproximadamente 14 bytes por pixel
(RGBA16F + R16F + depth de 32 bits): 3,5 MiB em 512² ou 110,7 MiB em 4K,
sem contar framebuffer base, readback e overhead do driver. Ainda falta benchmark Release desse modo. A viewport real do FreeCAD foi
validada separadamente na integração Qt 6/X11 abaixo. O modo permanece experimental
e não muda a ABI pública do Coin.

## Medição inicial — 24–25/09/2026

Release, AMD Radeon Graphics RADV RENOIR (Vulkan) e radeonsi (GL), 512²,
`BASE_COLOR`, RGBA com readback, 8 frames de aquecimento e 30 medidos. A
cena é exportada do FreeCAD, **não** a viewport real. O benchmark executa
BGFX antes de GL; as linhas abaixo são observações de uma rodada, não uma
distribuição contrabalançada.

| Cena | BGFX mediana / p95 (ms) | GL mediana / p95 (ms) | BGFX/GL |
| --- | ---: | ---: | ---: |
| PartDesign | 1,672 / 1,747 | 0,337 / 0,359 | 4,96× |
| Assembly | 1,704 / 1,754 | 0,344 / 0,356 | 4,95× |

Antes do cache, o Assembly marcou 3,169 ms de mediana; após o cache marcou
1,704 ms (46% menor), mas ainda longe do GL. Num trace estático do Assembly,
`lower_ms` caiu de cerca de 0,5 ms para <0,001 ms e `upload_ms` para
<0,001 ms em hits; `read_wait_ms` continuou aproximadamente 1,7–2,1 ms.
Essa espera inclui progresso de frames/driver/GPU, **não** é uma query de
tempo de execução GPU isolada. `submit_frame_ms` é apenas a chamada BGFX
de entrega do frame. O pico RSS do Assembly em processos separados foi
85.284 KiB para BGFX e 95.756 KiB para GL; isso inclui bibliotecas/runtime
distintos e não comprova menor uso de memória GPU.

Qualidade visual `BASE_COLOR`, 512², GL alinhado por inversão de linhas:

| Cena | MAE RGB | IoU de silhueta |
| --- | ---: | ---: |
| PartDesign | 0,656 | 0,999978 |
| Assembly | 0,907 | 1,000000 |
| EngineBlock | 0,640 | 0,999968 |
| BIM | 0,681 | 0,999988 |

O gate simples MAE ≤1 e IoU ≥0,99 passou, mas a MAE é maior do que a
paridade WebGPU já documentada para EngineBlock e BIM. Converter a cor de
vértice de UNORM8 para float não reduziu a MAE de EngineBlock (0,640 em
ambas as variantes); a diferença não deve ser atribuída só à quantização
da cor do material. Imagens PPM desta rodada foram geradas apenas em `/tmp`.

## CAMERA_PATCH e diagnóstico do gargalo — 25/09/2026

Quando o Core comprova que só a câmera mudou, o Wiring passa
`CoinRenderFrameReuseDecision::CAMERA_PATCH` ao BGFX. O Core recalcula apenas
as matrizes dos draws; a Infra mantém vértices, índices e buffers GPU.
Revisão-base, tamanho e convenção de profundidade precisam coincidir; caso
contrário, há lowering completo. A revisão do cache só avança após readback
bem-sucedido. `COIN_BGFX_DISABLE_CAMERA_PATCH=1` desliga apenas essa
otimização para comparação A/B no mesmo binário. O trace informa
`camera_patch`, `resource_cache_hit` e `read_wait_frames`.

Release, mesma AMD e cenas exportadas, 512², `BASE_COLOR`, RGBA com
readback, 8 warmup + 60 frames de câmera móvel por rodada. Três rodadas
por modo, intercaladas; valores abaixo são as medianas das medianas/p95
de cada rodada, não percentis agregados de 180 frames:

| Cena | BGFX sem patch mediana / p95 (ms) | BGFX com patch mediana / p95 (ms) | Interpretação |
| --- | ---: | ---: | --- |
| PartDesign | 1,944 / 2,332 | 1,936 / 2,474 | Diferença dentro da variação |
| Assembly | 3,080 / 3,599 | 1,931 / 2,419 | Mediana 37,3% menor; p95 32,8% menor |

Reprodução (repetir para `PartDesign.iv`):

```sh
SCENE=/caminho/Assembly.iv
for disabled in 1 0 0 1 1 0; do
  env VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/radeon_icd.json \
    COIN_GLX_PIXMAP_DIRECT_RENDERING=1 \
    COIN_BGFX_DISABLE_CAMERA_PATCH="$disabled" \
    /tmp/coin-bgfx-release/bin/coin_render_gl_benchmark \
    --backend both --size 512 --warmup 8 --frames 60 --dynamic \
    --readback color --rgba-output copy --scene "$SCENE"
done
```

O GL permaneceu em cerca de 0,4 ms/frame nessas rodadas: o patch melhora
BGFX, mas não o torna competitivo com GL nesse perfil. No Assembly, o trace
passou de aproximadamente 0,7–1,2 ms de lowering e 0,1–0,6 ms de upload
por frame para ~0,002 ms e <0,001 ms, respectivamente. O pico RSS em
quatro processos separados foi 86.988–87.536 KiB sem patch e
85.060–85.196 KiB com patch; isso não mede memória GPU. O teste Vulkan
offscreen compara pixels do patch e do lowering completo e exige igualdade
exata em 32². A equivalência visual da cena FreeCAD com câmera móvel ainda
não foi medida em imagens exportadas.

Com `COIN_RENDER_TRACE_PHASES=1 COIN_WGPU_GPU_TIMESTAMPS=1`, o conector
avança frames BGFX adicionais, **só para diagnóstico**, até obter a query
Vulkan do frame submetido. `gpu_frame_ms` é a duração GPU relatada pelo
BGFX; `gpu_query_drain_ms`/ `gpu_query_frames` registram a perturbação.
Numa rodada de 30 frames de Assembly com patch, a mediana foi ~0,126 ms
para o frame GPU e ~1,85 ms para a espera CPU do readback; três avanços
BGFX foram necessários para publicar cada readback. Não se deve subtrair
esses tempos como se fossem estágios seriais independentes. Medições de
performance comparáveis com GL devem deixar `COIN_WGPU_GPU_TIMESTAMPS`
desligado.

## Avaliação OpenGL do BGFX — 25/09/2026

O mesmo build Release e o mesmo `CoinRenderFramePlan` agora executam BGFX/OpenGL por
`COIN_BGFX_RENDERER=opengl`; Vulkan continua sendo o padrão. O teste
`CoinBgfxOffscreenTest` passou nos dois renderers. A primeira execução OpenGL
revelou readback invertido; a Infra agora normaliza as linhas antes de publicar
os pixels. Não há alteração de ABI pública do Coin 4.

Este computador tem AMD e NVIDIA. Para comparar com Coin/GL na AMD, a medição
fixou `EGL_PLATFORM=x11`,
`__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json`
e o ICD RADV. `glxinfo -B` e a seção X11 de `eglinfo -B` identificaram
radeonsi/RENOIR; o BGFX não reportou PCI vendor/device id para OpenGL, então
a atribuição da GPU BGFX decorre da seleção EGL Mesa/X11, não de telemetria
direta do BGFX. Sem fixar essa seleção, uma comparação nesta máquina é ambígua.

Release, 512², `BASE_COLOR`, RGBA com readback, cenas `.iv` exportadas do
FreeCAD (não a viewport real), 8 frames de aquecimento e 60 medidos por
processo. Três rodadas intercaladas por cena e renderer. Cada valor abaixo é
a mediana das três medianas ou dos três p95 por rodada; não é o percentil
agregado de 180 frames. Coin/GL e BGFX foram medidos em processos separados.

| Cena | BGFX/OpenGL mediana / p95 (ms) | Coin/GL mediana / p95 (ms) | BGFX/Vulkan mediana / p95 (ms) |
| --- | ---: | ---: | ---: |
| PartDesign | 0,364 / 0,450 | 0,326 / 0,345 | 1,688 / 1,866 |
| Assembly | 0,376 / 0,446 | 0,345 / 0,411 | 1,733 / 2,033 |

BGFX/OpenGL ficou aproximadamente 4,6× mais rápido que BGFX/Vulkan na
mediana deste perfil, mas ainda 9–12% mais lento que Coin/GL. A causa exata
da diferença entre os renderers BGFX não está isolada: o readback síncrono,
progresso de frames e driver são medidos juntos; trace/timestamp perturba
fortemente o tempo. Não atribuir todo o ganho à execução GPU ou ao row flip.

Pico RSS mediano dos três processos por cena, em KiB:

| Cena | BGFX/OpenGL | Coin/GL | BGFX/Vulkan |
| --- | ---: | ---: | ---: |
| PartDesign | 109.052 | 91.760 | 80.060 |
| Assembly | 114.200 | 95.920 | 85.204 |

RSS inclui bibliotecas/runtime/driver; não representa somente memória GPU.
Na comparação visual das quatro cenas a 512², o IoU de silhueta foi 1,0
após inverter as linhas GL e a MAE RGB foi PartDesign 0,655, Assembly 0,907,
EngineBlock 0,638 e BIM 0,681. A diferença visual é essencialmente a mesma
do BGFX/Vulkan, sem evidência de ganho de qualidade pelo backend OpenGL.

Reprodução após o build acima (substituir o caminho da cena e repetir para
`PartDesign.iv` e `Assembly.iv`):

```sh
SCENE=/caminho/Assembly.iv
for backend_renderer in bgfx:opengl gl:opengl bgfx:vulkan; do
  backend=${backend_renderer%:*}; renderer=${backend_renderer#*:}
  env EGL_PLATFORM=x11 \
    __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json \
    VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/radeon_icd.json \
    COIN_BGFX_RENDERER="$renderer" COIN_GLX_PIXMAP_DIRECT_RENDERING=1 \
    /usr/bin/time -f 'peak_rss_kib=%M' \
    /tmp/coin-bgfx-release/bin/coin_render_gl_benchmark \
    --backend "$backend" --size 512 --warmup 8 --frames 60 \
    --readback color --rgba-output copy --scene "$SCENE"
done
```

## Correção da cor de fundo — 25/09/2026

A análise pixel a pixel das imagens anteriores encontrou uma causa única
para a MAE do BGFX/OpenGL: todos os pixels de malha eram iguais aos do
Coin/GL, mas todo pixel de fundo tinha `(26,26,26)` em BGFX contra
`(25,25,25)` em Coin/GL. O Core arredondava o clear de `0,1` para um
`uint32_t` antes da chamada ao BGFX. Agora conserva os quatro floats do
`CoinRenderFramePlan` e a Infra usa a paleta float de clear do BGFX. Isso preserva
a conversão final do renderer, inclusive no caminho de câmera em cache;
uma mudança pequena de clear já invalida o `CAMERA_PATCH`.

Nova comparação Release a 512², mesmas cenas `.iv`, BGFX/OpenGL ou Vulkan
contra Coin/GL com inversão de linhas, na seleção AMD/Mesa/X11 acima:

| Cena | MAE RGB BGFX/OpenGL antes → depois | MAE RGB BGFX/Vulkan depois | IoU OpenGL depois |
| --- | ---: | ---: | ---: |
| PartDesign | 0,655 → 0 | 0,00136 | 1,0 |
| Assembly | 0,907 → 0 | 0 | 1,0 |
| EngineBlock | 0,638 → 0 | 0,00180 | 1,0 |
| BIM | 0,681 → 0 | 0,00068 | 1,0 |

O zero significa **RGB idêntico** nessas quatro imagens; não afirma
igualdade de alpha, depth, outros materiais ou da viewport real do FreeCAD.
No Vulkan, restaram apenas 0–3 pixels divergentes por imagem, em bordas.
O teste Core agora exige clear float sem quantização e recusa um camera patch
se o clear mudar mesmo que a versão anterior arredondasse para o mesmo byte.

Na repetição de tempo com 8 warmup e 3 × 60 frames em processos separados,
mediana das três medianas e dos três p95 por rodada (ms):

| Cena | BGFX/OpenGL | Coin/GL | BGFX/Vulkan |
| --- | ---: | ---: | ---: |
| PartDesign | 0,486 / 0,992 | 0,411 / 0,806 | 2,149 / 2,740 |
| Assembly | 0,541 / 0,986 | 0,445 / 0,886 | 2,224 / 2,927 |

Pico RSS mediano (KiB) PartDesign: 108.896 BGFX/OpenGL, 91.748 Coin/GL,
79.876 BGFX/Vulkan. Assembly: 114.052, 95.980 e 85.216, respectivamente.
Não é uma medida de memória GPU.

Todos os caminhos ficaram mais lentos que na rodada anterior, portanto esta
amostra não isola o custo da mudança de clear e não sustenta alegação de
ganho ou regressão de performance. O ganho comprovado aqui é visual.

## Remoção do depth CPU não publicado e trace refinado — 25/09/2026

No perfil BGFX, a profundidade é testada na GPU, mas não é entregue ao
chamador. O target alocava e preenchia um vetor de floats na CPU, de
largura × altura, que o backend logo descartava. Agora ele não o aloca
nem o preenche no build BGFX. O backend Rust e o Recording mantêm o
comportamento anterior. O controle diagnóstico
`COIN_BGFX_DIAGNOSTIC_CPU_DEPTH_FILL=1` restaura a alocação e o fill
antigos para comparar os dois caminhos no mesmo binário; não habilita
readback de profundidade no BGFX.

Assembly exportado do FreeCAD, Release, AMD/Mesa/X11, BGFX/OpenGL,
1024², `BASE_COLOR`, RGBA `borrow`, readback síncrono, 20 frames de
aquecimento + 300 medidos em cada um de cinco processos por variante,
intercalados. Os valores são medianas das cinco medianas, dos cinco p95
e dos cinco picos RSS por processo:

| Depth CPU | Mediana / p95 (ms) | Pico RSS (KiB) |
| --- | ---: | ---: |
| Fill antigo (`=1`) | 1,420 / 1,811 | 120.712 |
| Sem fill (`=0`, padrão) | 1,079 / 1,497 | 116.688 |

A melhora observada foi de 24,0% na mediana, 17,4% no p95 e 4.024 KiB
de pico RSS a menos. O resultado é específico deste perfil offscreen;
o RSS inclui BGFX, driver e demais bibliotecas, não mede VRAM. A/B
anterior no mesmo binário a 512² também favoreceu o skip, de 0,402 /
0,861 para 0,385 / 0,820 ms (mediana / p95). A tentativa de trocar a
inversão de linhas por cópia de linhas inteiras não melhorou 1024² e
foi revertida.

Com `COIN_RENDER_TRACE_PHASES=1`, o trace BGFX agora separa
`draw_encode_ms`, `blit_encode_ms`, `frame_wait_ms` e
`row_flip_ms`. Os dois primeiros são custos de **emissão CPU**, não
tempo de execução GPU. `read_wait_ms` ainda agrega espera por frames
BGFX/driver/GPU e inversão de linhas; em um trace 1024² do Assembly,
suas medianas foram ~1,189 ms, ~1,001 ms para `frame_wait_ms` e
~0,183 ms para `row_flip_ms`. Trace altera o tempo absoluto.
O teste visual repetido a 512² manteve MAE RGB 0 e IoU 1,0 contra
Coin/GL para PartDesign e Assembly, após alinhar a orientação GL.

Comparação final, no mesmo build Release e seleção AMD/Mesa/X11: cinco
processos por caminho, BGFX/OpenGL e Coin/GL intercalados, 20 warmup +
300 frames por processo, RGBA `borrow` no BGFX e `getBuffer()` no GL.
Cada entrada é a mediana das cinco medianas / dos cinco p95 (ms):

| Cena | Resolução | BGFX/OpenGL | Coin/GL |
| --- | ---: | ---: | ---: |
| PartDesign | 512² | 0,356 / 0,819 | 0,344 / 0,723 |
| PartDesign | 1024² | 1,041 / 1,368 | 1,003 / 1,428 |
| Assembly | 512² | 0,386 / 0,846 | 0,363 / 0,783 |
| Assembly | 1024² | 1,040 / 1,421 | 1,100 / 1,503 |

O BGFX se aproxima de Coin/GL, mas a liderança varia entre cenas e
resoluções; uma rodada anterior do Assembly 1024² favorecia Coin/GL.
Esses números incluem readback e não comparam janelas. Um render sem
readback no BGFX ainda exigiria um experimento específico: pular
`bgfx::read` mede a emissão de trabalho no host, não o tempo puro da GPU.

Reprodução do A/B (substituir a cena e o prefixo do build, se necessário):

```sh
for depth_fill in 1 0; do
  env EGL_PLATFORM=x11 \
    __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json \
    VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/radeon_icd.json \
    COIN_BGFX_RENDERER=opengl COIN_GLX_PIXMAP_DIRECT_RENDERING=1 \
    COIN_BGFX_DIAGNOSTIC_CPU_DEPTH_FILL="$depth_fill" \
    /usr/bin/time -f 'peak_rss_kib=%M' \
    /tmp/coin-bgfx-evaluation-build/bin/coin_render_gl_benchmark \
    --backend bgfx --size 1024 --warmup 20 --frames 300 \
    --readback color --rgba-output borrow --scene /caminho/Assembly.iv
done
```

## Cobertura comum entre BGFX e Rust/wgpu

O primeiro teste de contrato compartilhado é
`CoinRenderBackendContractTest`. Ele constrói uma cena Open Inventor, usa
`CoinRenderSceneManager` e confere o resultado RGBA, a vista emprestada,
o resize e a política de profundidade. O mesmo código é compilado em
builds separados para BGFX, Rust/wgpu e Recording; no build BGFX também
roda com `COIN_BGFX_RENDERER=opengl`. Isso respeita a seleção de
backend em CMake, sem tentar carregar os dois executores no mesmo
processo.

As expectativas são orientadas pela semântica do Coin e pela query de
capacidades: triângulos indexados `BASE_COLOR` devem renderizar em
todos os perfis estabelecidos. Ao trocar a cena para iluminação, quem
anuncia `COIN_RENDER_FEATURE_LIGHTS` deve renderizar; quem não anuncia
deve responder `UNSUPPORTED`, nunca mostrar uma imagem silenciosamente
incorreta. A query BGFX v2 inicializa um target temporário e comprova a
disponibilidade da GPU antes do teste. O contrato também valida Vulkan e OpenGL, os formatos
necessários e o diagnóstico `BUSY` quando a thread da API ou o orçamento de views impedem a sonda.

Ampliaremos essa mesma matriz por recurso (linhas/pontos, materiais,
texturas, alpha, depth, janela, múltiplos alvos). O contrato e as cenas
de referência ficam comuns; shaders, formatos, sincronização,
readback, ciclo de vida do dispositivo e handles de janela permanecem
específicos das respectivas Infra. Testes específicos continuam
necessários para essas regras e para diagnosticar falhas de cada API.

## Próximo gate de produto

O perfil já cobre iluminação PHONG, materiais heterogêneos, texturas, linhas,
pontos, transparência, subviewports e resize. Antes de aceitar BGFX como
alternativa geral ainda faltam os controles adicionais de
sorting/backfaces/número de camadas, paridade RGBA bit-a-bit e integração portátil além de Qt 6/X11; também é
preciso ampliar a validação de culling e coordenadas contra Coin/GL. O readback
síncrono do BGFX avança vários frames e não deve ser confundido com apenas
tempo de execução GPU. Apesar do ganho para câmera móvel no Assembly, ainda
não há vantagem geral sobre GL neste perfil.

Multitextura/SoTextureCombine foram qualificados no [perfil P08](coin-render-multitexture-contract.md).
