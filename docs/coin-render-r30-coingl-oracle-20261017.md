# R30 — controle CoinGL dos modos de linha

**Captura histórica invalidada:** o callback offscreen limpava cor e depth
entre as passagens do manager. O [oráculo corrigido](coin-render-r30-offscreen-fix-20261017.md)
mostra HIDDEN_LINE CoinGL distinto de WIREFRAME e ocultando arestas nas sete
células. Os números abaixo descrevem a falha de captura anterior.

Uma action GL delega a `SoRenderManager::render` dentro do contexto de
`SoOffscreenRenderer`, com manager separado do adaptador CoinRender.
O ciclo `prepareFrame/finishFrame` é pareado para cada modo. A mesma esfera
opaca, sem textura, é capturada em 64×64 RGB nas sete células Linux disponíveis:
BGFX AMD/NVIDIA × Vulkan/OpenGL e wgpu AMD/NVIDIA Vulkan + AMD OpenGL.

`SHADED_HIDDEN_LINES` passou o gate explícito: soma do erro RGB ≤20.000 e
máximo por canal ≤10. Os valores foram 14.670/10 na AMD e 16.410/10 na
NVIDIA, iguais nos dois executores. O controle interno prova a terceira
passagem com depth `GREATER` e 2.166 índices de linha tracejada, contra
9.024 da passagem sólida.

`HIDDEN_LINE` revelou uma divergência de cobertura que permanece aberta.
CoinRender acende 409 pixels de aresta; CoinGL, 619. Na AMD, 389 pixels
coincidem, 20 são exclusivos do CoinRender e 230 do CoinGL. Na NVIDIA,
408 coincidem, 1 é exclusivo do CoinRender e 211 do CoinGL. O máximo
por canal é 204; a soma RGB é 153.000 na AMD e 129.744 na NVIDIA. Esses
resultados são observações, não PASS de paridade. O controle adicional `WIREFRAME` separa a causa: CoinRender acende 620
pixels e CoinGL 619, com 600 em comum na AMD e 619 na NVIDIA. No próprio
CoinGL, `HIDDEN_LINE` e `WIREFRAME` foram imagens RGB **idênticas byte a
byte** nas duas GPUs. A divergência desta fixture vem da passagem de
ocultação, não do traçado puro das linhas. O contrato portátil conserva a
ocultação verificada pelo teste independente; reproduzir essa imagem nativa
sem ocultação seria uma regressão. Outras geometrias e drivers ainda precisam
de controle antes de generalizar esse achado.

[Logs e hashes](validation/coin-render-r30-coingl-oracle-20261017/verification.json).
O gate GL é opcional no teste de produto, ativado com
`COIN_RENDER_REQUIRE_GL_REFERENCE=1`, porque depende de contexto GLX
físico. O teste comum continua executável sem CoinGL.

Um segundo controle usa cubo inclinado para separar arestas dianteiras e
traseiras. CoinRender HIDDEN_LINE reduz a cobertura de 229 para 180 pixels;
CoinGL mantém 230 em ambos os modos, com máscara idêntica. O gate portátil
exige a redução no cubo. O resultado repetiu nas sete células Linux disponíveis; a igualdade nativa agora foi observada em duas
geometrias, sem ser extrapolada a outros drivers ou cenas.
