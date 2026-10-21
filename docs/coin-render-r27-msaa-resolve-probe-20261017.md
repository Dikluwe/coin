# R27/C18 — sonda física de MSAA e resolve wgpu

Uma sonda isolada de GPU verificou RGBA8 linear com depth32, contagens exatas
1/2/4, resolve para textura single-sample e readback RGBA8 em AMD/Vulkan,
NVIDIA/Vulkan e AMD/OpenGL. O shader desenha um triângulo vermelho sobre clear
preto opaco numa superfície 16×16. A aresta assimétrica separa as posições
de amostra: 1× tem cobertura binária; 2× e 4× produzem pixels intermediários.

| GPU/API | 1× parciais | 2× parciais | 4× parciais | Soma do vermelho 1×/2×/4× |
| --- | ---: | ---: | ---: | --- |
| AMD/Vulkan | 0 | 2 | 34 | 17850 / 17851 / 19390 |
| NVIDIA/Vulkan | 0 | 2 | 34 | 17850 / 17849 / 19382 |
| AMD/OpenGL | 0 | 2 | 34 | 17850 / 17851 / 19390 |

A primeira tentativa de 2× falhou na criação da textura mesmo com o flag de
formato anunciado pelo adaptador: wgpu exige habilitar
`TEXTURE_ADAPTER_SPECIFIC_FORMAT_FEATURES` no `DeviceDescriptor` para 2×.
Após habilitá-lo, a textura, o pipeline, o depth com mesma contagem, o resolve
e o readback passaram. A primeira aresta diagonal não discriminava 2× nesta
amostragem; a fixture final usa inclinação diferente e exige cobertura parcial
em 2×/4×. O alfa resolvido permaneceu 255 em todos os 256 pixels.

[Teste reproduzível](../testsuite/coinwgpu/rust/msaa_resolve_probe.rs): executar
`cargo test --test msaa_resolve_probe -- --ignored --nocapture` com ICD/backend
selecionados. A sonda ainda não altera `CoinRenderTarget`, o plano, a ABI nem
BGFX. R27/C18 continuam **não implementados** no produto: faltam opção tipada
no alvo, consulta por formato/contagem, anexos de janela/offscreen, pipeline
por contagem, resolve após o último passe, política de readback de depth,
RTT/OIT, resize, rollback e testes públicos nas duas Infra.
