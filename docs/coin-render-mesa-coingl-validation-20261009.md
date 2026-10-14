# Mesa privado com CoinGL original — 2026-10-09

Validação na AMD Renoir física, Mesa 25.2.8. A biblioteca CoinGL foi compilada
sem o complemento restrito de `SoIndexedFaceSet::GLRender`, usando o arquivo
`SoIndexedFaceSet.cpp` de `5430d7d3ff^` e os demais objetos do build da branch
de estudo. O `libCoin.so.80` resultante tem SHA256
`6bb822fcd772a97402f9771dabf45996bef38c1cabcc3793ab51af0450b78a39`.
O teste foi `CoinRenderDrawStyleTest` do mesmo build. `strace openat` confirmou
o carregamento desse CoinGL, do `libGLX_mesa.so.0` privado e do
`libgallium-25.2.8.so` privado; veja [loader-openat.txt](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-coingl-20261009/loader-openat.txt).

O Mesa candidato reuniu os patches de GS suplementar, transição GS→NULL e
preservação de `CLIP_DIST0` já descritos em
[Mesa clip boundary](coin-render-mesa-clip-boundary-20261009.md). O
[oracle OpenGL estrito](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-coingl-20261009/candidate-oracle.log)
passou com código 0 na AMD, usando o par GLX/Gallium privado.
Uma tentativa anterior que misturou o GLX instalado com Gallium privado não
criou FBConfig; esse resultado foi descartado. O candidato validado pelo oracle
tem SHA256 `f85a15810fa6908574e4418fd76ee8f45b88020c0afb7fce62b93e7e883b1642`.

| Mesa privado | CoinGL original | Resultado |
| --- | --- | --- |
| Candidato do oracle, sem extensão | Gate completo | Exit 1: a borda `clip 1` ficou 0/18 pixels; [log](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-coingl-20261009/original-candidate-full-gate.log). |
| Candidato com `GL_QUADS` de quatro vértices | Sonda `--probe-clipped-boundary` | Exit 0: borda 18/18 pixels, preenchimento 18/18; [log](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-coingl-20261009/quad-probe.log). |
| Candidato com `GL_QUADS` de quatro vértices | Gate completo | Exit 1: primeira divergência no `clip 5`, com dois planos de usuário; [log](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-coingl-20261009/quad-full-gate.log). |

Uma instrumentação temporária no `st_draw_clip_boundary` registrou, para a
sonda CoinGL, `mode=7` (`MESA_PRIM_QUADS`), quatro vértices,
`index_size=0`, um draw e um plano ativo. O candidato só selecionava o GS
suplementar para `MESA_PRIM_POLYGON` de quatro vértices e
`MESA_PRIM_TRIANGLES` de seis. O [patch de uma linha](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-coingl-20261009/mesa-gs-quad-coingl-probe.patch)
incluiu `MESA_PRIM_QUADS` no primeiro caso. Ele é apenas uma sonda e não
estabelece a semântica geral de quads. O caso `clip 5` permanece fora do guard
`ClipPlanesEnabled == 1` e do shader suplementar de um plano.

Os ensaios usaram `MESA_EXPERIMENTAL_CLIP_BOUNDARY=1`,
`MESA_SHADER_CACHE_DISABLE=true`, `AMD_DEBUG=nongg`,
`COIN_RENDER_REQUIRE_GL_REFERENCE=1` e o par GLX/Gallium privado no início de
`LD_LIBRARY_PATH`, com `LIBGL_DRIVERS_PATH` apontando ao driver privado. O
gate foi limitado por `timeout`; os três processos terminaram antes dele. Os
[hashes](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-coingl-20261009/hashes.txt) distinguem
CoinGL, candidato original e variante quad. Após o ensaio, `glxinfo -B`
confirmou renderização direta AMD com o Mesa instalado; a consulta ao journal
do intervalo não mostrou timeout, reset ou fault do amdgpu.

`st_draw.c`, `gl_nir_linker.c` e `libgallium-25.2.8.so` privados foram
restaurados byte a byte a partir dos backups. Seus SHA256 são,
respectivamente, `452c00c9333f77200e2a36fe942eceffc1d047cd9c61fd9f4868db59ae2c5156`,
`06ced335231f656b07c397f15f6b955626f1189731cec366c4313d937e37e1e5`
e `1dbd4e2530d7774fa08158f18a1152b7679607be9cf2199c5b3e60a7ea1b8e63`.
O driver instalado e a branch Coin não foram alterados por esses ensaios.

Próximos controles: suportar dois ou mais planos de usuário com recorte
sequencial; verificar modos `POINT` em `GL_QUADS`, edge flags, atributos
interpolados, culling, depth/stencil e stipple, além da transição para shaders
do aplicativo. O gate CoinGL completo e a integração geral do Mesa permanecem
abertos. O complemento restrito no Coin segue necessário com o driver instalado.
