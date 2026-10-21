# B10 — undo compacto para Cubes qualificados

A cena city de 40 mil objetos altera 960 mil posições de Cube por quadro.
Antes, o overlay gerava 960 mil entradas novas e guardava 960 mil posições
anteriores para rollback. A rota nova carrega somente um intervalo de 24
vértices e três meias dimensões por Cube. Ela verifica, antes de qualquer
mutação, que cada vértice anterior é finito e tem a mesma magnitude por eixo,
que os intervalos não se sobrepõem, que os limites de atualização são
respeitados e que materiais, modelos e draws são válidos. A aplicação e o
rollback preservam o sinal de cada coordenada, inclusive zero negativo.
Se a prova do intervalo falhar, a action usa a rota genérica anterior.
`COIN_RENDER_DISABLE_CUBE_RANGE_OVERLAY=1` força a rota antiga para A/B.

O teste `CoinRenderFrameReuseCoreTest` comparou a atualização compacta com a
genérica bit a bit, testou o rollback conjunto de geometria/modelo/material/
identidade de draw e recusou intervalos sobrepostos, coordenada assimétrica,
NaN, comprimento errado e draw inválido sem mutação. Passou nos builds BGFX
e wgpu. Na cena city, o trace mostra zero entradas individuais em vez de
960 mil, e a preparação da geometria caiu de ~33–46 ms para ~11–14 ms na
amostra curta AMD/OpenGL.

O A/B intercalou processos independentes por variante, com 10 quadros de
aquecimento e 40 medidos em BGFX, 20 em wgpu, 512×512, geometria 100% dinâmica.
As medianas abaixo são medianas entre processos; p95 é a mediana dos p95 de
cada processo. Os 26 pares de capturas dos quadros escolhidos são
idênticas byte a byte entre as variantes.

| GPU/API | Processos por variante | Mediana genérica → compacta | p95 genérico → compacto |
| --- | ---: | ---: | ---: |
| AMD BGFX/OpenGL | 3 | 204,480 → 184,552 ms (−9,75%) | 235,138 → 191,915 ms |
| NVIDIA BGFX/Vulkan | 3 | 215,071 → 200,909 ms (−6,58%) | 230,314 → 221,579 ms |
| AMD wgpu/Vulkan | 2 | 202,533 → 192,913 ms (−4,75%) | 215,799 → 213,878 ms |

Após as guardas finais de índices, dois pares AMD BGFX/OpenGL adicionais de
20 quadros mediram 207,904 → 187,728 ms e 193,550 → 185,554 ms, cada um
preservando as duas capturas. O segundo par usou o binário final. Um processo
compacto do primeiro A/B AMD teve
p95 de 376,564 ms, e um NVIDIA teve p95 de 297,689 ms; o ganho de mediana
não elimina essas caudas. Também não fecha o custo restante de lowering,
empacotamento, janela, atualização LESS ou cenas acima do limite do buffer
BGFX. B10 permanece aberto.

[Logs, CSVs, capturas comprimidas e hashes](validation/coin-render-b10-compact-cube-20261017/verification.json).
