# P16 — viewport Qt/manager/FreeCAD

P16 qualifica um perfil de viewport real: Part/BRep, grade, seleção on-top,
links aninhados, arrays de links, documentos e montagem `App::Part`.
O mesmo FreeCAD executa as mesmas macros com Coin/GL, CoinBgfx/OpenGL,
CoinBgfx/Vulkan e CoinWgpu/Vulkan. Exportar a cena não conta como qualificação.
Os casos, resultados e limites abaixo delimitam o fechamento; os bloqueios
F19/F20 do [inventário P15](coin-render-node-inventory.md) continuam abertos.

O estado atual de R20/R21 está na continuação Linux abaixo. As tabelas P16
seguintes são históricas e conservam seus limites e ambiente originais.

## Contrato e responsáveis

**Wiring:** `CoinRenderManagerAdapter::syncFromRenderManager()` copia cena,
fundo, viewport e a modalidade de transparência configurada no
`SoGLRenderAction` do manager. Ler essa configuração não executa GL.
`CoinRenderSceneManager` encaminha a modalidade para a action comum; uma
mudança invalida o plano capturado pela action. Preparação/autoclip, sensores,
câmera, captura de SoState, callbacks dos nós FreeCAD, caminhos diferidos e
publicação continuam nos donos existentes.

A regressão encontrada foi concreta: a GUI configurava blending no manager,
mas a action nativa conservava `SCREEN_DOOR`. No replay parcial de
`SoFCPathAnnotation`, o material zero completamente transparente descartava a
face selecionada inteira. Havia caminho e geometria, mas nenhum pixel. Copiar
a política do manager corrige os dois backends sem uma regra FreeCAD em cada
executor. A fixture exige Face1 visível através de um cubo opaco maior, troca
para Face2/objeto inteiro, resize, remoção sem resíduo e idle.

**Core:** classificação de alpha, ordenação/camadas, transformações, recortes,
strokes e decisões mecânicas permanecem comuns. P16 não acrescenta interpretação
do Coin em BGFX ou Rust.

**Infra:** BGFX e wgpu apresentam superfícies nativas. wgpu acrescenta um trace
opt-in após submit/present e a verificação de erros, identificando renderer,
adapter, surface, serial e dimensão. O trace não exige readback. A captura de
pixels é feita da tela pelo harness, fora do renderer.

**Shell:** `COIN_RENDER_TRANSPARENCY` é processado em ambos os backends;
`COIN_BGFX_TRANSPARENCY` permanece um fallback de compatibilidade do BGFX.
O runner exige a evidência de submissão do backend pedido, rejeita fallback
posterior para GL, resultado inconsistente, renderer diferente e ausência de
hardware quando exigido. `REFERENCE_PASS`, SKIP e UNSUPPORTED têm identidade
própria e não viram PASS nativo. Filtros que selecionam zero variantes agora
falham explicitamente; uma matriz vazia não representa execução.

## Perfil e oráculos

- `freecad-viewport`: geometria vermelha de uma `App::Part` alcançada por dois
  `App::Link`, array azul expandido, expose/resize, seleção `Instance/Box.Face2`
  com identidade preservada e pixels novos, limpeza, array 3→2, save/reopen,
  documento novo, destruição do owner, câmera e idle. A macro verifica shape,
  ViewProvider e uma única cópia das bibliotecas Part/PartGui carregadas.
- `freecad-multi`: dois documentos com câmeras e preselection independentes,
  resize/maximize/restore/minimize/restore, fechamento do primeiro owner e
  apresentação do survivor. Inclui geometria transparente no segundo alvo.
- `freecad-grid`: grade visível, independência da câmera, resize, remoção e idle.
- `freecad-path-selection`: seleção diferida de face e objeto inteiro através
  do oclusor; existência do nó não substitui pixels visíveis.
- `freecad-mouse-elements`: eventos XTest reais de movimento/click, ray pick,
  hover/seleção/limpeza de Face/Edge/Vertex em projeções ortográfica,
  perspectiva e perspectiva com placement/rotação. Não usa apenas a API de
  preselection como substituto do ponteiro.
- Quarter: expose, resize, maximize/minimize, dock/panel, DPR inicial 1/2,
  recriação, idle, wheel/rotation e dois viewports em object/weighted OIT.

Os mesmos limiares por caso são aplicados aos executores: geometria colorida
na região central; mudança de ≥100 amostras para face/câmera/array; tolerâncias
específicas menores para edges/vertices; resíduo de limpeza ≤30 no cenário de
links e ≤limiar registrado nos casos BRep. O on-top remove pixels em igualdade
com sua imagem inicial. A comparação valida contratos e interação, não exige
igualdade global entre algoritmos de transparência diferentes.

## Execução e evidência

Perfil Linux X11/Xwayland, Weston headless com renderer GL e Metacity em uma
sessão privada. GPU AMD RADV RENOIR integrada, vendor `0x1002`, device `0x1638`.
O adapter BGFX/OpenGL identifica vendor `0x0000`; sua prova física vem também
da consulta GL acelerada na própria sessão. wgpu identifica `IntegratedGpu`
na submissão efetiva. Xvfb/software não é evidência física deste fechamento.

FreeCAD base `228c679d78845c3fb6f5eb3d1a27f48aceab68b5`, com patches locais
preservados, copiado/reconfigurado em `/tmp/freecad-p16-build`.
`FreeCADMain`, `FreeCADGui`, `PartApp` e `PartGui` foram recompilados contra
CoinRender. Scripts, recursos, preference packs/templates e exemplos precisam
estar presentes no build; sem isso o host não cria corretamente seus
ViewProviders. O [patch suplementar](../examples/coinrender/freecad_p16_coinrender.patch)
neutraliza CMake/includes/tipos da integração local existente. O opt-in legado
`FREECAD_COIN_WGPU_EXPERIMENTAL` / `FREECAD_COIN_WGPU=1` é mantido por
compatibilidade; não ativa o adapter por padrão.

| Matriz final | Coin/GL referência | BGFX OpenGL | BGFX Vulkan | wgpu Vulkan |
|---|---:|---:|---:|---:|
| Quarter, lifecycle/input/DPR/two-viewports | — | 12 PASS | 12 PASS | 12 PASS |
| FreeCAD, cinco casos, object/DPR 1 | 5 REFERENCE_PASS | 5 PASS | 5 PASS | 5 PASS |
| FreeCAD, viewport/multi/on-top, weighted OIT/DPR 1 | — | 3 PASS | 3 PASS | 3 PASS |
| FreeCAD, viewport com links/array/documentos, object/DPR 2 | 1 REFERENCE_PASS | 1 PASS | 1 PASS | 1 PASS |

**63 PASS nativos em hardware + 6 REFERENCE_PASS**, sem SKIP, UNSUPPORTED ou
fallback nas células qualificadas. O [registro compacto](inventories/freecad-p16-viewport-results.json)
guarda casos, modalidades, adapter, medidas relevantes, hashes das fontes e
resultados, e os diretórios dos artefatos brutos em `/tmp/coin-p16-final-*`.
As duas execuções DPR 2 inicialmente vazias foram excluídas e repetidas após
corrigir o runner; uma tabela vazia não conta como passe.

Na fixture multi, a referência GL mostrou que Face1 estava oculta na câmera
axonometric. O teste final pede Face6, visível, em todos os executores e conserva
a exigência de alteração no primeiro documento e estabilidade no segundo.
O hover por API de faces ocultas não está qualificado por esse perfil. A
referência falha anterior foi excluída; as seis células nativas multi foram
repetidas com a mesma fixture final. No on-top, Face1 permanece intencionalmente
oculta pelo oclusor: GL e os três backends produziram **2.198 amostras alteradas**.

Regressões auxiliares: 77 testes Python do runner/harness + 3 do inventário;
C++ manager preparation, Shell e composição passaram nos builds BGFX e wgpu;
Product/adapter e seleção passaram em Vulkan nos dois builds. No build Recording,
manager/Product/Shell/composição passaram, e seleção GPU foi explicitamente
SKIP (sem GPU compilada); esse SKIP não integra a matriz nativa acima.

## Checklist de fechamento

- [x] Wiring sincroniza a política do manager e preserva lifecycle/captura comum.
- [x] Core mantém um dono para alpha, camadas, transformações e estados efetivos.
- [x] Infra apresenta os três perfis nativos em hardware, sem readback do renderer.
- [x] Shell compartilha a configuração e rejeita backend errado, fallback e matriz vazia.
- [x] Expose/resize, input Face/Edge/Vertex, grade e on-top têm pixels e estados verificados.
- [x] Links aninhados, arrays, save/reopen, dois documentos e destruição do owner passam.
- [x] Object/weighted OIT e DPR 1/2 estão registrados na granularidade da tabela.
- [x] Referência GL, limites e reprodução acompanham o fechamento.


Os artefatos por execução guardam `results.json`, inventário de hardware,
proveniência da sessão, logs e todos os PNGs. As execuções preliminares que
falharam por recursos ausentes, mistura de Part/PartGui de dois builds ou
notificações sobre o viewport não contam como passes. O perfil privado agora
suprime notificações de profiling antes de iniciar o host e a fixture detecta
bibliotecas Part duplicadas. Não se altera a configuração pessoal do usuário.

## Reprodução

Configure/instale CoinRender separadamente com BGFX ou wgpu. Recompile o host
contra o pacote neutro `CoinRender::CoinRender`, habilitando o opt-in externo.
O mesmo executável FreeCAD pode carregar cada instalação por `LD_LIBRARY_PATH`.
Inclua os diretórios dos módulos Part/Material do build testado: builds copiados
podem conservar RUNPATH de módulos antigos, misturando duas cópias de Part.

Exemplo do perfil object/DPR 1 usado nesta máquina:

```sh
env PATH=/home/linuxbrew/.linuxbrew/bin:/usr/bin:/bin \
  LD_LIBRARY_PATH=/home/dikluwe/.local/lib/python3.12/site-packages/PySide6/Qt/lib:/tmp/coin-p16-bgfx-prefix/lib:/tmp/freecad-p16-build/Mod/Part:/tmp/freecad-p16-build/Mod/Material:/tmp/freecad-p16-build/lib \
  __GLX_VENDOR_LIBRARY_NAME=mesa \
  __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json \
  VK_DRIVER_FILES=/usr/share/vulkan/icd.d/radeon_icd.json \
  python3 testsuite/qt-quarter/run_isolated.py \
  --server xwayland --weston-prefix /tmp/coin-p16-weston \
  --harness /tmp/coin-p16-qt/QtQuarterRegression \
  --freecad /tmp/freecad-p16-build/bin/FreeCAD \
  --artifacts /tmp/coin-p16-reproduction \
  --backend bgfx --renderer opengl --mode object --scale 1 \
  --case freecad-viewport --case freecad-multi --case freecad-grid \
  --case freecad-path-selection --case freecad-mouse-elements \
  --timeout 120 --require-hardware
```

A linha wgpu/weighted OIT da matriz acima exercita o perfil Part/BRep
opaco escolhido em P16; ela não comprova weighted OIT com geometria translúcida.
A [campanha P17](coin-render-p17-campaign.md) exclui essa combinação porque a
ponte wgpu a rejeita explicitamente em cena translúcida.

Para BGFX/Vulkan, troque `--renderer vulkan`. Para wgpu, troque o prefixo de
bibliotecas e `--backend wgpu --renderer vulkan`. Weighted OIT usa
`--mode weighted_oit`; DPR 2 usa `--scale 2`. A referência usa a mesma instalação
BGFX, `--reference-gl --renderer opengl`, um caso por execução e sem
`--require-hardware`: o runner desativa o opt-in do host, sem atribuir um PASS
nativo à referência. Execute sessões GPU sequencialmente.

## Limites e próxima etapa

- Não certifica todos os workbenches, addons, rótulos Text2, SoImage, shaders
  customizados, volume, cubemap, RTT ampliado nem geometria GL-only do P15.
- Montagem significa `App::Part` com instâncias; não certifica solver, joints ou
  todas as ferramentas do workbench Assembly.
- DPR inicial 1/2 não certifica migração entre monitores físicos com escalas
  distintas. Não qualifica Windows/D3D, macOS/Metal, outros drivers ou dispositivos.
- Não é uma campanha de performance, comparação de memória ou timestamps GPU.
  Mediana/p95, throughput, warmup/vsync e janela versus offscreen são P17/P18.

P16 fecha este perfil utilizável. Expandir outros casos exige resolver e testar
suas dependências F19/F20, sem transformar o inventário em certificado de suporte.


## Branch integrada e comprovação de Vulkan — 23/10/2026

A branch [`codex/coin-render-freecad`](https://github.com/Dikluwe/coin/tree/codex/coin-render-freecad) reúne o CoinRender até R35, incluindo os 59 commits posteriores à base `codex/coin-render` (`24ddf557af`). A fonte de produção recompilada foi `733362b107a0d617c7c342799d239e8913f3ae63`, ancestral desta branch; ambos os backends permanecem no código. Esta campanha testa **BGFX/Vulkan no FreeCAD Linux AMD**, sem estender sua qualificação a todas as GPUs, APIs ou workbenches.

Coin/CoinRender foram reconstruídos com C++20, Debug `-O0 -g0`, SDK BGFX API161 e os patches Vulkan versionados já descritos em R35. FreeCAD `228c679d78`, com o incremento gráfico R20/R21 abaixo e as adaptações locais não gráficas já discriminadas, foi reconstruído com C++20/Release `-O0 -g0 -DNDEBUG`, Qt6/Python3.12 e Part/Mesh/Sketcher. Não se misturou o antigo `libCoinWgpuExperimental` com o `libCoinRender` atual. As bibliotecas carregadas foram verificadas no mapa do próprio processo.

**22 perfis distintos aprovados**, com API Vulkan/GPU física verificadas em todos: viewport (quatro), múltiplos documentos (dois), grade (quatro), seleção por path (quatro), picking de faces/arestas/vértices (quatro) e admissão de frames (quatro). Os cruzamentos registrados usam object/weighted OIT e DPR 1/2 quando aplicáveis. GPU em série, processos novos e watchdog de 120 s, com dez segundos reservados ao diagnóstico; sem skips ou aumento de prazo. Os 111 testes Python do runner passaram.

A rodada identificou uma subamostragem incorreta no teste de grade: depois de resize em DPR 2, linhas pretas de um pixel coincidiam com a paridade omitida pela varredura de dois em dois. O PNG reprovado tinha 48.192 pixels escuros, dos quais a amostra contava somente 733. O teste agora conta todos os pixels e aumenta os limiares de 1.000 para 4.000, conservando a densidade mínima exigida. As quatro variantes passaram; uma contraprova que remove a grade antes do resize continua reprovando exatamente nesse gate. Os demais critérios de câmera, remoção, imagem atual e idle permanecem. Não houve alteração de produção para encobrir essa reprovação de fixture.

O pacote inicial incompleto foi recusado: primeiro por catálogos de materiais ausentes e depois por FreeCAD sem suporte SWIG. Recursos de materiais e a ponte SWIG/Pivy fazem parte do pacote revalidado. Neste host, `SetupSwig` só descobre SWIG com `BUILD_SKETCHER=ON`; SWIG 4.2.0 e o Pivy instalado confirmaram a mesma API runtime 4. Habilitar `FREECAD_USE_EXTERNAL_COIN_PIVY=ON`, manter uma única imagem Coin (inclusive o alias privado `libCoin.so.80c`) e inicializar os submódulos GSL/Pivy necessários antes de configurar. `git archive` sozinho não inclui submódulos.

Além dos alvos anteriores, compilar os recursos reais do host:

```sh
cmake --build "$FREECAD_BUILD" --parallel 6 --target \
  FreeCADMain PartGui MeshGui SketcherGui SketcherScripts PartScripts MaterialScripts \
  MaterialLib MaterialModelLib AppearanceLib PatternLib FluidMaterialLib MachiningLib \
  MaterialToolsLib MaterialsAPILib FreeCADGui_Resources Stylesheets_data \
  PreferencePacks_data PreferencePackTemplates_data
```

Repetir a seleção de API/GPU, prefixos privados e sessão isolada descritos abaixo, com o harness recompilado contra esses mesmos headers/bibliotecas. Executar o runner com `--backend bgfx --renderer vulkan --require-hardware --timeout 120`, os seis valores `--case freecad-viewport --case freecad-multi --case freecad-grid --case freecad-path-selection --case freecad-mouse-elements --case native-admission` e `COIN_TEST_HOST_HELPER` apontando para o novo `libFreeCADHostConsumerProbe.so`. Incluir lib/Mod/Part/Mod/Material/Mod/Mesh/Mod/Sketcher do mesmo host em `LD_LIBRARY_PATH`.

### O que comprova a API em uso

`COIN_BGFX_RENDERER=vulkan` solicita a API. Com `COIN_RENDER_TRACE_PHASES=1`, a identificação vem de `bgfx::getRendererType()`/capacidades do dispositivo efetivamente criado, e não do texto da variável:

```text
COIN_RENDER_PHASE bgfx_device renderer=Vulkan vendor_id=0x1002 device_id=0x1638 homogeneous_depth=0
```

O runner exige também submissão e imagem atual, rejeita fallback e API diferente da solicitada. A demonstração interativa do pacote recompilado confirmou quatro vistas nativas, seriais públicos positivos, `wgpuOpenGLViewport=false`, status SUCCESS e nenhuma razão de fallback. Os nomes `FREECAD_COIN_WGPU`/`wgpu*` são históricos do opt-in e das propriedades; a API ativa é a do dispositivo acima.

Uma observação independente no GDB acompanhou os endereços devolvidos pela resolução de funções Vulkan, sem alterar o código do renderer. As duas chamadas chegaram a `libvulkan_radeon.so`, a partir de `bgfx::vk::CommandQueueVK::kick` e `bgfx::vk::SwapChainVK::present` da **libCoinRender atual**, e retornaram:

```text
VULKAN_API_RESULT name=vkQueueSubmit VkResult=0
VULKAN_API_RESULT name=vkQueuePresentKHR VkResult=0
```

Zero é `VK_SUCCESS`. O debugger foi desconectado após a observação e a janela continuou interativa. Isso comprova uso real de Vulkan nesse processo; sucesso de present isoladamente não garante visibilidade, por isso os gates de captura e mudança de imagem continuam necessários. Carregar `libvulkan`, executar `vulkaninfo` ou exibir “Vulkan” no título não substitui essa verificação. Qt e outros componentes podem carregar GL sem que a viewport renderize por GL.

O runtime local atual fica em `/mnt/Laranja/Git/externos/coin/build-freecad-current-20261023`, separado das instalações anteriores. `open-vulkan.sh` reabre a demonstração; `demo/vulkan-calls.log` conserva as chamadas e suas pilhas, e `demo/current-package.json` identifica as bibliotecas e os seriais observados. Os artefatos operacionais ficam fora do Git; a branch contém fontes, testes e reprodução. **B34, B94 e B95 continuam externos abertos**, com as operações reprovadas não qualificadas.

## R20/R21 — FreeCAD/Quarter no Linux, continuação de 22/10/2026

Esta continuação integra adaptações do host e delimita consumidores reais;
não certifica todos os workbenches ou a tradução de GLRender arbitrário.
Na entrega de 22/10, R29, R30 e R35 ficaram para depois; tiveram continuação
posterior na matriz e na revalidação integrada acima. B34 e B94 permanecem falhas externas abertas,
com operações afetadas fora da qualificação.

### Alterações de comportamento

Quarter conserva o alvo nativo após uma recusa de autoria `UNSUPPORTED` ou
`INVALID_SCENE`. A edição da cena pode recuperar a publicação sem frame GL
preparatório ou fallback. A recusa tardia preserva pixels, contagem e o serial
real do alvo; erro fatal de criação de superfície continua diagnóstico fatal.
A propriedade de diagnóstico `wgpuFrameSerial` lê o serial de submissão do
CoinRenderTarget, distinto da contagem de frames de Quarter.

NaviCube solicita clear de depth restrito ao viewport de seu primeiro draw,
antes de compor suas faces/rótulos. O callback de sincronização lê o viewport
somente nas actions que habilitam esse elemento; busca e escrita de metadados
não preparam nem alteram o overlay. Isso elimina o abort pré-existente de
SoSearchAction no NaviCube, reproduzido também no baseline. O novo overload experimental
`beginForegroundPass(TRUE)` delimita esse pedido. Escopos aninhados restauram
a ordem e o pedido pendente do pai; escopo explicitamente vazio não transfere
seu clear para uma camada posterior. A chamada antiga sem argumentos conserva
a preservação de depth. Não há alteração da ABI legada de Coin nem do protocolo
privado 69/FrameView de 560 bytes.

A seleção BoundBox do host também emite geometria no callback comum; antes,
a caixa era desenhada somente no caminho GL de SoFCSelectionRoot/SoFCSelection.
A seleção Full também conserva os overrides de cor/material e o escopo do
host, inclusive no SolarDiagram BIM. O destaque wgpu em perspectiva usa a
profundidade da face já recortada pelo Core, preservando triângulos finitos
que cruzam o eye plane e recusas de NaN/Inf.

O host encaminha a soltura do botão aos filhos antes de consumir o clique
para seleção. Isso permite que o dragger encerre também um clique sem movimento;
antes ele permanecia ativo e iniciava um arraste no movimento seguinte, já sem
botão pressionado. Arrastes reais conservam callbacks e a alteração de Placement.

O viewer prepara o estado de interação antes da captura comum. Mesh preserva
a redução legada durante navegação ativa acima de RenderTriangleLimit: três
vértices por face amostrada, stride `faces/limite + 1`, point size `min(stride,3)`.
Soltar o botão restaura as faces com a mesma câmera. A fixture usa BASE_COLOR
explícito para isolar o sampler, sem qualificar normais/iluminação. O perfil usa Mesh sem VBO,
pois o caminho VBO legado não usa esse redutor.

### Consumidores e limites

| Perfil | Consumidor real e operações observadas |
| --- | --- |
| sketcher-datum | Sketcher em edição, dimensão, glifos e atualização |
| measure-distance | Measure com distância entre vértices, texto e edição da referência |
| mesh-curvature | Mesh com colorbar foreground, glifos, seleção e atualização |
| fem-mesh | FemMesh tetraédrica, picking de face e mudança da malha |
| draft-dimension | Draft Dimension, texto, seleção e mudança da dimensão |
| partdesign-pad | Body/Sketch/Pad, picking com nomes topológicos resolvidos pelo host e edição |
| transform-dragger | edição Transform, haste real, arraste XTest e callbacks C++ start/motion/finish |
| bim-site | Arch Site com SolarDiagram, rótulos, picking e mudança/remoção do diagrama |
| mesh-interactive | Mesh::Feature acima do limite durante movimento com botão pressionado, amostras projetadas e restauração |

Os consumidores verificam conteúdo (≥20 pixels), glifos quando aplicável,
mudança autoral (≥30 pixels), limpeza (≤30 pixels), dois tamanhos de viewport,
idle, DPR solicitado e identidade de picking. O comportamento visual de hover
e seleção é conferido contra o host CoinGL: Measure/FEM não oferecem esses
highlights neste perfil; a ausência deve conservar pixels. Draft e o SolarDiagram
BIM oferecem seleção, mas não hover. Não se acrescenta highlight para fazer o teste passar.
Os observadores de input e dragger são C++, sem callback Python dentro do
despacho Qt/Coin. A soltura exige zero draggers ativos e nenhum event grabber.
A configuração de UI é privada por processo, com docks sobrepostos desativados,
para que painéis não cubram pixels nem interceptem a entrada da fixture.
Sketcher/Transform usam docks flutuantes e ocultos antes do baseline, com
verificação em Qt C++ e igualdade estrita dos tamanhos capturados.

A captura inicial nativa sincroniza pela contagem e pelo serial reais do alvo:
aguarda a nova publicação e uma janela de 600 ms sem outra publicação, sem
solicitar render nem consultar pixels para decidir quando capturar. Esse
intervalo observa a apresentação após os redraws pendentes; não impõe 600 ms
para compilação fria de pipelines. O prazo global permanece 120 s. O perfil retido anterior também observa
contagem/serial após show, edição, remoção e resize; a entrada no idle não
exige um frame novo.
Se faltar conteúdo, o diagnóstico preserva FAIL e observa novamente em
50/500/1000 ms, registrando contagem/serial antes e depois, sem solicitar frames.
Isso distingue publicação estável de redraw ainda pendente; observação tardia
não promove a ocorrência original a PASS.

Os novos consumidores devem fechar documentos e o aplicativo normalmente;
o prazo é de 120 segundos, com watchdog e captura de pilhas em caso de travamento.
Os casos P16 antigos continuam úteis para viewport, mas seu encerramento forçado
não constitui evidência de teardown normal.

### Qualificação atual

| Perfil | Variantes nas sete células | Resultado |
| --- | ---: | --- |
| Quarter: admissão, foreground, NaviCube, RTT, lifecycle, input e composição | 224 | 224 PASS |
| Nove consumidores reais, dois tamanhos por processo, DPR inicial 1/2 | 126 | 126 PASS |
| P16: viewport, documentos, grade, seleção e elementos, object/weighted e DPR delimitados | 70 | 70 PASS |
| Nós retidos anteriores e Part::Spline, object/DPR 1 | 14 | 14 PASS |
| CoinGL: nove consumidores em DPR 1/2 e seis variantes P16 | 24 referências | 24 REFERENCE_PASS |

São **434 variantes nativas em hardware**, sem fallback, SKIP ou UNSUPPORTED
contados como passe. As 126 variantes dos novos consumidores também fecharam
normalmente o host, após salvar/fechar os documentos e remover probes/filtros.
A tabela não conta novamente as repetições de investigação e regressão.
O teste de busca percorre o NaviCube e seu callback reais, exige cena/câmera
inalteradas e reproduz abort 134 no baseline, com a mesma entrada. O viewport
foi reexecutado nas sete células em DPR 1/2 após essa correção; essas 14
repetições não aumentam a contagem de variantes da tabela.

Regressões proporcionais: composição GPU 21/21 nas sete células; preparação e
composição CPU 6/6 nos dois builds; runner/oráculos Python 111/111; Rust 59
aprovados e quatro ignorados, sem somar os ignorados às aprovações. Os 35.892
símbolos dinâmicos legados de Coin são idênticos no baseline/candidato em ambos
os builds; o protocolo privado permanece 69/FrameView 560.

O teste legado CoinRenderManagerLayerViewportGlReferenceTest não obteve sua
referência GLX nesta sessão: SKIP 77 na AMD e BadValue na criação do contexto
NVIDIA. Ele não é contado como aprovação. As comparações CoinGL desta entrega
usam os fixtures Qt/FreeCAD, com referência produzida e critérios mantidos.

### Ambiente e reprodução

Baseline Coin anterior: `a9c0ab6d58651e6131eb6bcf8e87f294358a1ccb`.
Candidato: código `7887925de206c547882cfb6d8dca83eeef654a91`; testes `a8bc0fea20d10484cbeaa82282246ec99305f8df`.
O host local é FreeCAD `228c679d78845c3fb6f5eb3d1a27f48aceab68b5`, com os
adaptadores retidos já existentes. As alterações gráficas podem ser aplicadas
sobre o pai público `d8d85f05ffad8277db968d6872c4a7a9b733c1b3`:

```sh
git apply --check --ignore-space-change /caminho/coin/examples/coinrender/freecad_r20_r21_public_base.patch
git apply --ignore-space-change /caminho/coin/examples/coinrender/freecad_r20_r21_public_base.patch
git apply --check --ignore-space-change /caminho/coin/examples/coinrender/freecad_r20_r21_host_228c.patch
git apply --ignore-space-change /caminho/coin/examples/coinrender/freecad_r20_r21_host_228c.patch
```

O patch `freecad_r20_r21_linux.patch` é somente o incremento para o snapshot
local com adaptações anteriores. Não aplique as duas alternativas juntas.
A cadeia pública foi conferida nos 55 arquivos gráficos resultantes. O build
local preservou alterações não gráficas pré-existentes em Writer/Material;
esta comparação não declara um build integral de um FreeCAD público limpo.
Para reconstruir o baseline gráfico, use outra cópia do host candidato na RAM
e reverta somente o incremento, conservando o pré-requisito de link X11 comum
aos dois builds:

```sh
git apply -R --check --ignore-space-change --exclude=src/Gui/CMakeLists.txt /caminho/coin/examples/coinrender/freecad_r20_r21_linux.patch
git apply -R --ignore-space-change --exclude=src/Gui/CMakeLists.txt /caminho/coin/examples/coinrender/freecad_r20_r21_linux.patch
```

Os 55 arquivos gráficos dessa reconstrução coincidem com o baseline testado,
normalizando apenas LF/CRLF. Use Coin `a9c0ab6d58651e6131eb6bcf8e87f294358a1ccb`
e seus próprios headers no baseline; mantenha entradas e flags de execução iguais.

O opt-in permanece `FREECAD_COIN_WGPU_EXPERIMENTAL=ON` no build do host e
`FREECAD_COIN_WGPU=1` na execução. Compile Coin/CoinRender e FreeCAD com os
headers correspondentes; instale os headers experimentais com
`COIN_INSTALL_RENDER_EXPERIMENTAL=ON`. Para BGFX, aplique também os patches de
SDK já exigidos pelo projeto. Os builds desta continuação usaram C++20 no host,
Qt6, Python3.12 e módulos Part, Mesh, Sketcher, Measure, FEM, Draft, PartDesign e
BIM, incluindo suas dependências. Nesta máquina foram usados headers Qt 6.4.2
e runtime Qt 6.6 do PySide6, com um único conjunto Qt por processo. FEM Netgen
não é necessário para a fixture.

Use apenas uma imagem libCoin no processo, inclusive Pivy. O teste recusa duas
bibliotecas com inodes distintos. Pivy da distribuição pode pedir o SONAME
`libCoin.so.80c`, enquanto este Coin instala `libCoin.so.80`: vincule Pivy ao
Coin testado ou alinhe o alias exclusivamente no prefixo privado de testes.
Não carregue a biblioteca do sistema em paralelo com a candidata.
SolarDiagram usa ladybug-core 0.44.62, ladybug-geometry 1.35.6 e pysolar 0.13
neste perfil. FreeCAD redefine PYTHONPATH;
para dependências Python privadas use seu argumento `--python-path`.

Exemplo com builds e artefatos descartáveis em diretório exclusivo na RAM:

```sh
RAM=/dev/shm/coin-r20-local
mkdir -p "$RAM/tmp"
export TMPDIR="$RAM/tmp" PYTHONDONTWRITEBYTECODE=1
export COIN_TEST_COIN_REPO="$PWD"
cmake -S testsuite/qt-quarter -B "$RAM/harness" -G Ninja \
  -DCOIN_SOURCE_DIR="$PWD" -DCOIN_BUILD_DIR="$RAM/coin-build" \
  -DFREECAD_SOURCE_DIR="$RAM/freecad-source" \
  -DFREECAD_BUILD_DIR="$RAM/freecad-build"
cmake --build "$RAM/harness" -j2
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s testsuite/qt-quarter -p 'test_*.py'
export COIN_TEST_HOST_HELPER="$RAM/harness/libFreeCADHostConsumerProbe.so"
export COIN_TEST_HOST_CONSUMER=sketcher-datum
cat > "$RAM/run_diagnostic.py" <<'PYTHON'
import os, sys
from pathlib import Path
repo = Path(os.environ['COIN_TEST_COIN_REPO'])
sys.path.insert(0, str(repo / 'testsuite/qt-quarter'))
import run as suite
execute = suite.execute
def guarded(command, env, timeout):
    watchdog = repo / 'scripts/coinrender/run_with_timeout.py'
    wrapped = [sys.executable, str(watchdog), '--timeout', str(timeout),
               '--diagnostic-timeout', '10', '--diagnostic-dir',
               str(Path(env['COIN_TEST_ARTIFACTS']) / 'watchdog'), '--', *command]
    return execute(wrapped, env, timeout + 15)
suite.execute = guarded
raise SystemExit(suite.main())
PYTHON
python3 testsuite/qt-quarter/run_isolated.py --server xwayland \
  --weston-prefix /caminho/weston --artifacts "$RAM/session" --exec -- \
  python3 "$RAM/run_diagnostic.py" --harness "$RAM/harness/QtQuarterRegression" \
  --freecad "$RAM/freecad-build/bin/FreeCAD" --artifacts "$RAM/result" \
  --backend wgpu --renderer vulkan --case freecad-screen-content \
  --mode object --scale 1 --timeout 120 --require-hardware
```

O wrapper aplica o watchdog ao processo real de cada caso, antes de o runner
encerrar o filho. O prazo funcional é 120 s; os 15 s adicionais do pai reservam
somente tempo para diagnóstico, sem reaplicar a cena.

Defina LD_LIBRARY_PATH com Qt e o prefixo Coin testados, mais lib/Mod/Part e
Mod/Material do mesmo build FreeCAD. Repita os nove valores da tabela, DPR 1/2,
um processo por variante. CoinGL usa `--reference-gl`, BGFX/OpenGL e retira
`--require-hardware`; `REFERENCE_PASS` não conta como submissão nativa.
Sem COIN_TEST_HOST_CONSUMER, `freecad-screen-content` conserva os perfis retidos
anteriores; `COIN_TEST_RETAINED_NODES=1` e `COIN_TEST_SPLINE_CONSUMER=1`
selecionam respectivamente nós retidos e Part::Spline.

Quarter usa `native-admission`, `foreground-scope`, `navicube`, `rtt-window` e
os casos de lifecycle listados acima. A recusa RTT usa tamanho zero, que é
`INVALID_SCENE`; RGBA16F já é suportado e não é uma contraprova válida.
O runner exige trace da submissão real, incluindo adapter_name no caminho
wgpu/OpenGL, e rejeita software, fallback ou ausência de prova física.

A matriz física é wgpu AMD/Vulkan, AMD/OpenGL e NVIDIA/Vulkan; BGFX AMD/Vulkan,
AMD/OpenGL, NVIDIA/Vulkan e NVIDIA/OpenGL. Hardware exercitado: AMD RENOIR
0x1002:0x1638, Mesa 25.2.8, e GeForce RTX 3060 Laptop, NVIDIA 615.71.09.
GPU AMD usa Xwayland acelerado em
Weston/Metacity privados. NVIDIA/OpenGL BGFX usa Xorg privado com a NVIDIA como
provider principal, sem PRIME, a partir da configuração
[headless existente](../testsuite/reproducers/b34-native-x11/nvidia-primary-headless.conf).
Use uma cópia privada com Virtual 2400×1600 para comportar os dois tamanhos da
fixture, sem alterar a configuração do usuário; o procedimento de isolamento
está em [B34 principal](coin-render-feature-matrix.md#b34--comparação-nvidia-principal-e-falha-externa--21102026).
Antes de iniciar o WM/Qt nesse Xorg privado, defina um monitor lógico RandR
compatível com o framebuffer; sem outputs/monitores, Qt usa uma tela de fallback
640×480 apesar do framebuffer maior. Com DISPLAY/XAUTHORITY dessa sessão privada:

```sh
xrandr --setmonitor COIN-R20 2400/813x1600/542+0+0 none
xrandr --listmonitors
```

O monitor deve aparecer e a geometria Qt corresponder à sessão. Isso prepara
somente o ambiente de teste; não altera o renderer nem acrescenta reaplicações.
No Xwayland AMD com offload NVIDIA, a criação da superfície EGL continua
falhando: esse cruzamento não é qualificado nem convertido em fallback.
DPR inicial não certifica migração entre monitores físicos. Windows, macOS,
outros drivers, callbacks GL arbitrários e workbenches completos exigem seus
próprios perfis. Os registros P15/P16 de 2026-10-06 acima permanecem históricos.
