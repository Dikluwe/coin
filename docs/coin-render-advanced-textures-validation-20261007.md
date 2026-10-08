# Texturas avançadas — validação Linux de 2026-10-07

Entrega do [perfil P07/P24 avançado](coin-render-advanced-textures-profile.md),
na continuação `codex/coin-render`, a partir de `ae2e64c41f4e75c4255df7399afb2463696c0b30`.
Fontes, comandos, hashes e resultados ficam em
[`docs/validation/advanced-textures-20261007`](validation/advanced-textures-20261007/summary.json).
Os builds permanecem em `/mnt/Laranja/Git/externos/coin-render-artifacts`.

## Resultado do perfil novo

| Backend/API | Adaptador reportado | Checagens CPU/GPU | Resultado |
|---|---|---:|---|
| wgpu Vulkan | AMD RADV Renoir, 1002:1638 | 221 | PASS |
| wgpu Vulkan | NVIDIA RTX 3060 Laptop, 10de:2560 | 221 | PASS |
| wgpu OpenGL | AMD radeonsi Renoir, vendor 1002, device ID ausente | 221 | PASS |
| BGFX Vulkan | AMD, 1002:1638 | 216 | PASS |
| BGFX Vulkan | NVIDIA, 10de:2560 | 216 | PASS |
| BGFX OpenGL | vendor/device 0000:0000 | 215 | PASS no runtime OpenGL; identidade física não certificada |

Os números são checagens, não cenas nem frames exclusivamente GPU: cada execução
inclui os controles CPU. PASS inclui as recusas explícitas esperadas no contrato,
como NPOT/mips direto BGFX e sampler compartilhado base/mips BGFX/OpenGL. Não
significa que esses casos recusados passaram a ser suportados. Esta campanha
não qualifica Android, Windows, D3D12, Metal, FreeCAD ou outra GPU.

O teste `CoinRenderAdvancedTextureTest` cobre:

- binary16 por padrões de bits independentes, ties-to-even, NaN/Inf recusados;
- média SRGB preto/branco → RGB 188, alpha 128; média NPOT 3×1 → 85;
- cinco formatos, nível base/minificação, cache com bytes iguais e formatos distintos;
- modelos MODULATE/REPLACE/DECAL/BLEND em unidades 0/3, alpha linear SRGB/BC3/HDR;
- captura autoral de BC3, tamanho desalinhado recusado, recuperação NPOT,
  qualidade zero/reativação e limites anisotrópicos;
- faixas direcionais com contraste isotrópico zero e anisotrópico acima de 100;
- samplers base/mips compartilhando uma imagem, com controle numérico azul 128
  e recusa atômica da combinação no BGFX/OpenGL;
- RTT HDR16 base/mips sob minificação: clear 2/0,5/0,25 modulado por 0,25 produz
  RGBA8 RGB 128/32/16, provando preservação acima de 1 antes da composição;
- produtor RTT NPOT 3×5 não uniforme no wgpu, com mip final RGB 85;
- captura pública `SoSceneTexture2::RGBA16F`, HDR32 recusado e recuperação;
- fault wgpu 306 na codificação de mips, preservação de pixels/serial e recuperação.

Os testes `CoinRenderRttProfileTest --mips` e `--mips-direct` passaram nas seis
células. Cada rota verifica sete controles de limiar estrito >0,5, desligamento,
reativação, NPOT/admissão, dimensão inválida sem publicação e recuperação. Os
controles POT minificados mantêm a tolerância anterior de MAE≤4 contra CoinGL,
com máximo MAE RGB observado **0** nesta matriz. O perfil BGFX direto valida a
recusa NPOT; não faz uma conversão staged silenciosa.

## Diagnósticos que mudaram a implementação

O wgpu/OpenGL entregava níveis inferiores incorretos quando as views de fonte e
saída usavam a mesma textura. O [controle anterior](validation/advanced-textures-20261007/diagnostic-wgpu-gl-mips-before.log)
registrou erro HDR máximo 128 e erro NPOT 85. Fontes isoladas com cópias GPU por
nível corrigiram ambos, sem map/readback CPU. O orçamento do grafo passou a
reservar conservadoramente essas fontes, também nos demais backends.

A autogeração BGFX não reproduziu a redução exata de área NPOT na fixture
3×5. O controle em desenvolvimento registrou erro máximo RGB 85. Foi adicionada
recusa antes de qualquer produtor; a redução por área BGFX continua na checklist.
A fixture e o oráculo ficam no teste versionado, sem relaxar tolerância.

BGFX/OpenGL também contaminava o intervalo de mip de um sampler quando a mesma
imagem era usada simultaneamente em nível base e com mips. O
[diagnóstico anterior](validation/advanced-textures-20261007/diagnostic-bgfx-gl-shared-lod-before.log)
registrou erro máximo 64. A combinação é agora recusada por frame sem alterar
pixels/serial. Vulkan/wgpu mantêm o controle positivo. Views independentes são
uma melhoria pendente.

A admissão acontece em duas etapas: limites conhecidos do conector antes de
preparar o dispositivo; formatos/capacidades no adaptador preparado, ainda antes
de submeter o primeiro produtor. Isso conserva a recusa do 65º produtor wgpu
sem preparação e as recusas estáticas BGFX. A inspeção ignora o registro de
capacidades BGFX vazio quando não há runtime vivo.

## Gates integrados e falhas anteriores preservadas

A campanha inclui textura, RTT, ownership, publicação, seleção, Core/reuso,
packing FFI, cache e multidevice. Os resultados completos são preservados,
incluindo os erros encontrados e corrigidos durante a integração. O conjunto
final wgpu ficou em 33 passes e três falhas anteriores, entre 36 CTests; BGFX,
47 passes e duas falhas de sampling, entre 49. Ownership/captura rechecados:
5/5 wgpu e 8/8 BGFX. Recording: quatro passes e um skip de seleção GPU após
seus controles CPU. Rust: 40 testes unitários e cinco de shader, todos PASS.

O gate legado de sampling permanece falhando na fixture projetiva AMD, a partir
da qualidade 0,5: wgpu/AMD Vulkan e OpenGL registram MAE 8,08/max 78;
BGFX/AMD Vulkan é equivalente e BGFX/OpenGL registra MAE 7,99/max 77. NVIDIA
Vulkan passou nos dois backends, incluindo 234 controles e quatro rejeições.
A [execução com todas as fontes do commit anterior](validation/advanced-textures-20261007/baseline-head-sampling.log)
reproduziu exatamente MAE 8,08/max 78 em AMD/wgpu Vulkan. Não se atribui essa
falha à extensão de formatos nem se declara paridade projetiva AMD resolvida.
O limite original MAE≤1,5/max≤4 foi mantido; investigar LOD/footprint nativa fica
como estudo aberto.

Dois outros gates wgpu falham na base anterior e na extensão:

- [LargeBindings](validation/advanced-textures-20261007/baseline-CoinWgpuLargeBindingsTest.log):
  expectativa de 25.600 draws depois do packing;
- [MultiDevice](validation/advanced-textures-20261007/baseline-CoinWgpuMultiDeviceTest.log):
  expectativa de contagem/bytes de upload de instâncias após atualização.

Esses controles antigos de instancing/bindings continuam abertos; não foram
alterados para converter o resultado em PASS. Os testes de imagem/qualidade do
perfil novo têm expectativas numéricas próprias. Esta campanha não mede FPS.

Continuação posterior desta mesma data: os dois gates de instancing foram
reconciliados, com imagens/depth e contagens exatas preservados. O sampling AMD
foi isolado contra CoinGL e continua aberto. Ver
[diagnóstico e resultados atualizados](coin-render-sampling-instancing-validation-20261007.md).
As contagens e afirmações acima descrevem esta campanha histórica.

## Reprodução

Compilar os testes com o backend desejado e o protocolo privado **50**. Os
módulos Rust novos fazem parte de `DEPENDS`, evitando usar shaders antigos em
build incremental. Exemplo da campanha desta máquina:

```sh
python3 testsuite/coinrender/run-advanced-textures.py \
  --wgpu-build /mnt/Laranja/Git/externos/coin-render-artifacts/p23-fps-20261007/build-linux-wgpu \
  --bgfx-build /mnt/Laranja/Git/externos/coin-render-artifacts/raster-study-20261007/build-bgfx \
  --output docs/validation/advanced-textures-20261007
```

O runner executa cada célula sequencialmente, registra seleção ICD/GL/EGL,
identidade retornada, comando, exit code e SHA256 do log. A ausência de IDs BGFX
OpenGL permanece visível. `manifest.json` fixa fontes e binários da campanha;
os resultados adicionais de CTest e Rust também são versionados.
