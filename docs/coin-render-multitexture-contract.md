# P08 — Multitextura e strokes

P08 fecha o perfil abaixo em CoinRender, CoinBgfx e CoinWgpu. A qualificação
usa as mesmas cenas, expectativas numéricas e referência Coin/GL para os três
executores CPU, BGFX e wgpu. Não fecha integralmente P02, P05, P07 ou P09.

## Responsabilidades

Wiring habilita `SoTextureCombineElement` e usa a interpretação de campos do
próprio `SoTextureCombine::doAction()`, sem consultar hardware GL em uma action
de captura. Captura imagens, samplers, matrizes, coordenadas e combine por
unidade. Os índices dos extremos de `SoLineSet` avançam junto dos vértices;
isso corrige o detalhe Coin usado por callbacks e picking.

`CoinRenderTextureCombineCore.h` valida e compila o estado em quatro vetores
por unidade, com operações e argumentos normalizados. Também fornece o
executor CPU e a prova conservadora de alpha para composição. Core é o único
dono da interpretação dos enums Coin. Infra BGFX e wgpu executam esse programa,
com seus próprios shaders, recursos, bindings e submissão. A expansão de
strokes continua em `CoinRenderStrokeCore.h`.

## Contrato implementado

- Oito unidades, numeradas 0–7, incluindo unidades esparsas e unidade zero
  desligada. Cada unidade conserva imagem, sampler, matriz e UV explícito.
- RGB: REPLACE, MODULATE, ADD, ADD_SIGNED, SUBTRACT, INTERPOLATE, DOT3_RGB
  e DOT3_RGBA. Alpha: as seis primeiras operações. Escalas independentes 1, 2
  e 4; clamp ao fim de cada estágio; constante RGBA limitada a [0,1].
- Fontes PRIMARY_COLOR, TEXTURE, CONSTANT e PREVIOUS. Operandos RGB de cor,
  alpha e seus complementos; alpha aceita SRC_ALPHA e ONE_MINUS_SRC_ALPHA.
  PRIMARY_COLOR é a cor antes da cascata. Unidade desligada não altera PREVIOUS.
  DOT3_RGBA substitui alpha, ignora a operação alpha e conserva sua escala.
- Combine vale para polígonos preenchidos, contornos LINES/POINTS e linhas e
  pontos nativos. Coordenadas, cor/alpha, W e distância de fog atravessam a
  expansão comum; UV e cor interpolam em perspectiva. Pontos conservam o UV
  do vértice, sem semântica de point sprite.
- Linhas sólidas e com padrão usam a mesma montagem de fragmentos unitários.
  Largura arredondada replica cobertura no eixo menor. Pontos usam tamanho
  inteiro arredondado, mínimo um, e centro diferente para tamanhos pares e
  ímpares. Uma linha ou ponto isolado compõe alpha uma única vez por fragmento.
- A fase de linhas nativas continua por polilinha e reinicia nos resets Coin
  já qualificados. Após clipping, Core inicia na primeira cobertura visível;
  o GL deixa a fase inicial de uma linha recortada indeterminada.
- Operações/escalas inválidas, índice UV adicional inválido e excesso de
  unidades falham antes da publicação: pixels e serial anteriores permanecem
  disponíveis. Corrigir a cena permite a próxima submissão. Separators
  restauram o estado de combine.

A montagem de raster aliased e a cascata seguem os contratos das seções 3.3,
3.4 e 3.8.13 da [especificação OpenGL 2.1](https://registry.khronos.org/OpenGL/specs/gl/glspec21.pdf),
confrontados com a implementação Coin. Para linhas, a comparação admite a
alternativa permitida de uma coordenada por eixo e um fragmento unitário;
não afirma igualdade bit a bit. Pontos da matriz são comparados pixel a pixel.

## ABI privada wgpu

Revisão **26**: vértice de **100 bytes**, estado de **2280 bytes**, unidade de
textura de **96 bytes** e draw de **56 bytes**. Os campos anteriores conservam
seus offsets; UVs adicionais começam em 44, unidades extras em 1096 e programas
de combine em 1768. C++ e Rust verificam tamanhos e offsets. A ponte rejeita
programas normalizados inválidos e valores não finitos. A API/ABI pública
estável de libCoin não muda.

## Matriz e limites

`CoinRenderMultitextureTest` executa operações, fontes, operandos e escalas
contra tabelas numéricas independentes, fast path ligado/desligado e cinco
formas de geometria. Exercita oito unidades simultâneas, UVs variáveis,
matrizes independentes, unidade esparsa, avanço dos detalhes de LineSet,
isolamento de estado, rejeição e recuperação.

A matriz de raster cobre sete direções/posições de linha, larguras
0,75/1/2/3/4/6,6, máscaras 0/ffff/000f/aaaa, alpha 0,5, centros fracionários
de ponto, clipping e publicação de quadro vazio. A matriz de
`CoinRenderDrawStyleTest` também qualifica UVs da unidade sete com W de 1 a 4,
clipping, matrizes, modelos e fog, além de bindings, alpha/cor
interpolados, W variável, textura/fog, contornos e continuidade nativa.

O ensaio de combine usa imagens RGBA e UVs explícitos, com sampling linear;
os limites já declarados de formatos, modelos legados, qualidade e UV
procedural/default continuam em P07. A qualificação de todos os bindings e
shapes é P05; onze modalidades de transparência são P09, e OIT/peeling ampliado
é P10. Drivers físicos, MSAA, viewports externos e integração FreeCAD seguem
as pendências correspondentes. Os ensaios GL usam a ordem canônica Coin:
TextureUnit, TextureCombine, Texture2 e shape.

Os shaders BGFX de object blend, weighted OIT e peeling compartilham o executor
de superfície. Isso não substitui a qualificação de todas as combinações de
transparência em P09/P10. wgpu segue as modalidades que já oferece.

## Evidência de fechamento — 2026-09-28

- wgpu Debug/C++20: 21 CTests aprovados; a matriz P08 e SoDrawStyle rodaram
  com `COIN_RENDER_REQUIRE_GL_REFERENCE=1`. O caso adicional de DOT3_RGBA com
  escalas independentes foi confirmado na execução final do target P08.
- BGFX Release/C++11: 38 dos 44 CTests passaram na rodada geral, incluindo
  estilo, multitextura, iluminação, clipping, composição, readback e falhas.
  Os seis testes de superfície falharam porque o oráculo CPU solicitava
  SORTED_LAYERS_BLEND sem executor CPU. Após corrigir a fixture para source-over
  nas amostras de um único primitivo, os seis passaram com GL obrigatório e
  um novo caso numérico de combine em object/OIT/peeling, Vulkan e OpenGL.
  Assim, os 44 testes distintos ficaram aprovados; não se omite a primeira falha.
- Rust: 10 testes unitários e dois testes de validação WGSL aprovados, incluindo
  rejeição de programas normalizados e dados extras inválidos.
- `git diff --check` passou. A ABI pública estável de libCoin permanece intacta.

Logs desta sessão: `/tmp/coin-p08-wgpu-tests.log`,
`/tmp/coin-p08-bgfx-tests.log`, `/tmp/coin-p08-bgfx-surface-tests.log`,
`/tmp/coin-p08-rust-test.log`, `/tmp/coin-p08-wgpu-final.log` e
`/tmp/coin-p08-bgfx-final.log`. Os logs em `/tmp` são evidência local temporária;
as fixtures e expectativas ficam versionadas no repositório.

Para reproduzir P08 em um build wgpu ou BGFX, com Mesa/GLX e Xvfb disponíveis:

```sh
env __GLX_VENDOR_LIBRARY_NAME=mesa COIN_GLXGLUE_NO_PBUFFERS=1 \
  COIN_GLX_PIXMAP_DIRECT_RENDERING=1 COIN_RENDER_REQUIRE_GL_REFERENCE=1 \
  xvfb-run -a ctest --test-dir <build> -R '^CoinRenderMultitexture' --output-on-failure
```

Os testes BGFX com sufixos `_vulkan`/`_opengl` fixam o renderer e o mecanismo
object. Para a superfície compartilhada pelos três passes, executar
`CoinBgfxSurface_.*` com `COIN_WGPU_REQUIRE_GL_REFERENCE=1`, nome histórico da
variável desse teste. A matriz passou com Mesa/llvmpipe; hardware físico segue
a qualificação de produto, sem promessa de paridade bit a bit entre drivers.
