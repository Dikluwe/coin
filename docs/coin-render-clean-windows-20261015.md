# Validação Windows da branch limpa CoinRender

A branch limpa foi testada com MSVC 2022 x64 Release na revisão `2620d12da6064cbfc773ac8acfa05adbd7c1b1c9`, derivada da publicação Linux `95ee5e37b389cc9236a59e6fbc99de4ad65f8263`. A matriz cobre CoinRender desligado, Recording, BGFX e wgpu, com instalações separadas e consumidores públicos. Os resultados abaixo qualificam somente os domínios executados nesta GPU.

A matriz completa dos executores foi executada em `2620d12da6064cbfc773ac8acfa05adbd7c1b1c9`. Recording foi repetido em `eb13545855503661e79c6c660e09aa57081142fa` após restringir as exigências de executor GPU aos builds BGFX/wgpu e registrar corretamente o retorno 77 do teste Win32 de janela como skip, como já ocorria no Unix. O primeiro lote Recording exigia GPU indevidamente e registrou 37 falhas; seu XML e log permanecem preservados. A tabela usa a repetição corrigida. Os dois testes GPU de janela foram reconferidos após a mudança de registro.

## Correção necessária

O primeiro build BGFX falhou com C2491 em `CoinRenderAction.cpp`: `SbLazyPimplPtr<T>` estava marcado com `COIN_DLL_API`, que vira `dllimport` ao consumir Coin, enquanto suas funções são definidas no `.hpp` para `COIN_INTERNAL`. A branch limpa havia excluído a remoção desse marcador já presente no estudo. O ajuste restaura a declaração do template sem import/export, como em `SbPimplPtr`; não modifica seu layout ou corpos. O alvo CoinRender DLL passou após o ajuste. A regressão executa os consumidores DLL, a biblioteca padrão e o renderer no Windows.

A primeira configuração Recording também foi recusada: o runner habilitou o exemplo de janela reservado a BGFX/wgpu. A configuração foi corrigida no runner. Esse erro não exigiu mudança na biblioteca. A matriz inicial foi interrompida antes de concluir a configuração wgpu para evitar repetir a falha de template já diagnosticada; o helper de espera foi encerrado antes do runner. Os logs e o recibo da interrupção permanecem nos arquivos de evidência.

O teste de bounding box também precisou da definição guardada de `GL_DEPTH_CLAMP` (0x864F, conforme o Khronos vendorizado), ausente no header GL do Windows. O ajuste é restrito ao teste, sem mudar runtime, oráculos ou tolerâncias; seu alvo passou após a inclusão. As tentativas com erro estão preservadas.

## Ambiente e dependências

Windows 10 Pro 19045, NVIDIA GTX 1060 6GB, driver 581.08, MSVC 2022 x64. Builds pesados e processos GPU executados separadamente, com dois jobs no build e testes CTest em um processo por vez. CMake solicita o mínimo C++11 para Coin; MSVC utiliza seu modo compatível disponível. Os consumidores públicos exigem C++17 e C11.

O SDK BGFX corrigido foi reutilizado da campanha anterior, com as mesmas revisões e o patch final preservado em `cmake/patches`. Os 11 arquivos binários de dependência conferem com os hashes originais; não houve recompilação dessa dependência nesta campanha. Coin/CoinRender e os consumidores foram recompilados a partir da clean. Os SDKs BGFX/wgpu e seus diretórios de instalação são separados.

## Resultados

| Configuração | Passes | Skips | Falhas |
|---|---:|---:|---:|
| off | 77 | 0 | 0 |
| recording | 131 | 50 | 0 |
| bgfx | 313 | 1 | 1 |
| wgpu | 218 | 1 | 0 |

SDK instalado: **14/14** processos aprovados no gate registrado, incluindo C11 e `native`/`portable` em D3D12, Vulkan e OpenGL. O gate C++ exige NVIDIA física, API solicitada, módulos carregados do prefixo correspondente, referência CoinGL disponível e os oráculos existentes de cubo e mip. Os hashes build/instalação das DLLs são comparados. Skips não contam como passe.

Headers/sondas instalados: 4/4 configurações aprovadas. A instalação desativada deve excluir os 17 headers experimentais; Recording/BGFX/wgpu devem instalá-los byte a byte iguais à fonte. A sonda inclui os 17 headers via targets CMake importados e verifica o padrão native.

Rust serial: código de saída 0; resultados dos executáveis: 42 passes/0 falhas/0 ignorados (ok), 2 passes/0 falhas/0 ignorados (ok), 3 passes/0 falhas/0 ignorados (ok). Execução offline/locked/release com dois jobs e `--test-threads=1`, sem rustdoc. O problema paralelo observado no Linux não é declarado corrigido por uma execução serial.

## Falhas e limites

- bgfx: `CoinBgfxInstancingOpenGLTest`. Ver XML/logs para as condições e os valores medidos.
- Comandos da matriz final com saída diferente de zero: `final-bgfx-ctest`.

Esses controles não repetem nem encerram automaticamente as 42 falhas da campanha visual anterior. No BGFX/OpenGL, o caso `affine PHONG directional+point` apresentou erro RGB máximo 138 em um pixel (179,59): instanciado 8,11,17 e geral 146,50,46; média 0,00104777 e um pixel acima de 3. O limiar existente permaneceu inalterado. Isso reproduz o sintoma conhecido no Linux, sem provar sua causa. A discrepância instancing BGFX/OpenGL/NVIDIA e o teardown paralelo Rust só podem mudar de estado mediante seus próprios testes. O perfil público Windows permanece sujeito ao status de qualificação retornado pela API; passar um smoke não publica uma capacidade geral. FreeCAD integrado, DPI físico distinto, Intel, outros perfis de perda real de device e performance completa não integram esta matriz. PBR em desenvolvimento não foi incorporado.

A primeira repetição Recording corrigida passou com 127 passes/54 skips. Uma repetição final manteve `COIN_RENDER_REQUIRE_GL_REFERENCE=1` e desativou somente as exigências indevidas de executor GPU, para executar também quatro casos RTT CoinGL que dependem desse opt-in. A tabela usa essa última repetição; `recording-corrected-without-gl-optin.xml` conserva a intermediária, e `recording-reference-retry-ledger.json` registra os comandos finais. A tentativa anterior dessa repetição retornou 1 no MSBuild sem diagnóstico de compilação no log; a repetição com o acesso usado pelos builds anteriores passou. Ambos os recibos estão preservados, sem atribuir causa ao código ou ao ambiente sem evidência adicional.

## Reprodução e arquivos

Os comandos exatos, códigos de saída, tempos e hashes de logs estão em `ledger.json` (tentativa inicial) e `fixed-ledger.json` (ajuste de template) e `final-ledger.json` (após o ajuste do token do teste). Os XMLs CTest conservam testes aprovados, falhos e ignorados. `analysis.json` verifica os receipts físicos e caminhos dos módulos dos clientes SDK. `header-sdk-results.json`, `bgfx-dependency-check.json`, os arquivos de hashes DLL e `rust-serial.json` registram as verificações complementares.

[Evidências Windows](validation/coin-render-clean-windows-20261015) incluem os scripts, fontes da sonda, falhas iniciais, logs e PNGs derivados sem mudar os RGB dos PPMs. `pixel-manifest.json` registra os hashes dos PPMs originais mantidos no diretório de execução; `manifest.json` protege os bytes publicados. As dependências são reproduzíveis pelas [instruções BGFX](../cmake/patches/README.md), e o consumidor segue o [README público](../examples/coinrender/sdk-consumer/README.md).

O runner `validate.py --source <checkout> --tag <rodada>` mantém builds/prefixes fora do checkout, seleciona executores/APIs e conserva logs separados. Recording exige os exemplos/benchmarks de janela desligados. Os testes Rust e a auditoria do SDK são comandos separados, sem compilações GPU simultâneas.

## Execuções explícitas adicionais

Câmera com referência obrigatória: **5/6 execuções comprovadas**, com API e NVIDIA física nos recibos de perspectiva e ortográfica. Os seis processos retornaram zero, mas BGFX/Vulkan produziu log vazio nas duas tentativas e não é contado como passe. Permanecem preservados `camera-initial-checks.json`, o log inicial vazio e `camera-bgfx-vulkan-retry.log`; a causa não foi demonstrada. O consumidor público BGFX/Vulkan passou separadamente e não substitui este teste de câmera. `analysis.json` registra a verificação dos recibos das cinco execuções comprovadas. Na matriz CTest original, esse teste ficou como skip por falta do opt-in; os resultados adicionais estão em `camera-checks.json` e nos logs `camera-*`, sem converter o skip histórico em passe.

Reconferência da janela após o registro Win32 do retorno 77: bgfx: 1/1 passes, wgpu: 1/1 passes. Os XMLs `*-window-registration.xml` conservam o status real.

