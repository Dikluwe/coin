# Evidências: empréstimo da composição

O [relatório](../../coin-render-composition-copy-linux.md) compara CoinGL/OpenGL, BGFX/Vulkan, BGFX/OpenGL e wgpu/Vulkan na cidade de 40.000 objetos, offscreen 1024 × 1024, na mesma GPU NVIDIA. Valores e percentuais vêm dos CSVs/JSONs preservados.

## Protocolo e contagens

| Campanha | Rodadas por caso | Warmup por processo | Quadros medidos por processo | Processos únicos | Medidos | Warmups |
|---|---:|---:|---:|---:|---:|---:|
| Cold, estático | 9 | 0 | 1 | 63 | 63 | 0 |
| Steady, cinco casos | 3 | 5 | 15 | 105 | 1.575 | 525 |
| Ablação, dois casos e três APIs | 3 | 0/5 | 1 | 36 | 36 | 90 |

Steady cobre `static`, `camera`, `transforms-10`, `materials-10` e `geometry-10`. Timing reúne 168 processos únicos, 1.638 quadros medidos e 525 warmups. O controle CoinGL de cada caso/rodada é o mesmo processo copiado para antes/depois; CSV e log iguais por SHA-256 são contados uma vez.

As estatísticas usam a mediana dos quadros medidos de cada processo, depois a mediana entre processos. Warmups ficam nos arquivos e fora dessas estatísticas. Cold conserva outliers e mostra mínimo/máximo por processo. Quinze quadros por processo limitam conclusões sobre caudas.

A verificação RGB é separada do timing: 28 processos antes e 28 depois, sete quadros por processo, sem warmup; 392 PPMs produzidos e 196 pares comparados. Os PPMs ficam fora deste arquivo de evidências; hashes, checksums, logs e resultados estão preservados em `steady/`.

## Escopo da mudança e ablação

O empréstimo é local à submissão: Action/Builder fornece uma prova validada, Target ativa a referência por RAII e os consumidores fazem packing/lowering síncrono. A referência não é armazenada em tickets assíncronos, caches persistentes nem no payload Rust. Os caminhos gerais conservam composição própria.

O perfil é opaco e exige ordem e ranges de identidade. `SCREEN_DOOR`, inclusive nível zero, fica fora dele. A campanha usa `SORTED_OBJECT_BLEND` com materiais opacos; os campos e a ordem da composição, incluindo `sortObject`, permanecem preservados.

`COIN_RENDER_DISABLE_COMPOSITION_BORROW=1` desliga conjuntamente o empréstimo no Target e no schedule dos consumidores. A ablação usa três rodadas por API, `static` com zero warmups e `transforms-10` com cinco warmups, um quadro medido em cada processo. Ela não separa causalmente as duas intervenções.

`copied_bytes` e `borrowed_bytes` são bytes lógicos dos itens: 40.001 × 56 = 2.240.056 bytes por composição. Não medem capacidade dos vetores, allocator, RSS ou memória GPU. `computed_items` registra uma composição calculada e movida, sem chamar esse movimento de cópia.

`composition_transfer.copy_lookup_ms` soma, por linha medida, os intervalos de cópia/ativação e lookup do Target e do schedule. O intervalo `composition_schedule_copy.copy_ms` inclui a realização literal completa do schedule, não apenas memcpy. `composition_identity.qualify_ms` descreve a classificação completa já existente; inclui warmups nas somas de eventos e não mede overhead incremental puro. O contrato admite cardinalidades específicas dos dois casos desta ablação; eventos extras permanecem no trace bruto.

`diagnostic-observation.json` preserva a observação do contrato admitido. `inclusive-description.json` e `inclusive-analysis.py` preservam o subtotal descritivo `classify+sort+finish+transferência/lookup`, alinhado e somado por linha medida. Ele inclui o passe existente de classificação e não isola overhead novo. O primário continua transferência/lookup; a variação líquida conjunta usa `total_ms`.

`supplemental/` preserva um probe estreito de BGFX/Vulkan `materials-10`, mesma build, três pares on/off: seis processos, 90 quadros medidos, 30 warmups e seis provas admitidas. Total off→on: 52,641419→52,709456 ms (+0,068037 ms; +0,129%); transferência/lookup: 0,367967→0,000712 ms. O aumento principal de +2,997 ms (+5,72%) não se repetiu com essa magnitude; a variação positiva do probe permanece registrada. O probe não entra em matched168/ablação36, não substitui os dados principais e não estabelece causalidade.

## Fontes, gates e histórico

`stage-metadata.json` registra fonte final, produção `f6d05aedb83d01204c2ebb20fee78768a587e963`, baseline Render `24bc92d8d60d3a1ce782e7787d2060a6b08261fb` e controle CoinGL `4d63bb993022ee8d40802558b0871a4803002b8d`. `binaries-*.json` identifica os binários congelados; 20 hashes são conferidos após a campanha. Hardware antes/depois está preservado, sem acompanhamento contínuo de clocks.

`hardware-after-main.json` preserva o snapshot anterior ao probe. `hardware-after.json` e os 20 hashes pós-campanha foram atualizados após o probe.

Os 29 gates finais passaram sem skips: sete Core/CPU, quatro Action/Reuse e 18 GPU. `gates/commands.json` guarda comandos efetivos, definições CTest, categorias, fontes e hashes dos executáveis, incluindo overrides CLI e marcadores obrigatórios.

`gates-initial/` e `gates-retry-initial/` preservam duas tentativas de quatro execuções, cada uma com três passes e uma falha no novo oracle Action. A primeira corrigiu o escopo Material por objeto. A segunda corrigiu a expectativa de retry: `BACKEND_ERROR` mantém `TARGET_ERROR`; o teste agora confirma bloqueio, recuperação pública por resize e sucesso. Essas oito execuções são históricas e não somam aos 29 gates finais. A produção permaneceu igual; o fixture BG recebeu PHONG antes das execuções.

`build-commands.json` e `builds/` preservam os sete builds com sucesso: WG inicial, WG incremental/BG da primeira fonte de testes e os rebuilds dos dois ajustes do oracle.

## Arquivos e reprodução relocável

- `cold/` e `steady/`: CSVs/logs, summaries e ferramentas de recomputação; controles compartilhados identificados por hash.
- `diagnostic/`, `diagnostic-contract.json`: comandos, traces brutos, cardinalidades e derivação da ablação.
- `analysis.json`, `analyze-composition-copy.py`: comparações recalculadas, amostras e fontes.
- `stage-metadata.json`, `collection-inputs.json`, `report-inputs.json`, `stage-files.json`: configuração, proveniência e inventário SHA-256; o inventário exclui a si próprio.
- `plot-and-report.py`, `composition-copy.png`, `composition-copy.svg`: gerador e figuras do relatório.
- `execution/`, `orchestration/`, `builds/`: ferramentas, comandos e logs preservados. Os caminhos originais dos executores documentam o ambiente medido; não são executados pelo validador.

Copie o relatório e esta árvore mantendo `docs/coin-render-composition-copy-linux.md` e `docs/validation/composition-copy-linux/`. Na raiz dessa cópia:

```sh
python3 docs/validation/composition-copy-linux/validate-archive.py
```

O validador é somente leitura: recalcula CSVs/traces, confere fontes, gates, hashes e regeneração textual idêntica. Não executa benchmark, build, GPU, Git nem rede. `--recompute-rgb-if-available` também reabre os PPMs originais quando ainda existem; sem eles, confere a evidência registrada. Binários e PPMs não integram este arquivo de evidências.
