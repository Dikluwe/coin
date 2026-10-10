# Mesa radeonsi: contorno após clipping de polígono

Investigação de 2026-10-09 para uma correção **no Mesa**, separada do reparo
restrito de CoinGL que fecha o gate deste PC. A correção no driver ainda não
foi obtida; nenhum resultado desta página deve ser tratado como patch pronto.

## Regressão reproduzível

O [oracle OpenGL](../testsuite/reproducers/raster-clip-edge/RasterClipEdgeGlOracle.cpp)
agora aceita `--strict-boundary`. Ele testa `GL_POLYGON` e `GL_TRIANGLES` em
`GL_LINE`, `GL_POLYGON` em `GL_POINT`, um plano fixo e `gl_ClipDistance`, além
de controles pré-recortados, linha explícita e preenchimento. Retorna 1 quando
faltam bordas ou pontos criados pelo clipping. A
[especificação OpenGL 4.6 Compatibility, §14.6.4](https://registry.khronos.org/OpenGL/specs/gl/glspec46.compatibility.pdf)
define o contorno recortado como a entrada dos modos `LINE` e `POINT`.

| Mesa 25.2.8 | Borda `GL_POLYGON` (18 pixels) | Pontos novos (4+4 pixels) | Exit estrito |
| --- | ---: | ---: | ---: |
| radeonsi Renoir, driver instalado | 0 | 0+0 | 1 |
| llvmpipe | 18 | 4+4 | 0 |

O mesmo 0/18 ocorre com `gl_ClipDistance`; `GL_TRIANGLES` conserva apenas
1/18. A linha explícita e o polígono pré-recortado dão 18/18 no radeonsi;
o preenchimento recortado também dá 18/18. Assim, o plano, as coordenadas e
o raster de linha isolado funcionam. A perda está na passagem do contorno
novo do clipper ao modo de polígono. Esta localização é uma **inferência** dos
controles, não uma confirmação do bloco de hardware responsável.

## Experimentos no Mesa privado

O fonte Mesa 25.2.8 e `libgallium-25.2.8.so` foram recompilados em uma árvore
privada. A ordem de `LD_LIBRARY_PATH` e `strace openat` confirmaram que cada
ensaio carregou o binário recompilado, em vez da cópia instalada. Cada
variante foi isolada e o fonte/build privados foram restaurados após os testes.
Nenhuma biblioteca do sistema foi substituída.

| Alteração em `radeonsi/si_state.c` | Borda | Pontos | Conclusão |
| --- | ---: | ---: | --- |
| `PA_CL_CLIP_CNTL.BOUNDARY_EDGE_FLAG_ENA=1` | 0/18 | 0+0 | Não corrige; [AMD descreve o bit como não utilizado](https://www.amd.com/content/dam/amd/en/documents/radeon-tech-docs/programmer-references/SI_3D_registers.pdf). |
| `PA_CL_CLIP_CNTL.PS_UCP_MODE=2` | 0/18 | 0+0 | Expansão de pontos não gera os vértices ausentes do polígono. |
| `PA_CL_CLIP_CNTL.DX_LINEAR_ATTR_CLIP_ENA=0` | 0/18 | 0+0 | Não muda o contorno. |

O [ensaio anterior de `USE_VTX_EDGE_FLAG=0`](coin-render-raster-amd-followup-20261008.md)
também não restaurou a borda. Os [logs do oracle](validation/raster-clip-edge-mesa-fix-20261009)
preservam os resultados, sem caminhos locais de build.

## Protótipo de tratamento por geometry shader

O [reproducer de geometry shader](../testsuite/reproducers/raster-clip-edge/RasterClipBoundaryGsProbe.cpp)
gera os dois pontos de interseção de triângulos ou de um quad completo com o
plano e os emite como `line_strip` ou `points`. O quad é codificado como uma
primitiva `LINES_ADJACENCY` de quatro vértices para o experimento; isso mantém
as quatro arestas externas juntas, mas ainda não é uma conversão geral de
`GL_POLYGON` no Mesa. O shader usa posições em espaço de recorte e interpola
a cor; a variante com shader lê `gl_ClipDistance[0]` diretamente. O caminho de
plano fixo usa uma constante equivalente para o caso ortográfico do oracle.
`GL_PROGRAM_POINT_SIZE` é necessário quando o geometry shader escreve
`gl_PointSize`.

No Mesa privado com radeonsi Renoir, o protótipo passou nas oito combinações:

| Entrada | Plano | `line_strip`, coluna x16 | `points`, extremidades x16 | Ponto interno |
| --- | --- | ---: | ---: | ---: |
| Dois triângulos | Fixo ou `gl_ClipDistance[0]` | 18/18 | 4+4 | 6 pixels |
| Quad inteiro | Fixo ou `gl_ClipDistance[0]` | 18/18 | 4+4 | 0 pixels |

O mesmo resultado ocorreu no llvmpipe. No caso triangulado, a diagonal interna
também gera um ponto na interseção com o plano; o quad inteiro não o gera. A medição demonstra
que o geometry shader da GPU consegue rasterizar as interseções; ela
**não** corrige o Mesa nem é equivalente ao contorno completo de `GL_POLYGON`.
O [oracle estrito sem o shader suplementar](../testsuite/reproducers/raster-clip-edge/RasterClipEdgeGlOracle.cpp)
continua falhando no radeonsi.
[Saída do protótipo na AMD](validation/raster-clip-edge-mesa-fix-20261009/gs-probe-amd.log).

## Integração experimental no Mesa privado

Um [patch experimental do state tracker](validation/raster-clip-edge-mesa-fix-20261009/mesa-gs-quad-line-experiment.patch)
liga um shader NIR suplementar após o draw original somente quando a variável
`MESA_EXPERIMENTAL_CLIP_BOUNDARY` está presente. No caso de `GL_POLYGON` com
quatro vértices em modo `LINE` e um plano ativo, o segundo draw apresenta os
quatro vértices como `LINES_ADJACENCY` e emite só a borda criada pelo recorte.
O Mesa converte o plano fixo em `gl_ClipDistance` para essa tentativa. O patch
interpola apenas `COL0` e não foi preparado para uso geral.

Com a saída NIR de `gl_ClipDistance` reduzida a **um componente**, o caso
isolado `GL_POLYGON`/`LINE` passou no radeonsi: **18/18** pixels da borda,
sem erro GL. A primeira versão exportava quatro componentes para um array de
tamanho um e perdeu o contexto GPU. Na execução do oracle completo, a variante
suplementar para `GL_TRIANGLES` também causou timeout e reset de GPU; sua causa
exata ainda não foi demonstrada. A versão preservada do patch impede a
submissão das variantes de triângulo e ponto. A compilação dessa versão passou,
mas o display X desapareceu após o segundo reset, impedindo uma nova medição
de hardware. Nenhum driver instalado foi modificado.
[Registro da tentativa](validation/raster-clip-edge-mesa-fix-20261009/mesa-gs-draw-experiment.log).

Após salvar o patch, os três fontes alterados foram restaurados byte a byte
da tarball Mesa 25.2.8 e a biblioteca privada foi recompilada. O display X
voltou e `glxinfo` confirmou o renderer AMD; não houve novo ensaio do patch
restrito na GPU após essa recuperação. O oracle estrito na biblioteca privada
restaurada voltou ao baseline: `GL_POLYGON`/`LINE` 0/18 e exit 1.

O build restrito passou o oracle e as oito combinações do protótipo com
softpipe num Xvfb isolado. O softpipe já passava sem o segundo draw; portanto,
esse controle confirma a compilação e execução da integração, não que ela
fecha o defeito no radeonsi.

Para integrar o tratamento no Mesa, o shader gerado deve receber todos os
varyings do estágio anterior, interpolar atributos `smooth` e `noperspective`
segundo as respectivas regras, conservar `flat` no vértice provocador, e
transportar tamanho do ponto e identificadores. Também precisa lidar com os
seis planos do frustum, até oito planos do usuário e `gl_ClipDistance`, faces,
flags de aresta, culling, depth/stencil, stipple, blend, transform feedback e
programas que já usam geometry/tessellation shaders. Também deve preservar a
identidade das arestas externas antes da decomposição de `GL_POLYGON`, para
não introduzir vértices na diagonal interna. O protótipo cobre apenas um
plano, triângulos, cor simples e parte dos estados usados pelo oracle. Não deve ser
promovido como patch do driver.

## Reprodução da transição de estado — 2026-10-09

A sessão `DISPLAY=:0` voltou a oferecer renderização direta na AMD. O patch
restrito foi recompilado no Mesa privado; `strace openat` confirmou o carregamento
da biblioteca do build. O oracle completo com a flag experimental voltou a
provocar `ring gfx timeout` e reset. Como a saída padrão estava em buffer,
esse ensaio completo não identificava qual draw ficava preso.

Um executável temporário com um único `GL_POLYGON`/`GL_LINE` mostrou que o
shader suplementar realmente é selecionado (`mode=9`, quatro vértices, plano
ativo). Tanto com NGG padrão quanto com `AMD_DEBUG=nongg`, ele terminou com
**18/18**, erro GL zero e saída 0. Num segundo executável temporário, o mesmo
draw foi seguido por `GL_TRIANGLES`/`GL_LINE`. Com `AMD_DEBUG=nongg`, o
primeiro draw passou; o segundo chegou como `mode=4`, seis vértices e **não**
selecionou o shader suplementar. Seu `glFinish` aguardou cerca de 11 segundos;
o kernel registrou `ring gfx timeout` e `GPU reset(2)`. Assim, desativar NGG
não impede o reset da sequência e o shader de triângulos do protótipo não é
necessário para provocá-lo.

Um controle manteve `lower_ucp` ativo e desativou somente o draw suplementar.
Os dois draws terminaram sem novo reset e retornaram ao baseline 0/18 e 1/18.
O gatilho, portanto, está no draw suplementar ou na transição de estado após
ele. A causa interna exata do radeonsi ainda não foi demonstrada. A primeira
medição `nongg` desta rodada, que deu 0/18, havia omitido por engano
`MESA_EXPERIMENTAL_CLIP_BOUNDARY`; ela não é uma comparação válida do patch.

[Registro dos ensaios e resets](validation/raster-clip-edge-mesa-fix-20261009/mesa-gs-state-transition-20261009.log).
Os três fontes e a biblioteca Mesa privados foram restaurados byte a byte;
os hashes SHA256 coincidem com os backups. O driver instalado permaneceu
intacto.

## Vínculo do GS e barreira VGT — 2026-10-09

Com logs temporários em `si_bind_gs_shader` e apenas o primeiro draw, o radeonsi
recebeu a sequência `NULL -> GS -> NULL`. Ambas as mudanças executaram o corpo
da função; `ngg=0` e `current_rast_prim=3` (linha) após a desvinculação. Esse
valor é esperado até o draw seguinte, porque `si_update_rasterized_prim` deixa
a primitiva sob controle de `draw_vbo` quando não há GS, e `si_draw_vbo` a
recalcula antes de atualizar os shaders. O draw único manteve **18/18** e erro
GL zero. Portanto, não há evidência de que a restauração do CSO tenha omitido
a chamada ao driver.

Foi testada uma barreira `SI_BARRIER_EVENT_VGT_FLUSH` ao desativar o GS, com
`AMD_DEBUG=nongg`, no Mesa privado. O segundo `glFinish` retornou, mas a
leitura seguinte só terminou depois de `ring gfx timeout` e reset do amdgpu
às 19:29:28. O resultado do segundo caso permaneceu `0/18` para a borda e
o executável saiu com código 1. Logo, o flush isolado **não** resolve o reset
nem fecha o oracle. O tempo da leitura mostra que o retorno de `glFinish`
sozinho não era prova de recuperação.

Os quatro fontes e a biblioteca privados foram restaurados byte a byte; os
cinco hashes SHA256 coincidem com os backups. `glxinfo -B` confirmou novamente
renderização direta na AMD com o driver instalado Mesa 25.2.8. Não houve
alteração do driver instalado.

[Log da vinculação e do teste VGT](validation/raster-clip-edge-mesa-fix-20261009/mesa-gs-bind-vgt-flush-20261009.log).

## Aplicação da pesquisa Bateia 0125 — 2026-10-09

A pesquisa separou o defeito original de contorno do reset introduzido pelo
GS suplementar. O Bateia leu `../build/compile_commands.json` e preservou
hashes dos inputs, mas as ASTs selecionadas ultrapassaram 32 MiB; sua análise
C/C++ terminou `not_evaluated`. As conclusões abaixo foram verificadas por
leitura do Mesa 25.2.8, não atribuídas ao analisador automático. O relatório
completo e a bancada rastreável estão no projeto Bateia, sob
`00_nucleo/relatorios/pesquisa-mesa-0125.md` e
`lab/bancada/pesquisa/0125/`.

No caminho de software, `draw_pipe_clip.c` transporta as flags de aresta da
primitiva, marca como visível a borda criada por um **plano do usuário** e
passa a geometria ao estágio `draw_pipe_unfilled.c`. Este só emite a linha ou
o ponto quando a flag da primitiva e a do vértice permitem. O mesmo clipper
suprime a nova edge flag nos planos do frustum. Portanto, os casos do oracle
com plano do usuário e `gl_ClipDistance` não devem ser extrapolados para os
seis planos do frustum. `st_atom_rasterizer.c` e `radeonsi/si_state.c` mostram
a tradução do modo LINE/POINT até os bits `POLY_MODE`/`PTYPE`, mas isso não
prova que o hardware produz as novas interseções e flags. O controle seguinte
é capturar **em CPU** os vértices e as duas classes de flags entre clipping e
unfilled, para plano do usuário e frustum, com o mesmo quad.

Para o reset, a leitura de `si_bind_gs_shader` mostrou que GS→NULL marca o
estágio GS como dirty e chama `si_shader_change_notify`, que muda chaves e
bases de userdata. Este último não marca por si só o VS/TES como dirty. Em
`si_update_shaders`, o ramo sem GS limpa o estado GS, mas só substitui o copy
shader legado pelo VS/TES normal se o estágio anterior estiver dirty. Assim,
uma transição sem outra invalidação pode deixar o VS enfileirado apontando
para o copy shader. Isso é uma **hipótese de estado CPU**, ainda não uma
medição do PM4 emitido ou prova da causa do reset. O controle discriminante
é comparar, sem submissão à GPU, o contexto inicial sem GS com GS→NULL:
`dirty_shaders_mask`, variante/PM4 VS, `VGT_GS_MODE`,
`VGT_SHADER_STAGES_EN` e bases de userdata devem coincidir antes do draw.

Um [patch candidato](validation/raster-clip-edge-mesa-fix-20261009/mesa-gs-unbind-dirty-stage-candidate.patch)
marca VS ou TES como dirty ao desativar GS. O controle em `drm-shim` abaixo
mostrou que ele restaura a variante VS enfileirada e `VGT_GS_MODE` em CPU.
Não houve execução física ou leitura de pixels com este patch. A correção de
propagação para shaders combinados, citada no Mesa 26.0, já consta das notas
do Mesa 25.2.6 e do fonte 25.2.8; não é uma correção posterior ausente deste
baseline.

SwiftShader e ANGLE forneceram referências de organização para clipping e
restauração de estado, respectivamente. São implementações com contratos
diferentes; seus achados não validam o comportamento OpenGL do radeonsi.

## Controles em CPU: clipping e transição GS — 2026-10-09

Foi compilado um `drm-shim` privado do Mesa 25.2.8. Ele fornece um render
node simulado (`DRM 3.49`) e descarta o ioctl de submissão, sem executar
comandos na GPU. A [fixture EGL](validation/raster-clip-edge-mesa-fix-20261009/mesa-gs-drm-shim-fixture.cpp)
executou um polígono em `GL_LINE` com o GS suplementar experimental, seguido
de triângulos em `GL_LINE` depois de GS→NULL, com `AMD_DEBUG=nongg`.
Nos dois casos `glGetError()` foi zero; esse resultado não valida pixels.

- [Baseline sem a correção](validation/raster-clip-edge-mesa-fix-20261009/mesa-gs-drm-shim-baseline-20261009.log): após GS→NULL, `dirty=0x8` no bind e `dirty=0x28` na atualização; o VS enfileirado continuou sendo a variante da etapa GS e `vgt_gs_mode=0x300033`.
- [Com a correção candidata](validation/raster-clip-edge-mesa-fix-20261009/mesa-gs-drm-shim-candidate-20261009.log): `dirty=0x9` no bind e `dirty=0x29` na atualização; o VS normal foi enfileirado e `vgt_gs_mode=0x0`.

Assim, a invalidação faltante é um defeito de estado CPU reproduzido, e o
patch candidato corrige **essa** divergência. O controle seguinte compara o
PM4 e testa fisicamente a sequência que antes provocava reset.

Com o mesmo Mesa privado, a [fixture softpipe](validation/raster-clip-edge-mesa-fix-20261009/mesa-softpipe-clip-fixture.cpp)
executou o quad em `GL_LINE` sob `EGL_PLATFORM=surfaceless`, uma vez com
`glClipPlane` e outra com clipping do frustum. O
[patch de instrumentação temporária](validation/raster-clip-edge-mesa-fix-20261009/mesa-softpipe-clip-instrument.patch)
registrou `draw_pipe_clip.c` e `draw_pipe_unfilled.c`:

- [Plano do usuário](validation/raster-clip-edge-mesa-fix-20261009/mesa-softpipe-user-clip-20261009.log): `plane=6 user=1`; os novos vértices em `x=0` chegaram com `edgeflag=1` e máscara de aresta ativa, formando a borda vertical.
- [Frustum](validation/raster-clip-edge-mesa-fix-20261009/mesa-softpipe-frustum-clip-20261009.log): `plane=1 user=0`; a interseção em `x=-1` chegou com `edgeflag=0`, e a borda vertical foi suprimida pelo estágio `unfilled`.

Essas medições confirmam o contrato de flags do caminho de software. Elas
não provam que o radeonsi implementa a mesma geometria no caminho acelerado.
Todos os sete fontes Mesa privados usados nos controles e
`libgallium-25.2.8.so` foram restaurados byte a byte a partir dos backups;
oito comparações retornaram `MATCH`. O driver instalado não foi modificado.

## PM4 e ensaio físico do patch candidato — 2026-10-09

O Mesa privado foi instrumentado com
[`si_print_current_ib`](validation/raster-clip-edge-mesa-fix-20261009/mesa-gs-pm4-instrument.patch)
e executado no `drm-shim` com a
[fixture sem `glFlush` intermediário](validation/raster-clip-edge-mesa-fix-20261009/mesa-gs-pm4-fixture.cpp).
Os três draws no mesmo command stream foram: polígono original sem GS,
contorno suplementar com GS, e triângulos após GS→NULL. As capturas completas
sem códigos ANSI estão em
[baseline](validation/raster-clip-edge-mesa-fix-20261009/mesa-gs-pm4-baseline-20261009.log)
e [candidato](validation/raster-clip-edge-mesa-fix-20261009/mesa-gs-pm4-candidate-20261009.log).
Os números de endereço do programa VS variam entre processos; a diferença
relevante é a emissão de um novo programa após a transição.

| PM4 após o terceiro draw | Sem correção | Com correção |
| --- | --- | --- |
| Último `VGT_GS_MODE` | `0x00300033` (modo GS, sem escrita de retorno) | `0x00000000` (escrita explícita) |
| Último `VGT_SHADER_STAGES_EN` | `0x00010000` (GS desligado) | `0x00010000` (GS desligado) |
| Programa VS | Permanece o copy shader do draw com GS | Novo VS normal é vinculado |

O baseline, portanto, desligava GS em `VGT_SHADER_STAGES_EN`, mas conservava
`VGT_GS_MODE` e o programa VS da etapa anterior. O patch candidato remove
essa inconsistência no PM4. As bases de userdata não foram isoladas como
causa independente nesta fixture; a captura completa permite inspecioná-las.

A biblioteca candidata foi recompilada **sem a instrumentação PM4** e
carregada por GLX com renderização direta na AMD Renoir (`DRM 3.64`,
`AMD_DEBUG=nongg`). O
[repro físico de dois draws](validation/raster-clip-edge-mesa-fix-20261009/mesa-gs-gpu-two-draws-fixture.cpp)
terminou sem travar: o polígono `GL_LINE` teve **18/18** pixels na borda e o
triângulo seguinte terminou com erro GL zero, embora sem a borda esperada.
[Saída](validation/raster-clip-edge-mesa-fix-20261009/mesa-gs-gpu-candidate-two-draws-20261009.log).

O [oracle estrito completo](validation/raster-clip-edge-mesa-fix-20261009/mesa-gs-gpu-candidate-oracle-20261009.log)
também terminou sem timeout. Saiu com código 1 porque ainda falham
`triangles-line`, `polygon-point` e os casos `gl_ClipDistance` de linha e
ponto. No intervalo desses dois ensaios com `AMD_DEBUG=nongg`, o
[journal do kernel](validation/raster-clip-edge-mesa-fix-20261009/mesa-gs-gpu-candidate-kernel-20261009.log)
não registrou eventos.

A mesma sequência foi repetida com `AMD_DEBUG` desativado: o
[repro curto](validation/raster-clip-edge-mesa-fix-20261009/mesa-gs-gpu-candidate-default-two-draws-20261009.log)
e o [oracle completo](validation/raster-clip-edge-mesa-fix-20261009/mesa-gs-gpu-candidate-default-oracle-20261009.log)
terminaram com os mesmos resultados e sem timeout. O
[journal desse intervalo](validation/raster-clip-edge-mesa-fix-20261009/mesa-gs-gpu-candidate-default-kernel-20261009.log)
também não registrou eventos. `glxinfo -B` depois de cada par confirmou
renderização direta na AMD. Isso valida a ausência de reset **nessas quatro
execuções**, não uma prova de estabilidade geral ou da correção completa do
clipping.

A instrumentação temporária foi retirada. Os três fontes do state tracker
coincidem com o patch GS experimental aplicado uma vez, `si_state_shaders.cpp`
coincide com o patch candidato, e `si_state_draw.cpp` coincide com o baseline.
A biblioteca privada foi recompilada nesse estado; o driver instalado não foi
alterado.

## GS suplementar para `GL_TRIANGLES`/`LINE` — 2026-10-09

Com a correção candidata de GS→NULL já validada no PM4, reativei apenas o
segundo draw dos seis vértices do oracle (`MESA_PRIM_TRIANGLES`, `count=6`,
`GL_LINE`, um plano de recorte). O
[patch incremental](validation/raster-clip-edge-mesa-fix-20261009/mesa-gs-triangle-line-after-transition.patch)
se aplica sobre o patch experimental de quad; os dois dependem também do
[patch de transição GS→NULL](validation/raster-clip-edge-mesa-fix-20261009/mesa-gs-unbind-dirty-stage-candidate.patch)
para este ensaio. A variável `MESA_EXPERIMENTAL_CLIP_BOUNDARY=1` continua
obrigatória. Não há mudança no driver instalado.

No `drm-shim`, uma instrumentação temporária confirmou os dois draws
suplementares: `POLYGON` com quatro vértices e `TRIANGLES` com seis; ambos
terminaram com erro GL zero. Retirei a instrumentação e executei na AMD
Renoir física, com renderização direta e a biblioteca Mesa privada. O caso
mínimo estrito passou em ambos os caminhos (`AMD_DEBUG=nongg` e configuração
padrão): **18/18** pixels na borda para `polygon-line` e `triangles-line`,
sem erro GL. O controle llvmpipe anterior também tinha `x17=1` para
`triangles-line`; esse pixel não foi criado pelo patch.

O [registro completo](validation/raster-clip-edge-mesa-fix-20261009/mesa-gs-triangle-line-20261009.log)
contém o resultado do shim e dos quatro ensaios físicos. O oracle estrito
completo terminou sem timeout nos dois caminhos, porém com código 1:
`polygon-point` segue sem os pontos novos; `gl_ClipDistance` continua sem a
borda em `LINE` e sem os pontos em `POINT`. Os casos de preenchimento e os
controles passaram. No intervalo dos quatro ensaios, o journal filtrado não
apresentou reset, timeout de ring ou fault de GPU; `glxinfo -B` ao final
confirmou a renderização direta com o Mesa instalado. Essa evidência cobre
somente as quatro execuções e as geometrias exercitadas.

O código fonte e `libgallium-25.2.8.so` privados foram restaurados byte a
byte ao estado anterior ao experimento. Esse estado mantém o GS restrito a
quad e o patch GS→NULL, conforme a seção anterior. O patch incremental fica
como artefato de estudo; não deve ser integrado sem ampliar a semântica e a
cobertura de testes.

## Pontos e ligação de `gl_ClipDistance` — 2026-10-09

A variante experimental de `GL_POLYGON`/`POINT` foi liberada apenas para o
polígono de quatro vértices do oracle. O
[patch isolado](validation/raster-clip-edge-mesa-fix-20261009/mesa-gs-quad-point-after-transition.patch)
passou no `drm-shim` e na AMD física: os dois pontos da borda recortada
produziram **4+4** pixels, sem erro GL. O oracle completo, nessa variante,
terminou com código 1 porque ainda não incluía o patch de triângulos nem
resolvia os shaders do aplicativo.

O [patch combinado](validation/raster-clip-edge-mesa-fix-20261009/mesa-gs-line-point-combined-after-transition.patch)
habilita `POLYGON/LINE`, `TRIANGLES/LINE` com seis vértices e
`POLYGON/POINT` com quatro vértices. No Mesa privado com a correção GS→NULL,
o shim terminou com erro GL zero. Na AMD física, o caso mínimo passou para os
três desenhos com `AMD_DEBUG=nongg` e com a configuração padrão. O oracle
estrito completo passou todos os casos de geometria fixa nesses dois modos,
mas terminou com código 1: os únicos FAIL são
`shader-clip-distance-polygon-line` e
`shader-clip-distance-polygon-point`.
[Saídas e condições](validation/raster-clip-edge-mesa-fix-20261009/mesa-gs-point-combined-20261009.log).
O journal filtrado não registrou reset, timeout de ring ou fault, e a sessão
AMD continuou com renderização direta. Esses resultados valem para as
execuções e geometrias testadas; a variante continua experimental.

Um log temporário confirmou que o segundo draw com GS **é selecionado** nos
dois casos `gl_ClipDistance`. O
[NIR capturado](validation/raster-clip-edge-mesa-fix-20261009/mesa-gs-clipdistance-nir-20261009.log)
mostra que o vertex shader do aplicativo foi ligado com
`next_stage=MESA_SHADER_FRAGMENT` e sua escrita em `CLIP_DIST0` tem
`no_varying`; o GS suplementar, inserido depois da ligação pelo state
tracker, tenta ler `VARYING_SLOT_CLIP_DIST0`. O Mesa usa essa escrita para o
recorte de função fixa, mas ela não foi preparada como varying para um GS
posterior. Isso explica o limite observado do protótipo. Uma solução geral
precisa preservar/recompilar a saída do vertex shader para o novo estágio ou
integrar a geometria suplementar antes da otimização dos varyings; reconstruir
a distância a partir de `gl_Position` não preservaria `gl_ClipDistance`
arbitrário de um shader do aplicativo.

No CoinGL, a cena escolhe os estados e envia chamadas OpenGL: por exemplo,
`SoGLDrawStyleElement` chama `glPolygonMode`, `SoGLClipPlaneElement` chama
`glClipPlane`, e `SoFaceSet` envia primitivas com `glBegin`. A implementação
OpenGL do contexto atual executa essas chamadas. Nesta AMD Linux, ela é o
Mesa, cujo state tracker encaminha os draws ao Gallium/radeonsi. O tratamento
restrito no `SoIndexedFaceSet` consulta o renderer e complementa a geometria
apenas no perfil afetado. O Mesa privado deste estudo fica fora do Coin e do
driver instalado.

A instrumentação foi retirada e os arquivos privados `st_draw.c` e
`libgallium-25.2.8.so` foram restaurados byte a byte ao estado anterior.

## `CLIP_DIST0` preservado e oracle estrito completo — 2026-10-09

Como controle da hipótese acima, executei o patch combinado com
`MESA_GLSL_DISABLE_IO_OPT=1` apenas no processo do oracle. Na AMD física, o
oracle estrito completo terminou com **código 0**, incluindo os dois casos
`gl_ClipDistance`. Isso confirmou que a otimização de varyings na ligação do
programa era o obstáculo imediato do protótipo.

Em seguida, apliquei um
[patch NIR restrito](validation/raster-clip-edge-mesa-fix-20261009/mesa-gs-clipdist-varying-after-link.patch)
ao Mesa privado: quando `MESA_EXPERIMENTAL_CLIP_BOUNDARY=1`, há vertex shader
e não há geometry shader do aplicativo, ele mantém `CLIP_DIST0` como varying
após a otimização. As demais otimizações de IO permanecem ligadas. O
[NIR do shim com o patch](validation/raster-clip-edge-mesa-fix-20261009/mesa-gs-clipdistance-nir-preserved-20261009.log)
mostra a escrita de `VARYING_SLOT_CLIP_DIST0` sem `no_varying`, e o shader do
aplicativo ligou e desenhou sem erro GL.

Com os patches combinado, GS→NULL e NIR restrito, o
[oracle estrito completo](validation/raster-clip-edge-mesa-fix-20261009/mesa-gs-full-oracle-targeted-20261009.log)
passou na AMD Renoir física **duas vezes**, com `AMD_DEBUG=nongg` e com a
configuração padrão. `MESA_GLSL_DISABLE_IO_OPT` ficou desativado nesses dois
ensaios, e o cache de shader foi desligado para forçar a compilação da
variante candidata. Todos os desenhos passaram, inclusive `POLYGON/POINT` e
`gl_ClipDistance` em linha e ponto. O journal filtrado não apresentou reset,
timeout de ring ou fault; depois, o Mesa instalado permaneceu com renderização
direta na AMD.

Esse resultado fecha **o oracle isolado**, não a correção geral do Mesa. O GS
suplementar só cobre as geometrias e o plano da fixture e interpola apenas
posição/cor. O patch NIR exporta `CLIP_DIST0` para programas elegíveis sempre
que a variável experimental está ativa, mesmo que um draw específico não
use o segundo GS. Ainda faltam semântica de atributos, múltiplos planos,
edge flags, culling, depth, stipple, transform feedback, shaders com estágios
adicionais e regressões de desempenho. O gate do Coin com seu tratamento
restrito não foi substituído por esse Mesa privado.

Depois do ensaio, `st_draw.c`, `gl_nir_linker.c` e
`libgallium-25.2.8.so` privados voltaram byte a byte ao estado anterior; os
objetos foram recompilados dos fontes restaurados. O driver instalado não foi
alterado.

## Trabalho restante

Uma correção precisa gerar a geometria do polígono **depois** do clipping e
antes de aplicar `LINE` ou `POINT`, preservando flags de aresta, atributos
interpolados, face/culling, depth, stipple e shaders do aplicativo. O
`draw_pipe_clip.c` do Mesa já demonstra a semântica de interpolação e flags
na trilha de software; integrá-la ao caminho acelerado do radeonsi ou criar
uma variante de geometry shader requer desenho e testes próprios. Alterar
apenas os três bits acima não fecha o oracle. O candidato privado desta
seção passa a fixture OpenGL isolada, mas ainda precisa de integração geral e
de validação com a biblioteca CoinGL original. O tratamento restrito no Coin
continua necessário com o Mesa instalado neste computador.

Para repetir o oracle após compilar o Mesa privado:

```sh
c++ RasterClipEdgeGlOracle.cpp -o raster-clip-edge -lGL -lglut
LD_LIBRARY_PATH="$MESA_BUILD_DRI:$MESA_PRIVATE_LIB" \
  LIBGL_DRIVERS_PATH="$MESA_BUILD_DRI" \
  ./raster-clip-edge --strict-boundary
```

`$MESA_BUILD_DRI` deve conter `libgallium-25.2.8.so` e um link
`radeonsi_dri.so` para ele; o loader deve ser verificado com `strace`.
