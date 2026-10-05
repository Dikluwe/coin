# Qualificação de objetos: primeiro quadro no Windows

Branch `codex/coin-render-transform-performance`, base `5d518cc300`.
Essa base contém as melhorias Linux `5d08e1396b` e a qualificação Windows
descrita em [linux-updates-windows](coin-render-linux-updates-windows.md).

## Alteração compartilhada

`CoinRenderActionP::qualifyObjectPayloads` consultava as opções de overlay de
material e de Cube durante o registro de cada ocorrência. Na cidade com
40.000 prédios, isso podia fazer até 80.000 consultas ao ambiente do processo.
A alteração lê cada opção uma vez por qualificação e conserva o resultado
local até concluir o registro. A próxima qualificação relê as duas opções.

A leitura local das opções termina com a qualificação. As provas de
propriedade, material, geometria, conflitos e limites continuam completas.
O renderer/lowering BGFX, a bridge/Rust wgpu e o readback não foram alterados.
O ganho ocorre no CoinRender compartilhado, após a submissão, dentro do tempo
do primeiro resultado. A imagem e a admissão dos próximos overlays são iguais.

Um caso de regressão usa o mesmo plano e fontes, desliga cada overlay
independentemente e ambos juntos, e depois religa ambos. A cada qualificação,
confere os mapas de ocorrências completos e o payload exato do plano.
Isso impede transformar a otimização num cache permanente das opções.

## Sondas separadas com tracing

Uma sonda por versão/backend, 1024×1024, um warmup e três quadros medidos.
Estas sondas localizam a etapa; não fazem parte das medianas abaixo.

| Backend OpenGL | Payload antes ms | Payload depois ms |
|---|---:|---:|
| bgfx | 229.911 | 16.583 |
| wgpu | 244.131 | 14.870 |

Esses custos dependem da plataforma e do ambiente do processo. Não se
extrapola o ganho absoluto do CRT Windows para o Linux.

## Medição antes/depois e referência CoinGL

Windows 10, GTX 1060 6GB, driver 581.08. Os binários de ambos os backends da
base foram preservados antes da edição. Ambos os lados foram medidos novamente
em três processos por variante, intercalados, 1024×1024, 30 warmup e 120 quadros
medidos, com readback síncrono de cor e cópia RGBA. Sem builds, testes ou outras
medições GPU concorrentes; tracing e a sonda de identidade CoinGL são separados.
Caches de sistema/driver permanecem. A identidade da GPU/API é validada nos logs.

Os números são medianas das três medianas por processo. O primeiro quadro
exclui parsing, enquadramento, preparação do helper, construção do target e
capability probe. Os logs preservam também tempo até o primeiro resultado
desde `main`, faixas, p95 e p99. A referência principal é `SoGLRenderAction`
via `SoOffscreenRenderer` da mesma revisão local. Três processos não provam
significância estatística; os resultados da campanha anterior são separados.

| Variante | Primeiro antes ms | Primeiro depois ms | Redução | Aquecido antes ms | Aquecido depois ms |
|---|---:|---:|---:|---:|---:|
| bgfx-d3d12 | 891.21 | 732.16 | 17.8% | 9.73 | 9.73 |
| bgfx-opengl | 757.38 | 555.15 | 26.7% | 8.85 | 8.93 |
| bgfx-vulkan | 939.96 | 736.23 | 21.7% | 7.81 | 7.82 |
| coingl | 707.71 | 683.90 | 3.4% | 36.47 | 37.16 |
| wgpu-d3d12 | 919.90 | 680.84 | 26.0% | 11.75 | 11.73 |
| wgpu-opengl | 640.01 | 384.92 | 39.9% | 13.32 | 13.35 |
| wgpu-vulkan | 625.77 | 428.81 | 31.5% | 9.76 | 9.81 |

## Regressão

| Suíte | Casos | Falhas | Skips |
|---|---:|---:|---:|
| tests-bgfx-opengl | 16 | 0 | 0 |
| tests-bgfx-vulkan | 16 | 0 | 0 |
| tests-bgfx-d3d12 | 16 | 0 | 0 |
| tests-wgpu-gl | 17 | 0 | 0 |
| tests-wgpu-vulkan | 17 | 0 | 0 |
| tests-wgpu-dx12 | 17 | 0 | 0 |
| tests-action-bgfx | 1 | 0 | 0 |
| tests-action-wgpu | 1 | 0 | 0 |

As suítes BGFX têm 16 casos e as wgpu 17: o gate adicional de annotations é
definido só para wgpu pelo CMake. Todos os casos selecionados foram executados.
Câmera, comparação fast/full e referência CoinGL são obrigatórios nas seis
combinações; há ainda ownership, rollback, limites das provas, material/Cube,
clipping, iluminação, texturas e composição. Os testes CPU da ação foram
executados também imediatamente depois das builds. Não houve skips.
O gate BGFX de clipping fixa Vulkan nas propriedades CTest do projeto; essa
configuração foi mantida nas três suítes. O gate de câmera usa a API selecionada.
A bridge Rust não mudou; sua qualificação anterior consta no relatório base.

## Imagens

Os 21 pares estáticos antes/depois são exatamente iguais em RGB. A comparação
dos seis backends depois com CoinGL preserva as métricas por imagem:

| Variante | MAE RGB | Maior erro de canal | Pixels com erro >3 |
|---|---:|---:|---:|
| bgfx-opengl | 0.022741 | 88 | 3 |
| bgfx-vulkan | 0.022607 | 31 | 1 |
| bgfx-d3d12 | 0.022607 | 31 | 1 |
| wgpu-opengl | 0.022607 | 31 | 1 |
| wgpu-vulkan | 0.022607 | 31 | 1 |
| wgpu-d3d12 | 0.022607 | 31 | 1 |

Foram feitas 150 comparações animadas contra CoinGL: camera, transforms-10,
materials-10 e geometry-10 na cidade, e materials-10 na paleta de 40.001 slots.
São smoke tests de imagem, com dois warmup e cinco quadros, step 120. Os cinco
quadros lógicos 0/120/240/360/480 têm state digests correspondentes; todas as
35 combinações variante/caso produzem mudança visível. Não se afirma ganho de
desempenho animado com essa amostragem curta.

| Variante | Pares RGB | Maior MAE RGB | Maior erro de canal | Maior contagem de pixels >3 |
|---|---:|---:|---:|---:|
| bgfx-d3d12 | 25 | 0.027781 | 157 | 5 |
| bgfx-opengl | 25 | 0.027853 | 139 | 6 |
| bgfx-vulkan | 25 | 0.027781 | 157 | 5 |
| wgpu-d3d12 | 25 | 0.027778 | 157 | 6 |
| wgpu-opengl | 25 | 0.027778 | 157 | 7 |
| wgpu-vulkan | 25 | 0.027778 | 157 | 6 |

Diferenças entre backends são registradas, inclusive pixels de borda isolados;
MAE pequeno não prova igualdade. Comparações usam RGB orientado do PPM, sem
correção de pixels, e não igualdade do hash RGBA bruto entre backends.

## Instalação e evidências

As instalações locais `build/coin-render-bgfx-install` e
`build/coin-render-install` foram atualizadas após gates e medições. SHA-256
de Coin4.dll, CoinRender4.dll e benchmark instalado corresponde à build
qualificada de cada backend em `installed-hashes.json`.

Comandos, sondas, logs, resultados de teste, manifests, scripts e métricas:
[evidências](validation/shared-overlay-first-frame-windows-20261005).
[Resumo JSON](validation/shared-overlay-first-frame-windows-20261005/summary.json).
As imagens e cópias de binários permanecem no diretório correspondente de
`build`; hashes estão no manifesto. `qualify.ps1` executa os gates,
`measure.py` a campanha serial e `summarize.py` valida cobertura e imagens.
