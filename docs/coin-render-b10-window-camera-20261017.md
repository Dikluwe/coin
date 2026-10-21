# B10 — janela dinâmica e custo do patch de câmera LESS

A cena city de 40 mil objetos foi repetida no alvo de janela X11 BGFX/OpenGL
na AMD Renoir, com 512×512, geometria 100% dinâmica, 10 quadros de aquecimento
e 20 medidos por processo. O A/B intercalou três processos da rota genérica
(`COIN_RENDER_DISABLE_CUBE_RANGE_OVERLAY=1`) com três do undo compacto. O
readback RGBA solicitado ao alvo de janela foi idêntico byte a byte nos três
pares de quadros finais. A identificação OpenGL do host registra AMD Radeon
Graphics/radeonsi Renoir.

| Rota | Mediana das medianas de CPU por quadro | Mediana dos p95 |
| --- | ---: | ---: |
| Undo genérico | 209,624 ms | 237,308 ms |
| Undo compacto | 190,899 ms (−8,93%) | 198,736 ms |

O tempo inclui atualização de cena, renderização, apresentação solicitada e
readback. O benchmark não mede duração GPU nem latência física de scanout;
o readback é do alvo de janela, não uma captura XGetImage da tela.

Para medir o custo da correção B02 em `LESS`, a mesma cidade recebeu somente
animação de câmera. O trace BGFX/OpenGL mostrou `camera_patch=1`, um batch
instanciado de 40.001 instâncias, `world_space=0` e reuso do buffer de
geometria. Em 20 quadros medidos após cinco de aquecimento, forçar captura
completa com `COIN_RENDER_DISABLE_CAMERA_OVERLAY=1` custou 142,293 ms de
mediana na AMD/OpenGL e 139,934 ms na NVIDIA/Vulkan; o patch custou 10,982
e 10,880 ms, respectivamente. Os quadros 0 e 19 foram idênticos byte a byte
em cada GPU/API. O teste prova que o reenvio das matrizes de instâncias
introduzido por B02 ainda é muito mais barato que reconstruir esta cena.
Não extrapola para outros tamanhos, mutações mistas ou drivers.

[Logs, CSVs, capturas comprimidas, hashes e identificação OpenGL](validation/coin-render-b10-window-camera-20261017/verification.json).
B10 continua aberto para caudas de processos longos, GPU timing, tamanho
acima do limite BGFX, renderers de janela restantes e outras combinações.
