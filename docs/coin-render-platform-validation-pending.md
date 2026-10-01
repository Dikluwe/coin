# CoinRender — validação pendente fora deste host

Este registro acompanha apenas evidência que exige outro sistema operacional,
processador ou GPU física. Implementação, teste local e qualificação são estados
separados: preparar uma rota não qualifica a combinação. Atualizar a linha quando
houver execução identificada por SO, arquitetura, GPU, API, driver e alvo.

| Escopo | Recurso necessário | Estado atual | Evidência para fechar |
|---|---|---|---|
| P20 — Intel Linux | GPU Intel física em outro computador do usuário | Células da matriz não executadas neste host AMD/NVIDIA | Executar janela e offscreen, GL/BGFX/wgpu conforme suporte real, com oráculo de pixels e tolerâncias |
| P21 — Windows x64 | Outro computador do usuário com Windows, MSVC, Rust MSVC e GPU D3D12 | Rota Win32/wgpu e smoke preparados; sem build/execução nativos | Compilar, apresentar, capturar, resize/DPI/multiwindow, recuperação e comparar fixture com Coin/GL |
| P21 — BGFX/D3D11 e D3D12 | Outro computador do usuário com Windows, GPU e toolchain BGFX | Conectores ainda não implementados; D3D12 rejeitado explicitamente | Implementar mecanismo e validar por API/driver em máquina física |
| P22 — macOS/Metal | Host macOS indisponível; requer toolchain Apple e GPU Metal, em Intel e/ou Apple Silicon conforme matriz | Rota AppKit/wgpu e smoke preparados; sem build/execução nativos | Compilar, apresentar, capturar, testar Retina, resize, múltiplas janelas e ciclo de vida do layer |
| P23 — Android | Android/ARM com GPU | NDK r30 e target Rust arm64 instalados neste host; ponte wgpu e objetos Android compilados; Coin base ainda bloqueia link final por GL desktop; sem dispositivo | Isolar GL desktop no Coin base, compilar/empacotar, validar superfície, pause/resume, recriação de recursos e matriz por API/driver |
| P27.4 — oitava sombra Coin/GL | Contexto Coin/GL com pelo menos nove unidades de textura utilizáveis (uma da cena e oito mapas) | Mesa/GLX deste host reporta oito unidades e Coin/GL cria só sete mapas; BGFX e wgpu executam o oitavo e alteram o readback | Repetir a fixture de oito spots num contexto que crie oito mapas Coin/GL e comparar o delta incremental da oitava luz com BGFX e wgpu |
| P27.5 — sombras Windows/Intel | Outro computador do usuário com Windows e GPU Intel física | Perfis opacos P27.2 wgpu em AMD/RADV e P27.3 BGFX em NVIDIA/Vulkan qualificados neste host; sem evidência Windows/Intel | Executar a mesma fixture Coin/GL versus wgpu e BGFX onde disponível para spot, direcional, pares, câmera/frustum, resize e falha; registrar API, driver, pixels e tolerâncias |
| P27.5 — sombras BGFX em outras GPUs/APIs | AMD/RADV, Intel e BGFX/OpenGL neste Linux; Windows quando o conector existir | P27.3 qualificado em NVIDIA GeForce RTX 3060 Laptop GPU, Vulkan, driver 610.57.04 | Repetir fixtures P27.3 por GPU/API/driver e documentar diferenças de readback e limites de formato |
| P27.5 — sombras macOS/Metal | Host macOS indisponível | Sem build ou readback Metal do perfil de sombras | Quando houver host, repetir as fixtures P27 de GPU/GL aplicáveis, resize e falhas; registrar arquitetura, GPU, versão do SO e driver |

Pendências executáveis neste Linux, como Wayland nativo, falha BGFX/OpenGL na
NVIDIA e oráculos visuais, permanecem nos documentos de suas fases. A separação
Wiring/Core/Infra/Shell é por responsabilidade: compor contratos e mecanismos
existentes prevalece sobre construir hierarquias ou subsistemas paralelos.
