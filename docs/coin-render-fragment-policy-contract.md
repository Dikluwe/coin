# CoinRender: profundidade, teste de alpha e REPLACE

Contrato implementado em 2026-10-05 na branch
`codex/coin-render-transform-performance`, tomando o Coin/OpenGL como referência.

## Profundidade

Com `SoDepthBuffer.test = FALSE`, o fragmento não compara nem escreve
profundidade, mesmo quando o campo `write` está ligado. O plano conserva os
valores capturados; CPU, wgpu e BGFX aplicam a regra na execução. Os attachments
temporários de peeling mantêm sua política própria.

O caso discriminante usa três desenhos sobrepostos: azul distante, vermelho
próximo com teste desligado/escrita ligada e verde intermediário com teste e
escrita ligados. O resultado do Coin/OpenGL é verde. Comparar apenas os dois
primeiros desenhos não detecta a escrita indevida.

## SoAlphaTest

A adaptação na action atualiza o estado nativo de `SoLazyElement`, conservando
herança e push/pop de separadores sem chamar `GLRender`. O plano transporta
uma função semântica e uma referência em `[0,1]`; constantes GL ficam na
adaptação do estado Coin.

| Função | Fragmento aceito quando |
| --- | --- |
| NONE / ALWAYS | Sempre |
| NEVER | Nunca |
| LESS / LEQUAL | Alpha menor / menor ou igual à referência |
| EQUAL / NOTEQUAL | Alpha igual / diferente da referência |
| GEQUAL / GREATER | Alpha maior ou igual / maior que a referência |

A comparação ocorre sobre o alpha final, depois das texturas e de
`SoTextureCombine`, antes de fog, escrita de profundidade e blend. O fragmento
descartado não altera a cor nem a profundidade. Referências fora do intervalo
são limitadas como no GL; NaN e funções inválidas produzem `INVALID_SCENE`.

Com teste efetivo, a captura normaliza o alpha primário do material com a
mesma precisão de 8 bits do `glColor4ub` usado pelo Coin. Por exemplo, material
com alpha `0.5` produz `128/255`, portanto `LEQUAL 0.5` descarta o fragmento.
Essa conversão ocorre por material antes da interpolação e das texturas, na
adaptação comum; os executores recebem o valor normalizado. `NONE` e `ALWAYS`
conservam o contrato anterior. A classificação nativa de transparência e o
valor de SCREEN_DOOR são preservados independentemente dessa normalização.

`SoImage` e glifos mono de `SoText2` herdam o teste. O buffer gray de `SoText2`
conserva o teste próprio `GREATER 0.3` instalado pelo Coin/OpenGL. A máscara de
cobertura continua impedindo escrita nas regiões vazias do raster.

Os shaders de superfície compartilham o teste com RTT, peeling e weighted OIT.
No BGFX, testes efetivos usam o caminho geral em vez do instancing restrito.
Mapas de sombras ainda não têm casters com alpha de material/textura: a
combinação de sombras e teste efetivo é recusada antes da submissão. `NONE` e
`ALWAYS` não descartam fragmentos e permanecem admitidos.

A ABI C privada wgpu passa de revisão 43 para **44**, acrescentando função e
referência ao final do estado transportado. Consumidores precisam recompilar
o par C++/Rust; os offsets anteriores são preservados.
Neste build Linux, o estado comum cresce de 1.640 para 1.648 bytes e o estado
FFI de 2.280 para 2.288 bytes. O stride da tabela GPU de materiais permanece
80 bytes; nenhuma classe pública Coin foi alterada.

## REPLACE por formato de imagem

Para imagens RGB e luminância, `SoTexture2.model = REPLACE` substitui RGB e
preserva o alpha anterior. Imagens RGBA e luminância/alpha substituem ambos.
A conversão para armazenamento RGBA8 não pode transformar essa diferença
do formato original em alpha opaco.

A captura normaliza RGB/L usando o programa comum de combinação já existente:
RGB vem de `TEXTURE`; alpha vem de `PREVIOUS`. Isso vale em cada unidade de
textura. Um `SoTextureCombine` explícito continua tendo precedência. Imagens
canônicas, digests e transporte GPU não precisam de um novo formato.
Os gates de precedência colocam `SoTextureCombine` antes da imagem ativa,
como nos exemplos do Coin: o GL aplica a combinação ao configurar a imagem.
Esta campanha não qualifica mudanças de combine posteriores à imagem na
ordem de travessia.

## Validação

O gate [CoinRenderFragmentPolicyTest](../testsuite/coinrender/CoinRenderFragmentPolicyTest.cpp)
tem modos `--capture` e `--gpu`. O segundo exige uma referência Coin/OpenGL
válida; somente indisponibilidade do executor GPU permite skip. Comparações
de RGB não certificam paridade geral do alpha de framebuffer: CoinRender
mantém seu contrato existente de composição source-over.

A evidência Linux fica em
[validation/fragment-policy-linux](validation/fragment-policy-linux).
Essa campanha cobre wgpu/Vulkan e BGFX/Vulkan/OpenGL nesta máquina NVIDIA.
Windows e outros drivers precisam de execução própria dos mesmos gates.

Passaram **330 comparações RGB** do novo gate (110 por executor), com erro
máximo de 1/255. Também passaram as 279 comparações de regressão de raster,
46 gates CTest e 34 testes Rust em série. Os dois builds passaram o modo CPU
do novo gate. Não houve skip nos gates finais.

O peeling usa a política padrão de profundidade do algoritmo, sem um nó
`SoDepthBuffer.write = FALSE` interferindo na coleta temporária do CoinGL.
O OIT usa um único contribuinte sobrevivente para comparar ao blend ordenado.
O oracle RTT usa produtor uniforme e UVs interiores: prova o teste de alpha
do produtor staged/direct, sem certificar filtros, bordas CLAMP ou geometria
geral de RTT. A recusa em sombras verifica diagnóstico e preservação do frame,
com controle GPU PHONG válido antes da entrada recusada.
