# Views base/mips armazenadas no BGFX/OpenGL — 2026-10-08

## Mudança

O conector OpenGL do BGFX aplica o intervalo de mips ao objeto GL. Quando uma
imagem armazenada aparece no mesmo plano com samplers base-only e mipmapped, o
lowering conserva a textura completa para o sampler com mips e cria uma segunda
textura apenas com o nível 0 para o sampler base. Os draws apontam para slots
distintos; filtros e cores escolhidos pelo autor não mudam. A textura adicional
participa do cache e do orçamento existente de metadados. Isso cobre também
quadros de produtor RTT que consomem imagens armazenadas.

Um token de produtor RTT direto não contém pixels CPU para criar esse segundo
objeto. A combinação base/mips sobre o mesmo token continua recusada na admissão
antes de alocação, submissão ou publicação. Redução de NPOT RTT direto por área
no BGFX também continua pendente.

## Controles locais

Binário `CoinRenderAdvancedTextureTest` do build BGFX deste estudo. O controle
numérico combina duas unidades que referenciam a mesma imagem com filtros
`NEAREST` e `LINEAR_MIPMAP_LINEAR`, esperando RGB `(0, 0, 128)` no centro.

| Executor | Checagens | Resultado | Erro RGB máximo do caso compartilhado |
|---|---:|---|---:|
| Core CPU | 198 | PASS | 0 |
| BGFX OpenGL nativo | 384 | PASS | 0 |
| BGFX OpenGL portátil | 384 | PASS | 0 |
| BGFX Vulkan nativo | 380 | PASS | 0 |

O OpenGL reportado pelo BGFX não expõe vendor/device (`0000:0000`); `glxinfo -B`
no mesmo ambiente identifica AMD Radeon Graphics, driver radeonsi Renoir. O
Vulkan reporta AMD `1002:1638`. No OpenGL, dois replays do mesmo plano e revisão
também deram erro máximo zero. Os logs completos e a identidade GL estão em
[`validation/advanced-textures-mixed-views-20261008`](validation/advanced-textures-mixed-views-20261008).

Estes controles qualificam Linux/AMD deste PC. Não certificam NVIDIA, Windows,
Android nem alias de token RTT direto.
