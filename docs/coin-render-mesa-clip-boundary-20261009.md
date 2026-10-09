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

## Trabalho restante

Uma correção precisa gerar a geometria do polígono **depois** do clipping e
antes de aplicar `LINE` ou `POINT`, preservando flags de aresta, atributos
interpolados, face/culling, depth, stipple e shaders do aplicativo. O
`draw_pipe_clip.c` do Mesa já demonstra a semântica de interpolação e flags
na trilha de software; integrá-la ao caminho acelerado do radeonsi ou criar
uma variante de geometry shader requer desenho e testes próprios. Alterar
apenas os três bits acima não fecha o oracle. O workaround restrito no Coin
continua necessário para o gate local até haver um patch Mesa que faça o
oracle estrito passar também com a biblioteca CoinGL original.

Para repetir o oracle após compilar o Mesa privado:

```sh
c++ RasterClipEdgeGlOracle.cpp -o raster-clip-edge -lGL -lglut
LD_LIBRARY_PATH="$MESA_BUILD_DRI:$MESA_PRIVATE_LIB" \
  LIBGL_DRIVERS_PATH="$MESA_BUILD_DRI" \
  ./raster-clip-edge --strict-boundary
```

`$MESA_BUILD_DRI` deve conter `libgallium-25.2.8.so` e um link
`radeonsi_dri.so` para ele; o loader deve ser verificado com `strace`.
