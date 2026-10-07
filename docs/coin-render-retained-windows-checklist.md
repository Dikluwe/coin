# Continuação Windows — nós retidos

Esta lista acompanha o [perfil Linux](coin-render-retained-linux-profile.md).
Não reutilizar os passes Windows anteriores como prova deste código novo.

- [ ] Atualizar Coin e CoinRender na mesma revisão e reinstalar o header
  experimental; compilar MSVC BGFX e wgpu com protocolo 49, incluindo o novo
  export privado `coin_wgpu_surface_submission_serial`.
- [ ] Aplicar os patches FreeCAD anteriores e o incremental
  `examples/coinrender/freecad_linux_retained_callbacks.patch`, depois
  `examples/coinrender/freecad_linux_rtt_recovery.patch`; validar contextos
  com `git apply --check --ignore-space-change` antes de aplicar. Recompilar
  FreeCADGui, PartGui e MeshGui com a integração experimental.
- [ ] Repetir o gate CoinRenderNodeInventoryTest: rejeição de callback fora/dentro
  de apply, publicação preservada, registro tardio de subclasses e recuperação
  na mesma action. Polygon deve usar generatePrimitives, sem a subcena temporária.
- [ ] Executar a macro `freecad_screen_content.FCMacro` com
  `COIN_TEST_RETAINED_NODES=1`, object/weighted OIT, DPR 1/2 e APIs disponíveis.
  Registrar oito tipos, glifos, mutação, remoção, resize, picking e idle.
- [ ] Repetir o consumidor `Part::Spline` com `COIN_TEST_SPLINE_CONSUMER=1`:
  Bezier e `COIN_TEST_SPLINE_KIND=bspline-curve`/`bspline-surface`, object/weighted
  OIT e DPR 1/2; ControlPoints liga/desliga, Shape regenerada e remoção sem resíduos.
- [ ] Repetir RTT direto de janela: produtor, resize, formato recusado com
  imagem anterior preservada e recuperação sem fallback. Adaptar `rtt-window`
  para Win32; verificar serial por surface e resize sem mudança de tamanho.
- [ ] Repetir os gates RTT Mips/MipsPbuffer e Publication: limiar estrito >0,5,
  cadeia POT staged, orçamento, NPOT recusado e recuperação.
- [ ] Repetir ColorBar frio em três ranges/precisões, com câmera foreground
  height 10, glifos presentes e comparação CoinGL.
- [ ] Repetir cinco entradas inválidas dos tipos Part/Mesh no mesmo target,
  comparar RGBA anterior e recuperar após corrigir os campos.
- [ ] Ensaiar cache de programas ativo/desativado em OpenGL com a spline;
  investigar o lifecycle sem assumir que a ocorrência Linux se repete no Windows.
- [ ] Qualificar arraste por eventos e DPI entre monitores, incluindo os
  consumidores completos escolhidos (Sketcher/Measure, Part, Mesh/FEM).
- [ ] Registrar a GPU/API do backend efetivamente usado e impedir fallback
  CoinGL contado como passe. A sonda EGL desta entrega é específica do Linux.

O harness Qt desta campanha usa X11 e dependências do build FreeCAD Linux;
seus controles C++ precisam de um runner/adaptação Win32. A macro de conteúdo
é compartilhada, mas requer o bootstrap de viewport experimental, PySide/Pivy
e as variáveis de artifacts/macro do runner. Não presumir que o runner Xwayland
possa ser executado no Windows.
