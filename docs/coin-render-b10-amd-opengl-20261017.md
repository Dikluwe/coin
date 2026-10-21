# B10 — linha de base AMD/OpenGL para a cidade dinâmica

A cena city histórica de 40 mil objetos foi executada na AMD Renoir com
CoinGL, BGFX/OpenGL e wgpu/OpenGL. Cada combinação teve dois processos
independentes, 10 quadros de aquecimento e 40 amostras em 512×512. O runner
intercalou os processos e preservou manifesto, hashes dos binários, CSVs e
logs em [evidências](validation/coin-render-b10-amd-opengl-20261017/).
O SHA-256 da cena é `bb9ccc612cd38ffb749ee599d9350a80974649eab5bf477d2f344538a42b2576`;
é a mesma cena da [linha de base NVIDIA/Vulkan](coin-render-b10-current-baseline-20261017.md).

| Cena | CoinGL mediana / p95 | BGFX/OpenGL mediana / p95 | wgpu/OpenGL mediana / p95 |
| --- | ---: | ---: | ---: |
| Estática | 21,013 / 21,666 ms | 1,171 / 1,421 ms | 1,624 / 1,954 ms |
| Geometria 100% | 209,231 / 216,770 ms | 199,255 / 212,343 ms | 204,274 / 238,621 ms |

Na geometria dinâmica, as medianas de atualização foram 44,217 ms no
CoinGL, 59,401 ms no BGFX e 60,327 ms no wgpu. Dois processos por célula
localizam o custo, mas não dão precisão suficiente para declarar vantagem de
cauda. Traces separados da estatística acima mediram, nos cinco quadros
finais, medianas de ~22,6/19,1 ms na validação do overlay de posições,
~9,0/9,4 ms na validação dos vértices e ~15 ms na classificação de
composição (BGFX/wgpu). O lowering BGFX mediu ~40,9 ms; o empacotamento da
ponte wgpu ~42,3 ms, além de ~8,1 ms no Rust. Os spans são aninhados e não
devem ser somados diretamente.

O caso estático é rápido nas duas alternativas; a atualização integral dos
960 mil vértices e 40 mil objetos concentra o custo. Próximos experimentos
B10 devem comparar A/B uma alteração delimitada no overlay/empacotamento,
com igualdade visual e rollback, além de janela, política canônica/LESS e
GPU timings. B10 permanece aberto.
