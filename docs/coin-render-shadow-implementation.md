# Sombras PBR implementadas no CoinRender

Perfil verificado em 13/10/2026: BGFX/Vulkan, GPU AMD `1002:1638`, PBR opaco,
HDR, IBL e views CAD offscreen/Xlib. [Marcos S1–S6](coin-render-shadow-milestones.md)
registram implementação e provas; [evidência final](validation/shadows/s6/summary.json)
registra 17 controles e cinco views CAD reais.

## Contratos

`SoPBRMaterial` continua descrevendo a superfície. `SoShadowGroup`, as luzes e
`SoShadowStyle` descrevem participação e iluminação. CoinRender combina a
visibilidade de cada luz com sua contribuição GGX antes de tone mapping:

\[
L_o=L_e+L_{IBL}+\sum_i V_i f_{GGX,i} L_i\max(0,n\cdot l_i).
\]

Sombras diretas não multiplicam emissão ou IBL. Linhas técnicas BASE_COLOR
permanecem após a conversão HDR/sRGB e não participam dos mapas. As luzes
respeitam seus índices na travessia Coin. Não foi introduzida uma nova camada
arquitetural; o planejamento usa snapshots, matemática Sb e o executor existente.

`SoShadowSpotLight.sourceRadius` é raio em unidades mundiais, com default zero.
`SoShadowDirectionalLight.angularRadius` é raio angular em radianos, qualificado
entre zero e 0,1. Esses campos pertencem à luz, são lidos pela sintaxe nativa Coin
e não mudam quando a view muda. Os valores são usados pelo PCSS; VSM/PCF
continuam aproximações pontuais. CoinGL legado não executa esse PCSS.

## Qualidade e recursos

| Mecanismo | Implementação e limite |
| --- | --- |
| VSM | Lookup legado; `epsilon` continua sendo acréscimo de variância |
| Bias PBR | Coeficientes em texels da projeção, convertidos na distância do receptor; inclinação usa normal geométrica |
| Momentos PBR | Segundo momento recebe `0.25*(dx²+dy²)` da profundidade normalizada |
| PCF | 3×3, nove comparações antes da média; raio zero produz sombra dura |
| PCSS | 16 amostras de bloqueadores e 16 comparações; profundidade spot radial é convertida para distância axial por raio |
| Penumbra spot | Aproximação `r*(z_receiver-z_blocker)/z_blocker`, convertida para UV pela projeção |
| Penumbra direcional | Aproximação `tan(angularRadius)*(z_receiver-z_blocker)` |
| Direcional estável | Extensão esférica em níveis relativos ao desenho; centro alinhado a texels com SbDP; profundidade mantém casters fora da view |
| Attachments | RGBA32F + D32F: 20 bytes/texel; teto padrão de 160 MiB/target, incluindo oito mapas 1024² |
| Cache | Attachments persistentes e chaves exatas de conteúdo/projeção/recorte/culling/dispositivo; 16 MiB de chaves por target |
| Amostragem | Orçamento conservador por área dos triângulos em pixels físicos e taps, incluindo sobreposição; default 128 Mi amostras |

PCF/PCSS usam leitura pontual dos primeiros momentos. Isso evita comparar uma
profundidade já filtrada. O limite de penumbra é uma regra de admissão: um emissor
que excede o raio configurado é recusado, sem clamp físico. O orçamento de
amostragem recusa a submissão antes dos attachments, sem reduzir resolução.
A publicação anterior permanece intacta. O driver pode adiar destruição de
attachments até concluir frames em voo. Limites de recursos não garantem FPS.

Chaves grandes demais desativam somente o cache de conteúdo; o mapa é redesenhado
corretamente. Alterações na geometria, culling, recorte, projeção, correção de
momentos ou geração do dispositivo invalidam o conteúdo. Cor da luz, intensidade,
BRDF, bias de lookup e tamanho do emissor não exigem regenerar a profundidade.

## Uso e views

```cpp
CoinRenderOptions options;
options.renderer = COIN_RENDER_RENDERER_VULKAN;
options.pbrShadowFilter = COIN_RENDER_PBR_SHADOW_PCSS;
options.pbrEnvironmentBundle = "studio.cibl";
// Structural options belong to target creation.
// Spot sourceRadius is world distance; directional angularRadius is radians.
```

O `coin_render_viewer` existente aceita `--shadows vsm|pcf|pcss` e
`--shadow-angular-radius radians`. Ele usa uma luz direcional de sombra e um
chão receptor relativo aos limites da peça. Exemplo:

```sh
coin_render_viewer --fixture flange --view iso-perspective --pbr \
  --environment studio.cibl --shadows pcss --cad-tools \
  --width 800 --height 600 --offscreen --validate-depth \
  --capture-depth --frames 1 --capture-prefix /tmp/flange
```

[Comandos reproduzidos](validation/shadows/s6/cad/commands.json), manifests,
bundle de ambiente, logs e imagens reais estão arquivados com as provas.

## Validação e limites

Os controles verificam radiância por luz, HDR, emissão, presença de IBL e acordo
com o programa IBL qualificado; placas finas e AS_IS/NO_CULLING; escala de unidades;
estabilidade de projeção; hits e invalidação do cache; falhas de alocação e perda
de dispositivo; comparação PCF/PCSS; penumbra maior com emissores maiores;
recusas por orçamento; compatibilidade Phong/CoinGL. As views CAD incluem flange,
montagem em janela nativa e carcaça recortada com oracle independente de profundidade.

A QA final corrigiu uma lista de defines CMake que perdia IBL na variante de
sombras. O teste de invariância anterior era insuficiente; o teste atual exige
contribuição efetiva e compara com o programa IBL sem grupos de sombra.

PCSS continua sendo uma aproximação de visibilidade com uma BRDF pontual; não
integra a BRDF sobre a área do emissor. Há transições discretas de extensão na
direcional, sem blending temporal/cascatas. Inputs e matrizes GPU são float,
apesar do cálculo SbDP. Texturas de material PBR, transparência/transmissão,
sombras omnidirecionais de point lights e outros executores PBR não foram
qualificados. Os riscos históricos de cache/ShapeHints do CoinGL permanecem
uma fila própria; os seis marcos não os declaram resolvidos.
