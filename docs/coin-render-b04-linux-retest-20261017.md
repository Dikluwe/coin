# B04 — repetição das cenas visuais Linux no checkout atual

A campanha original de 10/10/2026 registrou **16 PASS e 32 FAIL em 48 processos Linux**. Recompilei sem alterar o `main.cpp` original contra os builds atuais BGFX e wgpu desta branch e repeti as 48 células: **48 PASS, 0 FAIL**. Os 32 resultados antigos permanecem FAIL no ledger histórico; o reteste aparece por ID na [camada atual](inventories/coin-render-b04-linux-retest.json) e no [ledger legível](coin-render-visual-case-ledger.md).

Cada processo preservou 1280×960, janela X11 contra offscreen da mesma API/política, um warmup e dois frames, câmera móvel ou fixa conforme o caso, RGB máximo exigido **0** e cobertura acima de 10%. Foram exercitados BGFX e wgpu, Vulkan/NVIDIA RTX 3060 Laptop e OpenGL/AMD Renoir, políticas de sampling native e portable. As cenas incluem cidade de 40 mil edifícios, cidade de um milhão de instâncias lógicas, terreno de um milhão de triângulos e 24 sólidos com textura/transparência. A repetição da cidade com câmera fixa preserva os dois heights originais. O fixture não mede latência de apresentação nem compara contra CoinGL; prova consistência janela/offscreen neste contrato.

A auditoria independente descompactou os **192 PPMs** das 96 comparações, refez diferença RGB e cobertura, conferiu hashes, API/GPU e os logs históricos por ID. As células anteriormente falhas de câmera, upload do terreno, BGFX/OpenGL e primeira captura wgpu/OpenGL passaram nesta campanha. Uma passagem exata de wgpu/OpenGL não explica a ocorrência intermitente histórica do B03.2; esse diagnóstico continua separado.

O resultado Windows permanece **30 PASS/42 FAIL em 72 processos históricos**; nenhum deles foi executado neste host Linux. Esta campanha encerra a reconciliação visual B04 nas células Linux disponíveis, mas não encerra B04 para Windows nem qualifica outros drivers, plataformas ou o universo de recursos das etapas 3 e 4.

[Recibos, fontes, hashes e verificação](validation/coin-render-b04-20261017/verification.json). Repetir a auditoria: `python3 scripts/coinrender/audit_b04_linux_retest.py docs/validation/coin-render-b04-20261017`.
