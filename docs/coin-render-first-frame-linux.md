# Primeiro quadro do CoinRender no Linux — 2026-10-03

Correção na branch `codex/coin-render-first-frame`, baseada em `cf3db7ff01`
(`codex/coin-render-large-scenes`).

## Causa e correção

A captura copiava os mesmos 24 vértices e 36 índices de Cube para cada ocorrência.
O replay existente evitava gerar primitivas novamente, mas ainda expandia seus dados
na CPU. A cidade de 40 mil prédios tinha 960.024 vértices e 1.440.036 índices no plano.
A captura e a preparação do primeiro quadro pagavam esse custo antes de poder usar
os caches dos quadros seguintes. Conversão, recursos GPU e espera do readback também
contribuem para a diferença entre o primeiro quadro e os quadros aquecidos.

O builder agora compartilha faixas imutáveis de geometria entre Cubes nativos
compatíveis com o mesmo material. O cache é limitado a 32 materiais, reiniciado a
cada frame e invalidado ao aprender um template de dimensões ou normal binding
diferentes. Transformes, materiais, profundidade, ordenação e identidade dos draws
continuam sendo capturados por ocorrência. Ocorrências consecutivas preservam
o draw unido, repetindo seus índices sem alterar faixas de outras ocorrências.
Texturas, funções de coordenadas, subclasses e callbacks observadores mantêm as
restrições anteriores do replay.

O agrupamento opaco WGPU aceita faixas compartilhadas e expande cada ocorrência
no destino com seu próprio model/view e normal matrix. A sequência inteira é
qualificada antes de alterar os dados. As faixas originais e o frame Coin permanecem
imutáveis; estados incompatíveis usam os draws normais. A câmera móvel mantém o
rebuild existente do batch, evitando posições antigas. BGFX já suportava faixas
compartilhadas no lowering. Não foi introduzido instancing GPU.

## Método

- Builds Release/Ninja, BGFX e RUST_BRIDGE, com testes e benchmarks habilitados.
- Cidade determinística, seed 136, grade 200 × 200, 480.012 triângulos.
- Offscreen 1024 × 1024, RGBA readback síncrono, publicação por cópia.
- Três pares alternados antes/depois, processos novos, 4 quadros de aquecimento e 8 medidos.
- Primeiro quadro = primeiro render/apply com captura e readback. Parsing, enquadramento
  da câmera e a sondagem inicial de disponibilidade ficam fora desse intervalo.
- NVIDIA RTX 3060 Laptop, driver 610.57.04: BGFX/Vulkan, BGFX/OpenGL, WGPU/Vulkan.
- AMD Renoir Radeon, Mesa 25.2.8: WGPU/OpenGL. Comparar antes/depois no mesmo caminho;
  o resultado AMD não representa uma comparação de GPUs.
- Caches do sistema/driver preservados, sem simular reboot. Nenhuma compilação ou
  outra execução de teste GPU concorria com os pares finais. O piloto com compilação
  concorrente foi descartado da tabela.

## Resultados

Tempos abaixo são medianas das três execuções. “Aquecido” é a mediana das três
medianas de oito quadros de cada processo.

| Caminho | Primeiro antes | Primeiro depois | Redução | Aquecido antes/depois |
|---|---:|---:|---:|---:|
| bgfx-vulkan | 848.36 ms | 688.82 ms | 18.81% | 3.10/3.03 ms |
| bgfx-opengl | 819.84 ms | 649.87 ms | 20.73% | 3.12/3.16 ms |
| wgpu-vulkan | 641.49 ms | 447.38 ms | 30.26% | 8.47/8.22 ms |
| wgpu-opengl | 660.68 ms | 460.55 ms | 30.29% | 11.45/11.62 ms |

Os seis processos de cada caminho produziram o mesmo checksum RGBA FNV64.
As capturas PPM do primeiro par também têm SHA-256 idêntico antes/depois.
As pequenas diferenças dos tempos aquecidos são observações destas execuções;
a correção visa a captura e o primeiro quadro, sem estabelecer uma garantia de FPS.

### Fases e memória do plano

Uma execução diagnóstica separada em Vulkan, 1/1 quadros, reportou:

| Fase | BGFX antes/depois | WGPU antes/depois |
|---|---:|---:|
| Captura | 353,54/208,27 ms | 365,06/207,99 ms |
| Montagem do plano | 38,17/28,95 ms | 34,72/28,98 ms |
| Backend completo | 464,97/451,24 ms | 223,41/210,63 ms |

O plano corrigido contém **216 vértices e 324 índices**: nove malhas (base e oito
materiais). Os dados de vértices caíram de 96.002.400 para **21.600 bytes**; os de
índices, de 5.760.144 para **1.296 bytes**. Os 40.001 draws e estados continuam no
plano. O payload de estados é 65.601.640 bytes. A geometria GPU ainda é expandida
por ocorrência; essa economia é de captura CPU, não uma medida de redução de VRAM.

BGFX ainda gastou 136,65 ms no lowering, 155,88 ms na espera do readback e
preparação adicional do alvo/backend. WGPU gastou 136,74 ms no empacotamento.
A inicialização e a expansão GPU continuam deixando o primeiro quadro mais lento
que os quadros estáticos aquecidos.

## Validação

As 24 execuções de testes selecionadas terminaram com código zero:

- Cinco testes core no build BGFX: Action, FrameCore, FrameReuseCore,
  IndexedGeometryCore e BgfxCore. Dois no build WGPU: Action e FfiFrame.
- Seis suites no BGFX/Vulkan e sete no WGPU/Vulkan: DepthContract CPU,
  Texture, DrawStyle, Composition, ShadowReference com GPU obrigatória,
  Transparency e, no WGPU, Material. Os logs não reportaram skips.
- DepthContract `--gpu` nos quatro caminhos da tabela de desempenho.

Os novos testes comparam o stream expandido com captura completa, incluindo
materiais alternados, draws unidos, dimensões alteradas entre frames e
transformes. O teste FFI percorre os 768 vértices e índices de 256 ocorrências
com geometria compartilhada e não compartilhada, confere normais/materiais,
câmera móvel e um estado incompatível no fim da sequência.

Quatro controles antes/depois em Vulkan (BGFX e WGPU, câmera e material animados)
usaram 10 mil prédios, 1024 × 1024 e 1/2 quadros. Todos conservaram o checksum
RGBA. Resultados completos em `controls.json`; estes são controles funcionais,
sem qualificar desempenho dinâmico.

## Reprodução

```sh
python3 examples/coinrender/generate_large_scene.py city-40000.iv --grid 200 --seed 136
COIN_BGFX_RENDERER=vulkan bin/coin_render_gl_benchmark \
  --backend bgfx --scene city-40000.iv --size 1024 --warmup 4 --frames 8
WGPU_BACKEND=vulkan bin/coin_render_gl_benchmark \
  --backend wgpu --scene city-40000.iv --size 1024 --warmup 4 --frames 8
```

Para NVIDIA PRIME foram usados `__NV_PRIME_RENDER_OFFLOAD=1`,
`__GLX_VENDOR_LIBRARY_NAME=nvidia` e o ICD NVIDIA. Para WGPU/OpenGL AMD,
`WGPU_BACKEND=gl`, sem essas variáveis PRIME, com o ICD Radeon.

Logs e resultados estruturados: [validation/first-frame-linux](validation/first-frame-linux/).
A medição não qualifica Direct3D12, janelas nativas nem o caminho Coin/OpenGL legado.
