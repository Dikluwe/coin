# Avaliação BGFX sobre o `FramePlan` experimental

Esta branch adiciona um conector **BGFX/Vulkan headless** a
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

- Linux, BGFX com renderer Vulkan, alvo offscreen e um alvo ativo por processo.
- Triângulos indexados opacos, sem textura/fog, `BASE_COLOR`, viewport inteira;
  clear, teste de profundidade e readback **somente RGBA** síncrono.
- Ordem de draws preservada com `ViewMode::Sequential`; shaders SPIR-V gerados
  por `shaderc` durante o build. Não há shader binário vendorizado.
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
env VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/radeon_icd.json \
  COIN_GLX_PIXMAP_DIRECT_RENDERING=1 \
  /tmp/coin-bgfx-release/bin/wgpu_gl_benchmark \
  --backend both --size 512 --warmup 8 --frames 30 \
  --readback color --rgba-output copy --scene /caminho/Assembly.iv
```

O teste Core roda sem GPU. O teste offscreen desenha um triângulo vermelho e
confere o pixel central; se Vulkan não inicializar, relata indisponibilidade.
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
