# P25 — efeitos, estado de implementação

P25 cobre duas famílias distintas. `SoShadowGroup`, `SoShadowStyle`,
`SoShadowSpotLight` e `SoShadowDirectionalLight`, `SoTexture3` e
`SoTextureCubeMap` e `SoSceneTextureCubeMap` são semântica Coin. SSAO não é nó Coin e só pode entrar
como extensão opt-in. Não inferir equivalência entre a qualidade visual de um
efeito novo e o comportamento Coin/GL.

## Contrato Coin e caminho GL

O GL de `SoShadowGroup` cria shadow maps com Variance Shadow Maps, renderiza
casters e receivers, usa `SoShadowStyle` e pode usar iluminação Phong por
fragmento dentro do grupo. O Wiring já captura grupo, estilos e luzes e o
Core produz passes para os perfis opacos P27.1–P27.2; o contrato ampliado
continua em P27.4. `SoTexture3::callback` publica imagem
volumétrica no elemento Coin; o FramePlan atual só captura textura 2D.
`SoTextureCubeMap::callback` e `SoSceneTextureCubeMap::callback` chamam
`doAction`, que está vazio neste Coin;
o caminho GL configura seis faces e a amostragem de cubo. Reinterpretar esses
nós nos backends criaria duas decisões semânticas concorrentes.

## Estado verificável

- [x] A Wiring comum identifica volume e cube map com imagem/arquivo,
  cube RTT com cena,
  `SoShadowGroup` ativo e `SoShaderProgram` ativo. Perfis de sombras fora do
  P27.1–P27.2 retornam `UNSUPPORTED` antes da submissão. Shader Coin é a
  frente I07, não implementação de SSAO.
- [x] `SoShadowGroup` inativo preserva travessia normal dos filhos. Teste de
  caracterização verifica status, diagnóstico, ausência de submit e recuperação.
- [x] Capturar grupo, casters, receivers, estilos e luzes dos perfis opacos
  P27.1–P27.2 sem consultar GL no Core; o Core decide passes e limites e o
  wgpu aloca e submete seus recursos. Textura e o contrato amplo seguem abertos.
- [ ] Implementar volume/cube map em BGFX e wgpu com mesmo plano, formatos,
  wrap/filtros, orientação das faces e expectativa de cor verificável.
- [ ] Completar sombras Coin em BGFX e wgpu: os perfis opacos de até duas
  luzes passaram no wgpu (P27.1–P27.2), enquanto BGFX, três ou mais luzes,
  transparência, cenas próprias, RTT e a matriz final seguem abertos.
- [ ] Projetar SSAO como opção tipada, desativada por padrão, com depth/normal
  e composição explícitos. Testar disponibilidade e custo em cada backend.
- [ ] Qualificar em GPU física, por renderer/driver/formato, sem marcar suporte
  funcional a partir de um probe de capacidade.

`CoinRenderNodeInventoryTest` comprova a identificação dos nós e o contrato
de rejeição. `CoinRenderShadowReferenceTest` comprova os perfis opacos wgpu
P27.1–P27.2 contra Coin/GL; os demais efeitos continuam sem executor.
