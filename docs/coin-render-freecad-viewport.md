# P16 — viewport Qt/manager/FreeCAD

P16 qualifica um perfil de viewport real: Part/BRep, grade, seleção on-top,
links aninhados, arrays de links, documentos e montagem `App::Part`.
O mesmo FreeCAD executa as mesmas macros com Coin/GL, CoinBgfx/OpenGL,
CoinBgfx/Vulkan e CoinWgpu/Vulkan. Exportar a cena não conta como qualificação.
Os casos, resultados e limites abaixo delimitam o fechamento; os bloqueios
F19/F20 do [inventário P15](coin-render-node-inventory.md) continuam abertos.

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
