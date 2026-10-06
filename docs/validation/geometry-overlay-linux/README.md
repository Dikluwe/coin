# Evidências: validação do overlay de geometria

O [relatório](../../coin-render-geometry-overlay-linux.md) compara CoinGL/OpenGL, BGFX/Vulkan, BGFX/OpenGL e wgpu/Vulkan na cidade de 40.000 objetos, offscreen 1024 × 1024, mesma GPU NVIDIA. Dados e percentuais são derivados dos CSVs/logs preservados.

## Protocolo

| Campanha | Rodadas por grupo | Warmup por processo | Medidos por processo | Processos únicos | Medidos | Warmups |
|---|---:|---:|---:|---:|---:|---:|
| Matched: quatro casos/quatro variantes | 3 | 5 | 15 | 84 | 1.260 | 420 |
| Ablação: dois casos/três APIs/on-off | 3 | 3 | 7 | 36 | 252 | 108 |
| Verificação RGB antes e depois | 1 | 0 | 7 | 32 | 224 | 0 |
| Probe suplementar BGFX/Vulkan geometry-10 on/off | 3 | 5 | 15 | 6 | 90 | 30 |

Matched cobre `geometry-10`, `geometry-100`, `transforms-10` e `materials-10`. Para cada caso/rodada, o mesmo processo CoinGL aparece nos arquivos antes/depois: identidade de CSV/log por SHA-256 permite contá-lo uma vez. São sete processos efetivos por caso/rodada, 16 comparações por variante/caso. Medianas são calculadas por processo e depois entre as três rodadas; warmups permanecem registrados e ficam fora das estatísticas.

RGB é uma campanha separada, com sete quadros lógicos de 0 a 600, passo 100: 224 PPMs produzidos, 112 pares antes/depois. Pixels ficam fora desta árvore; resultados, referências, hashes, checksums de imagem/estado e logs de captura são preservados. Sem campanha de primeiro quadro/startup nesta etapa.

O probe suplementar usa a mesma build, sem traces, em três pares alternados on/off. A mediana de três medianas por processo foi off 60,156368 → on 56,841774 ms (−5,51%); o primeiro par aumentou 6,01% e permanece junto aos outros dois. A magnitude do aumento de 49,03% da comparação principal BGFX/Vulkan geometry-10 não se repetiu nessa amostra; a comparação principal permanece integral, sem atribuição de causa. O probe não é somado aos 84 processos matched ou aos 36 da ablação e não mede a fase local de validação.

## Mudança e significado das métricas

Core valida os slots atuais e prepara o undo na ordem original. Slots estritamente crescentes dispensam sort e scratch. Na primeira inversão, o prefixo já validado e os demais updates formam runs máximos consecutivos inclusivos; só os intervalos são ordenados. Sobreposições/duplicações recusam a transação. Updates, undo e mutações mantêm sua ordem original.

A união usa `uint64_t(last)+1`, sem wrap de 32 bits. Scratch opcional tem capacidade de até 65.536 runs e no máximo `4 × positions` bytes. Cap/OOM opcional recai no algoritmo literal após liberar scratch; alocações obrigatórias do undo mantêm seu comportamento. Não há cache entre chamadas nem confiança em revision para validar os slots.

`COIN_RENDER_DISABLE_GEOMETRY_INTERVAL_VALIDATION=1` força o caminho literal. A ablação usa o mesmo binário on/off, três rodadas por API e geometria 10%/100%, separada da comparação de fontes. Materiais/transformações sem posições não consultam essa opção nem emitem `geometry_overlay_validation`.

`geometry_overlay_validation.validation_ms` inclui validação das posições, montagem do undo, unicidade e limpeza do scratch. Exclui `fprintf`, validação posterior de draws/modelos e escritas finais do overlay. Não é whole-overlay time, tempo isolado de sort nem memcpy.

`positions`, `runs` e `sorted_items` descrevem a lista processada e o volume ordenado. `scratch_bytes` é capacidade de scratch para colisões, sem undo obrigatório/allocator: `ordered` usa zero; `intervals` ordena R runs; `literal` ordena N slots. A redução de itens ordenados não implica redução igual do total do quadro.

O primeiro full capture da ablação não emite essa fase. Cada evento é associado ao próximo marcador Action antes da seleção pelas flags warmup do CSV; os sete medidos têm eventos comprovados. Séries brutas, linhas, cardinalidades, modos e provas ficam preservados em `diagnostic/` e `analysis.json`; não se usa um slice arbitrário de eventos.

## Fontes e gates

Baseline Render medido: `9a594fa39c7ca7924f9931863d4bdd7a37165cee`, com snapshot `a2e19d360db9c4ef187550ae1513fddd0585a371`. Atual: `96e5ed80fa3ab3c9016cf10e6bbfc9b638335a33`. CoinGL: `4d63bb993022ee8d40802558b0871a4803002b8d`. `binaries-*.json` registra 8 binários baseline, 8 atuais e 4 CoinGL; os 20 hashes são reconferidos depois das campanhas. Hardware antes/depois é um snapshot, sem acompanhamento contínuo de clocks.

Os 12 gates finais passaram sem skips. O grupo registrado `core_cpu` tem quatro execuções: dois Core CPU (FrameCore e PlanAssembly) e duas integrações GPU (FrameReuseCore WG/BG, que executam Target/backend real). Os demais grupos são dois Action/Reuse mistos e seis GPU dedicados. A categoria original do manifesto é preservada. `gates/commands.json` registra definições CTest, comandos efetivos, categorias, executáveis, fontes/hash antes/depois e overrides/marcadores CLI. Dois builds WG/BG concluídos com sucesso permanecem em `build-commands.json` e `builds/`.

O bootstrap do executor teve um caminho de fonte incorreto e parou antes de executar testes. `history/` preserva comandos e log; `collection-inputs.json` registra seu diretório originalmente vazio (Git não mantém diretórios vazios). A tentativa não soma aos 12 gates finais. Nenhuma correção adicional de produção decorreu desse erro de script.

## Arquivos e reprodução relocável

- `steady/`: CSVs/logs before/after e verificação, summary e ferramentas de recomputação; manifest de cópias originais.
- `diagnostic/`, `diagnostic-contract.json`, `diagnostic-observation*.json`: ablação, alinhamento Action, provas admitidas e observação inicial preservada.
- `regression-probe/`, `regression-probe.py`: seis processos quiet, CSVs, stdout/stderr/logs, comandos, hashes e resumo suplementar recalculável; `stage-metadata.json` preserva esse resumo completo em `focused_probe`.
- `analysis.json`, `analyze-geometry-overlay.py`: comparações e fontes recalculadas.
- `stage-metadata.json`, `collection-inputs.json`, `report-inputs.json`, `stage-files.json`: configuração/proveniência e inventário SHA-256; o inventário exclui a si próprio.
- `plot-and-report.py`, `geometry-overlay.png`, `geometry-overlay.svg`: gerador e figuras científicas.
- `execution/`, `orchestration/`, `builds/`, `gates/`, `history/`: scripts, comandos e logs registrados. Caminhos originais descrevem o ambiente medido.

Copie esta árvore e o relatório mantendo `docs/validation/geometry-overlay-linux/` e `docs/coin-render-geometry-overlay-linux.md`. Na raiz dessa cópia:

```sh
python3 docs/validation/geometry-overlay-linux/validate-archive.py
```

O validador é somente leitura: recalcula CSVs e traces com alinhamento, confere gates/builds/fontes/hashes e a regeneração textual idêntica. Não executa build, benchmark, GPU, Git, rede ou figuras. `--recompute-rgb-if-available` também reabre os PPMs originais quando disponíveis. Regressões e outliers são mantidos; amostras curtas não estabelecem caudas nem causalidade de toda variação do quadro.

`hardware-after-main.json` preserva o snapshot entre a campanha principal e o probe; `hardware-after.json` e os 20 hashes finais foram registrados depois do probe. O validador também recalcula os seis CSVs e três pares suplementares, verifica ausência de trace nos logs/ambiente e inclui seus três hashes no confronto com os 20 binários finais.

`load-observation.json` registra carga durante a terceira rodada, que teve outliers grandes. A lista GPU amostrada mostrou nosso benchmark; `ps` incluiu Codex e navegadores. Esse snapshot não prova uma causa nem mede continuamente toda a campanha. Nenhuma rodada foi descartada; o gráfico preserva as faixas das três rodadas.
