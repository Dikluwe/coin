# B10 — construção direta do undo rejeitada

O trace anterior localizou ~18 ms na passagem que valida e prepara o undo
de 960 mil posições. Testei construir cada entrada diretamente no vetor,
eliminando o temporário `SbVec3f`, e guardar uma referência local ao vetor
de vértices. O binário anterior foi congelado antes da edição. O A/B usou
três processos intercalados por variante em AMD/OpenGL e NVIDIA/Vulkan,
com 10 quadros de aquecimento e 40 medidos na cena city de 40 mil objetos.

| GPU/API | Antes: mediana / p95 | Candidato: mediana / p95 |
| --- | ---: | ---: |
| AMD/OpenGL | 207,298 / 289,415 ms | 202,790 / 250,350 ms |
| NVIDIA/Vulkan | 216,577 / 222,391 ms | 214,722 / 227,514 ms |

As medianas caíram 2,17% e 0,86%, mas as caudas tiveram outliers grandes:
o p95 piorou na NVIDIA, e o maior p95 por processo do candidato na AMD
chegou a 400,698 ms. Em trace separado na AMD, a mediana da passagem do
overlay caiu de 24,938 para 22,229 ms, também com outlier de 54,741 ms.
Seis pares de imagens foram idênticos byte a byte; os testes de rollback e
validação passaram nos builds BGFX e wgpu. **A edição foi revertida** porque
o ganho pequeno não demonstrou estabilidade de cauda.

A próxima intervenção deve reduzir a quantidade de memória percorrida ou
reutilizar trabalho comprovadamente idêntico, sem perder o undo exato e a
recusa anterior à mutação. [CSVs, logs, imagens comprimidas e hashes](validation/coin-render-b10-undo-direct-20261017/verification.json).
