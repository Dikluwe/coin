# P27 — fechamento local Linux (2026-10-02)

O perfil funcional descrito em [P27](coin-render-p27-shadows.md) foi exercitado
em seis células físicas, com executor GPU e referência Coin/GL obrigatórios,
sem skips. A campanha inicial usou AMD/Mesa como referência Coin/GL. Após corrigir
GLX/PRIME, BGFX/OpenGL e wgpu/Vulkan também foram comparados com Coin/GL
na própria NVIDIA física. As duas rotas offscreen, pixmap e pbuffer, passaram.

| Executor | GPU física / driver | Resultado |
| --- | --- | --- |
| BGFX Vulkan | AMD Radeon Graphics, RADV RENOIR, Mesa 25.2.8, `1002:1638` | 49/49 |
| BGFX OpenGL | AMD Radeon Graphics, radeonsi, Mesa 25.2.8 | 49/49 |
| BGFX Vulkan | NVIDIA RTX 3060 Laptop, 610.57.04, `10de:2560` | 49/49 |
| wgpu Vulkan | AMD Radeon Graphics, RADV RENOIR, Mesa 25.2.8, `1002:1638` | 46/46 |
| wgpu Vulkan | NVIDIA RTX 3060 Laptop, 610.57.04, `10de:2560` | 46/46 |
| BGFX OpenGL | NVIDIA RTX 3060 Laptop, 610.57.04, PRIME | 49/49 com referência Coin/GL na NVIDIA |

As seis células completas somam 288 testes. A correção NVIDIA foi seguida
por 49 testes de regressão AMD/OpenGL, 46 testes wgpu/Vulkan com oráculo
NVIDIA/GL e oito testes focados de pbuffer NVIDIA, todos sem skips. A suíte Coin também passou:
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
`COIN_BGFX_RENDERER=opengl`. Para NVIDIA/OpenGL, usar
`__NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia` e o padrão
`NVIDIA`. O runner prefere pixmap; `COIN_GLXGLUE_NO_PBUFFERS=0` seleciona
a tentativa de pbuffer. Para wgpu, usar seu build e o argumento `wgpu`.
O runner registra seleção física, rejeita adapters de software, exige GPU e
GL, e falha se houver skips. Ele cobre sombras, composição, alfa RTT,
transparência, peeling/OIT, qualidade, Wiring, ownership, seleção, depth,
SceneTexture staged/direct, orçamento e readback por API onde disponível.
Resize e recuperação referem-se às fixtures e falhas injetadas; não foi
provocada uma perda física real do dispositivo.

## NVIDIA/GLX PRIME resolvido

A falha era no Coin/GL, com três causas distintas:

1. `glXChooseVisual` solicitava implicitamente buffer simples. A NVIDIA
   PRIME só oferecia os visuais RGBA necessários com buffer duplo. A seleção
   agora tenta esses formatos depois das opções de buffer simples.
2. A resolução de símbolos consultava EGL antes de GLX, mesmo para um
   contexto GLX. A sonda confirmou zero configurações via resolvedor EGL e
   15 via GLX para a mesma solicitação NVIDIA. A resolução agora usa a API
   selecionada pelo Coin; o pbuffer também admite buffer duplo como fallback.
3. O produtor `SoSceneTexture2` herdava a unidade usada pelo mapa de sombra.
   Na NVIDIA, a textura ficava fora das quatro unidades de pipeline fixo,
   desabilitando sua contribuição e tornando o alfa opaco. O produtor agora
   começa na unidade zero, dentro do push/pop existente. O mesmo contrato
   já era aplicado pelo Wiring dos executores novos.

O FBO NVIDIA passou de alfa 255 incorreto para alfa variável esperado; os
casos existentes de transparência, ALPHA_TEST, peeling/OIT e RTT capturam a
regressão. A instrumentação temporária foi removida. EGL também passou em
um contexto EGL 1.5 ativo na AMD, com RTT e readback, `GL_error=0`. As sondas,
logs e evidências estão em `validation/p27-linux/glx-offscreen-fix/`.
O log antigo `nvidia-gl-reference-unavailable.txt` registra a falha anterior.

## O que permanece aberto

- **Oito mapas Coin/GL nativos:** o limite relevante é
  `cc_glglue_max_texture_units`, baseado nas unidades de coordenadas de
  textura, e não o número de samplers do fragment shader. O contexto Mesa
  fornece oito, sendo uma reservada à cena. NVIDIA/GLX também anuncia oito
  em `GL_MAX_TEXTURE_COORDS`. BGFX/wgpu executam oito mapas, mas o oráculo
  local usa sete luzes coincidentes com intensidade total equivalente.
  Com `COIN_RENDER_REQUIRE_GL_EIGHT_MAP_REFERENCE=1`, o controle negativo
  recusa a referência: exige nove unidades e informa as oito disponíveis.
- **Windows/Intel e macOS/Metal:** dependem de outro computador. A execução
  necessária e os critérios permanecem no
  [arquivo de plataformas pendentes](coin-render-platform-validation-pending.md).

P27 permanece aberto apenas quanto aos critérios e extensões explicitamente
pendentes; este fechamento não transforma os perfis qualificados em uma
promessa para todas as combinações possíveis do Coin.
