# P27 — fechamento local Linux (2026-10-02)

O perfil funcional descrito em [P27](coin-render-p27-shadows.md) foi exercitado
em cinco células físicas, com executor GPU e referência Coin/GL obrigatórios,
sem skips. A referência offscreen Coin/GL usa AMD/Mesa neste host, inclusive
quando o executor Vulkan é NVIDIA. Isso permite comparar os mesmos dados e
pixels entre implementações; não qualifica o contexto offscreen NVIDIA/GLX.

| Executor | GPU física / driver | Resultado |
| --- | --- | --- |
| BGFX Vulkan | AMD Radeon Graphics, RADV RENOIR, Mesa 25.2.8, `1002:1638` | 49/49 |
| BGFX OpenGL | AMD Radeon Graphics, radeonsi, Mesa 25.2.8 | 49/49 |
| BGFX Vulkan | NVIDIA RTX 3060 Laptop, 610.57.04, `10de:2560` | 49/49 |
| wgpu Vulkan | AMD Radeon Graphics, RADV RENOIR, Mesa 25.2.8, `1002:1638` | 46/46 |
| wgpu Vulkan | NVIDIA RTX 3060 Laptop, 610.57.04, `10de:2560` | 46/46 |
| BGFX OpenGL, qualificação limitada | NVIDIA RTX 3060 Laptop, 610.57.04, PRIME | 10/10 de recursos, RTT, readback e qualidade; comparação completa Coin/GL aberta |

As cinco células completas somam 239 testes. A suíte Coin também passou:
370 testes, 85.374 verificações. Os [logs preservados](validation/p27-linux/)
registram inventário, capacidades, limite GL e resultados por célula.

## Correções fechadas nesta campanha

- **Wiring:** `apply(path)` até um receiver captura a cena inteira do grupo
  para o mapa. O caster fora do caminho continua projetando a mesma sombra.
  O Core inclui seus bounds quando a captura é implícita; cenas próprias
  externas continuam com a política de bounds do grupo original.
- **Override:** `SoComplexity::callback` respeita e publica o override de
  qualidade como `GLRender`. A fixture verifica material e qualidade, antes
  e depois de retirar o override, além dos caminhos até grupo e receiver.
- **Fixture RTT:** o alvo dos grupos irmãos preserva o renderer selecionado,
  permitindo executar a referência completa em BGFX/OpenGL.
- **Regressão Coin:** as cenas Inventor usam o token público `LightModel`;
  a renomeação interna para `CoinRenderLightModel` não altera o formato `.iv`.
  A ordem dos includes de `SoAnnotation` permite gerar e compilar CoinTests.

O shader de sombras Coin/GL continua amostrando uma imagem instalada mesmo
com `textureQuality=0`. Por isso o teste de override de qualidade verifica o
contrato do nó em Coin/GL com o grupo inativo, enquanto o executor novo mantém
sombras ativas. Não se declara equivalência de desativação de textura com o
shader de sombras GL. A fixture registra as comparações separadamente.

## Reproduzir

Compilar previamente os alvos de teste da configuração escolhida. O runner
não recompila binários. Usar o display X11 físico (`DISPLAY=:0` neste host).

```sh
env VK_DRIVER_FILES=/usr/share/vulkan/icd.d/radeon_icd.json \
  __GLX_VENDOR_LIBRARY_NAME=mesa COIN_BGFX_RENDERER=vulkan \
  COIN_SHADOW_GPU_VENDOR_PATTERN=AMD \
  .github/scripts/qualify-coin-render-shadows-linux.sh \
  build-bgfx-recovery/coin-build bgfx /tmp/p27-linux/amd-bgfx-vulkan
```

Para NVIDIA/Vulkan, trocar o ICD por `nvidia_icd.json` e o padrão por `NVIDIA`.
Manter GLX Mesa para o oráculo. Para AMD/OpenGL, selecionar
`COIN_BGFX_RENDERER=opengl`. Para wgpu, usar seu build e o argumento `wgpu`.
O runner registra seleção física, rejeita adapters de software, exige GPU e
GL, e falha se houver skips. Ele cobre sombras, composição, alfa RTT,
transparência, peeling/OIT, qualidade, Wiring, ownership, seleção, depth,
SceneTexture staged/direct, orçamento e readback por API onde disponível.
Resize e recuperação referem-se às fixtures e falhas injetadas; não foi
provocada uma perda física real do dispositivo.

## O que permanece aberto

- **Oito mapas Coin/GL nativos:** o limite relevante é
  `cc_glglue_max_texture_units`, baseado nas unidades de coordenadas de
  textura, e não o número de samplers do fragment shader. O contexto Mesa
  fornece oito, sendo uma reservada à cena. NVIDIA/GLX também anuncia oito
  em `GL_MAX_TEXTURE_COORDS`. BGFX/wgpu executam oito mapas, mas o oráculo
  local usa sete luzes coincidentes com intensidade total equivalente.
  Com `COIN_RENDER_REQUIRE_GL_EIGHT_MAP_REFERENCE=1`, o controle negativo
  recusa a referência: exige nove unidades e informa as oito disponíveis.
- **NVIDIA/OpenGL com oráculo:** BGFX funciona com
  `__NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia`.
  Coin `SoOffscreenRenderer` falha ao encontrar visual RGBA X11 compatível,
  inclusive nos caminhos pbuffer e pixmap testados. Os dez testes GPU passam,
  mas não encerram essa célula de comparação.
- **Windows/Intel e macOS/Metal:** dependem de outro computador. A execução
  necessária e os critérios permanecem no
  [arquivo de plataformas pendentes](coin-render-platform-validation-pending.md).

P27 permanece aberto apenas quanto aos critérios e extensões explicitamente
pendentes; este fechamento não transforma os perfis qualificados em uma
promessa para todas as combinações possíveis do Coin.
