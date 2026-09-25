# Avaliação BGFX sobre o `FramePlan` experimental

Esta branch adiciona um conector **BGFX headless (Vulkan ou OpenGL)** a
`CoinWgpuExperimental`. O nome histórico `SoWgpuRenderAction` permanece apenas
para compartilhar a travessia Coin e o `FramePlan`; BGFX não é wgpu-native,
nem transforma essa ação em uma API gráfica estável. `libCoin` e a ABI pública
do Coin 4 não são alteradas.

## Por que a arquitetura ajuda

O Wiring existente continua a capturar Open Inventor com `SoCallbackAction` e
entrega o mesmo `FramePlan`. `SoWgpuBgfxCore` valida um subconjunto e converte
cor/material, geometria e matrizes sem incluir headers BGFX. A Infra
`SoWgpuBgfxBackend` usa somente o plano convertido para criar shaders,
buffers, framebuffer, submeter e fazer readback. Shell e diagnósticos existentes
continuam a traduzir `UNSUPPORTED` e erros de backend. Assim a comparação
isola a troca do executor, sem duplicar um segundo scene graph.

## Perfil implementado

- Linux, BGFX com renderer Vulkan por padrão ou OpenGL com
  `COIN_BGFX_RENDERER=opengl`, alvo offscreen e um alvo ativo por processo.
- Triângulos indexados opacos, sem textura/fog, `BASE_COLOR`, viewport inteira;
  clear, teste de profundidade e readback **somente RGBA** síncrono.
- Ordem de draws preservada com `ViewMode::Sequential`; shaders SPIR-V e GLSL
  330 gerados por `shaderc` durante o build. O readback OpenGL é invertido
  por linhas para cumprir a mesma orientação RGBA do Vulkan.
- Um cache privado por target retém o plano convertido e os buffers de geometria
  quando a revisão não nula do `FramePlan`, dimensões e convenção de depth
  continuam iguais. O payload de geometria é limitado a 32 MiB; revisão zero
  nunca reutiliza. `COIN_WGPU_TRACE_PHASES=1` informa `resource_cache_hit`.
- `CAMERA_PATCH` preserva buffers GPU e atualiza somente MVPs para uma
  mudança de câmera validada; `COIN_BGFX_DISABLE_CAMERA_PATCH=1` permite
  medir o caminho completo no mesmo build.
- Profundidade não é publicada: chamar
  `SoWgpuRenderTarget::setDepthReadbackEnabled(FALSE)` antes de renderizar.
- Iluminação, alpha, linhas/pontos, textura, RTT direto, janela X11 e
  `applyAsync` retornam `UNSUPPORTED` em vez de aparentar paridade.
- `gpu_available=0` na query de capacidades BGFX porque `bgfx::init` é global
  e não há probe inofensivo; a disponibilidade real é comprovada por um
  `prepare`/frame offscreen. Os bits anunciados descrevem apenas o perfil.

O BGFX tem estado de processo e thread de API próprios. O conector atual
recusa alvos concorrentes; o alvo deve ser destruído na mesma thread que fez
`prepare`. Não deve ser carregado no mesmo processo de outro
usuário de BGFX; uma futura integração precisaria de ownership compartilhado.

## Build reproduzível

Instale um pacote CMake `bgfx::bgfx` e seu `shaderc` correspondente. O
adaptador foi desenvolvido com `bgfx.cmake` e BGFX em
`81d81fba72c42d348c589514c774bbfe01e110fa`. O build **não** baixa
dependências automaticamente. Exemplo após a instalação do BGFX em `$prefix`:

```sh
cmake -S . -B /tmp/coin-bgfx-release \
  -DCMAKE_BUILD_TYPE=Release -DCOIN_BUILD_WGPU=ON \
  -DCOIN_WGPU_BACKEND=BGFX -DCOIN_BUILD_TESTS=ON \
  -DCOIN_BUILD_WGPU_BENCHMARKS=ON \
  -DCMAKE_PREFIX_PATH="$prefix" \
  -DCOIN_BGFX_SHADERC_EXECUTABLE="$prefix/bin/shaderc" \
  -DCOIN_BGFX_SHADER_INCLUDE_DIR="$prefix/include/bgfx"
cmake --build /tmp/coin-bgfx-release --target WgpuBgfxCoreTest WgpuBgfxOffscreenTest wgpu_gl_benchmark -j4
ctest --test-dir /tmp/coin-bgfx-release -R '^WgpuBgfx' --output-on-failure
env EGL_PLATFORM=x11 \
  __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json \
  COIN_BGFX_RENDERER=opengl \
  /tmp/coin-bgfx-release/bin/WgpuBgfxOffscreenTest
env VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/radeon_icd.json \
  COIN_GLX_PIXMAP_DIRECT_RENDERING=1 \
  /tmp/coin-bgfx-release/bin/wgpu_gl_benchmark \
  --backend both --size 512 --warmup 8 --frames 30 \
  --readback color --rgba-output copy --scene /caminho/Assembly.iv
```

O teste Core roda sem GPU. O teste offscreen desenha um triângulo vermelho,
confere orientação, cache e `CAMERA_PATCH` nos dois renderers em processos separados.
Os testes de paridade do perfil WebGPU amplo não são uma afirmação de suporte
BGFX e devem continuar rodando no backend Rust/Recording.

## Medição inicial — 24–25/09/2026

Release, AMD Radeon Graphics RADV RENOIR (Vulkan) e radeonsi (GL), 512²,
`BASE_COLOR`, RGBA com readback, 8 frames de aquecimento e 30 medidos. A
cena é exportada do FreeCAD, **não** a viewport real. O benchmark executa
BGFX antes de GL; as linhas abaixo são observações de uma rodada, não uma
distribuição contrabalançada.

| Cena | BGFX mediana / p95 (ms) | GL mediana / p95 (ms) | BGFX/GL |
| --- | ---: | ---: | ---: |
| PartDesign | 1,672 / 1,747 | 0,337 / 0,359 | 4,96× |
| Assembly | 1,704 / 1,754 | 0,344 / 0,356 | 4,95× |

Antes do cache, o Assembly marcou 3,169 ms de mediana; após o cache marcou
1,704 ms (46% menor), mas ainda longe do GL. Num trace estático do Assembly,
`lower_ms` caiu de cerca de 0,5 ms para <0,001 ms e `upload_ms` para
<0,001 ms em hits; `read_wait_ms` continuou aproximadamente 1,7–2,1 ms.
Essa espera inclui progresso de frames/driver/GPU, **não** é uma query de
tempo de execução GPU isolada. `submit_frame_ms` é apenas a chamada BGFX
de entrega do frame. O pico RSS do Assembly em processos separados foi
85.284 KiB para BGFX e 95.756 KiB para GL; isso inclui bibliotecas/runtime
distintos e não comprova menor uso de memória GPU.

Qualidade visual `BASE_COLOR`, 512², GL alinhado por inversão de linhas:

| Cena | MAE RGB | IoU de silhueta |
| --- | ---: | ---: |
| PartDesign | 0,656 | 0,999978 |
| Assembly | 0,907 | 1,000000 |
| EngineBlock | 0,640 | 0,999968 |
| BIM | 0,681 | 0,999988 |

O gate simples MAE ≤1 e IoU ≥0,99 passou, mas a MAE é maior do que a
paridade WebGPU já documentada para EngineBlock e BIM. Converter a cor de
vértice de UNORM8 para float não reduziu a MAE de EngineBlock (0,640 em
ambas as variantes); a diferença não deve ser atribuída só à quantização
da cor do material. Imagens PPM desta rodada foram geradas apenas em `/tmp`.

## CAMERA_PATCH e diagnóstico do gargalo — 25/09/2026

Quando o Core comprova que só a câmera mudou, o Wiring passa
`SoWgpuFrameReuseDecision::CAMERA_PATCH` ao BGFX. O Core recalcula apenas
as matrizes dos draws; a Infra mantém vértices, índices e buffers GPU.
Revisão-base, tamanho e convenção de profundidade precisam coincidir; caso
contrário, há lowering completo. A revisão do cache só avança após readback
bem-sucedido. `COIN_BGFX_DISABLE_CAMERA_PATCH=1` desliga apenas essa
otimização para comparação A/B no mesmo binário. O trace informa
`camera_patch`, `resource_cache_hit` e `read_wait_frames`.

Release, mesma AMD e cenas exportadas, 512², `BASE_COLOR`, RGBA com
readback, 8 warmup + 60 frames de câmera móvel por rodada. Três rodadas
por modo, intercaladas; valores abaixo são as medianas das medianas/p95
de cada rodada, não percentis agregados de 180 frames:

| Cena | BGFX sem patch mediana / p95 (ms) | BGFX com patch mediana / p95 (ms) | Interpretação |
| --- | ---: | ---: | --- |
| PartDesign | 1,944 / 2,332 | 1,936 / 2,474 | Diferença dentro da variação |
| Assembly | 3,080 / 3,599 | 1,931 / 2,419 | Mediana 37,3% menor; p95 32,8% menor |

Reprodução (repetir para `PartDesign.iv`):

```sh
SCENE=/caminho/Assembly.iv
for disabled in 1 0 0 1 1 0; do
  env VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/radeon_icd.json \
    COIN_GLX_PIXMAP_DIRECT_RENDERING=1 \
    COIN_BGFX_DISABLE_CAMERA_PATCH="$disabled" \
    /tmp/coin-bgfx-release/bin/wgpu_gl_benchmark \
    --backend both --size 512 --warmup 8 --frames 60 --dynamic \
    --readback color --rgba-output copy --scene "$SCENE"
done
```

O GL permaneceu em cerca de 0,4 ms/frame nessas rodadas: o patch melhora
BGFX, mas não o torna competitivo com GL nesse perfil. No Assembly, o trace
passou de aproximadamente 0,7–1,2 ms de lowering e 0,1–0,6 ms de upload
por frame para ~0,002 ms e <0,001 ms, respectivamente. O pico RSS em
quatro processos separados foi 86.988–87.536 KiB sem patch e
85.060–85.196 KiB com patch; isso não mede memória GPU. O teste Vulkan
offscreen compara pixels do patch e do lowering completo e exige igualdade
exata em 32². A equivalência visual da cena FreeCAD com câmera móvel ainda
não foi medida em imagens exportadas.

Com `COIN_WGPU_TRACE_PHASES=1 COIN_WGPU_GPU_TIMESTAMPS=1`, o conector
avança frames BGFX adicionais, **só para diagnóstico**, até obter a query
Vulkan do frame submetido. `gpu_frame_ms` é a duração GPU relatada pelo
BGFX; `gpu_query_drain_ms`/ `gpu_query_frames` registram a perturbação.
Numa rodada de 30 frames de Assembly com patch, a mediana foi ~0,126 ms
para o frame GPU e ~1,85 ms para a espera CPU do readback; três avanços
BGFX foram necessários para publicar cada readback. Não se deve subtrair
esses tempos como se fossem estágios seriais independentes. Medições de
performance comparáveis com GL devem deixar `COIN_WGPU_GPU_TIMESTAMPS`
desligado.

## Avaliação OpenGL do BGFX — 25/09/2026

O mesmo build Release e o mesmo `FramePlan` agora executam BGFX/OpenGL por
`COIN_BGFX_RENDERER=opengl`; Vulkan continua sendo o padrão. O teste
`WgpuBgfxOffscreenTest` passou nos dois renderers. A primeira execução OpenGL
revelou readback invertido; a Infra agora normaliza as linhas antes de publicar
os pixels. Não há alteração de ABI pública do Coin 4.

Este computador tem AMD e NVIDIA. Para comparar com Coin/GL na AMD, a medição
fixou `EGL_PLATFORM=x11`,
`__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json`
e o ICD RADV. `glxinfo -B` e a seção X11 de `eglinfo -B` identificaram
radeonsi/RENOIR; o BGFX não reportou PCI vendor/device id para OpenGL, então
a atribuição da GPU BGFX decorre da seleção EGL Mesa/X11, não de telemetria
direta do BGFX. Sem fixar essa seleção, uma comparação nesta máquina é ambígua.

Release, 512², `BASE_COLOR`, RGBA com readback, cenas `.iv` exportadas do
FreeCAD (não a viewport real), 8 frames de aquecimento e 60 medidos por
processo. Três rodadas intercaladas por cena e renderer. Cada valor abaixo é
a mediana das três medianas ou dos três p95 por rodada; não é o percentil
agregado de 180 frames. Coin/GL e BGFX foram medidos em processos separados.

| Cena | BGFX/OpenGL mediana / p95 (ms) | Coin/GL mediana / p95 (ms) | BGFX/Vulkan mediana / p95 (ms) |
| --- | ---: | ---: | ---: |
| PartDesign | 0,364 / 0,450 | 0,326 / 0,345 | 1,688 / 1,866 |
| Assembly | 0,376 / 0,446 | 0,345 / 0,411 | 1,733 / 2,033 |

BGFX/OpenGL ficou aproximadamente 4,6× mais rápido que BGFX/Vulkan na
mediana deste perfil, mas ainda 9–12% mais lento que Coin/GL. A causa exata
da diferença entre os renderers BGFX não está isolada: o readback síncrono,
progresso de frames e driver são medidos juntos; trace/timestamp perturba
fortemente o tempo. Não atribuir todo o ganho à execução GPU ou ao row flip.

Pico RSS mediano dos três processos por cena, em KiB:

| Cena | BGFX/OpenGL | Coin/GL | BGFX/Vulkan |
| --- | ---: | ---: | ---: |
| PartDesign | 109.052 | 91.760 | 80.060 |
| Assembly | 114.200 | 95.920 | 85.204 |

RSS inclui bibliotecas/runtime/driver; não representa somente memória GPU.
Na comparação visual das quatro cenas a 512², o IoU de silhueta foi 1,0
após inverter as linhas GL e a MAE RGB foi PartDesign 0,655, Assembly 0,907,
EngineBlock 0,638 e BIM 0,681. A diferença visual é essencialmente a mesma
do BGFX/Vulkan, sem evidência de ganho de qualidade pelo backend OpenGL.

Reprodução após o build acima (substituir o caminho da cena e repetir para
`PartDesign.iv` e `Assembly.iv`):

```sh
SCENE=/caminho/Assembly.iv
for backend_renderer in bgfx:opengl gl:opengl bgfx:vulkan; do
  backend=${backend_renderer%:*}; renderer=${backend_renderer#*:}
  env EGL_PLATFORM=x11 \
    __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json \
    VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/radeon_icd.json \
    COIN_BGFX_RENDERER="$renderer" COIN_GLX_PIXMAP_DIRECT_RENDERING=1 \
    /usr/bin/time -f 'peak_rss_kib=%M' \
    /tmp/coin-bgfx-release/bin/wgpu_gl_benchmark \
    --backend "$backend" --size 512 --warmup 8 --frames 60 \
    --readback color --rgba-output copy --scene "$SCENE"
done
```

## Correção da cor de fundo — 25/09/2026

A análise pixel a pixel das imagens anteriores encontrou uma causa única
para a MAE do BGFX/OpenGL: todos os pixels de malha eram iguais aos do
Coin/GL, mas todo pixel de fundo tinha `(26,26,26)` em BGFX contra
`(25,25,25)` em Coin/GL. O Core arredondava o clear de `0,1` para um
`uint32_t` antes da chamada ao BGFX. Agora conserva os quatro floats do
`FramePlan` e a Infra usa a paleta float de clear do BGFX. Isso preserva
a conversão final do renderer, inclusive no caminho de câmera em cache;
uma mudança pequena de clear já invalida o `CAMERA_PATCH`.

Nova comparação Release a 512², mesmas cenas `.iv`, BGFX/OpenGL ou Vulkan
contra Coin/GL com inversão de linhas, na seleção AMD/Mesa/X11 acima:

| Cena | MAE RGB BGFX/OpenGL antes → depois | MAE RGB BGFX/Vulkan depois | IoU OpenGL depois |
| --- | ---: | ---: | ---: |
| PartDesign | 0,655 → 0 | 0,00136 | 1,0 |
| Assembly | 0,907 → 0 | 0 | 1,0 |
| EngineBlock | 0,638 → 0 | 0,00180 | 1,0 |
| BIM | 0,681 → 0 | 0,00068 | 1,0 |

O zero significa **RGB idêntico** nessas quatro imagens; não afirma
igualdade de alpha, depth, outros materiais ou da viewport real do FreeCAD.
No Vulkan, restaram apenas 0–3 pixels divergentes por imagem, em bordas.
O teste Core agora exige clear float sem quantização e recusa um camera patch
se o clear mudar mesmo que a versão anterior arredondasse para o mesmo byte.

Na repetição de tempo com 8 warmup e 3 × 60 frames em processos separados,
mediana das três medianas e dos três p95 por rodada (ms):

| Cena | BGFX/OpenGL | Coin/GL | BGFX/Vulkan |
| --- | ---: | ---: | ---: |
| PartDesign | 0,486 / 0,992 | 0,411 / 0,806 | 2,149 / 2,740 |
| Assembly | 0,541 / 0,986 | 0,445 / 0,886 | 2,224 / 2,927 |

Pico RSS mediano (KiB) PartDesign: 108.896 BGFX/OpenGL, 91.748 Coin/GL,
79.876 BGFX/Vulkan. Assembly: 114.052, 95.980 e 85.216, respectivamente.
Não é uma medida de memória GPU.

Todos os caminhos ficaram mais lentos que na rodada anterior, portanto esta
amostra não isola o custo da mudança de clear e não sustenta alegação de
ganho ou regressão de performance. O ganho comprovado aqui é visual.

## Próximo gate de produto

Antes de aceitar BGFX como alternativa, ampliar o perfil sem falsos positivos:
profundidade de saída, iluminação, materiais por vértice, textura, fog,
transparência e múltiplos alvos; validar culling, convenção de coordenadas e
orientação do readback contra GL. Em Release e na mesma GPU AMD, medir ao menos
PartDesign e Assembly `BASE_COLOR` a 512²: mediana, p95, erro visual médio,
RSS/pico de recursos, traversal/FramePlan, lowering, upload, submit, GPU e
readback separados. O readback síncrono do BGFX avança vários frames e não
deve ser confundido com apenas tempo de execução GPU. Apesar do ganho para
câmera móvel no Assembly, ainda não há vantagem sobre GL neste perfil.
