# Decisão de promoção do sampling portátil — 2026-10-08

## Decisão

Manter a política `native` como padrão e a API `native`/`portable` nesta branch
de estudo. Ainda não integrar a API na linha de produção `coin-render`.
O caminho portátil funciona no perfil Linux offscreen qualificado, mas os gates
de equivalência CoinGL, cold start e outras plataformas não estão fechados.
Também não promover o workaround Mesa externo nem o shader `fine_uniform`.

## Evidências consideradas

- A [API por alvo](coin-render-sampling-api-linux-20261008.md) passou 170/170
  processos Linux offscreen, Rust 46/46 e C 11. A política é herdada em RTT e
  incompatibilidade é recusada antes da publicação.
- O [replay BGFX de um milhão](coin-render-bgfx-million-replay-20261008.md)
  deixou de repetir lowering/upload em AMD/Vulkan. A comparação de tempo usa
  baseline pré-API e não isola custo marginal da política.
- A [borda de recorte AMD/CoinGL](coin-render-raster-amd-followup-20261008.md)
  segue divergente no gate estrito, enquanto o contrato portátil passou.
- O [cold start FreeCAD](coin-render-freecad-cold-border-study-20261008.md)
  teve 11/12 controles sem aquecimento de texto na matriz anterior, com um
  diff de 1.476 pixels. Quatro controles AMD adicionais passaram; a NVIDIA não
  estava disponível nesta sessão por desencontro de módulo e biblioteca.
- A [sonda de sampling fino](coin-render-fine-sampling-study-20261008.md)
  encontrou custo nativo acima do baseline em cenas texturizadas AMD e variação
  desfavorável NVIDIA no milhão. Não estabelece ganho universal.
- [Views armazenadas BGFX/OpenGL](coin-render-advanced-textures-mixed-views-20261008.md)
  agora passam sem contaminação de LOD; tokens RTT compartilhados e redução
  direta NPOT por área continuam pendentes.

## Condições para nova decisão

1. Repetir cold start no NVIDIA com driver íntegro e localizar a transição da
   borda antes de considerá-la qualificada.
2. Fechar ou delimitar como incompatibilidade contratual a borda de recorte
   CoinGL AMD e as demais diferenças raster do gate estrito, sem elevar a
   tolerância para ocultá-las.
3. Medir custo marginal da API contra a produção na mesma revisão e nos mesmos
   adaptadores; repetir o replay BGFX em mais GPUs e cenas.
4. Qualificar Windows, Android físico, mais consumidores e as rotas FreeCAD
   necessárias ao alvo de produção.
5. Tratar separadamente os recursos P07/P24 ainda recusados: token RTT misto
   BGFX/OpenGL e mip NPOT direto BGFX. Não inferir suporte deles a partir da
   aprovação da política de sampling.

A decisão é sobre promoção da API, não sobre descartar a implementação de
estudo. O próximo gate deve conservar `native` padrão e registrar o adaptador,
a política e o baseline de cada comparação.
