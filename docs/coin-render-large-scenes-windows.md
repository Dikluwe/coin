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
grandes. Esses ensaios iniciais foram feitos com wgpu. O conector BGFX/Windows
foi implementado em seguida, com resultados próprios registrados abaixo.

## BGFX/Direct3D12 no Windows

As três cargas grandes passaram em 1024 × 1024, com quatro quadros de
aquecimento e oito medidos, incluindo readback síncrono:

| Edifícios | Mediana | P95 |
|---|---:|---:|
| 2.500 | 20,75 ms | 22,59 ms |
| 10.000 | 423,81 ms | 451,76 ms |
| 40.000 | 1.630,15 ms | 1.672,61 ms |

O novo backend renderizou a cidade de 40.000 edifícios em 1024 × 1024, com
quatro quadros de aquecimento e oito medidos: mediana 1.630,15 ms, P95
1.672,61 ms. O primeiro quadro levou 4.023,4 ms e o cleanup, 132,71 ms.
A imagem é pixel a pixel idêntica à captura wgpu/Vulkan da mesma cidade.

Essa execução já usa um índice por matriz para a preparação dos estados de
desenho. A implementação anterior fazia uma busca quadrática e o primeiro
teste BGFX de 40 mil foi interrompido ainda durante essa preparação.
O orçamento da biblioteca BGFX foi compilado para 131.072 chamadas de desenho.
Esse resultado confirma funcionamento de D3D12 nessa carga; o tempo por
quadro ainda é alto e não estabelece vantagem de desempenho sobre OpenGL.

O build, os testes Win32 e a qualificação dirigida estão documentados em
[CoinRender BGFX no Windows](coin-render-bgfx-windows.md).

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
