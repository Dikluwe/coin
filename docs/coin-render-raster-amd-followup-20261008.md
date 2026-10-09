# Raster AMD: borda criada por plano de recorte

Continuação controlada do [estudo de junções](coin-render-raster-junctions-study.md)
na branch `codex/coin-portable-sampling-study`. O gate estrito
`CoinRenderDrawStyleTest` foi repetido em BGFX/Vulkan AMD com CoinGL exigido no
Mesa instalado: exit 1, a diferença histórica em `clip=1,width=1,pattern=65535`,
`x=16,y=10..27`, CoinGL 0 contra Core 255. O mesmo binário, sem a comparação
CoinGL, passou o contrato portátil completo (exit 0). Nenhum gate ou tolerância
foi alterado. [Log estrito](validation/raster-amd-followup-20261008/bgfx-amd-vulkan-styles-strict.log)
e [controle portátil](validation/raster-amd-followup-20261008/bgfx-amd-vulkan-styles-portable.log).
Após acrescentar a sonda opcional, os gates padrão foram repetidos:
[estrito exit 1](validation/raster-amd-followup-20261008/bgfx-amd-vulkan-styles-strict-final.log)
e [portátil exit 0](validation/raster-amd-followup-20261008/bgfx-amd-vulkan-styles-portable-final.log).

Uma opção isolada no próprio teste seleciona só essa célula e informa quantos
dos 18 pixels em `y=10..27` são cobertos por linha CoinGL, preenchimento CoinGL
e linha Core. Essa sonda usa o backend CPU para Core e CoinGL para a referência;
os gates completos acima cobrem BGFX/Vulkan. O deslocamento é em pixels de
janela, aplicado ao plano em
coordenadas do objeto antes de rasterizar. Resultados:

| Deslocamento do plano | Coluna | Linha CoinGL | Preenchimento CoinGL | Linha Core |
| ---: | ---: | ---: | ---: | ---: |
| −0,25 px | 16 | 0 | 18 | 18 |
| 0 px | 16 | 0 | 18 | 18 |
| +0,25 px | 16 | 0 | 0 | 18 |
| +0,25 px | 17 | 0 | 18 | 0 |
| +1 px | 17 | 0 | 18 | 18 |
| 0 px, borda original direita | 45 | 18 | 0 | 18 |

O preenchimento prova que a geometria passa pelo plano. A borda original à
direita prova que CoinGL desenha linhas nessa cena. A borda **criada pelo
recorte** não aparece em CoinGL nos quatro deslocamentos, enquanto Core a
inclui no contorno. No deslocamento +0,25 px há também uma diferença de
quantização entre o preenchimento nativo e Core; ela não explica a ausência
contínua da borda nos outros deslocamentos. Esses controles identificam o
mecanismo visível, sem estabelecer ainda se a escolha vem do CoinGL, do estado
GL que ele emite ou da regra do driver para arestas criadas pelo clipping.

Logs: [−0,25](validation/raster-amd-followup-20261008/clip-probe-minus-quarter.log),
[zero](validation/raster-amd-followup-20261008/clip-probe-zero.log),
[+0,25](validation/raster-amd-followup-20261008/clip-probe-plus-quarter.log),
[+1](validation/raster-amd-followup-20261008/clip-probe-plus-one.log).
Todos os quatro processos da sonda retornam 1 porque o gate CoinGL permanece
ativo; esse retorno é o resultado esperado do estudo, não um PASS reclassificado.
Receita para um deslocamento:

```sh
COIN_RENDER_REQUIRE_GL_REFERENCE=1 COIN_DRAWSTYLE_CLIP_PROBE=1 \
COIN_DRAWSTYLE_CLIP_BIAS_PIXELS=0 \
COIN_BGFX_RENDERER=vulkan \
build-bgfx/bin/CoinRenderDrawStyleTest --probe-clipped-boundary
```

O ambiente usa o ICD AMD/RADV e o GLX Mesa do sistema; os comandos completos
com paths locais e logs ficam em
`/mnt/Laranja/Git/externos/coin-portable-sampling-artifacts/20261008-sampling-api-linux/raster-amd-followup-20261008`.
Próximo controle: isolar `glPolygonMode`/edge flags e a aresta de plano de usuário
em GL sem Coin, depois comparar sentido, larguras e clipping de frustum. Medir
efeito e custo de qualquer alternativa antes de escolher compatibilidade
CoinGL; o contrato portátil atual continua obrigatório.
