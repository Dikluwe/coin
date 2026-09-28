# CoinBgfxAction, fog e superfícies texturizadas

O backend BGFX usa `CoinBgfxAction`, um tipo registrado no sistema de tipos
Coin, derivado da infraestrutura compartilhada `CoinRenderAction`. Não é um
`typedef`: `getTypeId().getName()` retorna `CoinBgfxAction`. O scene manager
usado pelo adaptador FreeCAD cria essa ação em builds BGFX. A base antiga continua
disponível para compatibilidade e para o backend Rust/wgpu.

## Perfil implementado

- `SoEnvironment`: `NONE`, `HAZE` linear, `FOG` exponencial e `SMOKE`
  exponencial quadrático; cor, visibilidade e início do fog. Visibilidade zero
  herda o far plane. A distância é de câmera, não o depth normalizado.
- Até oito unidades `SoTextureUnit` (0–7), inclusive unidades esparsas. Cada uma
  captura sua imagem `SoTexture2`, UV explícita, `SoTexture2Transform`, modelo
  `MODULATE`/`REPLACE`/`DECAL`/`BLEND`, blend color e wrap `REPEAT`/`CLAMP`.
- Cascata em ordem crescente de unidade; fog depois de todas as texturas.
  Alpha não é alterado pelo fog. A classificação transparente considera a
  cascata: `DECAL` preserva alpha e `REPLACE` substitui o alpha anterior.
- `SoIndexedLineSet`, `SoLineSet` e `SoPointSet` texturizados, com largura,
  tamanho, padrão de linha, cores, transparência e fog. A expansão em triângulos
  mantém o W homogêneo e interpola UV em perspectiva. Pontos preservam o UV
  explícito constante, sem inventar uma semântica de point sprite.
- Shaders Vulkan e OpenGL para opacos, object blend, weighted OIT e depth
  peeling; os três passes usam a mesma função de superfície.

## Limites explícitos

`SoTextureCombine` não está implementado e retorna `UNSUPPORTED`, em vez de ser
ignorado. Mais de oito unidades e coordenadas procedurais nas unidades
adicionais também são rejeitadas. Esta entrega não acrescenta texturas 3D,
cube maps, point sprites ou RTT direto. Transparência aditiva e readback GPU de
depth/assíncrono são tratados em [bgfx-transparency-readback.md](bgfx-transparency-readback.md).
O perfil de qualidade capturado continua aceitando 0 (desligado) e 0.5 (linear).
O transporte Rust mantém sua ABI e rejeita novos atributos exclusivos de BGFX.

Mudanças de câmera em geometria expandida ou com fog exigem recaptura, para não
reutilizar posições de tela ou distâncias de câmera antigas. Recursos GPU
continuam sujeitos ao cache por revisão. A referência CPU usa uma regra de borda
única nos triângulos para evitar alpha duplicado na diagonal de linhas e pontos.

## Verificação

`CoinBgfxSurfaceFeaturesTest` verifica o nome registrado, 1/2/8 unidades, unidade 7
isolada, modelos, alpha, fog, linhas/pontos, UV em perspectiva, stipple e rejeições.
Inclui comparação CPU/BGFX e um oráculo numérico independente para a cascata e
fog. O CTest executa Vulkan/OpenGL × object/weighted_oit/sorted_layers.

Com `COIN_WGPU_REQUIRE_GL_REFERENCE=1`, também compara os pixels com Coin/GL.
Essa referência pode rodar sob Xvfb (Mesa software); não é prova de apresentação
OpenGL em hardware. `CoinRenderFogTest` exercita os quatro modos, câmeras ortográficas
e perspectiva, distâncias, textura, linhas, pontos e mudanças de estado.
