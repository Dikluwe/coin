# Evidência: base de câmera na captura inicial

Esta etapa remove uma preparação duplicada da base de câmera na mesma chamada de `rememberFrameRoot`. Os perfis de câmera e objetos mantêm suas admissões independentes. A base emprestada não licencia capturas futuras.

## Reproduzir sem GPU

Na raiz do checkout:

```bash
python3 docs/validation/capture-camera-basis-linux/validate-archive.py
```

O validador confere o inventário, os hashes das cópias originais, CSVs e seleção de warmups, controle Coin/OpenGL compartilhado, revisões por variante, contadores do diagnóstico, grupos de testes, registros RGB e regeneração textual do relatório. Ele pode ser executado depois de copiar `docs/coin-render-capture-camera-basis-linux.md` e este diretório mantendo a estrutura relativa.

## Conteúdo e contagens

| Conjunto final | Processos únicos | Quadros medidos | Warmups |
|---|---:|---:|---:|
| Cold: 9 rodadas, static, primeiro quadro | 63 | 63 | 0 |
| Steady: 3 rodadas × 5 casos | 105 | 1.575 | 525 |
| Ablação: 3 APIs × 3 rodadas × on/off | 18 | 18 | 0 |
| Verificação antes/depois: 4 variantes × 7 casos | 56 | 392 | 0 |

Coin/OpenGL tem um processo por caso/rodada nas campanhas de timing. Seu CSV e log são copiados para ambos os lados; o desconto do processo duplicado exige igualdade dos dois hashes. As verificações visuais têm processos novos antes e depois e não usam esse desconto.

- `cold/` e `steady/`: CSVs, logs, comandos, metadados, resumos e scripts de reprodução.
- `diagnostic/`: 18 processos finais, séries brutas e parser; uma linha de CSV e um evento `capture_camera_basis` por processo.
- `gates/`: 18 execuções finais aprovadas, com comandos e logs completos. Core puro: 2; Action/Reuse com integração: 4; demais gates GPU: 12.
- `gates-initial/`, `builds/` e `diagnostic-cli-initial/`: falhas iniciais preservadas e descritas no relatório. Não são somadas aos conjuntos finais aprovados.
- `analysis.json`: estatísticas recalculadas por processo, pareamento, contagens e proveniência.
- `binaries-*.json`, `binary-hashes-post-campaign.json`: 8 arquivos antes, 8 depois e 4 CoinGL; todos os 20 hashes permaneceram iguais após a campanha.
- `hardware-*.json`: dois snapshots de hardware. Não constituem histórico de clocks durante cada amostra.
- `plot-and-report.py`, `stage-metadata.json`, `report-inputs.json`: geração e hashes das entradas/saídas do relatório.
- `execution/` e `orchestration/`: ferramentas e comandos usados na máquina original. Os caminhos de builds e cenas são preservados como dados históricos; uma nova execução requer builds e ambiente apropriados.
- `stage-files.json`: inventário deste conjunto e do relatório externo. O próprio inventário é excluído para evitar referência circular.

## Limites da reprodução

Os 392 PPMs foram reabertos na etapa original: os 196 pares RGB e os hashes dos arquivos são idênticos. O Git guarda métricas e hashes, sem PPMs ou bibliotecas; a reprodução arquivada valida esses registros e não reabre pixels ausentes. A campanha é offscreen, com tempos de CPU/parede incluindo espera GPU e readback.

Cada variante CoinRender possui antes/depois próprios. As fontes anteriores são diferentes: BGFX `6182410f5789bbdc30d5ccd06fa341e1810aef8e`, wgpu `199b0e02b4f0b74ffb5d042d98e2837aafc31ad8`. O mapa por variante é a autoridade para essas fontes; a revisão do checkout do runner não representa todos os binários congelados. Coin/OpenGL mantém o controle separado `4d63bb993022ee8d40802558b0871a4803002b8d`.

Cold mantém todos os outliers. O diagnóstico mede a preparação removida; variações maiores no total do quadro não são integralmente atribuídas a esse trecho. Os aumentos de tempo em steady também permanecem no relatório.
