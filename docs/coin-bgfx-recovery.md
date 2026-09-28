# Recuperação do ambiente experimental após reboot

Os builds anteriores em `/tmp` não são persistentes. O código-fonte e os
testes permanecem nos repositórios; perder o build não desfaz a implementação.

O ambiente reconstruído usa `build-bgfx-recovery/` na raiz do Coin, já coberto
por `build*/` no `.gitignore`. Não substitui o build antigo do FreeCAD nem
instala bibliotecas em `/usr/local`.

## Organização

- `deps/bgfx.cmake`: dependências fixadas e ajustes locais do gerador CMake.
- `bgfx-build` e `bgfx-prefix`: biblioteca BGFX e compilador de shaders.
- `coin-build` e `coin-prefix`: Coin com backend BGFX e GL legado habilitados.
- `freecad-build`: FreeCAD experimental separado, com Part e Material.
- `weston`: pacotes Weston extraídos localmente, sem instalação global.
- `qt-quarter`: harness compilado contra as mesmas bibliotecas.
- `artifacts`: logs e resultados dos testes reais.

## Dependências fixadas

O gerador é `bkaradzic/bgfx.cmake-archived`, revisão
`a952acef35b431a39c790d39736867c515f32aff`. As revisões usadas na recuperação são:

- BGFX: `81d81fba72c42d348c589514c774bbfe01e110fa` (a revisão documentada anteriormente).
- bx: `25315498841259323e18f549d1ad9d9aba6632cc`.
- bimg: `87aaad3ac882e741889fdd4263224e5d12c26f99`.

O gerador arquivado antecede essas revisões. Os ajustes locais removem os
alvos obsoletos `fcpp` e `glsl-optimizer`, usam C++20 e SSE4.1, corrigem o
include do miniz, incluem os novos validadores SPIR-V e desabilitam DXC no shaderc Linux. Vulkan/SPIR-V e GLSL
permanecem habilitados. AVIF/WebP ficam desabilitados apenas no alvo auxiliar
`bimg_decode`; as texturas Coin continuam passando pelo caminho do Coin.
O diff do gerador deve ser preservado junto aos artefatos de recuperação.

BGFX é configurado com 1024 view IDs, 1024 framebuffers, 4096 shaders e
12 bits de programa. Os 16 view IDs reservados por alvo preservam a capacidade
anterior de 64 alvos. Pools compilados não substituem validação de todas as
combinações de composição e recursos.

## Pendência funcional ao retomar

O teste `freecad-mouse-links` cobre duas instâncias `App::Link` da mesma peça,
hover, clique, Ctrl para adicionar/remover, Shift, resize e isolamento dos
pixels de destaque por instância. O teste usa XTest em uma sessão X11 privada;
não cria seleção por API para simular o sucesso do mouse.

Os 60 testes Python do harness passaram após o reboot. Os 46 testes Coin
sem janelas passaram com Xvfb privado (sem alegar hardware OpenGL). A matriz nativa de App::Link passou em 8/8 células:
Vulkan/OpenGL × object/weighted OIT × DPR 1/2, com GPU física obrigatória.
A referência Coin/GL do mesmo build passou em 2/2 células e é explicitamente
`REFERENCE_PASS`, nunca BGFX `PASS`. Esses resultados precedem a correção
de recorte da barra de abas; os testes funcionais não detectavam esse defeito.

A comparação de 88 capturas do mesmo build encontrou máscaras de destaque
com IoU mínimo 0,9944, mas também revelou a barra de abas recoberta após
resize em DPR 2. Não se declara paridade visual completa. Links
aninhados/assemblies e seleção de arestas/vértices em links continuam pendentes.

O adaptador continua opt-in: `FREECAD_COIN_WGPU_EXPERIMENTAL=ON` no build e
`FREECAD_COIN_WGPU=1` no runtime. O runner isolado não altera bloqueio de tela,
screensaver ou serviços da sessão principal do usuário.

## Execução reproduzível

Os scripts locais `build-bgfx-recovery/rebuild.sh` e `test-links.sh` preservam
as configurações de reconstrução e da matriz. O build mínimo inclui Part,
Material e os recursos Python/GUI; não recompila todos os workbenches.

O runner usa Weston headless com renderizador GL e Xwayland privado,
2400×1600, verificando a resolução real via xdpyinfo. O antigo Weston X11
era reduzido pelo gerenciador de janelas e cortava capturas DPR 2; esses
resultados negativos foram preservados, não transformados em PASS.

Nesta máquina híbrida, a sessão de teste fixa localmente Mesa/AMD por
`__EGL_VENDOR_LIBRARY_FILENAMES`, `__GLX_VENDOR_LIBRARY_NAME=mesa`,
`DRI_PRIME=0` e `VK_ICD_FILENAMES`. Não altera preferências globais.
A configuração padrão NVIDIA abortou na inicialização do Qt; isso não
constitui aprovação nem diagnóstico de defeito BGFX.

Somente o perfil privado do cenário App::Link desativa DockWindows
ActivateOverlay. O teste confirma o destinatário do mouse para evitar que
um botão de dock seja confundido com uma face. Ctrl/Shift permanecem
pressionados até o evento real ser confirmado pelo Coin.

A regressão da barra de abas compara a captura da tela com pintura Qt
independente (`QTabBar::grab`), sem criar seleção por API. O recorte nativo
não altera dimensões da câmera nem as coordenadas de picking.


## Resultado final desta retomada

- `artifacts/links-bgfx-clipped/results.json`: 8/8 PASS com GPU física,
  incluindo o novo gate da barra de abas, hover após resize e idle.
- `artifacts/links-gl-clipped/results.json`: 2/2 REFERENCE_PASS,
  no mesmo build, com BGFX desabilitado em runtime.
- Nas quatro células DPR 2, 4494 amostras por barra: RGB MAE 0.
- 60/60 controles Python e 46/46 testes Coin sem janela passaram.
- O patch comprimido foi verificado por `git apply --check` contra os
  31 arquivos do baseline FreeCAD `d8d85f05ff`.

As 88 comparações estão em `artifacts/links-visual-clipped-measurements.json`.
Sem mudar o limiar ou recortar a barra para esconder a falha, o MAE RGB do
foreground no baseline redimensionado DPR 2 caiu de aproximadamente 111
para 1,932–1,934. O IoU mínimo das máscaras de destaque permanece 0,9944.
Os baselines completos têm MAE 1,932–4,069; diferenças de iluminação
continuam visíveis. São medições deste cenário, não declaração de
paridade completa com OpenGL.


## Continuação: hierarquias e especular

A pendência de links desta retomada ganhou cobertura nativa:
[links aninhados e iluminação](bgfx-linked-topology-lighting.md).
O consolidado tem 8/8 variantes BGFX em GPU e 2/2 referências GL, incluindo
faces, arestas e vértices por caminhos completos de montagens App::Part,
composição de placements, resize e isolamento entre instâncias.

O especular foi alinhado ao observador no infinito do GL legado, no BGFX,
WGSL e CPU. O erro dos baselines simples caiu para 0,303–0,554; o gate de 88
comparações aprova o novo runtime e rejeita o anterior. Controles Python
atuais: 76/76. Persistem os limites descritos no documento: Assembly solver,
links externos/arrays e equivalência visual universal não são aprovados.
A diferença Gouraud/PHONG da interpolação foi corrigida no caminho de
compatibilidade; ver [bgfx-gouraud-parity.md](bgfx-gouraud-parity.md).
