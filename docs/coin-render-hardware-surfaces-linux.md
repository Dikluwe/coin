# Hardware, superfícies e sombras — continuação Linux de 2026-10-07

Esta rodada qualifica o perfil local nas GPUs AMD Renoir `1002:1638`
(Mesa radeonsi/RADV 25.2.8) e NVIDIA RTX 3060 Laptop `10de:2560`
(driver 610.57.04), em Linux x86_64, kernel 6.17.0-42-generic, builds
Release. Intel gráfica, Windows, AppKit/Metal e dispositivo Android não
foram executados. A [checklist](coin-render-next-fronts-checklist.md) mantém
qualificação local, implementação pendente e hardware externo separados.

Código base: `94a2ee1a9b`, com o patch e os hashes de fontes da rodada no
[manifesto](validation/hardware-surfaces-linux-20261007/manifest.json).
Após retirar o include GL desnecessário de `SoAction` e condicionar o
`glFinish` do profiler ao build legado, as quatro bibliotecas nativas
Coin/CoinRender recompiladas permaneceram **byte a byte idênticas** às usadas
na campanha. A ponte privada permanece na revisão 49; a API pública, os
shaders e as máscaras de capacidades não mudaram.

## P27: seis células físicas e expectativa independente de oito mapas

| GPU | BGFX Vulkan | BGFX OpenGL | wgpu Vulkan |
|---|---:|---:|---:|
| AMD | 51 CTest + 1 teste de oito mapas | 52/52 CTest | 48/48 CTest |
| NVIDIA | 52/52 CTest | 52/52 CTest | 48/48 CTest |

São **304 verificações do perfil no agregado das seis células**, sem falhas ou skips nas
execuções qualificadas. Incluem Wiring, viewport, qualidade, transparência,
peeling/OIT, alfa RTT, staged/direct, ownership, seleção, depth e falhas
injetadas. A primeira execução AMD/wgpu foi interrompida pela recompilação
de um executável de teste cujo link GL faltava; ela está excluída e a célula
foi repetida integralmente, 48/48. Os tempos CTest não são benchmarks.

`CoinRenderShadowEightMapTest` define uma expectativa metamórfica própria:
uma luz de intensidade 0,8 e oito luzes coincidentes de intensidade 0,1
precisam conservar RGB (MAE <=0,25; máximo <=3). O plano comum deve conter
exatamente oito passes. Desligar a última luz precisa alterar mais de 100
pixels; retirar a recepção de sombras, mais de 20. Uma nona luz deve retornar
`UNSUPPORTED`, preservando imagem e serial, e a recuperação deve reproduzir
exatamente o quadro anterior. As seis execuções físicas produziram erro RGB
zero, 10.246 pixels afetados pela oitava luz e 2.132 pela sombra.
Esse teste pode ser registrado mesmo sem o renderer GL legado.

O oráculo CoinGL nativo de oito mapas continua indisponível: ambos os
contextos reais anunciam oito unidades de coordenadas e 32 samplers de
fragmento. CoinGL reserva uma unidade para a cena, precisando de nove para
oito mapas. O controle negativo exigindo
`COIN_RENDER_REQUIRE_GL_EIGHT_MAP_REFERENCE=1` falhou pelo diagnóstico
esperado nos dois dispositivos. A conservação de iluminação acima qualifica
o contrato portátil; não certifica um render CoinGL com oito mapas nem substitui uma referência
de oclusão com oito câmeras distintas. As
fixtures antigas de oito mapas que usam sete luzes equivalentes mantêm essa
qualificação restrita.

O runner de sombras agora exige que **o contexto offscreen CoinGL atual**
identifique a GPU física esperada e rejeita renderer de software; inventariar
apenas o adaptador Vulkan não comprova o oráculo. BGFX/OpenGL também exige
a identidade do seu contexto EGL corrente em um preflight renderizado; a
seleção EGL de outra GPU deve falhar antes do CTest. `--gl-capacity` registra
renderer, vendor, coordenadas e samplers com o contexto corrente.

## P20: janela capturada, offscreen e divergências AMD delimitadas

O benchmark X11 agora aceita `--capture-window --image-output frame.ppm`
também em CoinGL. A leitura GL ocorre no back buffer antes de apresentar;
as linhas do PPM são normalizadas para cima/baixo, sem corrigir cores.
Sem captura solicitada, não há nova leitura no caminho normal.

A matriz executou **32/32 células** (2 GPUs × 2 alvos × 2 cenas × 4 executores),
com capturas não degeneradas e cenas distintas. Os oito pares BGFX/wgpu
Vulkan tiveram RGBA exatamente igual dentro da mesma GPU. A prova BGFX/OpenGL
consulta renderer/vendor no contexto **EGL corrente**, além do inventário GLX;
isso evita atribuir uma GPU ao backend somente pela janela hospedeira.
A falha NVIDIA/OpenGL antiga em Xwayland não apareceu no X11 físico com EGL
NVIDIA explicitamente selecionado; isso não qualifica a combinação antiga.

O gate RGB, declarado antes da execução, é máximo <=2 e MAE <=1,0 em
0–255, em todos os pixels. Das 24 comparações com CoinGL, **22 passaram**:
NVIDIA passou nas 12; AMD passou em janela, opacidade offscreen e
transparência BGFX/OpenGL. AMD/transparência offscreen em BGFX/wgpu Vulkan
falhou nas duas comparações, com MAE 0,134013, máximo 17 e quatro pixels
acima de três níveis. Os quatro estão em bordas da imagem de referência e
coincidem, com erro <=1, com uma cor vizinha do CoinGL. BGFX e wgpu Vulkan
coincidem entre si. A antiga diferença de hash OpenGL/Vulkan foi assim
quantificada; não virou equivalência por alteração de tolerância.

`CoinRenderCameraReuseReferenceTest` também exige sua campanha explicitamente.
Passou com CoinGL em NVIDIA nas três APIs e AMD/BGFX/OpenGL. Nos dois
Vulkan AMD, o quadro 5 excede o gate RGB original de MAE <=0,10. A travessia
completa apresenta a mesma diferença do reúso. O diagnóstico wgpu encontra
48 pixels acima de três níveis, todos em bordas, 47 próximos de uma cor
vizinha com erro <=1. Sem exigir GL, o teste completo de câmeras passa nas
três APIs AMD, incluindo retorno exato à câmera inicial e invalidação por
mudanças de objetos/material/luz. Isso separa reúso correto de equivalência
com raster GL; a causa específica da seleção de fragmentos fica como estudo.
PPMs originais e coordenadas estão no arquivo de diagnóstico AMD.

`BumpProgramGLX` passou nas duas GPUs em ambos os builds. Na NVIDIA PRIME,
a seleção de visual agora admite double buffering, como o conector GLX
Coin já fazia. Ausência de display, visual ou programas ARB tem diagnóstico
de skip explícito. Os skips normais em campanhas sem opt-in não são passes.

## P22: Wayland nativo, escala observada e substituição de superfícies

wgpu/Vulkan passou em **quatro células AMD/NVIDIA × escala efetiva 1/2**,
com Weston headless GL e desktop-shell. O launcher anuncia a escala do
output via `--weston-scale`; o smoke exige o valor observado por GDK via
`COIN_RENDER_EXPECT_WAYLAND_SCALE`. Definir somente `GDK_SCALE=2` não mudou
a escala observada: essas tentativas foram excluídas da qualificação HiDPI. O controle negativo
com output 1 e expectativa 2 retorna o erro esperado, impedindo falsa aprovação.

Cada captura é comparada com offscreen e CoinGL na mesma GPU, com máximo
RGB <=2. A cena BASE_COLOR produziu erro zero. Duas janelas apresentam,
capturam somente sob pedido, redimensionam e suspendem com tamanho zero.
Três ciclos destroem action/target antes de GTK destruir o `wl_surface` e
criam uma janela nova. A outra janela conserva pixels e serial enquanto a
primeira muda; depois continua apresentando. Essa é recriação real da
superfície hospedeira, não perda física do device.

O oráculo GL usa `DISPLAY=:0`; apresentação e eventos GTK usam o socket
Wayland privado. GDK_BACKEND, escala e flags do smoke são aplicados só ao
processo filho, preservando o gerenciador X11 privado. As primeiras quatro
tentativas vazaram GDK_BACKEND para Metacity e falharam antes de executar o
smoke; também estão excluídas. Compositores físicos, escala fracionária,
monitores diferentes, formato concreto de swapchain, BGFX/Wayland e
AppKit/Metal continuam pendentes. A fixture opaca não qualifica todos os
perfis transparentes ou de sombras em apresentação Wayland.

## P23: avanço de compilação arm64 e bloqueio restante

NDK r30/API 26 e Rust `aarch64-linux-android` recompilaram a ponte estática,
`CoinRenderTarget`, `CoinWgpuBackend`, NativeActivity e native_app_glue.
A ação comum `SoAction` agora também compila sem headers GL desktop: o
profiler omite a sincronização GL quando o renderer legado está desligado.
`SoCallbackAction` compila no mesmo toolchain. Os hashes e o cabeçalho ELF
AArch64 dos objetos estão preservados.

O link completo continua **aberto neste Linux**, bloqueado por
`SoGLRenderAction.cpp` e outros componentes GL que a biblioteca Coin ainda
compila com `COIN_BUILD_LEGACY_GL_RENDERER=OFF`. Nenhum stub GL ou troca por
GLES foi usado para declarar sucesso. É necessário separar esses componentes
preservando inicialização de tipos, formatos Coin e ABI antes de produzir o
APK. Não há adb/dispositivo acessível validado nesta campanha; pause/resume,
rotação e TERM_WINDOW/INIT_WINDOW exigem APK ligado e dispositivo físico.

Atualização posterior desta mesma data: o primeiro link completo/APK **x86_64**
foi concluído e executado no AVD em wgpu/OpenGL ES. A fronteira CPU preserva a
ABI de Coin com entradas GL fracas indisponíveis; remover completamente os
fontes GL continua aberto. A campanha anterior acima continua histórica.
[APK, execução e limites](coin-render-p23-apk-validation-20261007.md).

## Reprodução e evidência

Os scripts de campanha, comandos, seleção de ICD/GLX/EGL, capacidade real,
logs, pixels e hashes estão em
[validation/hardware-surfaces-linux-20261007](validation/hardware-surfaces-linux-20261007/).
O runner P20 aceita `--session physical` para X11 físico ou `isolated` para
Xwayland privado. Usa as fixtures P17 verificadas por SHA-256 e builds Release.
As capturas de janela usam display ativo; o estado DPMS original foi restaurado.
A qualificação de sombras usa o runner `.github/scripts/qualify-coin-render-shadows-linux.sh`.

Oito testes CTest CPU passaram nos dois builds; CoinTests mantém 418 casos e
309.076 checks por build. Quatro testes Python verificam a comparação de
pixels e oito testes do launcher verificam isolamento/cleanup. Windows foi
reconciliado com sua campanha BGFX posterior, sem nova execução ou promoção
de resultados antigos à revisão atual.
