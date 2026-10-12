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

O mesmo teste completo, sem opções diagnósticas, passou no wgpu/Vulkan da
NVIDIA física com referência CoinGL obrigatória. No frame 5 de perspectiva,
MAE foi 0,0590617, máximo RGB 1 e nenhum pixel >3; a reconstrução foi
idêntica ao reuso. Os nove frames ortográficos também passaram. Essa célula
qualifica o driver NVIDIA neste fixture, sem alterar o FAIL AMD.

## Correção opcional para captura offscreen AMD/Vulkan

`CoinRenderTarget::readbackCenterSampledRGBA(3, pixels)` lê o centro de cada
bloco 3×3 de um alvo offscreen CPU-readable. O consumidor deve criar o alvo
com três vezes a largura e a altura lógicas **e** usar esse tamanho físico
no viewport da `CoinRenderAction`. A API recusa fator par, dimensões
incompatíveis e readback ainda não publicado. Não muda o raster da janela,
RTT, tickets assíncronos ou o modo padrão de 1×.

Com `COIN_RENDER_CAMERA_REFERENCE_SUPERSAMPLE=3`, o teste completo da câmera
passou em **wgpu/Vulkan AMD** e **BGFX/Vulkan AMD**, mantendo o oracle CoinGL
e o mesmo gate (MAE ≤0,10, até 131 pixels RGB >3). No frame 5 de perspectiva,
ambos obtiveram MAE 0,0594686 e 1 pixel >3; no frame 1 ortográfico,
wgpu obteve 0,0836589/3 pixels e BGFX 0,0842336/4 pixels. Todos os pares
reuso/reconstrução permaneceram RGB idênticos. Essa rota também corresponde à
sonda P20 em que 3× zerou os quatro pixels divergentes da cena isolada.

O custo geométrico é renderizar **9×** pixels e manter um alvo/readback físico
9× maior. Ainda faltam medição de desempenho, gate das demais cenas,
integração em janela/RTT e qualificação em outros drivers. A varredura P20
mostrou que fatores ímpares maiores não melhoram monotonamente a cobertura;
por isso a correção é opt-in para captura offscreen e a frente geral permanece
aberta.

## Matriz física AMD/NVIDIA e custo da opção — 2026-10-12

O [runner de câmera](run_camera_matrix.py) repetiu o gate CoinGL estrito nas
duas GPUs físicas e nos builds BGFX/wgpu. O teste passou a registrar o
adaptador CoinRender e o renderer do contexto CoinGL; o runner recusa vendor
incorreto. Para BGFX/OpenGL AMD, a sonda de capacidades retorna vendor `0`,
então o runner exige o trace do contexto EGL corrente (`renderer=AMD Radeon`).
Host: AMD Ryzen 7 5800H, Radeon `1002:1638` e NVIDIA RTX 3060 Laptop
`10de:2560`, Linux `6.17.0-42-generic`. [Resultados por célula](camera-matrix-results.json).

| GPU / API | 1× | 3× | Observação |
| --- | --- | --- | --- |
| AMD BGFX/Vulkan | FAIL, perspectiva frame 5 | PASS, 18 frames | 3× corrige este fixture |
| AMD wgpu/Vulkan | FAIL, perspectiva frame 5 | PASS, 18 frames | 3× corrige este fixture |
| AMD BGFX/OpenGL | PASS | PASS | contexto EGL AMD verificado |
| AMD wgpu/OpenGL | PASS | PASS | adaptador AMD verificado |
| NVIDIA BGFX/Vulkan | PASS | FAIL, perspectiva frame 5 | 3× eleva MAE a 0,148076 |
| NVIDIA BGFX/OpenGL | PASS | FAIL, perspectiva frame 5 | mesmo desvio 3× |
| NVIDIA wgpu/Vulkan | PASS | FAIL, perspectiva frame 5 | mesmo desvio 3× |
| NVIDIA wgpu/OpenGL | indisponível | indisponível | sem adaptador WebGPU GL neste build |

### Por que 3× falha na NVIDIA no frame 5

Capturei o par CoinRender/CoinGL do frame 5 em wgpu/Vulkan nas quatro
combinações AMD/NVIDIA × 1×/3×, usando
`COIN_RENDER_CAMERA_REFERENCE_IMAGES`. O fixture agora grava também o frame 5
quando ele passa; essa opção diagnóstica não altera o gate. A coluna `x=195`
contém os mesmos 44 centros de pixel descritos pela projeção independente
acima. A cobertura observada nesses centros foi:

| GPU | CoinGL 256×256 | CoinRender 1× | CoinRender 3×, amostra central |
| --- | ---: | ---: | ---: |
| AMD | 44/44 cobertos | 0/44 cobertos | 44/44 cobertos |
| NVIDIA | 0/44 cobertos | 0/44 cobertos | 44/44 cobertos |

Assim, o CoinRender muda a seleção desses pixels com o tamanho físico do
viewport nas **duas** GPUs. A referência CoinGL, renderizada sempre em
256×256, seleciona lados opostos da aresta na AMD e na NVIDIA. Os 44 pixels
da coluna são todos GPU preto/CoinGL coberto na falha AMD 1× e todos GPU
coberto/CoinGL preto na falha NVIDIA 3×. As duas falhas compartilham 47
coordenadas RGB >3; a NVIDIA 3× acrescenta `(122,206)` e `(253,89)`.

A aresta projetada em `x=195,501771270` fica só `0,001771270` pixel além
do centro `195,5`. No viewport 3×, ela cai em `x=586,505313810`, enquanto
a amostra central está em `586,5`. Como modelo, arredondar à grade de 1/256
pixel põe a aresta 1× exatamente no centro, mas a 3× em `586,50390625`,
além do centro. Isso explica por que a cobertura GPU troca ao escalar;
não demonstra que os drivers usem precisamente essa grade internamente.
Ambos os contextos CoinGL reportaram `GL_SUBPIXEL_BITS=8`; esse dado sozinho
não explica por que CoinGL AMD e NVIDIA escolhem lados diferentes. A causa
interna exata dessa diferença entre drivers permanece não comprovada.

PPMs e logs dos quatro controles estão em
`/tmp/coin-render-camera-cross-coverage-20261012`. O caminho NVIDIA 1×
continua passando o gate; a falha NVIDIA surge ao aplicar a opção 3×.
[Auditoria por pixel](check_cross_edge.py) e
[resultado consolidado](cross-edge-results.json) reproduzem a tabela com as
capturas da matriz física.

O [runner das cenas P20](run_p20_scene_matrix.py) comparou a cena opaca e a
transparente em 128×128 contra CoinGL. O benchmark usa a API pública de
amostra central para renderizar em 384×384 e publicar 128×128 no modo 3×.
O gate pré-existente é RGB máximo ≤2 e MAE ≤1,0. BGFX e wgpu Vulkan
produziram imagens idênticas entre si em cada GPU, cena e fator.
[Resultados por cena](p20-scene-results.json).

| GPU / cena P20 | 1×: RGB máximo, pixels >3 | 3×: RGB máximo, pixels >3 |
| --- | ---: | ---: |
| AMD / opaca | 0, 0 (PASS) | 146, 1 (FAIL) |
| AMD / transparente | 17, 4 (FAIL) | 1, 0 (PASS) |
| NVIDIA / opaca | 0, 0 (PASS) | 146, 1 (FAIL) |
| NVIDIA / transparente | 2, 0 (PASS) | 19, 4 (FAIL) |

Portanto, **3× não pode ser ativado automaticamente nem mesmo só por vendor,
API ou tipo de alvo**: na AMD/Vulkan corrige a câmera e a transparência P20,
mas regride a cena P20 opaca. O método público permanece uma opção explícita
do consumidor para cenas qualificadas. Esses resultados também descartam usar
3× como solução geral para janela ou RTT sem uma política adicional.

O [piloto de custo](run_offscreen_cost.py) executou três processos por célula,
cada um com 8 warmups e 60 frames, na cena transparente P20 em alvos 256×256
e 768×768, usando a API pública para publicar 256×256 no modo 3×. A mediana
abaixo é a mediana das medianas de tempo de `render+readback+publicação`,
incluindo a cópia da amostra central, de cada processo. Não inclui criação do
alvo. Os processos alternaram a ordem 1×/3×.
[Resultados por processo](offscreen-cost-results.json).

| GPU / backend Vulkan | 1× mediana | 3× mediana | Razão |
| --- | ---: | ---: | ---: |
| AMD / BGFX | 0,369 ms | 1,139 ms | 3,09× |
| AMD / wgpu | 0,433 ms | 1,215 ms | 2,81× |
| NVIDIA / BGFX | 0,317 ms | 1,038 ms | 3,27× |
| NVIDIA / wgpu | 0,520 ms | 1,587 ms | 3,05× |

O alvo RGBA8 físico exige 2,25 MiB em 768², contra 0,25 MiB em 256², além
das demais alocações do backend. O pico RSS mediano dos processos aumentou
aproximadamente 6,0–9,4 MiB conforme GPU/backend, incluindo inicialização
e alocações do driver. O custo medido é local e específico da cena;
não é previsão de FPS de janela. Logs/imagens integrais ficam em
`/tmp/coin-render-camera-matrix-20261012`,
`/tmp/coin-render-p20-center-api-20261012` e
`/tmp/coin-render-offscreen-api-cost-20261012`.

Reprodução, a partir da raiz do repositório, com builds BGFX e wgpu que
incluam a API de amostra central:

```sh
python3 docs/validation/perspective-coverage-20261012/run_camera_matrix.py \
  --bgfx-build /tmp/coin-render-closure-bgfx \
  --wgpu-build /tmp/coin-render-closure-wgpu \
  --output /tmp/coin-render-camera-matrix-20261012 --capture-images
python3 docs/validation/perspective-coverage-20261012/run_p20_scene_matrix.py \
  --bgfx-build /tmp/coin-render-closure-bgfx \
  --wgpu-build /tmp/coin-render-closure-wgpu \
  --output /tmp/coin-render-p20-center-api-20261012
python3 docs/validation/perspective-coverage-20261012/run_offscreen_cost.py \
  --bgfx-build /tmp/coin-render-closure-bgfx \
  --wgpu-build /tmp/coin-render-closure-wgpu \
  --output /tmp/coin-render-offscreen-api-cost-20261012
```

Reprodução do modelo, a partir da raiz do repositório (substituir `BUILD`
pelo diretório do build wgpu ou BGFX):

```sh
g++ -std=c++11 -Iinclude -I$BUILD/include \
  docs/validation/perspective-coverage-20261012/project_cubes.cpp \
  -L$BUILD/lib -Wl,-rpath,$BUILD/lib -lCoin -o /tmp/coin-render-project-cubes
/tmp/coin-render-project-cubes > /tmp/coin-render-projected-cubes.txt
python3 docs/validation/perspective-coverage-20261012/check_edge.py \
  /tmp/coin-render-projected-cubes.txt ACTUAL_PPM COINGL_PPM
python3 docs/validation/perspective-coverage-20261012/check_cross_edge.py \
  --projected /tmp/coin-render-projected-cubes.txt \
  --matrix /tmp/coin-render-camera-matrix-20261012 \
  --output /tmp/coin-render-cross-edge-results.json
```
