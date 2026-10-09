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
