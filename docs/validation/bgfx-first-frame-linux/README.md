# Evidências — primeiro quadro BGFX

[Relatório técnico](../../coin-render-bgfx-first-frame-linux.md) e [padrão de comparação](../../coin-render-benchmark-standard.md).

Baseline `8072f6306f`; código depois `ef39f25856` + `1cc68c65f7`, HEAD `1cc68c65f7`. Os conteúdos compilados são os mesmos desses commits. A referência principal é CoinGL/NVIDIA da revisão local atual, na mesma NVIDIA RTX 3060 Laptop usada nas variantes da campanha.

## Campanha final

- [results.json](results.json): 21 processos novos, sete caminhos × três rodadas intercaladas, com 30 warmup e 120 quadros medidos. Dados por processo, checksums, pico RSS e hashes dos PPM da primeira rodada.
- [medians.json](medians.json): medianas por caminho do primeiro quadro, aquecido, p95, tempo desde `main` até a imagem e pico RSS em KiB.
- [comparison.json](comparison.json): variações antes/depois e comparação com CoinGL na mesma GPU. wgpu aquecido teve +2,81% nesta amostra; não se afirma invariância.
- [rgb.json](rgb.json): comparação dos PPM orientados 1024 × 1024. RGB antes/depois exato nos três backends experimentais; as diferenças pequenas contra CoinGL persistem.
- `logs/*-1.log`, `logs/*-2.log`, `logs/*-3.log`: logs completos de cada processo.
- [nvidia-rgb-comparison.png](nvidia-rgb-comparison.png): painel visual reduzido. As métricas RGB usam os PPM originais, sem redução nem ajuste de fundo/alpha.
- [manifest.json](manifest.json) e [host-baseline.json](host-baseline.json): revisões, hardware e hashes das bibliotecas/executáveis/cena. `images/*.png` preserva o RGB 1024 × 1024 da primeira rodada; o manifesto registra os hashes dos PPM originais e dos PNG correspondentes.
- [measure.py](measure.py) e [analyze.py](analyze.py): comandos, ambiente NVIDIA, ordem intercalada e cálculo reproduzível no ambiente local original. Adapte caminhos de build para outro checkout; o script de análise usa os PPM originais em `/tmp`.

O primeiro quadro é render + readback de cor síncrono + cópia para o consumidor. Ele exclui parsing, enquadramento, construção do target e capability probe; `result_since_main_ms` inclui a preparação anterior. RSS é o pico do processo inteiro, não memória GPU. Processo novo não significa caches de driver/sistema vazios.

## Validação de correção

- [controls/bgfx-core-tests.log](controls/bgfx-core-tests.log), [controls/wgpu-core-tests.log](controls/wgpu-core-tests.log), [controls/recording-core-tests.log](controls/recording-core-tests.log): 14 invocações CPU selecionadas, todas aprovadas, em três builds (6 + 5 + 3). Comandos em [verify-core.py](verify-core.py).
- [controls/gpu.json](controls/gpu.json) e logs `controls/bgfx-*CoinRender*.log` / `controls/wgpu-*CoinRender*.log`: 26 invocações GPU Common aprovadas, cobrindo fog, textura/RTT, draw style, composição, shadows, profundidade, clipping, iluminação, multitexture e transparência. A validação de profundidade wgpu/OpenGL usa AMD e não entra nas tabelas de tempo NVIDIA. Comandos em [verify-gpu.py](verify-gpu.py).
- [controls/bgfx-runtime-gpu.log](controls/bgfx-runtime-gpu.log): 18 invocações GPU BGFX aprovadas para features de superfície, transparência/profundidade, instancing, offscreen, janela e múltiplos targets/ciclos de vida. O log verbose registra os comandos.
- [controls/bgfx-instancing-depth-gpu.log](controls/bgfx-instancing-depth-gpu.log): duas reexecuções GPU de instancing/profundidade, aprovadas após a correção do viewport fora do target.
- [controls/dynamic.json](controls/dynamic.json) e logs `controls/*-dynamic-*.log` / `controls/*-material-dynamic-*.log`: seis pares antes/depois para câmera e material em BGFX/Vulkan, BGFX/OpenGL e wgpu/Vulkan; checksum e RGB finais exatos em cada par. Comandos em [dynamic.py](dynamic.py).

Uma invocação selecionada não representa a suíte inteira. Os logs vazios de alguns testes Common são execuções silenciosas bem-sucedidas; `controls/gpu.json` registra seu retorno. Não houve skip de GPU nesses grupos.

## Diagnósticos e pilotos

Os pilotos têm uma amostra por configuração, instrumentação de fases e perfis menores de aquecimento/medição. São evidência de investigação, separada das medianas finais de 21 processos.

- `ablations/reserve/*.log`: trace antes/depois e controles sem reserva de captura ou com reservas BGFX padrão. Separa captura/finalização, validação, preparo do target, lowering, upload, encode e readback; identifica payload instanced, contagem de instâncias/draws e capacidade dos buffers.
- [ablations/reserve/results.json](ablations/reserve/results.json): 335,90/274,87 ms (Vulkan/OpenGL) com reserva de captura, contra 385,98/340,14 ms sem ela, em um piloto. A dica é limitada a 64 nodes, dois níveis e 65.536 entradas. Comandos em [reserve-pilot.py](reserve-pilot.py).
- Reservas do runtime BGFX: restaurar os padrões mediu 330,63/270,66 ms no mesmo piloto, ligeiramente menor que o ajuste atual. **Sem ganho de latência isolado validado** para pools de 64 KiB e um encoder.

O relatório também registra investigações de cache de spans, latência do runtime BGFX (`swapChain.maxFrameLatency`, valores 1 e 3) e guardas iniciais SCREEN_DOOR/LEQUAL. Esses pilotos intermediários não foram preservados neste pacote, pois seus builds não tinham manifesto de hashes; seus valores não entram nas conclusões quantitativas finais.

As flags privadas de ablação estão nos scripts. A medição final desativa trace/debug e limpa opt-outs; conserva o contrato de readback síncrono. Cenas, binários e PPM grandes permanecem em `/tmp`, fora do Git; hashes permitem verificar os artefatos. Os PNG completos preservam o RGB dos PPM finais. Nenhuma alteração de master ou push faz parte desta campanha.
