# B10 — linha de base anterior à otimização, NVIDIA/Vulkan

Continuação: [A/B da classificação finita inline](coin-render-b10-inline-finite-20261017.md).

Esta campanha **não fecha B10**. Ela localiza o custo na cena city histórica de
40 mil objetos, 512×512, transparência por objeto, com builds BGFX e wgpu da
revisão de trabalho. CoinGL foi executado pelo mesmo build BGFX como controle.
Os 12 processos foram intercalados: estático e geometria 100%, CoinGL,
BGFX/Vulkan e wgpu/Vulkan, duas rodadas independentes, 10 quadros de
aquecimento e 40 amostras por processo. O runner registrou NVIDIA RTX 3060
Laptop nos executores CoinRender, hashes de binários/cena, ambiente, CSVs e
logs. A cena histórica e os recibos estão no [pacote B10](validation/coin-render-b10-current-20261017/verification.json).

| Cena | CoinGL mediana / p95 | BGFX mediana / p95 | wgpu mediana / p95 |
| --- | ---: | ---: | ---: |
| Estática | 11,568 / 13,608 ms | 0,856 / 0,926 ms | 1,359 / 1,567 ms |
| Geometria 100% | 234,143 / 367,927 ms | 274,575 / 287,393 ms | 262,256 / 285,334 ms |

As medianas de atualização da geometria foram 45,834 ms no CoinGL,
61,014 ms no BGFX e 60,902 ms no wgpu. No quadro inicial, BGFX e wgpu
registraram 897,6 e 1152,7 ms, contra 369,5 ms no CoinGL. Esses são tempos
de CPU/render + readback/publicação segundo o contrato do benchmark; dois
processos por célula não bastam para declarar um delta estável de cauda.

Traces separados, excluídos da estatística acima, mostram no caso BGFX
dinâmico aproximadamente 71–72 ms de validação de vértices do plano,
15–16 ms de classificação de composição, 43 ms de lowering e 2,3 ms de espera
de readback. No wgpu, a validação de vértices ficou em aproximadamente
60–61 ms; o alvo registrou aproximadamente 84 ms de validação e 52 ms de
submit. O preparo do overlay de objetos gastou aproximadamente 61–75 ms,
conforme o executor e a amostra. Esses spans são **aninhados** e não devem
ser somados. Ambos mantiveram 40.001 instâncias e malha compacta de 24
vértices/36 índices; o custo medido não é o de 40 mil draw calls GPU.

O próximo experimento B10 deve usar A/B com a mesma cena, build e gate visual
para uma mudança delimitada na validação ou no lowering, preservando rejeição,
identidade, rollback e publicação. Também faltam a comparação de janela,
AMD/OpenGL, custo da política canônica/LESS, GPU timings e o caso de terreno
de 16 milhões de triângulos recusado pelo limite de buffer BGFX. Esta linha
de base não extrapola para Android, Metal ou Windows.
