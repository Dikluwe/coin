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
| P23 — Android | Android/ARM com GPU e toolchain NDK | Rota NDK/wgpu e smoke NativeActivity preparados; sem build/dispositivo | Compilar/empacotar, validar superfície, pause/resume, recriação de recursos e matriz por API/driver |

Pendências executáveis neste Linux, como Wayland nativo, falha BGFX/OpenGL na
NVIDIA e oráculos visuais, permanecem nos documentos de suas fases. A separação
Wiring/Core/Infra/Shell é por responsabilidade: compor contratos e mecanismos
existentes prevalece sobre construir hierarquias ou subsistemas paralelos.
