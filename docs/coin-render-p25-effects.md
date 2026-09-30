# P25 — efeitos, estado de implementação

P25 cobre duas famílias distintas. `SoShadowGroup`, `SoShadowStyle`,
`SoShadowSpotLight` e `SoShadowDirectionalLight`, `SoTexture3` e
`SoTextureCubeMap` são semântica Coin. SSAO não é nó Coin e só pode entrar
como extensão opt-in. Não inferir equivalência entre a qualidade visual de um
efeito novo e o comportamento Coin/GL.

## Contrato Coin e caminho GL

O GL de `SoShadowGroup` cria shadow maps com Variance Shadow Maps, renderiza
casters e receivers, usa `SoShadowStyle` e pode usar iluminação Phong por
fragmento dentro do grupo. O Wiring atual herda a travessia comum de filhos,
mas não captura um plano de shadow maps. `SoTexture3::callback` publica imagem
volumétrica no elemento Coin; o FramePlan atual só captura textura 2D.
`SoTextureCubeMap::callback` chama `doAction`, que está vazio neste Coin;
o caminho GL configura seis faces e a amostragem de cubo. Reinterpretar esses
nós nos backends criaria duas decisões semânticas concorrentes.

## Estado verificável

- [x] A Wiring comum identifica volume e cube map com imagem/arquivo,
  `SoShadowGroup` ativo e `SoShaderProgram` ativo; retorna `UNSUPPORTED` antes
  da submissão. Shader Coin é a frente I07, não implementação de SSAO.
- [x] `SoShadowGroup` inativo preserva travessia normal dos filhos. Teste de
  caracterização verifica status, diagnóstico, ausência de submit e recuperação.
- [ ] Capturar estado Coin dos casters, receivers, luzes, textura e coordenadas
  sem consultar GL no Core. O Core deve decidir passes, dependências e limites;
  cada Infra aloca e submete seus próprios recursos.
- [ ] Implementar volume/cube map em BGFX e wgpu com mesmo plano, formatos,
  wrap/filtros, orientação das faces e expectativa de cor verificável.
- [ ] Implementar sombras Coin: passes, mapas, estilos, transparência, bias,
  recuperação, resize e publicação transacional. Comparar cenas com Coin/GL.
- [ ] Projetar SSAO como opção tipada, desativada por padrão, com depth/normal
  e composição explícitos. Testar disponibilidade e custo em cada backend.
- [ ] Qualificar em GPU física, por renderer/driver/formato, sem marcar suporte
  funcional a partir de um probe de capacidade.

O teste `CoinRenderNodeInventoryTest` passou nas builds CPU/RECORDING e wgpu
após a mudança. Isso comprova diagnóstico e preservação do último frame,
não o suporte dos efeitos.
