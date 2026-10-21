# B10 — reuso dos pipelines R26

O encoder de bump criava os pipelines difuso, base e especular em cada frame.
Agora o dispositivo mantém um conjunto por formato de saída e função de
comparação de depth. O mesmo conjunto atende draws sem textura base, com
textura e com mipmaps; o teste GPU inspeciona o cache e exige uma só entrada.

O teste ignorado `bump_abi_payload_renders_with_dedicated_shader` executou
32 encodings com cache e 32 sem cache, em blocos alternados.
Uma segunda série de 32+32 inclui `queue.submit` e `device.poll(Wait)`, ainda
sem readback ou apresentação. Ambos os caminhos empacotam o mesmo triângulo,
estado, mapa normal, uniforms, bind groups e command buffer. A mediana
é o elemento central superior dos 32 tempos em milissegundos, medidos por
`Instant` no Rust de teste. A saída visual do encoder foi conferida antes
do A/B, incluindo textura base e mipmaps.

| GPU/API | Encoding sem/com cache | Encode + submit + wait sem/com cache |
| --- | ---: | ---: |
| AMD/Vulkan | 14,482019 / 0,487026 ms | 15,160972 / 0,799376 ms |
| NVIDIA/Vulkan | 14,882132 / 0,487847 ms | 15,370601 / 0,690674 ms |
| AMD/OpenGL | 15,027904 / 0,491213 ms | 16,241441 / 0,844230 ms |

[AMD/Vulkan](validation/coin-render-b10-bump-pipeline-cache-20261017/amd-vulkan.log),
[NVIDIA/Vulkan](validation/coin-render-b10-bump-pipeline-cache-20261017/nvidia-vulkan.log),
[AMD/OpenGL](validation/coin-render-b10-bump-pipeline-cache-20261017/amd-opengl.log).

Este A/B mede o fixture no build de teste. A segunda série inclui a espera
pela conclusão da GPU, mas não isola o tempo GPU. Ele não mede a action,
readback público, apresentação, p95, throughput nem a cena cidade B10. Os controles
públicos de janela/resize e offscreen/B12 passaram após o cache nas três
células com os mesmos pixels e seriais. A qualificação de desempenho geral
de B10 continua aberta.
