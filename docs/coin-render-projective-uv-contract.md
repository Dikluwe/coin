# CoinRender: coordenadas de textura projetivas

Implementação na branch `codex/coin-render-transform-performance`, tomando
o comportamento do Coin/OpenGL como referência.

## Captura e interpolação

O plano conserva a coordenada homogênea `(s,t,r,q)` em cada uma das oito
unidades. Coordenadas 2D são promovidas a `(s,t,0,1)`; coordenadas 3D conservam
`r` e usam `q=1`. O `SoPrimitiveVertex` e o elemento Coin de coordenadas 4D
conservam os quatro componentes, sem divisão durante a captura.

`SoTextureMatrixTransform` aplica a matriz completa. A transformação pode
usar `r`, produzir `q` variável ou fazer ambos. Clipping interpola os quatro
componentes; a expansão comum de linhas e pontos conserva os mesmos atributos.
O `q` da textura é independente do W geométrico e de `screenSpaceW` dos strokes.

No render clássico, os executores interpolam `(s,t,q)` em perspectiva e
amostram a textura em `(s/q,t/q)` no fragmento. Dividir nos vértices produz
outro resultado quando `q` varia: o gate inclui esse contrafactual e exige
diferença visível. O flip vertical de RTT ocorre depois da divisão.

A captura fica no adaptador Coin; transformação, validação e política de
amostragem pertencem ao contrato comum. BGFX e wgpu transportam e executam
esses valores. UV afim com `q=1` conserva o caminho sem divisão.

O fast path de IndexedFaceSet continua aceitando UV explícito 2D, incluindo
matriz projetiva. Entradas 3D/4D usam os callbacks nativos, preservando `r/q`.
Deduplicação, digest, comparação de atributos e reuso incluem os novos campos.

## Política do shader nativo de sombras

O shader gerado por `SoShadowGroup` usa ST transformado diretamente,
sem divisão por Q. A captura registra `DIRECT_ST` quando um grupo está ativo
e `SoShadowStyle` inclui `SHADOWED`; os demais desenhos usam `PROJECTIVE`.
Essa decisão independe da quantidade de mapas e do pipeline escolhido.

Coin redefine ShadowStyle para `CASTS_SHADOW_AND_SHADOWED` ao entrar no grupo.
Separators restauram o estado no pop. O produtor capturado de SceneTexture
usa estado independente, com shader de sombras desligado; o consumidor
conserva sua política após o retorno.

Um `SoShaderProgram` vazio dentro de um grupo ativo remove o programa gerado
e altera também iluminação/sombras. Esse perfil é recusado com `UNSUPPORTED`,
preservando o último quadro publicado. Fora do grupo, o nó vazio continua
admitido. Shaders explícitos ativos permanecem fora do contrato portátil.

## Domínio admitido

Com `PROJECTIVE`, Q transformado deve ser não zero, não subnormal e ter o mesmo
sinal em cada primitivo. Q negativo consistente é admitido. Pontos separados
podem ter sinais diferentes. A validação ocorre antes de clipping e expansão,
para não esconder uma singularidade entre os extremos de uma linha.
Contornos de polígonos em LINES conservam essa condição antes do clipping.

ST/R/Q capturados e matrizes devem ser finitos. A transformação e o resultado
ST/Q também devem caber em float finito. Entradas inválidas retornam
`INVALID_SCENE` antes da publicação; pixels e serial anteriores permanecem
disponíveis, e corrigir a cena permite renderizar novamente.

Com `DIRECT_ST`, somente ST transformado participa da amostragem; Q zero,
subnormal ou com mudança de sinal não causa divisão. A validação conserva
os requisitos de valores capturados/matrizes finitos e ST transformado finito.
A ponte Rust aplica as mesmas regras para consumidores diretos do FFI.

UV procedural/default, formatos de textura, filtros, qualidade, sombras e
RTT continuam com seus limites próprios. Este trabalho não amplia esses perfis.

## Transporte privado e custo

| Estrutura neste build Linux | Anterior | Atual |
| --- | ---: | ---: |
| Vértice comum | 100 bytes | 164 bytes |
| Estado comum | 1648 bytes | 1648 bytes |
| Vértice FFI wgpu | 100 bytes | 164 bytes |
| Estado FFI wgpu | 2288 bytes | 2292 bytes |
| Vértice BGFX completo | 188 bytes | 220 bytes |
| Vértice BGFX compacto | 124 bytes | 124 bytes |

A ABI privada wgpu passa para **45**. O vértice transporta ST/R/Q por unidade;
a política ocupa o novo campo final do estado FFI. C++ e Rust precisam ser
recompilados juntos. O estado comum usa um byte de padding anterior para a
política; a tabela GPU de materiais permanece com stride 80. A ABI pública
estável de libCoin não é alterada.

wgpu usa 13 atributos de vértice. BGFX usa os atributos tangent/bitangent
livres para os Q adicionais; o layout geral usa 15 atributos. Os layouts de
instancing permanecem inalterados. O aumento de memória dos vértices completos
é real; não há medição de desempenho nesta campanha semântica. Orçamentos de
transporte continuam calculados por `sizeof`.

## Validação e limites da referência

O [gate de UV projetivo](../testsuite/coinrender/CoinRenderProjectiveUvTest.cpp)
tem modos `--capture` e `--gpu`. O segundo exige CoinGL válido. A referência
mede RGB; não certifica alpha geral de framebuffer.

Nesta NVIDIA, `GL_MAX_TEXTURE_UNITS=4` para o caminho fixo. As comparações
nativas cobrem até quatro estágios. A unidade 7 isolada com REPLACE é confrontada
com uma cena CoinGL equivalente na unidade 0, conservando imagem, matriz e
coordenadas. Essa projeção é identificada nos logs.

Oito estágios simultâneos recebem um oracle analítico independente por pixel,
com gradientes bilineares, coordenadas homogêneas e produto dos oito estágios.
Cada unidade tem mutações A/B/A e contrafactual de divisão prematura. Os logs
e contagens distinguem esse oracle das comparações CoinGL; isso não certifica
oito estágios simultâneos no render fixo desta GPU.

Os controles de sombras executam um mapa spot real, com intensidade zero e
material emissivo branco para isolar a amostragem. Verificam os estilos do
grupo, sua desativação, Q ignorado em `DIRECT_ST` e a recusa de programa vazio.
O perfil executável exige ao menos um passe visível; um grupo com zero mapas
continua fora desse perfil. Peeling e weighted usam uma camada/contribuição,
sem reivindicar equivalência geral da composição de várias camadas.

O peeling clássico tem uma referência equivalente explicitamente identificada:
CoinGL em BLEND com uma única contribuição sobre fundo preto. O programa ARB
de `SORTED_LAYERS_BLEND` usa `TEX` sem projeção, ignora o ambiente da textura e
soma seu RGB à cor primária. Esse comportamento legado não fornece o oracle
projetivo clássico. Os controles de sombras usam o shader GLSL gerado e mantêm
a comparação nativa em `SORTED_LAYERS_BLEND`. Essa distinção acompanha os
[limites existentes de transparência](coin-render-transparency-contract.md).

RTT staged e direto têm produtor e consumidor projetivos, mutações separadas
de Q e controle de orientação. `SceneTexture` é visitado antes da matriz do
consumidor: o FBO nativo herda matrizes do estado externo, enquanto o caminho
pbuffer nativo e a captura atual iniciam um estado independente. A herança de
matrizes externas no produtor RTT não é qualificada por este gate. A diferença
encontrada nessa ordem de nós está preservada nos logs de piloto.

O modo `--capture` verifica o plano e o raster CPU nos perfis que esse executor
admite. Sombras, peeling/OIT de sombras e produtores RTT são executados pelo
modo `--gpu`, com alvos reais e referência CoinGL obrigatória.

Resultado desta campanha nos três executores:

| Gate | Resultado |
| --- | ---: |
| UV: comparação nativa CoinGL/GPU | 414 |
| UV: projeção CoinGL equivalente da unidade 7 para 0 | 12 |
| UV: peeling clássico, source-over equivalente de uma camada | 15 |
| UV: oito unidades simultâneas, oracle analítico/GPU | 150 |
| Regressão de fragmentos | 348 comparações GPU |
| Regressão de texto/imagem | 279 comparações GPU |
| Regressão de marcadores | 363 comparações GPU |
| Core/CTest | 58 testes |
| Rust/ABI/shaders | 38 testes |

Também passaram duas execuções de captura, três gates nativos de sombras,
multidispositivo wgpu e o draw style BGFX/OpenGL direto. O CTest de draw style
fixa Vulkan por configuração CMake, por isso OpenGL teve execução separada.
As execuções finais GPU não tiveram skips; os pilotos e suas correções de
fixture estão registrados separadamente.

A evidência fica em [validation/projective-uv-linux](validation/projective-uv-linux).
O perfil é Linux/NVIDIA com wgpu/Vulkan e BGFX/Vulkan/OpenGL. Windows e outros
drivers precisam executar seus próprios gates.
