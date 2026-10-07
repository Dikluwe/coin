# CoinRender: continuação da checklist no Windows, 2026-10-07

A base publicada `0d7ba61b7cc78318cb3ec7fdaa1efabe01cdeb23` foi baixada e
compilada em um checkout isolado. Esta campanha testa as correções Windows da
branch `codex/coin-render-windows-20261007`; não reutiliza resultados de builds
anteriores. O checkout principal e suas alterações locais foram preservados.

## Ambiente e revisão

- Windows 10 Pro 19045 x64; Visual Studio 2022, MSVC 19.44.35229.0,
  Windows SDK 10.0.26100.0, Release x64.
- NVIDIA GeForce GTX 1060 6 GB, PCI `10de:1c03`, driver NVIDIA 581.08
  (`32.0.15.8108`). A identificação efetiva da API/GPU aparece nos logs.
- CMake/CTest 3.31.6-msvc6, Rust/Cargo 1.99.0, Python 3.13.7.
- Dois monitores físicos, ambos com DPI observado 96. Não houve uma transição
  entre valores físicos distintos de DPI.
- Coin e CoinRender BGFX/wgpu recompilados; ponte Rust com protocolo 49.
  Builds em disco com paralelismo limitado (`--parallel 2`, `/MP2` no MSVC
  do build wgpu e dois jobs Cargo). Não havia RAM drive disponível.
- Revisão final de código/testes: `1a971511493b8e9fa30ec1f6dcd35766ceeaf416`.
  Os commits posteriores a `9fc89fb9cb0636d2ec416fc83dfbf6244055e96f` alteram
  apenas a fixture MultiDevice e o smoke Win32. As bibliotecas permanecem
  idênticas às testadas na suíte integral; seus hashes foram conferidos novamente.

## Correções encontradas nesta máquina

| Commit | Problema e correção | Verificação |
| --- | --- | --- |
| `c46f9a5ec9` | A atribuição de `COMPILE_DEFINITIONS` apagava `NOMINMAX`, impedindo a compilação MSVC. As definições internas agora são adicionadas ao target. | Builds BGFX e wgpu concluídos. |
| `2414975ffe` | O teste de referência de sombras usava tokens ausentes nos headers Windows GL 1.1. Usa os aliases ARB de mesmo valor presentes no fallback público do Coin. | Diagnóstico de capacidades compilado nas duas configurações. |
| `25fa5fb97f` | `memcmp` de draws incluía padding não inicializado do MSVC, invalidando igualdade/reúso. A comparação agora cobre explicitamente todos os campos. | Regressão com padding diferente falhou antes; os três testes de captura/core/reúso passaram nos dois builds depois. |
| `9fc89fb9cb` | O pbuffer WGL direto do CoinGL não construía mipmaps. Acima do limiar de qualidade 0,5, o produtor usa imagem armazenada com cadeia completa; o caminho direto de nível base permanece abaixo do limiar. | Oito casos focados FBO/pbuffer passaram. MAE RGB máximo pbuffer 0; FBO 0,666667; tolerância 4 preservada. |
| `146b2e2ed4` | A fixture MultiDevice declarava RTT opaco apesar do contrato conservador da ponte Rust. O teste agora exige rejeição dessa declaração antes de aceitar o RTT sem prova de opacidade. | Seis testes relacionados passaram, incluindo propriedade, publicação, FFI, readback e stress. |
| `8149ae058c` | O smoke Win32 não selecionava OpenGL explicitamente nem verificava substituição de HWND. Acrescenta seleção e três recriações, além de movimento entre monitores. | Resultados de superfície registrados separadamente por API; ver matriz final. |
| `8e3ef4125b` | Amplia o smoke para seriais de janela e ticket offscreen não consumido após destruição do produtor e recriação real de HWND. | Resultados registrados por API na rodada de smokes ampliados. |
| `1a97151149` | Acrescenta um modo explícito de apresentação sem captura para superfícies sem COPY_SRC. Exige rejeição de captura sem avanço do serial e recuperação posterior. | wgpu/OpenGL Win32 passou nesse escopo; pixels da janela não foram comparados. |

Os logs das falhas iniciais foram preservados. Nenhuma tolerância visual foi
relaxada e uma API indisponível não conta como passe por fallback.

## Suítes integrais e repetições

O runner habilita os gates GPU, referência CoinGL e câmera. Não impõe referência
nativa CoinGL de oito mapas sem nove unidades de coordenadas utilizáveis.
Uma suíte integral é **mista**: alguns casos fixam API por `ENVIRONMENT` ou
`cmake -E env`. O nome da rodada indica a API herdada pelos demais casos.

| Rodada | Passes | Falhas | Skips | Tempo |
| --- | ---: | ---: | ---: | ---: |
| BGFX integral, ambiente D3D12 | 297/297 | 0 | 0 | 1380,49 s |
| wgpu integral, ambiente dx12 | 204/205 | 1 | 0 | 2290,25 s |
| wgpu após correção da fixture, seis testes relacionados | 6/6 | 0 | 0 | 28,46 s |
| BGFX Vulkan, casos que herdam a API | 105/105 | 0 | 0 | 515,26 s |
| BGFX OpenGL, casos que herdam a API | 105/105 | 0 | 0 | 478,00 s |
| wgpu Vulkan, casos que herdam a API | 131/131 | 0 | 0 | 579,21 s |
| wgpu OpenGL, casos que herdam a API | 131/131 | 0 | 0 | 835,18 s |

A única falha da suíte wgpu integral foi `CoinWgpuMultiDeviceTest`, corrigida
e repetida no grupo de seis testes. Não foi executada uma segunda suíte integral
wgpu de 205 casos após essa mudança exclusiva da fixture.

A seleção das repetições exclui variantes que fixam outra API e smokes Win32,
já registrados separadamente. As listas selecionadas, comandos, variáveis,
inventários CTest e XML identificam o escopo exato de cada rodada.

## Win32, recriação e SDK instalado

| Backend/API | Janela e offscreen | Recriação/seriais/ticket | SDK público offscreen |
| --- | --- | --- | --- |
| BGFX/D3D12 | Comparação de pixels passou | Passou | Passou |
| BGFX/Vulkan | Comparação de pixels passou | Passou | Passou |
| BGFX/OpenGL | Comparação de pixels passou | Passou | Passou |
| wgpu/D3D12 | Comparação de pixels passou | Passou | Passou |
| wgpu/Vulkan | Comparação de pixels passou | Passou | Passou |
| wgpu/OpenGL | Apresentação sem captura; COPY_SRC indisponível | Passou sem comparar pixels da janela | Passou |

Os cinco smokes com captura obtiveram delta máximo de canal **0**, com
tolerância 3 mantida, para as fixtures opaca/transparente, recriadas e movidas
entre monitores. Todos exercitaram duas janelas, ausência de readback normal,
resize, minimizar/restaurar, três HWNDs substitutos e isolamento do serial da
janela sobrevivente. Cada recriação manteve válido um ticket offscreen não
consumido cujo produtor já tinha sido destruído; o RGBA retornou exatamente
igual ao controle e uma segunda consulta rejeitou o ticket consumido. Isso
verifica o ticket retido pelo usuário; não exige que a GPU ainda estivesse
executando a cópia quando o HWND foi destruído.

A tentativa inicial wgpu/OpenGL de capturar a janela falhou explicitamente:
`Window surface does not support COPY_SRC readback`. O modo
`--opengl --expect-no-window-readback` verifica essa rejeição, preservação do
serial e recuperação do render normal, depois o lifecycle e as recriações.
Seu log marca `pixel_comparison_enabled=0`: **não encerra a equivalência visual
de janela OpenGL/wgpu**. O offscreen OpenGL/wgpu e seus readbacks passaram.

As seis seleções explícitas confirmaram a API solicitada e o adaptador NVIDIA.
OpenGL informa vendor `0x10de` e device `0`, enquanto D3D12/Vulkan informam
`0x1c03`; o device zero do probe GL não é uma segunda GPU. O consumidor público
também registrou o renderer CoinGL `NVIDIA GeForce GTX 1060 6GB/PCIe/SSE2`,
oito unidades de coordenadas e 32 samplers de fragmento. Essas oito coordenadas
não satisfazem a referência CoinGL nativa de oito mapas, que precisa de nove.

Dois SDKs novos foram instalados em prefixes separados com
`COIN_INSTALL_RENDER_EXPERIMENTAL=ON`. Os headers/imported targets públicos
compilaram um consumidor sem headers privados; nas seis combinações a query
retornou a API pedida e um cubo vermelho publicou RGBA correto. Os hashes das
DLLs instaladas coincidem com os builds testados. O export privado
`coin_wgpu_surface_submission_serial` foi conferido na DLL wgpu instalada.

O teste de instancing BGFX foi executado diretamente em D3D12 e passou;
o registro CTest desse caso fixa Vulkan e, sozinho, não qualificava D3D12.

## Piloto de desempenho offscreen

Cena `city-2500.iv`, 256 × 256, publicação RGBA síncrona, seis casos:
estático, câmera, transformações 10%, materiais 10%, geometria 10%/100%.
BGFX D3D12/Vulkan/OpenGL e wgpu dx12/Vulkan/gl foram comparados com reserva
desligada/ligada; CoinGL foi mantido como controle adicional.

A verificação separada produziu 546 PPMs em 78 processos: **252 pares de
quadros literal/reserva idênticos**, sem imagens vazias. Cada caso animado
produziu sete hashes diferentes, e o estático um único hash. Os hashes de
todas as imagens e quatro PPMs representativos acompanham a evidência; o
conjunto completo permanece no diretório local de artifacts.

A análise usa **234 processos**, três por variante/caso, com 10 quadros de
aquecimento e 60 amostras por processo: 180 amostras por variante/caso.
Treze processos de geometria 10% da primeira rodada coincidiram com a auditoria
CPU de pixels; foram excluídos e o bloco inteiro foi repetido isoladamente,
na mesma ordem intercalada. As 247 tentativas e a lista de exclusão foram
preservadas. Não houve builds ou testes GPU concorrentes.

Não houve ganho uniforme. As razões reserva/literal da mediana total ficaram
entre **0,898 e 1,067** nesta amostra. A maior piora relativa de p95 foi
wgpu/Vulkan estático: **2,359 → 2,874 ms**, razão **1,218**. wgpu/Vulkan câmera
também piorou: mediana **1,769 → 1,887 ms**, p95 **2,643 → 3,037 ms**.
O controle estático também variou; esses números não demonstram, sozinhos,
uma causa na implementação da reserva. Ganhos e regressões observados estão
na [tabela completa](validation/windows-continuation-20261007/performance/summary.md).

O JSON conserva update/render/publicação, p99, primeiro quadro e medianas dos
três processos. Primeiro quadro significa processo novo, sem limpar cache do
driver. Plano de energia Equilibrado; estado/energia/clocks GPU amostrados por
rodada, com P8 observado antes dos blocos. A campanha não alterou display/driver,
não fixou clocks e não auditou continuamente o estado físico dos monitores.
Este é um piloto offscreen; janela sem readback, campanha de caudas mais longa
e atribuição causal de regressões continuam abertas.

## Evidência e reprodução

Os registros estão em
[`docs/validation/windows-continuation-20261007`](validation/windows-continuation-20261007/README.md).
Incluem XML, saída completa `LastTest.log`, falhas iniciais, seleção/inventário
CTest, comandos/ambiente, scripts, versões e hashes de DLLs/executáveis/shaders,
SDKs e os dados A/B. `test-summary.json` mantém contagens por rodada;
`evidence-sha256.json` identifica os arquivos publicados.

Os runners preservam os caminhos desta máquina. Para reproduzir, usar um
diretório novo de artifacts, adaptar os caminhos de source/build/dependências
e consultar o inventário CTest da revisão escolhida. Não executar em cima
dos registros arquivados. Builds locais:
`H:/Git/coin/build/windows-render-20261007/{bgfx,wgpu}`; prefixes instalados
`install-bgfx`/`install-wgpu` no mesmo diretório. O checkout usado foi
`C:/Users/Diklu/.codex/worktrees/coin-render-windows-20261007/coin`.

## Limites que permanecem abertos

- DPI físico distinto entre monitores e recepção de `WM_DPICHANGED` nesse cenário.
- Perda real do dispositivo GPU em campanha controlada. Falhas injetadas,
  minimização e recriação de HWND não substituem device removal real.
- Captura/comparação de pixels da janela wgpu/OpenGL: a superfície deste host
  não oferece COPY_SRC; apresentação e readback offscreen foram separados.
- Desempenho de janela sem readback e uma campanha maior para separar custo
  da reserva, variação de clocks/host e caudas de latência.
- CoinGL nativo com oito mapas requer contexto com nove unidades de coordenadas;
  a expectativa portátil de oito mapas é um resultado separado.
- FreeCAD Windows compilado com os hooks experimentais: Part/Spline, Mesh/FEM,
  ColorBar, picking, arraste por eventos e integração Quarter/Qt. A instalação
  FreeCAD existente não foi substituída por estas DLLs.
- D3D11 ainda depende de implementação; Intel física, AppKit/Metal, Wayland e
  Android dependem dos respectivos ambientes. Esta máquina não encerra essas células.

A checklist geral e a de nós retidos devem marcar somente os subescopos
efetivamente qualificados nesta campanha.
