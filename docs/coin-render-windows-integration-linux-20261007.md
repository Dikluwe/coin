# Integração Windows e continuação Linux — 2026-10-07

A entrega Windows `8d9f47305875d5cde92935acc383a7dcbff6fecc` foi conferida
no fork e integrada por **fast-forward** ao worktree Linux
`/tmp/coin-render-first-frame`, na branch única `codex/coin-render`.
A base anterior era `0d7ba61b7cc78318cb3ec7fdaa1efabe01cdeb23`; não havia
alterações locais pendentes. Os commits Windows, seus logs e o histórico Linux
foram preservados. A antiga referência de entrega Windows não é destino de
continuação. Os 796 artifacts Windows foram conferidos por SHA-256 tanto
no checkout quanto nos blobs do índice Git, normalizando apenas os separadores
dos caminhos do manifesto, sem modificar bytes/CRLF.

## Revalidação das correções no Linux

Builds Release completos BGFX e wgpu/Rust, GCC 13.3, Linux x86_64/kernel
6.17.0-42-generic, AMD Renoir `1002:1638` com Mesa 25.2.8 e NVIDIA RTX 3060
Laptop `10de:2560`, driver 610.57.04. GPU/API/GLX/EGL/ICD foram selecionados
explicitamente; campanhas GPU são seriais. Os tempos dos testes não são benchmarks.

| Célula | Regressões principais | RTT BGFX específico | Câmera separada |
|---|---:|---:|---|
| AMD/BGFX/Vulkan | 16/16 | 8/8 | Passou, expectativa portátil sem GL |
| AMD/BGFX/OpenGL | 16/16 | 8/8 | Passou com CoinGL |
| AMD/wgpu/Vulkan | 28/28 | Incluso | Passou, expectativa portátil sem GL |
| NVIDIA/BGFX/Vulkan | 16/16 | 8/8 | Passou com CoinGL |
| NVIDIA/BGFX/OpenGL | 16/16 | 8/8 | Passou com CoinGL |
| NVIDIA/wgpu/Vulkan | 28/28 | Incluso | Passou com CoinGL |
| AMD/wgpu/OpenGL | 11/11 focados | Incluso | Não executada nesta rodada |

São **163 execuções CTest no agregado das sete células**, sem falhas/skips nas campanhas
selecionadas, mais seis execuções de câmera. Incluem captura/core/reúso,
inventário de nós, ownership/publicação, múltiplos alvos, RTT staged/direct,
FBO/pbuffer/matriz, orçamento e mipmaps. wgpu inclui MultiDevice, stress,
FFI e readback/action async. Os testes específicos BGFX usam os nomes
`CoinRenderRttProfile{Staged,Direct,Mips,MipsPbuffer}_{api}` e
`CoinRenderSceneTexture{Budget}_{api}_{staged,direct}` do CTest atual;
não contam como executados por selecionar o nome wgpu em outro build.
As propriedades ENVIRONMENT e as listas efetivamente executadas estão nos logs/XML.

Mais dez testes CPU separados passaram nos dois builds, incluindo as novas
regressões de padding/igualdade de draws e readmissão de câmera. `CoinBgfxCoreTest` também passou
depois de ajustar o escopo de link do pacote, verificando os testes internos
que usam BGFX diretamente.

A mudança Windows `SoSceneTexture2` preserva o caminho direto do pbuffer
somente em qualidade <=0,5; acima disso usa a imagem armazenada para mipmaps.
As transições 0,3/0,5/0,51/0,7/0,85 e retorno atravessam FBO/pbuffer nas duas
GPUs, com os gates anteriores. A referência GLX local usa a rota disponível;
isso não substitui a execução WGL render-to-texture já registrada no Windows.
Nenhuma tolerância foi alterada. O MultiDevice corrigido exige rejeitar a
declaração de opacidade não comprovada sem escrita/publicação e depois aceita
o contrato conservador. Ele passou em Vulkan AMD/NVIDIA e OpenGL AMD.

A divergência de raster AMD/Vulkan com CoinGL permanece como estudo. O teste
portátil completo de câmera foi repetido nesses dispositivos sem impor GL;
as quatro execuções com GL nas células qualificadas continuam distintas.
Não há nova conclusão sobre o oráculo CoinGL nativo de oito mapas.

## Continuação viável: consumidor público e SDK relocável

Foi promovido um [consumidor portátil](../examples/coinrender/sdk-consumer/README.md)
baseado no consumidor público Windows. O projeto é independente do build Coin,
usa targets importados e headers instalados, solicita a API explicitamente e
compara todos os pixels RGB de um cubo BASE_COLOR 32×32 com CoinGL, orientados,
com máximo <=3. A indisponibilidade tem diagnóstico e retorno 3. Os fontes
CMake/C++/README também são instalados junto dos exemplos experimentais.

O primeiro configure do consumidor BGFX falhou porque o SDK exportava
`bgfx::bgfx` e `find_dependency(bgfx CONFIG)` para aplicações que não usam
BGFX diretamente. CoinRender é uma biblioteca **compartilhada** e a API pública
não expõe esses tipos. O link BGFX passou a ser privado ao módulo; o
BUILD_INTERFACE mantém os headers/símbolos transitivos usados pelos testes
internos. O pacote instalado deixa de exigir bgfx.cmake e seus headers.
A compilação completa dos testes internos continuou válida.

Dois SDKs novos foram instalados em prefixes próprios, sem substituir a
instalação de uso do host. Os prefixes foram copiados para novos diretórios e
os consumidores recompilados a partir dos fontes já instalados. Package
registries foram desligados, apenas o prefix relocado foi fornecido, e
`CMAKE_DISABLE_FIND_PACKAGE_bgfx=TRUE` confirmou que o cliente não precisa
achar o pacote BGFX. Compile commands contêm somente headers públicos
instalados; `ldd` confirma Coin/CoinRender no prefix relocado, sem DLLs/bibliotecas
do build original.

| Consumidor instalado | AMD | NVIDIA |
|---|---|---|
| BGFX Vulkan | RGB máximo 0 | RGB máximo 0 |
| BGFX OpenGL | RGB máximo 0 | RGB máximo 0 |
| wgpu Vulkan | RGB máximo 0 | RGB máximo 0 |
| wgpu OpenGL | RGB máximo 0 | Não executado |

Os sete processos confirmaram API, centro vermelho e comparação CoinGL,
sem adapter de software. Para BGFX/OpenGL foram conferidas as linhas do
contexto EGL **corrente** tanto do probe quanto do render; GLX sozinho não
comprova a GPU. Controles D3D12 em ambos os SDKs retornaram 3, com diagnóstico
sem fallback. Isso qualifica um smoke público/offscreen e relocabilidade,
não todos os perfis visuais ou superfícies.

A primeira instalação wgpu após habilitar o opt-in falhou por RPATH dos
exemplos ainda não relinkados; recompilar todos os alvos antes de instalar
corrigiu a tentativa. Houve também um configure reaproveitando cache de outro
source-dir; um build novo preservou a tentativa e resolveu o problema. As
falhas preparatórias ficam nos logs e não são contadas como passes.

## Evidência, reprodução e limites

[validation/windows-integration-linux-20261007](validation/windows-integration-linux-20261007/)
guarda source/base, scripts, comandos/ambiente, XML/logs, capacidade física,
manifestos, SDK exports, compile commands e bibliotecas carregadas. Os scripts
usam os paths deste host; executar com diretório novo e adaptar paths sem
sobrescrever arquivos arquivados. Os SDKs/binários completos ficam nos
artifacts locais; seus hashes são versionados.

A nova definição de export e o consumidor portátil precisam ser recompilados
no Windows na próxima rodada de SDK. Não há novo teste Windows nesta continuação
Linux, embora todos os commits e resultados Windows estejam integrados.
Continuam abertos DPI físico distinto, perda real de device, captura
wgpu/OpenGL Win32 sem COPY_SRC, consumidores FreeCAD Windows, raster AMD,
link/empacotamento Android, mips GPU diretos e estudos de desempenho. Nenhum
item externo é encerrado por esta revalidação local.
