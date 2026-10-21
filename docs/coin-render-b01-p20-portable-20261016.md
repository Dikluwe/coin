# B01.3a — P20 no perfil portátil, 2026-10-16

Continuação final: [B01 corrigido e qualificado no Linux disponível](coin-render-b01-completion-20261016.md), incluindo clipping/depth, instancing, janelas e sombras. Os limites e estados abaixo são os desta campanha anterior.

**P20 portátil qualificada nas 24 células CoinRender disponíveis neste host.**
A política explícita `COIN_RENDER_RASTER_CANONICAL_8BIT` passou nas duas GPUs,
com janela X11 e offscreen, cenas opacas e transparentes, BGFX OpenGL/Vulkan
e wgpu Vulkan. Esta foi a primeira campanha B01.3a; o B01 agregado foi
posteriormente fechado nas sete células Linux disponíveis, com os cruzamentos
B01.3b–d e o contrato nativo B01.2 documentados no relatório final acima.

| Verificação | Resultado |
| --- | --- |
| Execuções finais | 64/64 capturaram: 32 controle nativo + 32 rodada canônica (24 CoinRender + 8 CoinGL nativos) |
| CoinRender canônico × rasterizador CPU | 24/24; RGB máximo ≤2, MAE ≤1,0 |
| BGFX OpenGL/wgpu Vulkan × BGFX Vulkan, mesma GPU/superfície/cena | 16/16; RGB idêntico |
| BGFX Vulkan × wgpu Vulkan | 8/8; checksum RGBA8 exato, incluindo alpha |
| AMD × NVIDIA canônico | 12/12; opacas RGB idêntico, transparentes máximo 1 |
| Controle nativo × CoinGL | 22/24; duas divergências históricas AMD/Vulkan offscreen preservadas |
| Intel física | 16 células por política indisponíveis; host sem GPU Intel |

## Escopo e correções

As fixtures históricas P17/P20 foram preservadas byte a byte, com os SHA-256
originais: `32854a58541b03ad07bbf03f8cace6e8b394b456d83d6e9be0e20e6d5473c072`
(opaca, 36 objetos) e
`7fc70de5b45323521ff68714298049625b1f66ba1248a42c3952879d895a4caf`
(transparente, 49 objetos). Release, 128×128, um frame de aquecimento e três
frames medidos, transparência por objeto, captura síncrona final. Não é uma
medição de desempenho. O código base é `24ddf557af`, com as mudanças locais
B12/B02/B01 e desta continuação identificadas por hashes no manifesto.

Os benchmarks não tinham seletor para a nova política. Ambos agora aceitam
`--rasterization-policy native|canonical`, mantêm `native` como padrão,
rejeitam valores desconhecidos e recusam aplicar a política ao backend CoinGL.
O offscreen canônico usa opções imutáveis explícitas com a API consultada e
o modo de transparência solicitado; a construção nativa anterior foi preservada.
O runner exige confirmação da política e da API nas saídas, verifica GPU
física via IDs Vulkan/contexto EGL e exige um oráculo CPU para qualificar o
perfil canônico.

A primeira versão do cruzamento no runner comparou offscreen com janela.
Isso era um erro do teste: janela usa câmera ortográfica em `(0,0,8)`, altura
5,5 e fundo preto; offscreen usa perspectiva ajustada `viewAll(...,1.15)`,
direção `(-0.5,-0.35,-1)` e fundo `(0.1,0.1,0.1)`. Corrigimos o cruzamento
para respeitar cada câmera e superfície. Os resultados dessa rodada ficaram
em `initial-runner-diagnostic/`, sem contar para a qualificação final.

`CoinRenderP20ReferenceCapture` carrega as mesmas cenas e reproduz essas duas
câmeras em quatro capturas com o rasterizador CPU, sem contexto GPU. O código
de rasterização CPU é independente dos shaders BGFX/wgpu, mas compartilha a
captura da cena; por isso esta campanha complementa os oráculos geométricos
analíticos de B01.1. As 24 comparações RGB com CPU passaram no gate histórico
máximo ≤2 e MAE ≤1,0, sem alterar tolerâncias ou geometria.
Não foi necessária outra alteração no núcleo de rasterização para estas cenas.

## Controle CoinGL e limites

Na execução **nativa**, AMD/Vulkan BGFX e wgpu continuam com MAE 0,134012858,
máximo 17 e quatro pixels acima de três níveis na transparência offscreen.
As outras 22 comparações nativas passaram. O runner nativo retorna falha
nessas duas células; elas continuam registradas como B01.2.

No perfil **canônico**, todos os executores concordam com a CPU e entre si.
Contra CoinGL nativo, 12/24 comparações RGB passam (janelas) e 12/24 falham
(offscreen). As cenas opacas têm três pixels acima de três níveis, máximo
184; as transparentes, dois pixels, máximo 134. Esses resultados ficam nos
JSONs como diagnóstico com `qualification_gate=false`: a política canônica
pode deslocar uma aresta em até 1/256 de pixel e escolher outro fragmento de
alto contraste. A paridade com CoinGL nativo continua com seu gate próprio;
a aprovação portátil não é usada para aprová-la.

Este fechamento abrange as quatro combinações originais de executores da
P20, duas GPUs e duas superfícies disponíveis. Não qualifica wgpu/OpenGL P20,
Intel, Android, Metal, Windows, Wayland, tickets assíncronos, UV/LOD,
culling ou diagonais adversariais, clipping extremo, sombras ou primitivas
nativas recusadas. Janelas estão qualificadas somente para as cenas estáticas,
câmeras e captura final descritas aqui. Custo da política continua em B10.

## Evidência e reprodução

[Inventário por célula](inventories/coin-render-p20-portable-matrix.json),
[Resumo verificável](validation/coin-render-b01-p20-20261016/portable-summary.json),
[comandos finais](validation/coin-render-b01-p20-20261016/campaign.json),
[manifesto e hashes](validation/coin-render-b01-p20-20261016/manifest.json),
[fixtures](validation/coin-render-b01-p20-20261016/fixtures/scenes.json).
Os quatro diretórios `amd-native`, `amd-canonical`, `nvidia-native` e
`nvidia-canonical` contêm logs, identificação física, checksums e PPMs por célula.

1. Compilar `coin_render_gl_benchmark`, `coin_render_window_benchmark` e
   `CoinRenderP20ReferenceCapture` em Release, habilitando
   `COIN_BUILD_RENDER_BENCHMARKS` e `COIN_BUILD_RENDER_WINDOW_EXAMPLE`.
2. Criar o diretório CPU e executar
   `CoinRenderP20ReferenceCapture FIXTURES CPU_OUTPUT`.
3. Fixar DISPLAY/ICD/GLX/EGL/PRIME conforme
   [run_campaign.py](validation/coin-render-b01-p20-20261016/run_campaign.py) e
   executar `scripts/coinrender/run_p20_physical_matrix.py` para cada GPU e
   política. `canonical` exige `--cpu-reference-dir CPU_OUTPUT`; CoinGL recebe
   sempre `native`. A rodada nativa AMD termina com código 1 pelas falhas conhecidas.
4. Executar `python3 scripts/coinrender/summarize_p20_portable.py CAMPAIGN_DIR`.
   O resumo reprova células ausentes, falhas portáteis, diferenças entre GPUs
   fora do gate e falhas nativas diferentes das duas explicitamente conhecidas.

Também passaram cinco controles negativos do resumo (célula ausente, política
incorreta, reprovação CPU, falha nativa inesperada e divergência entre GPUs)
e oito verificações de argumentos inválidos nos dois builds.
[Resultados](validation/coin-render-b01-p20-20261016/negative-verification.json).

Hardware observado: AMD Renoir `1002:1638`, Mesa RADV/radeonsi 25.2.8;
NVIDIA RTX 3060 Laptop `10de:2560`, driver 615.71.09; Linux 6.17.0-42-generic.
