# Estudo: raster nas junções curvas e coincidentes

Registrado em 2026-10-06 por solicitação do usuário. O critério escolhido para
P02/P04/P05/P06 é o **contrato portátil CPU/BGFX/wgpu**, com diferenças do
raster CoinGL delimitadas. A reprodução específica do raster deste driver é
um estudo de melhoria futura; não bloqueia o primeiro perfil portátil.
Este registro não declara concluída a validação de P02/P04/P05/P06: diferenças
entre CPU e BGFX/wgpu continuam exigindo correção e qualificação.

## Estudo controlado de 2026-10-07

O [relatório com controles e alternativas](coin-render-raster-study-20261007.md)
isolou três mecanismos: ownership de endpoints dependente do sentido de emissão,
extrapolação nativa de depth nos caps versus clamp comum, e quantização subpixel
na câmera AMD. Há sondas por polígono/segmento, matrizes, imagens, CSVs e hashes.
BGFX/wgpu Vulkan repetem o mesmo resultado da cena branca na AMD; controles
NVIDIA delimitam a diferença de câmera. Melhorias de produção, P20/alpha e
custos continuam abertos; nenhum gate foi alterado.

## Evidência anterior e contexto

Achados adicionais em [P20/câmera AMD de 2026-10-07](coin-render-hardware-surfaces-linux.md):
quatro pixels de borda na transparência offscreen Vulkan e 48 no fixture
wgpu de câmera ultrapassam os gates CoinGL. Reúso e travessia completa
reproduzem a diferença de câmera; BGFX/wgpu Vulkan coincidem no fixture P20.
A classificação de borda está demonstrada pelos pixels/cores vizinhas, mas
a causa específica ainda exige estudo. Esses achados não são prova de que
o mecanismo das junções curvas seja a causa; nenhum gate foi relaxado.

Os [logs diagnósticos preservados](validation/raster-junctions-study-20261006/README.md)
registram o reproducer e o controle FILLED, com o alcance das observações.

A fixture `CoinRenderProceduralTextureTest --probe-cone` reproduzia MAE 2,93584
e máximo 120 RGB entre CPU e wgpu em LINES/DEFAULT. A lateral e a tampa
emitiam bordas coincidentes em sentidos opostos, com UVs distintos. Dar uma
orientação comum à aritmética das bordas sólidas preserva seus atributos e a
ordem de publicação e reduziu o máximo a 1. Padrões de stipple conservam a
orientação autoral, pois ela determina sua fase.

A esfera conserva concordância CPU/wgpu de máximo 1, mas o reproducer nativo
`--probe-sphere` mede máximo 9 contra CoinGL no perfil com mipmaps P07.
O testemunho de profundidade mediu, no pixel (26,23), aproximadamente
0,32754022 na expansão comum e 0,30524847 no CoinGL. O contexto nativo fornece
D24, mas a diferença é muito maior que uma unidade D24. Isso demonstra depth divergente, mas isoladamente não distingue interpolação
de troca de cobertura. As sondas de 2026-10-07 identificam ambos os mecanismos:
(26,21) é explicado pelo cap; (26,23), por cobertura de uma borda autoral ausente
na expansão comum. Nenhum resultado isolado demonstra erro do driver.

Uma execução FILLED de controle conserva concordância RGB e, no mesmo pixel,
nenhum dos dois cobre a face. Experimentos com interpolação perpendicular,
alinhamento por células e extrapolação dos caps não eliminaram todas as
junções e introduziram outras diferenças. Esses experimentos **não fazem parte da
implementação mantida na branch**. O perfil conserva a expansão comum validada e
não amplia tolerâncias para fazer o oráculo nativo passar.

O controle `CoinRenderGeometryViewportTest --probe-style-line` amplia o
estudo aos endpoints compartilhados de LineSet com PER_PART, alpha e viewport
externo. CPU/wgpu diferem no máximo 1; CPU/CoinGL chega a 18 no pixel (19,56),
próximo à junção em (20,58). Desligar depth ainda produz máximo 24, e largura 1
produz máximo 18: a seleção de cobertura na junção também participa, além da
profundidade. A semântica dos bindings conserva seus gates independentes com
referência CoinGL; as células ampliadas de estilos/junções usam o esperado
portátil. A origem precisa da diferença nativa permanece pergunta do estudo.

A ampliação expôs ainda diferenças CPU/BGFX OpenGL em 42 das 84 células
Sphere/Cone/Cylinder com iluminação, alpha e viewport externo. A troca de D24
por D32F e o desligamento do caminho compacto não as eliminaram; esses testes
isoladamente não demonstram a causa. O pior pixel da esfera, (22,30), tinha
centro x=22,5, na borda da faixa expandida. A política comum de ownership
**top/left**, com um passo de 1/256 de pixel para evitar amostras exatamente
na borda, fez as 84 células passarem. Bounding boxes conservam sua direção e
ownership previamente qualificados. O perfil OpenGL provisoriamente delimitado
foi descartado: o gate completo de 1.290 células permanece obrigatório.

O shader BGFX também conserva intervalos constantes explicitamente por
`near + (far-near)*depth` e só calcula o gradiente quando factor é diferente
de zero. O estudo físico futuro deve separar essas correções comuns da
reprodução específica do CoinGL. Os logs anteriores de falha são históricos,
não uma dispensa de falhas portáteis na qualificação final.

## Endpoints transparentes isolados — 2026-10-10

Duas sondas EGL/OpenGL mínimas reutilizam as posições 3D das bordas **50/1** e
**36/1** da esfera preservada no estudo de 2026-10-07. A projeção ortográfica
(80×80), largura 4 e os pixels `(26,23)` e `(33,20)` correspondem aos
testemunhos anteriores. Cada sonda desenha a borda autoral, a invertida e duas
linhas coincidentes em mesma direção ou direções opostas, com vermelho/azul e
alpha 0,5, `SRC_ALPHA/ONE_MINUS_SRC_ALPHA`. Depth, textura, iluminação,
multisampling e smoothing ficam desligados para isolar cobertura e blend.

| Pixel e renderer | Autoral | Invertida | Par mesmo sentido | Par oposto |
| --- | --- | --- | --- | --- |
| `(26,23)` e `(33,20)`, Radeon e llvmpipe | `(128,0,0,64)` | `(0,0,0,0)` | `(64,0,128,96)` | `(128,0,0,64)` |
| `(26,23)` e `(33,20)`, NVIDIA | `(127,0,0,63)` | `(0,0,0,0)` | `(64,0,127,95)` | `(127,0,0,63)` |

Os dois pixels têm o mesmo padrão nos três renderizadores. O segundo traço
oposto não cobre o endpoint amostrado; portanto, nesse pixel o par oposto
conserva apenas a contribuição vermelha, enquanto o par no mesmo sentido
acumula azul e altera alpha. Isso demonstra um efeito de cobertura do sentido
de emissão sobre a mistura, sem depender de LOD, depth ou CoinRender. Não
identifica ainda qual política portátil é desejável para uma junção completa
em uma cena com materiais, depth e texturas.

[Fixtures, seis logs e hashes](validation/raster-junctions-study-20261006/alpha-endpoints-manifest-20261010.json).
A sonda usa EGL surfaceless e o driver Mesa instalado ou NVIDIA 615.71.09;
não usa o candidato Mesa privado. A medição não é uma campanha de custo nem um
gate CPU/BGFX/wgpu. A próxima prova necessária é renderizar o mesmo par
semitransparente nas três rotas portáteis, com ordem de submissão, depth e UV
controlados, antes de escolher entre orientação autoral, junção explícita ou
perfil de compatibilidade CoinGL.

## Par semitransparente no contrato portátil — 2026-10-10

A [sonda CoinRender](validation/raster-junctions-study-20261006/portable-alpha-20261010/CoinRenderJunctionAlphaStudy.cpp)
renderiza as mesmas duas bordas históricas como `SoLineSet`, em quatro modos:
linha autoral, invertida, par no mesmo sentido e par em sentidos opostos.
Cada linha tem material vermelho ou azul com transparência 0,5. A cena usa
`SORTED_OBJECT_BLEND`, largura 4, alvo 80×80, clear transparente e depth
desligado. CPU, GPU e CoinGL recebem a mesma cena. Para CoinGL, a imagem é
lida **após** a passagem transparente diferida do `SoOffscreenRenderer` e
convertida da origem inferior para top-down; a leitura antecipada ou sem essa
conversão dá um resultado falso.

Foram executados [32 casos físicos](validation/raster-junctions-study-20261006/portable-alpha-20261010/summary.json):
CPU/wgpu e CPU/BGFX, cada um em AMD Renoir e NVIDIA RTX 3060, com dois
endpoints × quatro modos. Os binários GPU são do build congelado de 2026-10-07,
identificado por hashes no manifesto. Em todos os casos, CPU/GPU tiveram
**diferença máxima de 1** por canal RGBA no quadro completo; GPU/CoinGL
tiveram cobertura RGB idêntica e diferença RGB máxima de 1.

A campanha foi repetida no [build BGFX do checkout atual](validation/raster-junctions-study-20261006/portable-alpha-20261010/summary-current-bgfx.json):
16 casos físicos AMD/NVIDIA, duas bordas × quatro modos. CPU/BGFX continuaram
com máximo **1 RGBA**; BGFX/CoinGL coincidiram em cobertura e tiveram máximo
**1 RGB**. As imagens RGBA top-down completas, logs e hashes estão junto dos
manifestos. Esta é uma sonda manual, não um novo gate CTest nem uma medição de
latência.

No endpoint de um traço, a saída BGFX AMD é `(128,0,0,128)` e a CoinGL é
`(128,0,0,64)`; no par no mesmo sentido, BGFX dá `(64,0,128,191)` e CoinGL
`(64,0,128,96)`. CPU difere da GPU em até 1 por arredondamento. A diferença
de alpha ocorre também fora do pixel testemunha, com máximo 95 no quadro;
corresponde à composição alpha convencional do contrato portátil versus o
blend alpha observado no CoinGL legado. A orientação do par altera a cobertura
e o RGB de ambos, mas **não** introduziu divergência de cobertura entre as
três rotas nesta cena reduzida. A decisão de compatibilidade alpha requer
tratar separadamente o contrato portátil e a referência CoinGL.

O teste comprova esses endpoints sem textura, depth ou iluminação. A cena
completa de esfera/LineSet com UV, depth e materiais heterogêneos ainda pode
selecionar outra borda vencedora. As alternativas e seu custo de preparação,
expansão e submissão continuam sem benchmark comparável.

## Perguntas e próximos experimentos

- Identificar a borda e o polígono que fornecem cor, UV e profundidade em cada
  pixel divergente, com testemunhos por segmento e captura de estado GL.
- Separar cobertura, interpolação, caps, ownership de profundidade e LOD;
  usar segmentos isolados, cruzamentos mínimos e valores numéricos autorais.
- Comparar larguras pares/ímpares, sentidos de emissão, câmeras ortográficas/
  perspectivas, clipping, alpha e texturas com contraste controlado.
- Repetir em APIs/drivers e GPUs diferentes antes de escolher uma melhoria
  portátil ou uma rota específica de compatibilidade CoinGL.
- Medir custo de preparação, geometria expandida e submissão em campanha
  isolada; os tempos CTest não são benchmark.

O estudo deverá propor alternativas concretas com imagens e métricas, custos,
limites e efeito sobre estado/rejeição/recuperação. O esperado portátil permanece
fixo enquanto a abordagem futura não for escolhida e qualificada.

## Como reproduzir

Usar os builds Release e o ambiente GPU da campanha integrada da branch
`codex/coin-render`. O reproducer da esfera continua retornando erro quando o
oráculo nativo ultrapassa MAE 1/máximo 3, para manter a diferença observável.

```sh
CoinRenderProceduralTextureTest --probe-sphere
CoinRenderProceduralTextureTest --probe-cone
COIN_PROBE_FILLED=1 CoinRenderProceduralTextureTest --probe-sphere
CoinRenderGeometryViewportTest --probe-style-line
CoinRenderGeometryViewportTest --probe-style-line-depthless
CoinRenderGeometryViewportTest --probe-style-line-width1
COIN_BGFX_RENDERER=opengl CoinRenderGeometryViewportTest --study-curved-styles
```

Os gates de estilos verificam CPU/BGFX/wgpu. Referências CoinGL brutas são
mantidas nas células qualificadas e identificadas nos logs; aprovação portátil
não significa igualdade pixel a pixel nas junções nativas.
