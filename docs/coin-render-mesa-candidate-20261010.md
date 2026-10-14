# Mesa 25.2.8: candidato de borda recortada na AMD

Resultado em 2026-10-10. O código está na árvore **privada** de Mesa em
`coin-portable-sampling-artifacts/20261008-failure-fixes/mesa-study/mesa-25.2.8`.
O driver instalado não foi alterado. O [patch](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-clip-boundary-candidate-20261010.patch)
é uma diferença reproduzível contra a tarball Mesa 25.2.8. Sua aplicação com
`patch -p1 --dry-run` e a compilação de `libgallium-25.2.8.so` passaram.

## O que passou

Com `MESA_EXPERIMENTAL_CLIP_BOUNDARY=1`, GPU física Radeon Renoir/radeonsi,
`AMD_DEBUG=nongg` e cache de shaders desativado:

| Controle | Resultado |
| --- | --- |
| Oracle OpenGL `--strict-boundary` | exit 0; borda 18/18 e pontos 4+4 no polígono isolado |
| Biblioteca CoinGL original, `--probe-clipped-boundary` | exit 0; oito casos, incluindo `GL_QUADS`, plano duplo e stipple |
| `GL_QUADS` texturizado, modo `LINE`, 1/2/5/8 planos de usuário | exit 0 em cada caso; bordas vertical e horizontal e coordenadas `TEX0` 17/17 nos casos de dois ou mais planos |
| `GL_QUADS` com fog linear, dois planos | exit 0; somas de cor das duas bordas iguais às do llvmpipe (difere apenas a posição rasterizada de uma linha) |
| Mesma biblioteca privada sem a flag, dois planos | exit 1; 0/17 bordas novas, baseline preservado |
| llvmpipe instalado, dois planos | exit 0; 17/17 bordas texturizadas |

[Log consolidado](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-clip-boundary-validation-20261010.log),
[fixture de textura](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-multiplane-texture-20261010.cpp),
[fixture de fog](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-multiplane-fog-20261010.cpp)
e [runner local](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-clip-boundary-validation-20261010.py).
O log do kernel não mostrou timeout, reset ou fault AMD depois da rodada física.
O executável CoinGL usa `SoIndexedFaceSet.cpp` original reconstruído de
`5430d7d3ff^`; não é byte a byte o mesmo binário de 2026-10-09 porque outros
objetos da árvore de testes foram recompilados. O teste de atributos de
polígono do gate agregado também falha com o Mesa instalado; essa falha não
mede a borda recortada do candidato.

## Limite para integração geral

O patch emite um segundo draw com geometry shader para os casos cobertos.
O shader transporta posição, cor primária, `TEX0`, `FOGC` e até oito distâncias de
clipping. O guard ainda admite apenas `GL_POLYGON`/`GL_QUADS` de quatro
vértices e `GL_TRIANGLES` de seis vértices, sem índices, draw indireto,
geometry ou tessellation shader do aplicativo. Ele não conserva todos os
varyings e regras de interpolação: textura adicional, cor secundária,
`flat`, `noperspective` e atributos genéricos precisam de implementação e
verificação. Também não há demonstração de equivalência para culling,
depth/stencil, blend, transform feedback, queries, faces, múltiplas primitivas
por draw ou custo em produção. O draw suplementar pode alterar efeitos
colaterais desses estados. Portanto, o patch **não é correção geral do Mesa**
e não deve ser instalado ou enviado upstream como tal. A correção nativa do
contorno/edge flags no radeonsi continua em aberto.

O próximo critério para encerrar o item é substituir o segundo draw restrito
por tratamento geral de clipping/edge flags no driver (ou provar equivalência
completa para os estados acima), acrescentar regressões Mesa e passar o gate
CoinGL agregado após separar a falha de atributos que já ocorre no driver
instalado. Até lá, o workaround CoinRender permanece necessário no Mesa
instalado deste sistema.

## Localização da falha e caminho de um draw — 2026-10-10

O [controle de estágio](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-clip-stage-probe-20261010.cpp)
desenha um triângulo em `GL_LINE` com `gl_ClipDistance[0]`. Uma variante passa
por vertex shader; a outra usa também um geometry shader que apenas repassa
posição e distância. Nos dois casos, o Mesa AMD instalado recorta corretamente
a aresta original (0 pixels do lado removido, 22 do lado mantido), mas não
desenha a aresta criada pelo plano (0/33 pixels). O llvmpipe instalado tem
o mesmo recorte nas arestas originais e desenha 33/33 pixels da nova aresta
com e sem GS. Todos terminaram com erro OpenGL zero. A
[saída integral](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-clip-stage-20261010.log)
inclui os seis processos e a identificação de cada renderer.

Esse controle elimina a hipótese de que a perda dependa especificamente da
saída do vertex shader ou da ausência de um geometry shader do aplicativo.
Ele mostra que o recorte ocorre, mas ainda não separa duas possibilidades
internas: a flag da nova aresta não sai do clipper acelerado ou o modo
`LINE` já foi aplicado antes da criação dessa aresta. O código Mesa de
software marca explicitamente a nova aresta em planos de usuário em
`draw_pipe_clip.c` antes de `draw_pipe_unfilled.c`; `nir_lower_clip.c` no
caminho acelerado apenas produz as distâncias. Os registros da AMD têm
`BOUNDARY_EDGE_FLAG_ENA`, mas a [documentação SI da AMD](https://www.amd.com/content/dam/amd/en/documents/radeon-tech-docs/programmer-references/SI_3D_registers.pdf)
o descreve como não utilizado, e o ensaio anterior desse bit no Renoir não
mudou os pixels. A documentação SI não comprova sozinha o comportamento do
Renoir; o ensaio físico é a evidência para este dispositivo.

Há uma base de implementação no próprio Mesa:
`src/mesa/state_tracker/st_draw_hw_select.c` já usa um geometry shader NIR com
recorte Sutherland–Hodgman de triângulos e quads contra frustum e planos de
usuário, para calcular profundidade de seleção. Esse código recorta apenas
posições e rejeita explicitamente `gl_ClipDistance`/`gl_CullDistance`; ele
fornece o algoritmo, não um shader pronto para o raster. A
[prova isolada em um draw](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-one-pass-clip-probe-20261010.cpp)
usa esse método para emitir um `line_strip` do quad recortado. Com dois planos,
deu 17/17 pixels nas bordas vertical e horizontal tanto na AMD quanto no
llvmpipe, sem patch no driver instalado. A
[variante POINT](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-one-pass-point-probe-20261010.cpp)
gerou os quatro pontos novos (16 pixels por ponto) nos dois renderizadores.
As provas só transportam posição e usam equações de plano fixas; não são
uma correção Mesa.

O próximo protótipo deve adaptar esse recorte para **substituir** o draw de
polígono `LINE`/`POINT` em vez de acrescentar outro. Precisa ler
`gl_ClipDistance` arbitrário, transportar varyings com as regras `smooth`,
`noperspective` e `flat`, conservar flags de aresta, face/culling e o contrato
de transform feedback. `nir_create_passthrough_gs` já clona as saídas do
estágio anterior e trata parte das flags/flat, mas não recorta; o código
Asahi observa uma limitação sua com transform feedback e polygon mode.
Essas peças dão um caminho de implementação, ainda sem prova de equivalência
geral ou regressões completas.

## Protótipo integrado de um draw para linhas — 2026-10-10

A [mudança incremental](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-one-draw-polygon-line-incremental-20261010.patch) aplica-se **depois** do patch candidato acima, sobre a árvore privada Mesa 25.2.8. Seu `patch -p1 --dry-run` passou contra a cópia do candidato anterior. A biblioteca privada foi recompilada; o driver instalado não mudou. O novo geometry shader recorta `GL_POLYGON`/`GL_QUADS` de quatro vértices por Sutherland–Hodgman e substitui o draw original no modo `GL_LINE`, conservando posição, cor primária, `TEX0`, `FOGC`, `PSIZ` quando exportado e até oito distâncias de clip. O guard continua restrito a um draw direto sem índices e sem GS/tessellation do aplicativo. `GL_POINT` e triângulos ainda usam o caminho suplementar anterior.

Na Radeon Renoir física, a [campanha integral](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-one-draw-line-validation-20261010.log) passou: quads texturizados com 1/2/5/8 planos, fog com dois planos, oracle GL estrito, sonda da CoinGL original, controle sem a flag e controles llvmpipe. O oracle mede também os pontos, mas estes vieram do caminho anterior de dois draws. Nenhum timeout, reset ou fault AMD apareceu no log do kernel após a rodada.

A tentativa de usar o mesmo shader de um draw para `GL_POINT` **não passou** no radeonsi: o oracle estrito perdeu os pontos inferiores e da direita, embora os quatro vértices de entrada e os sinais das distâncias estejam corretos. Retirar `EndPrimitive` após cada ponto fez o caso com `gl_ClipDistance` do aplicativo passar, mas não o caso de plano fixo. Com essa alteração, a mesma variante de um draw passou o oracle estrito inteiro no softpipe privado; o desvio é específico do caminho AMD testado. A variante de pontos foi retirada do patch incremental. O diagnóstico ainda não prova se a falha é na compilação ACO, no estado do radeonsi ou na interação com o VS de função fixa.

Este protótipo também não cobre todos os atributos e estados GL (incluindo interpolação `flat`/`noperspective`, face/culling, transform feedback e efeitos colaterais de depth/stencil/blend/queries). O gate CoinGL agregado não foi executado nesta rodada. Assim, a integração geral do Mesa e a remoção do workaround CoinRender continuam abertas.

## Correção do caminho `GL_POINT` em um draw — 2026-10-10

A [mudança incremental para pontos](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-one-draw-point-fix-incremental-20261010.patch) aplica-se depois da mudança de um draw para linhas. Seu `patch -p1 --dry-run` passou contra a cópia daquela versão. No shader de pontos, a leitura dinâmica do índice no laço de emissão foi substituída por até 12 emissões com índices constantes, cada uma condicionada à contagem recortada. As distâncias de saída dos até oito planos são inicializadas para valores positivos. O recorte Sutherland–Hodgman e o draw único permanecem os mesmos. O comportamento observado é compatível com um problema no caminho de emissão indexada do radeonsi, mas não isola uma instrução defeituosa no compilador.

Na Radeon Renoir física, o oracle estrito passou tanto com plano fixo como com `gl_ClipDistance` do aplicativo: os pontos novos nas extremidades deram 4+4 pixels em ambos os casos. O [fixture de `GL_QUADS`/`GL_POINT` texturizado](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-multiplane-point-20261010.cpp) passou com 1, 2, 5 e 8 planos; cada um dos quatro vértices esperados deu 25/25 pixels na janela amostrada ([resultados](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-one-draw-point-results-20261010.log)). A [campanha completa](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-one-draw-point-validation-20261010.log) também passou: textura e fog em `LINE`, oracle, sonda CoinGL original, controle sem flag e controles de software. A biblioteca privada foi recompilada; o driver instalado não mudou.

O caminho de um draw agora cobre `GL_POLYGON`/`GL_QUADS` de quatro vértices em `LINE` e `POINT` sob o guard experimental. O teste com oito planos ainda leva cerca de um minuto com o cache de shaders desligado; esse custo precisa de medição e redução. A validação exata de interpolação de cor/`TEX0` nos pontos, estados GL gerais, outras formas de geometria e o gate CoinGL agregado continuam pendentes. Isso não encerra a integração geral do Mesa nem autoriza remover o workaround CoinRender.

## Laço de planos, custo de compilação e estados GL — 2026-10-10

A [terceira mudança incremental](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-one-draw-runtime-planes-and-guards-20261010.patch) aplica-se depois dos patches de linha e ponto. Seu `patch -p1 --dry-run` passou. O geometry shader agora usa um único laço NIR para os planos ativos em vez de gerar uma cópia do algoritmo de recorte para cada plano. Na Radeon Renoir, com o cache de shaders desligado, o fixture de oito planos em `GL_POINT` passou em **5,59 s** (antes levava aproximadamente um minuto); o de `GL_LINE` passou em **6,35 s**. Essas são durações totais de processo de ensaios isolados, não uma medição de produção ou de tempo de compilador puro. Nos dois casos, os pixels de borda/ponto e as cores esperadas do fixture continuaram presentes.

A [campanha física final](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-one-draw-optimized-validation-20261010.log) passou os dez casos, cobrindo textura/fog, oracle estrito, sonda da CoinGL original e controles. O [gate CoinGL agregado](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-one-draw-optimized-aggregate-20261010.log) terminou com exit 0 e confirmou geometria recortada, stipple e atributos de polígono. O log recente do kernel não mostrou timeout, reset ou fault AMD.

Os [controles de estado](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-one-draw-state-checks-20261010.log) passaram 9/9: `GL_POINT` com oito planos; depth 0,5, stencil 1 e blend 50% no novo contorno de linha e ponto; e equivalência ao Mesa AMD nativo para culling, `GL_FLAT`, edge flags e `GL_PRIMITIVES_GENERATED` quando esses estados são excluídos do shader experimental. O controle de edge flags retorna 1 em ambos os caminhos porque o fixture original exige a borda que o driver nativo ainda perde; os pixels e o erro OpenGL coincidem. O guard também exclui transform feedback ativo, consultas de primitivas escritas, stencil com configuração distinta por face e fragment shaders que leem face ou ID de primitiva. Esses quatro casos não receberam execução específica nesta rodada; o estado de stencil comum passou no controle acima.

Há um limite concreto de atributos. Um [controle de cor por vértice](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-color-state-20261010.cpp) produziu valores diferentes do softpipe nativo em alguns pontos criados por dois planos; `GL_FLAT` diferiu claramente antes do guard. A especificação OpenGL descreve a interpolação dos novos vértices ao longo das arestas recortadas e exige ajuste para [`noperspective`](https://registry.khronos.org/OpenGL/specs/gl/glspec46.core.pdf); esse ajuste ainda não foi implementado. Cor secundária, unidades de textura adicionais e atributos genéricos também não são transportados; o novo guard devolve ao Mesa nativo os vertex shaders que exportam saídas fora do conjunto POS/COL0/TEX0/FOGC/PSIZ/CLIP_DIST0/CLIP_DIST1. Um [oracle com varying genérico](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-generic-varying-oracle-20261010.cpp) confirmou [pixels idênticos](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-generic-varying-control-20261010.log) com e sem a flag (ambos ainda sem a nova borda). Portanto, mesmo com o gate CoinGL agregado verde, o patch continua experimental e **não é uma correção geral do Mesa**. Os guards preservam o comportamento nativo nos estados excluídos, inclusive a borda faltante do driver. O workaround CoinRender continua necessário fora do conjunto qualificado.

## Cor secundária e unidades de textura adicionais — 2026-10-10

A [quarta mudança incremental](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-extra-slots-incremental-20261010.patch) aplica-se depois da terceira na árvore privada Mesa 25.2.8. O shader de recorte de um draw agora transporta `COL1` e `TEX1`–`TEX7` quando o vertex shader os exporta. Variantes com esses slots usam um cache por máscara de saídas, número de planos e modo de polígono. O caminho suplementar anterior permanece com o guard de atributos original. O patch passou em `patch -p1 --dry-run` contra a versão anterior e a biblioteca privada recompilou.

Na Radeon Renoir física, [19/19 controles](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-extra-slots-validation-20261010.log) passaram: cada `TEX1`–`TEX7` em `LINE` e `POINT` com dois planos, `TEX7`/`LINE` com oito planos, `COL1` em ambos os modos e os controles `TEX1` sem a flag. Os [fixtures `TEX1` em linha](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-tex1-line-20261010.cpp), [`TEX1` em ponto](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-tex1-point-20261010.cpp) e de [cor secundária](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-secondary-color-20261010.cpp) são exemplos reproduzíveis; os demais `TEXn` foram gerados trocando `GL_TEXTURE1` por `GL_TEXTUREn`. A [combinação de oito unidades simultâneas](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-multitex8-20261010.cpp) também passou com dois e oito planos na Radeon (17/17 pixels nas duas bordas, erro GL zero). A campanha existente 10/10, os estados 9/9 e o gate CoinGL agregado também passaram com esta biblioteca. A primeira execução em sandbox só acessou `softpipe`; os resultados físicos vieram das execuções com acesso ao display/GPU.

Um [contraexemplo estrito com varying genérico](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-generic-strict-after-extra-slots-20261010.log) ainda termina com exit 1 na Radeon: `gl_ClipDistance` do aplicativo e `extraColor` fazem o guard devolver o draw ao driver nativo; o contorno novo mede 0/18 pixels em `LINE` e os pontos 0/4 + 0/4 em `POINT`.

Isso fecha o transporte desses slots na geometria de quatro vértices qualificada, **não** a correção geral do Mesa. O shader ainda devolve varyings genéricos, `flat`, culling, edge flags, transform feedback e consultas de primitivas ao caminho nativo, que pode continuar sem a nova borda. A interpolação `noperspective`, a equivalência de cor em todos os vértices recortados, geometrias mais amplas e a correção nativa do radeonsi permanecem abertas. O driver instalado e o workaround CoinRender não foram alterados.

## Varying genérico `float32/smooth` — 2026-10-10

A [quinta mudança incremental](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-generic-smooth-incremental-20261010.patch) transporta `VARYING_SLOT_VAR0`–`VAR31` que o vertex shader exporta e o fragment shader lê, desde que o IO ligado seja escalar/vetor `float32`, sem componente empacotado ou indireção, e a entrada FS use `smooth` (ou modo padrão). A qualificação desserializa uma cópia do NIR e reconstrói suas variáveis de IO com `st_nir_unlower_io_to_vars`; o shader de recorte nunca altera o NIR do aplicativo. `flat`, `noperspective`, tipos inteiros e slots de 16 bits continuam excluídos. A variante de geometry shader agora usa uma chave de cache que inclui a máscara de saídas genéricas. A compilação privada e o `patch -p1 --dry-run` passaram.

O [oracle estrito com `extraColor`](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-generic-smooth-strict-20261010.log) passou na Radeon: a nova borda em `LINE` teve 18/18 pixels e os dois pontos criados em `POINT` tiveram 4/4 pixels cada. O [controle de cores](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-generic-smooth-color-20261010.cpp) confirma cor nos pontos novos. Com **um** plano, as cores dos dois pontos criados foram iguais às do llvmpipe: `128,128,0` e `128,128,128` ([fixture](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-generic-smooth-oneplane-20261010.cpp)). Com dois planos, a cor no encontro dos cortes diferiu: `128,128,64` no shader de polígono inteiro e `128,255,0` no llvmpipe que triangula o quad. Isso exige uma decisão explícita de semântica para quads com atributos não planares antes de alegar equivalência geral. Os controles `flat` e `noperspective` deram pixels idênticos ao Mesa nativo com e sem a flag ([log](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-generic-qualifier-guards-20261010.log)).

A [regressão existente 10/10](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-generic-smooth-regression-20261010.log), os [estados 9/9](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-generic-smooth-states-20261010.log) e o gate agregado da CoinGL passaram com esta biblioteca. O patch permanece experimental: deserializar NIR em cada draw genérico ainda precisa de medição/otimização, e não resolve culling, edge flags, transform feedback, queries, outras primitivas ou interpolação `flat`/`noperspective`.

## `noperspective` por componente empacotado — 2026-10-10

A [sexta mudança incremental](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-generic-noperspective-incremental-20261010.patch) amplia o recorte genérico `float32` para `noperspective`. O parâmetro de interseção em clip space é convertido para a fração em tela usando os valores `w` dos extremos. O fragment shader ligado pode empacotar componentes `smooth` e `noperspective` no **mesmo** slot genérico. Por isso o patch lê os modos das instruções `load_interpolated_input` e armazena uma máscara por componente; a reconstrução de variáveis NIR sozinha perderia essa distinção. Entradas `flat`, tipos não `float32`, IO indireto ou formas barycêntricas não reconhecidas continuam no guard nativo. O shader privado compilou sem warning e o patch passou em `patch -p1 --dry-run` sobre o quinto incremento.

Nos [controles com `w` variável](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-generic-noperspective-validation-20261010.log), o novo ponto teve `128,128,0` em [`noperspective`](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-generic-noperspective-w-20261010.cpp) e `178,77,0` em [`smooth`](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-generic-smooth-w-20261010.cpp); ambos coincidiram com llvmpipe. No [controle misto](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-generic-mixed-interpolation-20261010.cpp), cujos componentes compartilham `VAR0`, Radeon candidata e llvmpipe deram `178,128,0`. Uma [variante com saídas distintas](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-generic-mixed-interpolation-distinct-20261010.cpp) diferiu em 1 nível de 8 bits numa amostra e coincidiu nas demais. O controle AMD sem flag ainda omite os pontos novos.

A [regressão principal 10/10](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-generic-noperspective-regression-20261010.log), os [estados 9/9](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-generic-noperspective-states-20261010.log) e o [gate CoinGL agregado](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-generic-noperspective-aggregate-20261010.log) passaram na Radeon. O driver instalado não mudou. A correção geral permanece aberta: `flat`, culling, edge flags, transform feedback, queries, geometrias além de `GL_POLYGON`/`GL_QUADS` de quatro vértices e equivalência dos atributos em todos os casos ainda não estão cobertos.

## `flat` genérico e vértice provocador — 2026-10-10

A [sétima mudança incremental](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-generic-flat-incremental-20261010.patch) aceita componentes genéricos `float32/flat` e os preenche com o valor do vértice provocador antes do recorte. A máscara de componentes vem de `load_input` no NIR do fragment shader; ela convive com `smooth`/`noperspective` empacotados no mesmo slot. A escolha primeiro/último é especialização do shader a partir de `ctx->Light.ProvokingVertex`. Para `GL_QUADS`, respeita também `QuadsFollowProvokingVertexConvention`, anunciado pelo renderer; essa escolha integra a chave de cache. A primeira tentativa com `nir_load_provoking_last` foi descartada porque o ACO abortou ao compilar esse intrinsic. A biblioteca privada foi restaurada, reimplementada sem o intrinsic e recompilada; a versão entregue não contém o caminho que abortou. O patch passou em `patch -p1 --dry-run` sobre o sexto incremento.

Nos [controles físicos e de softpipe privado](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-generic-flat-validation-20261010.log), os quatro pontos recortados do [quad padrão](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-generic-flat-last-20261010.cpp) ficaram amarelos. Com [primeiro vértice em `GL_POLYGON`](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-generic-flat-first-polygon-20261010.cpp), os dois renderizadores produziram vermelho. Em [`GL_QUADS`](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-generic-flat-first-quad-20261010.cpp), Radeon (`quads_follow=1`) produziu vermelho e softpipe (`quads_follow=0`) permaneceu amarelo, como indicado pelas capacidades consultadas. O [controle misto `smooth`/`flat`](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-generic-mixed-flat-20261010.cpp) coincidiu nos dois renderizadores (`178,255,0` no ponto inferior).

A [regressão principal 10/10](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-generic-flat-regression-20261010.log), os [estados 9/9](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-generic-flat-states-20261010.log) e o [gate CoinGL agregado](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-generic-flat-aggregate-20261010.log) passaram. Isto completa `smooth`, `noperspective` e `flat` para os componentes genéricos `float32` do polígono de quatro vértices qualificado. Tipos inteiros/16 bits, geometrias maiores, culling, flags de aresta, feedback e queries ainda precisam de tratamento; o driver instalado e o workaround CoinRender permanecem como antes.

## Triângulos diretos em LINE e POINT — 2026-10-10

A [mudança incremental para `GL_TRIANGLES`](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-triangles-incremental-20261010.patch) aplica-se após a variante de `flat`; `patch -p1 --dry-run` passou. O mesmo GS de recorte recebe agora três vértices por primitiva para desenhos diretos `GL_TRIANGLES` com contagem múltipla de três. A chave de cache inclui a quantidade de vértices de entrada. Quads e polígonos de quatro vértices mantêm a conversão anterior para `LINES_ADJACENCY`. A biblioteca Mesa privada foi recompilada; a instalação do sistema não mudou.

No [controle de estágio](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-triangles-validation-20261010.log), o triângulo com `gl_ClipDistance` em `LINE` produziu **33 pixels** na coluna da nova borda na Radeon candidata e no llvmpipe; a Radeon sem patch produziu zero. Com **dois triângulos no mesmo `glBegin(GL_TRIANGLES)`**, o resultado foi novamente 33/33/0. Em `POINT`, candidato e llvmpipe desenharam os três pontos esperados, inclusive o novo ponto `(32,6)`; o driver sem patch desenhou apenas os dois originais. Os [fixtures de dois triângulos LINE](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-clip-stage-two-triangles.cpp) e [POINT](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-clip-stage-two-triangles-point.cpp) estão preservados.

A [regressão principal 10/10](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-triangles-regression-20261010.log), os [controles de estado 9/9](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-triangles-states-20261010.log) e a sonda CoinGL original passaram. A cobertura nova é restrita a desenhos diretos sem índices, com planos ativos e sem GS/tessellation do aplicativo ou estados recusados pelo guard. Ainda faltam outras geometrias, índices, tipos de varying, culling, flags de aresta, feedback, queries e validação ampla de estados/rasterização. A integração Mesa geral segue aberta; a flag experimental e o workaround CoinRender permanecem.

## Componentes inteiros `flat` — 2026-10-10

O [incremento seguinte](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-flat-integer-incremental-20261010.patch) aceita varying genérico escalar/vetorial `int32` ou `uint32` somente quando **todos os componentes lidos** são `flat`. O recorte copia os bits do vértice provocador diretamente nos cruzamentos, sem interpolação aritmética desses componentes; isso também evita alterar padrões especiais de `float flat`. O patch passou no `patch -p1 --dry-run` sobre a versão de triângulos e foi compilado na biblioteca Mesa privada. A política continua recusando inteiros interpolados, tipos de 16/64 bits, matrizes, arrays, IO indireto e outros layouts não qualificados.

Nos [12 controles de Radeon candidata, Radeon nativa e llvmpipe](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-flat-integer-validation-20261010.log), os shaders dos [fixtures `ivec4`](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-flat-ivec4-20261010.cpp) e [`uvec4`](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-flat-uvec4-20261010.cpp) compilaram e ligaram. Em `POINT`, o candidato e o llvmpipe produziram verde nos quatro pontos do quad recortado tanto para `INT_MAX` como para `~0u`; a Radeon nativa produziu apenas o ponto original superior direito. Em `LINE`, a cor dos pixels produzidos no candidato preservou o valor inteiro, mas a cobertura das novas bordas difere do llvmpipe nesse fixture; a comparação de cor foi interpretada apenas nos pixels acesos.

Passaram a [regressão principal 10/10](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-flat-integer-regression-20261010.log), os [estados 9/9](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-flat-integer-states-20261010.log) e o [gate CoinGL agregado](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-flat-integer-aggregate-20261010.log). A qualificação cobre os valores e estados efetivamente ensaiados, não equivalência geral de todos os programas inteiros. O item Mesa geral permanece aberto.

O [controle combinado](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-triangle-flat-integer-validation-20261010.log) usou dois `GL_TRIANGLES` no mesmo desenho, `gl_ClipDistance`, `GL_POINT` e um varying `flat int` com valor `INT_MAX`. O ponto novo `(32,6)` foi branco no candidato e no llvmpipe; a Radeon nativa não o desenhou. [Fonte do fixture](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-triangle-flat-integer-20261010.cpp).

A [repetição sem `AMD_DEBUG=nongg`](/mnt/Laranja/Git/externos/coin-render/docs/validation/raster-clip-edge-mesa-fix-20261009/mesa-triangle-default-amd-20261010.log) passou tanto no controle combinado quanto no oracle estrito. Isso qualifica também o caminho AMD padrão para esses casos específicos.
