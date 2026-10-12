# Perspectiva AMD/Vulkan contra CoinGL — 2026-10-12

Repeti `CoinRenderCameraReuseReferenceTest` no checkout atual com
`COIN_RENDER_REQUIRE_CAMERA_REFERENCE=1`,
`COIN_RENDER_REQUIRE_GL_REFERENCE=1`, `DISPLAY=:0`, ICD Radeon,
`COIN_GLX_PIXMAP_DIRECT_RENDERING=1` e `COIN_GLXGLUE_NO_PBUFFERS=1`.
BGFX usou `COIN_BGFX_RENDERER=vulkan`; o build wgpu usou a ponte Rust.
O gate do teste permanece MAE ≤0,10 e no máximo 131 pixels com erro RGB >3.

| Executor | Frame 5 MAE | Pixels >3 | Reuso/reconstrução |
| --- | ---: | ---: | --- |
| wgpu/Vulkan AMD | 0,147283 | 47 | RGB 0 |
| BGFX/Vulkan AMD | 0,149582 | 49 | RGB 0 |

No controle wgpu/OpenGL da mesma AMD, os nove frames de perspectiva e os
nove ortográficos passaram; no frame 5 de perspectiva, MAE foi 0,0592906,
máximo RGB 2 e nenhum pixel >3. O gate Vulkan continua falhando sem que
se relaxe a tolerância.

O teste falhou nos dois executores, como na medição anterior. Salvei o par
`perspective-CoinGL-5-{actual,reference}.ppm` com
`COIN_RENDER_CAMERA_REFERENCE_IMAGES` e comparei os RGBs sem mudar o gate.
As referências CoinGL dos dois processos são byte a byte iguais. Os 47
pixels wgpu com erro >3 são compartilhados pelo BGFX; 44 estão em `x=195`.
Em wgpu, 45 pixels são pretos no GPU e cobertos no CoinGL, 2 são o inverso,
e nenhum tem ambas as imagens cobertas. BGFX acrescenta dois pontos
`(122,206)` e `(253,158)`; seu total inclui 46 GPU pretos/CoinGL cobertos,
2 inversos e 1 ponto coberto em ambos. Isso localiza o desvio dominante na
seleção de cobertura de arestas, sem provar ainda qual convenção/precisão
produziu a diferença.

SHA-256 das imagens PPM da sessão: CoinGL
`2f3b229dc67b2c5dbf5331145ad8890d39659fe1bffb91eea6b9699f528ff0dd`,
wgpu `9fe2e824f89d4f24a4f3691b83d09ef08a5fbb8209480d83e5c09b110bca9a42`
e BGFX `1f266f124fef4375098b24f68bd7746a3d37c48bdd1e428a6d62a072845bc416`.
Os PPMs locais estão em `/tmp/coin-render-perspective-20261012/` e
`/tmp/coin-render-perspective-bgfx-20261012/`; o teste reproduz as métricas.

## Aresta coincidente e perturbação controlada

O [projetor](project_cubes.cpp) reproduz a câmera do frame 5 e as matrizes
dos cubos, inclusive o `SoTransform` anterior às luzes que permanece no
estado da cena. Compilado contra a `libCoin` do build, ele grava as oito
quinas projetadas de cada cubo. O [comparador](check_edge.py) cruza essas
coordenadas com o par de PPMs do teste.

Os cubos `(8,1)`, `(8,4)`, `(8,7)`, `(8,10)` e `(8,13)` têm a aresta direita
frontal na mesma coordenada **x=195,501771270**. O centro da amostra da coluna
195 é x=195,5. Arredondar a aresta à grade de 1/256 pixel a coloca
exatamente nesse centro, enquanto a grade de 1/4096 a coloca em
x=195,501708984. Os intervalos Y projetados dessas cinco arestas contêm
respectivamente 9, 9, 8, 9 e 9 centros de pixel. A união coincide exatamente
com os **44/44** pixels pretos no Vulkan e cobertos no CoinGL nessa coluna,
em wgpu e BGFX. É uma explicação geométrica específica para essa coluna;
os outros 3/5 pixels divergentes requerem análise separada.

Para conferir a sensibilidade, o fixture aceita apenas para diagnóstico
`COIN_RENDER_CAMERA_REFERENCE_FRAME5_X_SHIFT`. Com `+0.0005`, o frame 5
mudou de MAE 0,147283/0,149582 e 47/49 pixels >3 para
**0,066213/0,0662181 e 7/7 pixels >3** em wgpu/BGFX. Os nove frames de
perspectiva passaram nos dois executores usando
`COIN_RENDER_CAMERA_REFERENCE_ONLY_PERSPECTIVE=1`, sem alterar o gate.
O deslocamento da câmera é um controle causal do fixture, não uma política
de rasterização para produção.

Ao permitir que o teste prosseguisse além do frame 5, a sequência ortográfica
da AMD/Vulkan falhou independentemente no frame 1: wgpu MAE 0,102193,
20 pixels >3. Reuso e reconstrução ainda coincidiram RGB 0. O modo
`COIN_RENDER_CAMERA_REFERENCE_ONLY_ORTHOGRAPHIC=1` reproduziu essa falha
sem executar a perspectiva. A frente geral permanece aberta.

Um controle ortográfico com `COIN_RENDER_CAMERA_REFERENCE_BASE_COLOR=1`
desativou a iluminação sem mudar a geometria. Ele falhou já no frame 0
(MAE 0,140722, 7 pixels >3); nos sete pontos, um dos dois RGBs é preto e
o outro é a cor base. Logo, iluminação não é necessária para produzir a
divergência ortográfica neste host. O fixture iluminado do frame 1 continua
com 20 pontos >3, dos quais 10 têm cores não pretas nos dois lados; a
seleção de superfície/face pode explicar parte deles, mas ainda não foi
modelada como no caso de perspectiva.

Reprodução do modelo, a partir da raiz do repositório (substituir `BUILD`
pelo diretório do build wgpu ou BGFX):

```sh
g++ -std=c++11 -Iinclude -I$BUILD/include \
  docs/validation/perspective-coverage-20261012/project_cubes.cpp \
  -L$BUILD/lib -Wl,-rpath,$BUILD/lib -lCoin -o /tmp/coin-render-project-cubes
/tmp/coin-render-project-cubes > /tmp/coin-render-projected-cubes.txt
python3 docs/validation/perspective-coverage-20261012/check_edge.py \
  /tmp/coin-render-projected-cubes.txt ACTUAL_PPM COINGL_PPM
```
