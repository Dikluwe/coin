# UV projetivo — validação Linux/NVIDIA

Base: `f4cebd50b5edf9df3f0158090febe35c6128b617`, branch
`codex/coin-render-transform-performance`, campanha de 2026-10-05.

[summary.json](summary.json) contém comandos, ambiente, contagens, métricas RGB
por oracle e hashes dos fontes, binários e logs. O contrato e os limites estão
em [coin-render-projective-uv-contract.md](../../coin-render-projective-uv-contract.md).

As três variantes passaram: wgpu/Vulkan, BGFX/Vulkan e BGFX/OpenGL. Os gates
UV totalizam 414 comparações nativas CoinGL/GPU, 12 projeções equivalentes da
unidade 7 para 0, 15 referências source-over equivalentes para peeling com
uma camada e 150 comparações analíticas GPU com oito unidades simultâneas.
Essas referências têm contagens separadas e rótulos próprios nos logs.

Os gates finais preservam MAE RGB da região ativa <=1 e o limite existente
de outliers acima de três níveis. Qualificam amostragem projetiva, matrizes,
R/Q, perspectiva, reuso A/B/A, linhas/pontos, políticas de sombras e RTT nos
perfis descritos. O alpha geral do framebuffer não é certificado.

Os logs de pilotos são falhas conservadas, seguidas por correções de fixture:
limite de quatro unidades fixas GL, alinhamento de raster, admissão de sombras,
shader nativo DirectionalLight, herança externa da matriz no FBO e programa
ARB de peeling. Não houve ampliação das tolerâncias. A herança externa no
produtor RTT permanece fora da qualificação, registrada como limitação.

Regressões: fragmentos 348, texto/imagem 279 e marcadores 363 comparações GPU;
Core 58, Rust 38, captura 2, referência nativa de sombras 3, multidispositivo
wgpu 1 e draw style BGFX/OpenGL 1. O teste CTest de draw style fixa Vulkan;
o run OpenGL separado executa diretamente seu binário.

`run.py` reproduz os comandos usando os caminhos absolutos desta máquina;
`archive.py` consolida os resultados. `abi-probe.cpp` e `abi-layout.json`
registram protocolo 45 e tamanhos privados do transporte. Logs arquivados
removem espaços ao fim da linha e linhas vazias no fim do arquivo; hashes brutos e arquivados são
conservados no resumo. Não foi executado benchmark de desempenho.
