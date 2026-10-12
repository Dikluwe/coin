# QuadMesh preciso, material flat e sombras — 2026-10-12

O fixture `CoinRenderShadowReferenceTest --precise-quadmesh-shadow` usa um
`SoShadowGroup` com cubo lançador de sombra e um `SoQuadMesh` 3D de duas faces
como receptor. A variável `COIN_QUADMESH_PRECISE_LIGHTING=1` ativa o fan
centrado; `SoMaterialBinding::PER_FACE` exige a cor flat do vértice provocador.
O teste percorre `quality=0,2` e `quality=1,0`, verifica `UNSUPPORTED`, o estado
capturado `BASE_COLOR` e a preservação byte a byte da publicação anterior.
Depois remove a ligação flat, usa o mesmo alvo e compara a recuperação com
CoinGL no interior 84×84 da imagem 128×128. A imagem recuperada difere da
anterior em 11.424 pixels.

| Executor e GPU | Rejeição 0,2/1,0 | Recuperação | CoinGL/GPU |
| --- | --- | --- | --- |
| BGFX/OpenGL AMD Mesa | 2/2 | PASS | erro médio/máximo 0/0 |
| BGFX/Vulkan AMD | 2/2 | PASS | 0/0 |
| BGFX/Vulkan NVIDIA | 2/2 | PASS | 0/0 |
| wgpu/Vulkan AMD | 2/2 | PASS | 0/0 |

O teste é opt-in no CTest (`COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU=1` ou
`COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU=1`) porque exige GPU e contexto CoinGL.
Na AMD, o CoinGL desta sessão precisou de
`COIN_GLXGLUE_NO_PBUFFERS=1`; sem isso a referência não obteve buffer, embora
a recuperação GPU tivesse sucesso. Foram usados `DISPLAY=:0`, o ICD Radeon
ou NVIDIA conforme a linha, e `LD_LIBRARY_PATH` do build correspondente.

## Limite de arquitetura

O `CoinRenderFramePlanBuilder` transforma a iluminação flat do fan preciso
em material iluminado `BASE_COLOR` para reproduzir o CoinGL sem sombras.
O perfil de sombras aceita apenas draws receptores `PHONG`; aceitar o estado
assado diretamente misturaria a contribuição iluminada e o fator de sombra.
Portanto a rejeição atual é correta para o contrato implementado. Para
suportar essa combinação, a captura e os shaders precisam carregar material
e normal flat por face sem assar a luz, e então calcular a iluminação/sombra
por fragmento. O item permanece aberto, assim como multitextura no centro do
fan e repetição Windows/Android/FreeCAD.
