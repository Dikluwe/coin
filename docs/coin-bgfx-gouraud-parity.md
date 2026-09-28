# PHONG do Coin com interpolação Gouraud

`SoLightModel::PHONG` seleciona os termos de iluminação do material. Não
seleciona interpolação Phong de normais por fragmento. O caminho GL normal do
Coin usa `GL_SMOOTH`: ilumina os vértices, limita suas cores a `[0,1]` e
interpola essas cores com correção de perspectiva.

O perfil experimental agora segue esse contrato em CPU, BGFX e no shader
standard WGSL. A função BGFX está em `coin_lighting.sh`, usada pelo vertex
shader dos programas opaco, weighted OIT e depth peeling. A antiga iluminação
por fragmento foi substituída no caminho de compatibilidade; não há um novo
seletor público de interpolação nesta mudança.

Normais transformadas pela matriz inversa transposta, ambient, diffuse,
specular, emissão e shininess são avaliados por vértice. Distância, atenuação,
cone e drop-off de luzes pontuais/spot também. O observador permanece no
infinito em `(0,0,1)` no espaço da câmera. Um produto `N.H` negativo não ganha
um piso especular artificial de `0.0001`.

Materiais e alpha diferentes por vértice são preservados. Na referência CPU,
o clipping interpola a cor já iluminada, sem escolher um material inteiro por
proximidade nem reiluminar a interseção. Texturas e composição continuam por
fragmento. O limite de oito luzes não mudou.

## Evidência desta mudança

- O teste anterior falhou: normais curvas produziram brilho `88/255`, embora
  o oráculo por vértice previsse `0.025/255`. A implementação corrigida passa.
- `CoinRenderLightingTest`: cinco oráculos independentes (especular com normais
  curvas, ponto em face larga, cone spot que não alcança os vértices,
  drop-off e `N.H` negativo), mais uma `SoSphere` real em cinco posições por
  luz direcional, pontual e spot. CPU/BGFX/Coin-GL ficam dentro de três níveis
  de UNORM8 por canal nas amostras da esfera.
- O teste não ignora mais a GPU em builds BGFX, apesar da exclusão desse
  perfil experimental na antiga convenience query `isGpuBackendAvailable`.
- `CoinRenderGouraudInterpolationTest`: oráculo de três materiais emissivos,
  saturação antes de interpolar, alpha heterogêneo, perspectiva e clipping
  próximo. Seis variantes BGFX Vulkan/OpenGL × object/weighted_oit/sorted_layers.
- 53 CTest selecionados, sem os testes `Window`, passaram sob Xvfb.
- Quatro testes Rust passaram; Naga faz parse e validação completos do WGSL,
  e um teste confirma que materiais são iluminados no vertex stage, não no
  fragment stage. Isso não é validação numérica de hardware wgpu.
- Dois smoke tests FreeCAD com GPU física passaram: Vulkan/weighted OIT/1×
  e OpenGL/object/2×, incluindo hover, seleção, Ctrl, resize e abas.

A referência GL foi ativada com `COIN_GLX_PIXMAP_DIRECT_RENDERING=1` e
`COIN_GLXGLUE_NO_PBUFFERS=1`. A indisponibilidade anterior sob Xvfb era do
contexto GLX indireto, não evidência de erro na iluminação. A comparação GL
sob Xvfb inclui renderização software; não conta como prova de GL acelerado.
Nos oráculos desta mudança, Coin/GL concordou com o cálculo independente.
Não se declara que Coin/GL seja infalível, nem equivalência pixel a pixel em
toda cena: antialiasing, tesselação diferente e composição de várias camadas
transparentes continuam sendo dimensões separadas.

## Repetição

Depois de construir o runtime de [bgfx-recovery.md](bgfx-recovery.md):

```sh
xvfb-run -a env COIN_GLX_PIXMAP_DIRECT_RENDERING=1 \
  COIN_GLXGLUE_NO_PBUFFERS=1 COIN_WGPU_REQUIRE_GL_REFERENCE=1 \
  COIN_BGFX_RENDERER=vulkan \
  build-bgfx-recovery/coin-build/bin/CoinRenderLightingTest

xvfb-run -a ctest --test-dir build-bgfx-recovery/coin-build \
  --output-on-failure -E Window --parallel 4

cargo test --offline --manifest-path src/rendering/coinrender/rust_bridge/Cargo.toml
```

Na máquina híbrida, use a seleção local Mesa/AMD descrita no guia de recuperação.
Logs persistidos em `build-bgfx-recovery/artifacts/gouraud-*`; capturas e
resultados físicos em `gouraud-freecad-vulkan` e `gouraud-freecad-opengl`.
