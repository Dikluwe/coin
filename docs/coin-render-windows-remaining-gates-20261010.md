# Windows: continuação dos gates em 2026-10-10

Checkout isolado da branch `codex/coin-portable-sampling-study`, base `b2776d4e5157bcca72221cbd3e286bbbd541fc42`, continuação de `b86404cafc`. A revisão testada inclui o diff de CoinBgfxBackend preservado no ledger do build e o patch externo BGFX abaixo; não deriva de `codex/coin-render`.

Windows 10 Pro 19045, i5-4670K, NVIDIA GTX 1060 6GB (PCI 10de:1c03), driver NVIDIA 581.08 / Windows 32.0.15.8108. Cada gate de janela/campanha exige essa GPU e a API física selecionada. Intel HD 4600 não foi qualificada nesta rodada. `native` permanece o padrão.

## Gates e limites

| Entrega | Resultado |
|---|---|
| Consumidor SDK instalado | 12 processos GPU native/portable × BGFX/wgpu × D3D12/Vulkan/OpenGL; 2 consumidores C11 |
| Sampling BGFX após trocar a dependência | 33 processos: API, avançadas, deep, RTT direto, viewport e perfil RTT |
| NPOT direto + oito sombras + peeling/weighted | 6/6 funcional e custo GPU, cinco níveis em cada amostra medida |
| Win32 smoke / sampling | 6/6 + 6/6 nas três APIs dos dois backends |
| Remoção real do device D3D12 BGFX | 12/12: native/portable × offscreen/janela × três processos independentes |
| Campanha maior de janela | 144/144 ABBA, 60 warmup + 600 medidos por processo; 86.400 frames medidos |

O piloto funcional adicional passou 72/72 antes da troca de dependência. Os seis workloads são: estático, câmera por `PostMessage(WM_MOUSEMOVE)`, alteração de transforms, materiais, geometria e malha de um milhão de triângulos. Os primeiros cinco usam 2.500 cubos / 30.000 triângulos; a malha usa 500.000 quads triangulados. São cenas sem textura em 640×480. Não equivalem à cidade de 40 mil objetos, ao milhão de instâncias nem aos consumidores FreeCAD.

Antes/depois de cada processo, a janela e um offscreen independente com o mesmo backend/API/policy são comparados com RGB máximo **0**, exigindo cobertura visível acima de 10%. Esses oráculos não substituem o CoinGL nem generalizam correção de toda a cena. Wgpu/OpenGL usa GDI fora do trecho medido por falta de COPY_SRC; demais combinações pedem readback antes do render. Não houve readback nas 600 chamadas medidas. Eventos são sintéticos, sem alegação de arraste físico ou input-to-photon.

## Recuperação real e dependência BGFX

`ID3D12Device5::RemoveDevice()` no device real do processo retorna `GetDeviceRemovedReason() = 0x887a0005`, após estado inicial saudável. O BGFX observa a perda por sua execução normal e callback. Não houve reset de adaptador, alteração de registro, TDR do driver ou fallback contado como passe. A implementação consultou a interface em runtime, disponível neste host. O [contrato Microsoft de RemoveDevice](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device5-removedevice) documenta essa operação; a disponibilidade e o HRESULT nesta máquina foram observados nos logs.

A falha inicial reproduzida era a retenção dos swapchains secundários em `RendererContextD3D12::shutdown()`. O renderer descartado conservava referências ao device removido; recriar D3D12 falhava, e o Coin recusava a API alternativa. O [patch da dependência](validation/windows-remaining-20261010/bgfx-device-loss.patch) libera os framebuffers nativos secundários antes de `preReset()`. O SDK foi copiado por `git archive` das quatro revisões fixadas, compilado e instalado em `bgfx-device-loss-install`, sem modificar a dependência original. As revisões e os comandos estão no [provenance](validation/windows-remaining-20261010/bgfx-device-loss-provenance.json) e [ledger do SDK](validation/windows-remaining-20261010/bgfx-device-loss-build-ledger.json). **Esta recuperação de janela é qualificada com esse SDK corrigido; recompilar apenas Coin contra o SDK antigo não incorpora o patch.**

A hook privada `COIN_BGFX_TEST_REMOVE_NATIVE_DEVICE_ONCE` invoca a API nativa apenas sob opt-in no Windows/D3D12; não altera o render padrão. O teste exige DEVICE_LOST, serial sem avanço e nenhuma nova imagem publicada. O peer deve ficar TARGET_LOST antes do novo submit, recuperar automaticamente no submit explícito e produzir RGB esperado (limite 1 já fixado); target recriado exige pixels exatamente iguais ao inicial. A expectativa inicial incorreta de que o peer devolvesse DEVICE_LOST no submit foi preservada e corrigida conforme o contrato de recuperação existente.

## NPOT: timers corretamente atribuídos

Fixture combinado 63×47, oito sombras, peeling/weighted, rollback de token e recuperação, com sampling `native` padrão. Os controles RTT `native`/`portable` estão no gate de sampling separado. `dx11` identifica bytecode DXBC do shader; a API exercitada aqui é **D3D12**, não D3D11. A instrumentação filtra a view de redução, separa frames de base/sombras/OIT antes da primeira redução e drena consultas de cada nível enquanto seus recursos vivem. As esperas e frames extras só ocorrem sob tracing. Em D3D12, o SDK preserva cada consulta concluída antes de sobrescrever o último resultado da view; o ledger liga exatamente frame/view/nível aos ticks com fence concluído. `latest-stats-summary.json` mantém os resultados de getStats separadamente. O custo final D3D12 vem da história de consultas em arquivo próprio, sem descartar níveis nem imputar valores. A primeira história emitida em stderr intercalou uma mensagem no recibo funcional weighted e reprovou o parser; esse log ficou preservado, e os ledgers separados eliminam a mistura de streams. A primeira tentativa em arquivo fechava o ledger no resize interno do pool; a rodada final reabre em `create()` e conserva também essa falha inicial. Não são um benchmark de produção.

Cada processo conserva 44 registros (quatro funcionais + 40 do benchmark); descarte de dez warmup deixa 30 amostras, com cinco consultas de redução e cinco de frame resolvidas. As consultas de frame incluem cópia + redução, excluindo base/sombras/OIT e readback. Não incluir espera CPU como custo GPU. A rodada `npot-final` passou funcionalmente, mas uma amostra medida weighted perdeu os níveis 4/5 em getStats e reprovou o gate de custo. Tentativas sem o primeiro timestamp D3D12 e a tentativa anterior sem filtro de view permanecem no pacote, sem passar como atribuição final.

| API / OIT | Área GPU mediana / p95 (ms) | Cadeia frames GPU mediana / p95 (ms) |
|---|---:|---:|
| bgfx-d3d12-npot-1 | 0.770848 / 2.669920 | 1.794656 / 4.382368 |
| bgfx-d3d12-npot-2 | 0.776848 / 1.810688 | 1.839040 / 2.976576 |
| bgfx-vulkan-npot-1 | 0.084832 / 0.098912 | 0.207680 / 0.232000 |
| bgfx-vulkan-npot-2 | 0.084064 / 0.094272 | 0.208000 / 0.232544 |
| bgfx-opengl-npot-1 | 0.053248 / 0.057344 | 0.077312 / 0.081920 |
| bgfx-opengl-npot-2 | 0.054784 / 0.092160 | 0.079872 / 0.129024 |

Custo restrito a esse fixture e às condições instrumentadas. Não generaliza HDR ou dimensões até 2047² no Windows; não reivindica ganho versus os traces anteriores com coleta diferente.

## Retorno CPU de janela sem readback

Métrica: CPU `render/present` e `total` (eventos + atualização + render/present). Inclui backpressure e espera da API; não mede fim GPU nem latência de display. O monitor `nvidia-smi` registra clocks, P-state, temperatura e utilização a cada segundo. A campanha final começa depois de todos os builds; a amostra wgpu anterior, durante compilação, está preservada como diagnóstico interrompido. Clocks não foram fixados e variação do host permanece uma limitação; não declarar ganho uniforme nem encerrar campanha de desempenho completa.

A cauda mais alta está em materiais BGFX/D3D12: medianas de render/present de 142,89–146,73 ms, p95 de 160,37–165,64 ms e máximo até 567,34 ms nos quatro processos. O passe funcional não fecha essa investigação de desempenho. A variação entre repetições também fica preservada; não atribuir diferenças somente à policy.

Tabela agrega os dois processos de cada policy no desenho ABBA, 1.200 chamadas medidas por policy/célula.

| Workload / backend / API | Native render mediana / p95 (ms) | Portable render mediana / p95 (ms) |
|---|---:|---:|
| static / bgfx / d3d12 | 5.5465 / 6.7720 | 5.6779 / 7.5272 |
| static / bgfx / vulkan | 8.8486 / 10.3679 | 9.2275 / 10.7963 |
| static / bgfx / opengl | 5.5951 / 7.2035 | 5.7836 / 8.4273 |
| static / wgpu / d3d12 | 16.6458 / 17.2170 | 16.6343 / 17.2585 |
| static / wgpu / vulkan | 2.2864 / 50.3956 | 1.6993 / 50.2640 |
| static / wgpu / opengl | 16.6468 / 16.9166 | 16.6450 / 16.9808 |
| camera-events / bgfx / d3d12 | 9.0181 / 10.4787 | 9.0640 / 10.1654 |
| camera-events / bgfx / vulkan | 12.1544 / 14.0426 | 12.4939 / 14.0778 |
| camera-events / bgfx / opengl | 9.0026 / 10.8749 | 8.8450 / 10.3171 |
| camera-events / wgpu / d3d12 | 16.6050 / 17.1459 | 16.6137 / 17.0351 |
| camera-events / wgpu / vulkan | 2.9149 / 50.1739 | 2.5102 / 50.2627 |
| camera-events / wgpu / opengl | 16.6149 / 16.9536 | 16.6146 / 16.9063 |
| transforms / bgfx / d3d12 | 33.3580 / 37.7147 | 33.1677 / 39.1512 |
| transforms / bgfx / vulkan | 35.3345 / 40.0385 | 35.5059 / 38.9660 |
| transforms / bgfx / opengl | 42.8606 / 45.5277 | 43.0228 / 48.0678 |
| transforms / wgpu / d3d12 | 16.2763 / 16.7175 | 16.2754 / 16.7881 |
| transforms / wgpu / vulkan | 6.2683 / 48.5998 | 6.9741 / 44.1332 |
| transforms / wgpu / opengl | 16.2548 / 16.4738 | 16.2632 / 16.4240 |
| materials / bgfx / d3d12 | 143.0990 / 162.4870 | 146.4365 / 164.5630 |
| materials / bgfx / vulkan | 40.5416 / 60.4653 | 36.8367 / 44.1610 |
| materials / bgfx / opengl | 31.9255 / 34.5851 | 31.7630 / 34.5301 |
| materials / wgpu / d3d12 | 16.2833 / 16.7239 | 16.2807 / 16.7954 |
| materials / wgpu / vulkan | 8.3256 / 30.6585 | 7.1130 / 46.1554 |
| materials / wgpu / opengl | 16.2734 / 16.5247 | 16.2700 / 16.4413 |
| geometry / bgfx / d3d12 | 34.5170 / 38.3250 | 34.8695 / 38.3217 |
| geometry / bgfx / vulkan | 35.4597 / 38.2392 | 35.9886 / 38.8624 |
| geometry / bgfx / opengl | 42.8309 / 45.0115 | 42.9591 / 45.0520 |
| geometry / wgpu / d3d12 | 16.2900 / 16.7745 | 16.3070 / 16.7890 |
| geometry / wgpu / vulkan | 7.1979 / 45.8077 | 7.3504 / 44.9820 |
| geometry / wgpu / opengl | 16.2918 / 16.4242 | 16.3041 / 16.4577 |
| million / bgfx / d3d12 | 4.9048 / 5.5275 | 4.8942 / 5.5363 |
| million / bgfx / vulkan | 4.6239 / 5.1132 | 4.6295 / 5.0420 |
| million / bgfx / opengl | 6.6026 / 7.4190 | 6.5097 / 7.4298 |
| million / wgpu / d3d12 | 26.2577 / 29.8929 | 26.1793 / 28.0129 |
| million / wgpu / vulkan | 25.7426 / 27.3666 | 25.8961 / 29.5036 |
| million / wgpu / opengl | 28.9441 / 31.8775 | 28.8086 / 30.4729 |

## Pendências e recibos

Os dois monitores ainda reportam DPI efetivo 96. Não se fechou mudança real entre DPIs diferentes nem WM_DPICHANGED físico. FreeCAD 1.0 instalado conserva Coin legado e não tem CoinRender integrado; Part::Spline, Mesh/FEM, ColorBar, arraste e RTT de janela do host ficam SKIP. Remoção de device wgpu/Vulkan/OpenGL e campanha de TDR/driver ficam abertas. O XML de pré-requisitos registra esses skips.

Os [artifacts](validation/windows-remaining-20261010/artifact-manifest.json) preservam logs, XML, comandos, códigos de saída, diffs, hashes de DLLs/executáveis, CSVs e pixels. PPMs locais são convertidos a PNG sem perda, sem resize nem tolerância; o [manifesto de pixels](validation/windows-remaining-20261010/pixel-manifest.json) registra SHA dos PPMs originais e RGB decodificado idêntico. Falhas iniciais não foram removidas.

Reprodução: os scripts em `validation/windows-remaining-20261010/scripts` usam os caminhos isolados desta máquina; CMake do consumidor está em `validation/windows-remaining-20261010/public-consumer`. Ajustar os caminhos para outro host conservando as revisões, patch e prefixes. Usar `run_native_removal.py <cohort> 3`; `qualify.py --phase npot --backend bgfx --name <cohort>` e `analyze_npot_history.py <cohort>`; `run_window_campaign.py <cohort> 600 60` e `analyze_window_campaign.py <cohort>`. Não executar dois gates GPU simultaneamente.

Para reconstruir a dependência, aplicar o patch na raiz da cópia isolada de `bgfx.cmake` (os paths começam por `bgfx/src`), conferir `git apply --check`, recompilar/instalar Release e apontar `bgfx_DIR`, `COIN_BGFX_SHADERC_EXECUTABLE` e o include para esse prefixo. A telemetria usa `COIN_BGFX_D3D12_QUERY_LOG` somente no gate NPOT; não ativá-la durante a medição de janela. Os scripts preservam as etapas iniciais e os repairs; o patch final é a referência completa do SDK testado.
