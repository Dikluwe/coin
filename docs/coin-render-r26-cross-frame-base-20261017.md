# R26 — textura base reutilizada entre frames wgpu

O executor bump opaco mantinha a mesma imagem base na GPU apenas dentro de um
frame. Agora cada dispositivo conserva a textura RGBA8 opaca e o sampler entre
submissões de janela e offscreen. A chave inclui dimensão, formato, digest e
filtros/wrap; uma comparação exata dos bytes impede que um digest repetido
reutilize conteúdo alterado. Entradas sem uso após oito seriais são retiradas;
recursos substituídos ficam retidos até o serial GPU concluído.

O teste direto `bump_abi_payload_renders_with_dedicated_shader` passou em
NVIDIA/Vulkan, AMD/Vulkan e AMD/OpenGL. Em cada célula, duas submissões com
a mesma imagem produziram um upload e um hit; a terceira, com bytes alterados
e digest inalterado, elevou o total para dois uploads. A instrumentação final
imprimiu `bump_base_cross_frame_cache uploads=2 hits=1`. Os pixels existentes
de base, mipmap e dois draws continuaram passando.

Após rebuild release, `CoinRenderBumpWindowTest` passou nas três células:
RGB `[204,51,25]` sem base, `[102,26,13]` com base, resize para 320×192 e
recuperação após rejeição de alpha, serial final 4. `CoinRenderBumpRejectionTest
--gpu` passou 11/11 nas três células, com adaptador GPU identificado e pixels
offscreen `[204,51,25,255]` e `[102,26,13,255]`. A suíte Rust completa passou
48/48 com AMD/Vulkan e `--test-threads=1`. Uma execução sem backend fixado,
em paralelo, teve `BadAccess` no EGL de um teste de sombras; a repetição
serial com Vulkan passou. `git diff --check` passou.

Isto verifica reuso e correção, sem medir ganho de tempo de frame. B10 conserva
essa medição; composição, outras texturas e demais cruzamentos de R26 continuam
abertos.
