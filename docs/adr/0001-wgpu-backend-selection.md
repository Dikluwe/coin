# ADR 0001: Seleção do Backend WebGPU de Referência para a Onda 1A

- **Status:** Aceito
- **Data:** 2026-09-22
- **Decisores:** Time Coin3D / Antigravity

## Contexto

A especificação [`SoWgpu-Onda0-Onda1.md`](../../estudos/So/SoWgpu-Onda0-Onda1.md) estabelece a evolução do renderizador WebGPU do Coin3D através de etapas incrementais. Na Onda 0, o scene graph foi desacoplado do pipeline gráfico através do `FramePlan` imutável e do `SoWgpuRecordingBackend`.

Para a Onda 1A (renderização offscreen em GPU real), três rotas técnicas foram avaliadas:
1. **Dawn (C++ / Google):** Requer compilação e empacotamento complexos do Chromium/Dawn, indisponível nativamente nos repositórios padrão da maioria das distribuições Linux e com interface C++ mutável.
2. **wgpu-native (C-API / Mozilla / gfx-rs):** Biblioteca C sobre `wgpu`, porém com evolução rápida de cabeçalhos (`webgpu.h` v22+ com futures e callback info assíncronos) que geram incompatibilidades frequentes de compilação sem um SDK empacotado globalmente no ambiente.
3. **Bridge Rust com `wgpu` (`RUST_BRIDGE`):** Utiliza diretamente o ecossistema `wgpu` em Rust através de uma C-ABI privada, estritamente tipada e versionada (`coin_wgpu_ffi.h`), permitindo compilação reproduzível via Cargo (`cargo build --locked`), integração comprovada com drivers Vulkan e GPUs dedicadas (NVIDIA RTX 3060), e execução hermética.

Além disso, o modo `RECORDING` já fornece um rasterizador de software em CPU determinístico para ambientes de CI onde hardware de GPU não está disponível.

## Decisão

Adotamos a seguinte estratégia de backends para o Coin3D:
1. **`RUST_BRIDGE` é o backend de referência oficial e obrigatório para a Onda 1A:** Todas as capacidades de hardware da Onda 1A (pipelines WGSL, alocação de buffers, render passes e readback) são implementadas e validadas sobre este backend.
2. **`RECORDING` é o backend de referência determinística sem GPU:** Mantido para testes de cena, snapshots textuais estáveis e CI headless.
3. **`DAWN` e `WGPU_NATIVE` permanecem experimentais:** A infraestrutura de dispatch e os stubs C++ são mantidos na árvore para futura expansão quando pacotes de sistema estabilizados estiverem amplamente distribuídos.

## Consequências

- **Positivas:**
  - Garantia de execução real em GPU física em ambientes de desenvolvimento e produção com Rust/Cargo instalado.
  - ABI C hermética e versionada que isola o runtime Rust de qualquer vazamento de tipos ou ABI C++ no Coin.
  - Segurança de memória na fronteira FFI via `std::panic::catch_unwind` e checagem estrita de bounds.
- **Negativas / Mitigações:**
  - Exigência do compilador Rust (`cargo` / `rustc`) quando `COIN_WGPU_BACKEND=RUST_BRIDGE` for ativado. (Mitigação: `COIN_WGPU_BACKEND=RECORDING` continua disponível sem Rust, com o mínimo de C++11 do Coin).
  - Isolamento de artefatos de compilação: `CARGO_TARGET_DIR` deve sempre apontar para dentro do diretório de build do CMake para não poluir o repositório Git.
