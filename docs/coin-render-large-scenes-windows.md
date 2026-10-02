# Cenas grandes no Windows — 2026-10-02

Branch local: `codex/coin-render-large-scenes`, baseada em `4e72bfa2ba`.
Windows 10, i5-4670K, 16 GiB RAM, GTX 1060 6 GB, driver 581.08.

## Cena reproduzível

`examples/coinrender/generate_large_scene.py` cria uma cidade Open Inventor
determinística, com seed 136, oito materiais, duas luzes direcionais, ruas entre
quarteirões, alturas variadas e uma base. Cada edifício referencia o mesmo nó
Cube, com transformação e material próprios. Compartilhar esse nó não implica
instancing na GPU. O arquivo não contém câmera: o benchmark enquadra o conjunto
com a mesma câmera perspectiva para todos os renderizadores.

| Grade | Edifícios | Triângulos na cena, incluindo a base | Arquivo |
|---|---:|---:|---:|
| 10 × 10 | 100 | 1.212 | 13.798 bytes |
| 50 × 50 | 2.500 | 30.012 | 327.589 bytes |
| 100 × 100 | 10.000 | 120.012 | 1.313.169 bytes |
| 200 × 200 | 40.000 | 480.012 | 5.285.711 bytes |

Os arquivos gerados e capturas ficam em `H:/Git/coin/build/large-scenes` neste
computador. O gerador é versionado; os arquivos grandes são regeneráveis.

## Resultados iniciais

Renderização offscreen a 1024 × 1024, incluindo RGBA readback síncrono.
São medidas de cena estática, não FPS de uma janela interativa. Os logs completos
estão em [validation/large-scenes-windows](validation/large-scenes-windows/).

| Caminho | Edifícios | Aquecimento / quadros medidos | Mediana | P95 | Resultado |
|---|---:|---:|---:|---:|---|
| wgpu/D3D12 | 100 | 2 / 8 | 17,08 ms | 17,94 ms | Passou |
| wgpu/D3D12 | 2.500 | 2 / 8 solicitados | — | — | Erro de bind group no primeiro quadro |
| wgpu/D3D12 | 10.000 | 3 / 12 solicitados | — | — | Mesmo erro |
| wgpu/Vulkan | 10.000 | 3 / 12 | 387,58 ms | 467,67 ms | Passou |
| wgpu/Vulkan | 40.000 | 2 / 8 | 1.340,99 ms | 1.371,11 ms | Passou |
| Coin/OpenGL | 10.000 | 3 / 12 | 17,67 ms | 22,62 ms | Passou |
| Coin/OpenGL | 40.000 | 8 / 20 | 35,55 ms | 36,99 ms | Passou |

No ensaio OpenGL de 40 mil com apenas dois quadros de aquecimento, o máximo foi
540,80 ms. Com oito quadros de aquecimento, o máximo foi 37,22 ms; ambos os logs
são preservados. Uma comparação visual das capturas de 10 mil objetos mostrou
diferença média absoluta RGB de 0,0145 / 0,0380 / 0,0232 em canais de 0 a 255.
Isso é uma observação da imagem completa, não uma tolerância de regressão.

O D3D12 reportou `BindGroup with 'Draw eight-unit texture bindings' label is
invalid` em `RenderPass::set_bind_group`. O mesmo gerador funciona no controle
pequeno e nos outros caminhos. A causa do erro de recursos ainda exige
diagnóstico; não foi corrigida nem convertida em aprovação.

O ensaio Vulkan de 40 mil demorou mais de nove minutos para encerrar, apesar
de os oito quadros medidos totalizarem 10,65 s. A mediana aquecida não representa
esse custo total. A maior amostra de memória do processo observada durante
a execução foi 553.070.592 bytes de peak working set; não é uma medida de VRAM.
As capturas de 40 mil em GL/Vulkan tiveram diferença média RGB de
0,0145 / 0,0304 / 0,0229 por canal.
Carregamento, primeiro quadro e cleanup passaram a ter marcas de tempo no
benchmark para localizar esse custo em uma próxima execução. As marcas e
a exportação PPM foram verificadas em um controle D3D12/OpenGL de 100 objetos
a 256 × 256, no binário final; esse controle não substitui a medição grande.

A suíte anterior 99/99 usa outras fixtures e não certifica essa carga de cenas
grandes. BGFX não foi executado: seu conector Windows ainda não está implementado.

## Reproduzir

```powershell
python examples/coinrender/generate_large_scene.py city-40000.iv --grid 200
cmake --build build-win --config Release --target coin_render_gl_benchmark
$env:WGPU_BACKEND = 'vulkan' # trocar por dx12 para D3D12
& ./build-win/bin/coin_render_gl_benchmark.exe --scene city-40000.iv `
  --backend wgpu --size 1024 --warmup 2 --frames 8 `
  --image-output city-40000-vulkan.ppm
# Para a referência: --backend gl. Aquecimento usado na repetição: 8; quadros: 20.
```

`--image-output` salva PPM fora do intervalo medido, corrigindo a origem vertical
do readback GL. Só é aceito no modo síncrono com cópia RGBA. O benchmark também
registra carregamento, primeiro quadro e cleanup, separadamente da mediana aquecida.
