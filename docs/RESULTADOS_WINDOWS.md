# Validação Windows das candidatas CC/Sb e XML/SCXML

Data: 6 de outubro de 2026. As 18 candidatas foram compiladas e testadas individualmente em Windows x64, estático Release e DLL Debug, com `COIN_THREADSAFE=ON`. As 36 configurações terminaram com build e CTest aprovados, após os pré-requisitos/correções indicados abaixo. Nenhum teste registrado foi ignorado.

Foram executadas 2606 entradas de CTest nessas 36 configurações, incluindo a suíte agregada `CoinTests`. Esse total conta repetições entre branches/configurações, não testes únicos. A integração cumulativa de todas as candidatas não foi executada neste ciclo Windows.

## Ambiente e isolamento

- Windows 10 x64 (10.0.19045), Visual Studio 2022 Build Tools / MSVC 19.44, SDK 10.0.26100.0.
- GPU dos testes WGL: NVIDIA GeForce GTX 1060 6GB, OpenGL 4.6.0, driver 581.08.
- Não havia disco RAM disponível; a máquina tinha cerca de 4,3 GiB de RAM livre. Builds em `H:/Git/coin/build/windows-candidates-20261006`, com MSBuild limitado a dois projetos e `/MP4`.
- Snapshots via `git archive` dos SHAs do manifesto. O checkout principal e sua modificação em `testsuite/bumprender/TestAdapter.h` foram preservados. Nenhum push, PR ou merge upstream foi feito neste ciclo.
- Dois diretórios de build distintos: `static` e `shared`. A troca de snapshot remove snippets gerados `*Test.cpp` cujo bloco `COIN_TEST_SUITE` não existe no source atual, evitando testes residuais de outra branch.

## Matriz final

| Branch | SHA base | Estático Release | DLL Debug | Condição da aprovação |
| --- | --- | --- | --- | --- |
| `codex/pre-pr/bump-fixture-static` | `55dbefa033` | 70/70 | 70/70 | SHA publicado, sem patch adicional |
| `codex/pre-pr/cc-dict-complete` | `9b9237f13d` | 80/80 | 80/80 | SHA publicado, sem patch adicional |
| `codex/pre-pr/cc-hash-complete` | `c5dc4c643a` | 78/78 | 78/78 | SHA publicado, sem patch adicional |
| `codex/pre-pr/sbdict-complete` | `b5ae0d4ff3` | 82/82 | 82/82 | SHA publicado, sem patch adicional |
| `codex/pre-pr/sbhash-complete` | `5112bbcaf0` | 78/78 | 78/78 | SHA publicado, sem patch adicional |
| `codex/pre-pr/sbsmallmap-complete` | `ff1d09ddf3` | 71/71 | 71/71 | Correção de link do alvo SbSmallMapFailureTest |
| `codex/pre-pr/gl-contexts-small-complete` | `87cc9003e9` | 71/71 | 71/71 | Correção de link do alvo SbSmallMapFailureTest |
| `codex/pre-pr/glglue-lifetime-complete` | `0d31242881` | 70/70 | 70/70 | SHA publicado, sem patch adicional |
| `codex/pre-pr/cc-xml-expat-2.9.0` | `aef999cb36` | 70/70 | 70/70 | Patch de fixture 55dbefa033, apenas na cópia de teste estática |
| `codex/pr/cc-xml-entity-abi` | `21dc59664b` | 72/72 | 72/72 | Patch de fixture 55dbefa033, apenas na cópia de teste estática |
| `codex/pr/cc-xml-path-loops-truncate` | `bffa176d45` | 70/70 | 70/70 | Patch de fixture 55dbefa033, apenas na cópia de teste estática |
| `codex/pr/cc-xml-escaping-inttypes` | `0935125576` | 70/70 | 70/70 | Patch de fixture 55dbefa033, apenas na cópia de teste estática |
| `codex/pr/cc-xml-parser-transactional` | `162dc998af` | 71/71 | 71/71 | Patch de fixture 55dbefa033, apenas na cópia de teste estática |
| `codex/pr/cc-xml-coalesce-cdata` | `5c4a34e045` | 70/70 | 70/70 | Patch de fixture 55dbefa033, apenas na cópia de teste estática |
| `codex/pr/cc-xml-dom-ownership` | `28d74b9982` | 70/70 | 70/70 | Patch de fixture 55dbefa033, apenas na cópia de teste estática |
| `codex/pr/cc-xml-dom-regressions-fuzz` | `dcb54791bd` | 70/70 | 70/70 | Patch de fixture 55dbefa033, apenas na cópia de teste estática |
| `codex/pr/scxml-sendelt-tokenize` | `5e8945f208` | 70/70 | 70/70 | Patch de fixture 55dbefa033, apenas na cópia de teste estática |
| `codex/pr/scxml-event-association-ownership` | `a4fc778e7c` | 70/70 | 70/70 | Patch de fixture 55dbefa033, apenas na cópia de teste estática |

## Correções e pré-requisitos

1. **Link do teste SbSmallMap.** Os heads publicados de `sbsmallmap-complete` e `gl-contexts-small-complete` falhavam com LNK1104 em `SbSmallMapFailureTest`: o alvo não declarava sua dependência do Coin. A correção acrescenta `target_link_libraries(SbSmallMapFailureTest PRIVATE Coin)`. As quatro configurações passaram 71/71 após essa alteração. Commit local `689d62859983c794216ec4e5947f00212e2ae607`, branch `codex/windows-smallmap-test-link`, sobre `ff1d09ddf3`. Patch portátil: [fixes/sbsmallmap-test-link.patch](fixes/sbsmallmap-test-link.patch). O mesmo delta precisa ser transportado para a candidata GL dependente; os heads remotos não foram alterados.
2. **Fixture bump das candidatas XML/SCXML.** As dez candidatas desta frente partem de master sem o fixture corrigido. Seus builds estáticos originais falharam por LNK2005/LNK1169, duplicação de `bumphack`. Aplicar apenas o delta de `55dbefa0330ca59a68ff2d47591f1256e7cba7b2` ao adapter da cópia estática permitiu build e suíte completos, preservando o código de produção de cada candidata. A DLL passou nos heads originais.
3. **Isolamento dos testes gerados.** A primeira execução de SbDict incluiu indevidamente `basehashTest.cpp`, gerado anteriormente pela candidata cc_hash. As duas execuções afetadas foram descartadas e repetidas após remover o snippet obsoleto; SbDict passou 82/82 em ambas. Essa foi uma correção no runner, sem patch no produto.
4. **Configuração inicial.** A tentativa inicial dentro do sandbox deixou flags vazias no cache CMake. A rodada válida usa as flags normais do MSVC: `/EHsc`, Release `/O2 /Ob2 /DNDEBUG`, Debug `/Zi /Ob0 /Od /RTC1`. O erro de setup não é classificado como defeito de branch. Os processos também recebem ambiente normalizado sem chaves PATH/Path duplicadas e MSBuild sem node reuse.

## Testes nativos WGL adicionais

Os drivers GLX originais foram adaptados no suporte de contexto e resolução de funções de plataforma. Os quatro consumidores GL são compilados de seus fontes reais com o hook de falha já existente. O driver mantém os auxiliares MockShader/UniformProbe do teste original para injetar falhas; a renderização GLSL e os contadores usam recursos GL nativos.

| Candidata | Configuração | Build | Testes WGL |
| --- | --- | --- | --- |
| `codex/pre-pr/gl-contexts-small-complete` | static Release | Aprovada | 1/1, sem falhas |
| `codex/pre-pr/glglue-lifetime-complete` | shared Debug | Aprovada | 2/2, sem falhas |
| `codex/pre-pr/glglue-lifetime-complete` | static Release | Aprovada | 4/4, sem falhas |

- Caches GL: cinco contextos WGL independentes, falhas de inserção, retry, hits, pixels, contagem de VBOs/programas/shaders e duas ordens de teardown.
- Glue: callbacks enquanto o empréstimo continua válido, 72 gerações, duas threads em contextos distintos, IDs `0x80000001` e `UINT32_MAX`, shutdown e ausência de callbacks registrados.
- ASan do glue: driver, `src/glue/gl.cpp` e `src/misc/SoContextHandler.cpp` instrumentados por MSVC `/fsanitize=address`; a biblioteca restante não foi inteiramente instrumentada. As verificações de poison do ASan inspecionam a liberação do registro e de suas seis estruturas possuídas. As execuções normais sem ASan conferem lifecycle/IDs/callbacks, mas não provam liberação de heap por si mesmas.

## Limites e pendências

- Esta é validação individual dos snapshots transportados e suas dependências. Falta o lab cumulativo Windows com todas as candidatas escolhidas; o lab fixo não foi modificado.
- `USE_EXTERNAL_EXPAT=ON` não foi testado: não havia instalação de desenvolvimento Expat preparada. A matriz principal usa o Expat embarcado correspondente a cada SHA.
- libFuzzer, UBSan, LeakSanitizer e TSan não foram executados no Windows. A contribuição de regressões DOM/fuzzer teve seus testes funcionais executados com `COIN_BUILD_FUZZERS=OFF`. Não declarar o conjunto inteiro livre de vazamentos a partir destes testes.
- Testes Linux/GLX e certos testes de interposição de malloc/OOM condicionados a `NOT WIN32` não fazem parte do CTest Windows. Zero testes ignorados significa zero skips entre os registrados, não equivalência integral da cobertura Linux.
- A correção de link precisa ser incorporada às candidatas dependentes antes de publicação upstream. O fixture também precisa ser explicitado/integrado como pré-requisito dos builds estáticos XML.

## Evidência e reprodução

JSON consolidado: [resultados-windows.json](resultados-windows.json). Logs completos, XML JUnit, SHAs e scripts estão em `../../build/windows-candidates-20261006`. As execuções originais com erro permanecem em `results.json`; as rodadas reparadas ficam em `link-repair-results.json` e `fixture-overlay-results.json`. `wgl-results.json` guarda os drivers adicionais.

Runner principal: `run-matrix.ps1`; repetições: `run-link-repair.ps1`, `run-static-fixture.ps1`; WGL: `run-wgl.ps1` e `run-wgl-asan.ps1`. O runner retoma as configurações já aprovadas; para reproduzir do zero, usar uma nova pasta de build e os mesmos SHAs e opções, preservando os logs existentes.

Comandos centrais:

```powershell
cmake -S <snapshot> -B <build> -G "Visual Studio 17 2022" -A x64 -DCOIN_BUILD_TESTS=ON -DCOIN_BUILD_SHARED_LIBS=<ON|OFF> -DCOIN_THREADSAFE=ON -DCOIN_BUILD_MSVC_MP=OFF
cmake --build <build> --config <Debug|Release> --parallel 2 -- /nr:false
ctest --test-dir <build> -C <Debug|Release> --output-on-failure --timeout 60 --parallel 2 --output-junit <resultado.xml>
```
