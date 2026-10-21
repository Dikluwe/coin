# B10 — experimento rejeitado e decomposição do overlay

Na cena city de 40 mil objetos, o overlay da geometria atualiza 960 mil
posições por quadro. Testei substituir apenas `std::isfinite` das três
coordenadas por `coin_render_is_finite_inline`, a mesma classificação de bits
que acelerou a validação do plano. O binário anterior foi congelado antes
da edição. O A/B AMD/OpenGL BGFX intercalou três processos por variante,
10 quadros de aquecimento e 40 amostras por processo.

| Variante | Mediana das medianas | Mediana dos p95 |
| --- | ---: | ---: |
| Antes | 202,656 ms | 211,069 ms |
| Classificador inline | 200,258 ms | 208,153 ms |

A diferença de mediana é 1,18% e não prova ganho estável. Traces separados
de cinco quadros por variante mediram 24,195 ms antes e 27,167 ms depois
na validação do overlay; portanto, o trecho visado não acelerou nessa
amostra. Três pares de imagens foram idênticos byte a byte. Os testes de
rollback, limites, NaN, infinito e subnormais passaram nos builds BGFX e
wgpu. **A troca foi revertida**; não é parte da implementação final.

Acrescentei somente um trace que separa a passagem de posições da checagem
final de intervalos, sem mudar o resultado. Em dez quadros medidos após o
revert, a mediana foi 19,170 ms: 18,122 ms na passagem que valida e guarda
o undo de 960 mil posições, 1,047 ms na checagem dos ~40 mil intervalos.
O próximo A/B deve atacar a passagem/undo, preservando recusa antes da
mutação e rollback exato. [CSVs, logs, imagens e hashes](validation/coin-render-b10-overlay-finite-20261017/verification.json).
