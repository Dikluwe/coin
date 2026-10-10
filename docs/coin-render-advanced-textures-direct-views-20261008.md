# Views base/mips de tokens RTT diretos no BGFX/OpenGL — 2026-10-08

Quando o mesmo token RTT direto é usado com filtro base-only e mipmapped no
OpenGL, o plano cria um objeto de nível base com `BGFX_TEXTURE_BLIT_DST`. Uma
cópia `bgfx::blit` do nível 0 original ocorre no primeiro view, antes de
qualquer draw desse view. O sampler com mips continua usando o token original.
Não há readback nem pixels CPU intermediários. A reserva conservadora de base
do orçamento RTT de 64 MiB já cobre esta cópia. Formatos que não podem criar o
destino de blit são recusados na admissão do adaptador; produtor ainda não
executado é identificado pelo `producerId` nessa admissão.

O controle usa um produtor RTT 4×4 com padrão vermelho/azul e um consumidor
com duas unidades (`NEAREST` e `LINEAR_MIPMAP_LINEAR`) sobre o mesmo token. O
pixel central esperado é RGB `(0, 0, 128)`; o erro máximo foi zero no
AMD/OpenGL nativo e portátil, inclusive no replay do mesmo plano e token. Um
grafo com dois produtores também usa o token misto dentro do segundo produtor,
com erro máximo zero. O teste completo também passou no CPU e no AMD/Vulkan:

| Executor | Checagens | Resultado |
|---|---:|---|
| Core CPU | 198 | PASS |
| BGFX OpenGL nativo | 395 | PASS |
| BGFX OpenGL portátil | 395 | PASS |
| BGFX Vulkan nativo | 380 | PASS |

[Logs completos](validation/advanced-textures-direct-views-20261008).
O controle direto é específico do BGFX/OpenGL nesta campanha; outras GPUs e
formatos de destino de blit precisam de qualificação. A redução NPOT direta por
área no BGFX permanece pendente.
