# B10 — determinante afim no lowering BGFX: experimento revertido

O lowering instanciado verifica determinantes de todos os modelos antes de
empacotar as instâncias. Testei usar `SbMatrix::det3()` para modelos afins,
mantendo `det4()` para as outras matrizes. O teste `CoinBgfxCoreTest` passou.

O A/B da cena city de 40 mil objetos usou AMD/Vulkan, 512×512, geometria
100% dinâmica, 10 quadros de aquecimento e 20 medidos por processo. Foram
três pares independentes em ordem AB/BA/AB, com o binário anterior congelado.
As imagens finais dos seis processos foram idênticas por SHA-256.

| Par | Mediana anterior → det3 | p95 anterior → det3 |
| --- | ---: | ---: |
| 1 | 188,457 → 187,641 ms | 199,271 → 208,983 ms |
| 2 | 192,768 → 194,001 ms | 204,979 → 222,588 ms |
| 3 | 200,380 → 186,662 ms | 210,053 → 203,198 ms |

O ganho variou de sinal e o p95 piorou em dois pares. A alteração foi
**revertida**. Esse resultado não prova que o determinante domina o custo
do lowering; a próxima intervenção deve atacar uma parcela medida maior
da montagem do plano/instâncias e repetir o A/B com gate visual.

[CSVs, logs e hashes](validation/coin-render-b10-det3-ab-20261017/verification.json).
