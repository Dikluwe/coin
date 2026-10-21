# B10 — A/B de janela nas APIs Linux disponíveis

A cena city de 40 mil objetos foi executada em X11, 512×512, geometria 100%
dinâmica, 10 quadros de aquecimento e 20 medidos por processo. Cada célula
abaixo teve um processo com undo genérico
(`COIN_RENDER_DISABLE_CUBE_RANGE_OVERLAY=1`) e um com o undo compacto.
Os tempos são de CPU para atualização, chamada de render/apresentação e,
quando disponível, readback solicitado. Não são duração GPU nem latência de
scanout. São pares exploratórios de processo único; ainda não estabelecem
cauda estável entre execuções.

| Backend / GPU / API | Mediana genérica → compacta | p95 genérico → compacto | Readback RGBA final |
| --- | ---: | ---: | --- |
| BGFX / AMD / Vulkan | 209,428 → 192,910 ms (−7,89%) | 270,636 → 202,350 ms | Exato |
| BGFX / NVIDIA / Vulkan | 203,134 → 185,363 ms (−8,75%) | 216,392 → 189,675 ms | Exato |
| BGFX / NVIDIA / OpenGL | 202,284 → 183,340 ms (−9,37%) | 207,916 → 188,969 ms | Exato |
| wgpu / AMD / Vulkan | 213,266 → 181,670 ms (−14,82%) | 234,084 → 199,216 ms | Exato |
| wgpu / NVIDIA / Vulkan | 205,916 → 188,768 ms (−8,33%) | 211,159 → 195,851 ms | Exato |
| wgpu / AMD / OpenGL | 198,135 → 177,434 ms (−10,45%) | 205,385 → 181,420 ms | X11 apresentado: RGB exato em par posterior |

Os cinco pares com readback foram iguais byte a byte. A wgpu/OpenGL AMD
recusou inicialmente a solicitação sem vsync porque a superfície oferece
apenas FIFO; os dois processos medidos usaram `--allow-vsync`. Essa célula
não oferece `COPY_SRC` de janela. Um par posterior com a nova opção
`--capture-presented` capturou via X11 um render de diagnóstico do estado
final, depois do intervalo cronometrado. O carimbo de clear RGB (19,167,226)
confirmou frame novo nas duas variantes; os PPMs 512×512 são idênticos por
SHA-256 (`ca13b2be…51e108`), com 28 cores e 48.537 pixels de geometria.
Os digests de estado também são iguais (`0xc2ebbde353ec4f93`). Nesse par,
as medianas de CPU foram 196,318 → 175,973 ms; ele acrescenta observação
visual do resultado apresentado, mas não um readback da API nem prova do
conteúdo de cada quadro cronometrado. [Recibo X11](validation/coin-render-b10-presented-20261017/).
A célula wgpu/NVIDIA/OpenGL segue indisponível.

## Repetição wgpu/OpenGL AMD

Três pares adicionais foram executados em processos independentes, alternando
a ordem genérico/compacto (AB, BA, AB), com os mesmos 10 quadros de aquecimento
e 20 medidos. O diagnóstico X11 foi feito somente após o tempo medido. As seis
capturas tiveram carimbo de frame novo, mesmo digest de estado final e PPM
idêntico (`ca13b2be…51e108`). A mediana das medianas por processo foi
**197,698 → 176,935 ms**; o ganho por par variou de **8,90% a 10,87%**.
O p95 melhorou em cada par, entre **10,19% e 13,85%**. Os máximos ainda
oscilaram; esta amostra sustenta o ganho nesta célula e nesta carga, sem
estabelecer duração GPU ou cauda estável entre drivers e sessões.
[Logs, CSVs, imagem e verificação](validation/coin-render-b10-wgpu-gl-repeat-20261017/verification.json).

## Repetição BGFX/Vulkan AMD

Mais três pares independentes, também em ordem AB/BA/AB, usaram readback
RGBA de janela em cada quadro. As seis imagens finais foram byte a byte
iguais (`SHA-256 428fd618…e164257`), com digest de cena final idêntico.
A mediana das medianas por processo foi **200,467 → 183,474 ms**. O ganho de
mediana por par foi **7,68%, 15,61% e 6,07%**; o p95 caiu **11,38%, 16,98%
e 5,54%**, respectivamente. O segundo par teve dispersão maior no genérico,
e a faixa de ganhos não sustenta um único número estável de latência.
O readback está incluído no tempo desta célula, diferentemente da captura
X11 de diagnóstico da wgpu/OpenGL. [Recibos](validation/coin-render-b10-bgfx-amd-vulkan-repeat-20261017/verification.json).

## Repetição BGFX/Vulkan NVIDIA

Três pares AB/BA/AB adicionais tiveram readback RGBA final idêntico em seis
processos (`SHA-256 45b36df7…9a9e05e5f`). A mediana das medianas por
processo foi **207,901 → 187,642 ms**. A mediana caiu **6,51%, 14,78% e
8,48%** por par; o p95 caiu **3,21%, 14,97% e 5,71%**. O máximo do compacto
foi pior em dois pares (1,74% e 0,57%), portanto o ganho de cauda extrema
não está demonstrado. O readback por quadro integra os tempos, como no par
AMD/Vulkan. [Recibos](validation/coin-render-b10-bgfx-nvidia-vulkan-repeat-20261017/verification.json).

O par BGFX/AMD/OpenGL foi medido separadamente em três processos por
variante: mediana entre processos 209,624 → 190,899 ms, com três readbacks
exatos ([recibo](coin-render-b10-window-camera-20261017.md)). Assim, todas
as sete células Linux executáveis têm uma medição de janela; seis têm
comparação visual nesta campanha; a sétima tem o par X11 acima. Próximas
etapas B10: repetir os pares de processo único nas outras células para
analisar caudas, medir duração GPU e investigar cenas maiores/particionamento.

[Logs, CSVs, imagens comprimidas, hashes e controle FIFO](validation/coin-render-b10-window-matrix-20261017/verification.json).
