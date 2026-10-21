# B22 — origem da diferença de alpha no VSM nativo

Investigação de 18/10/2026 no CoinGL atual: AMD Renoir/radeonsi Mesa 25.2.8 e
NVIDIA RTX 3060 Laptop/615.71.09, contextos OpenGL Compatibility. A cena é a
`MultiScene(8)` do [teste de AlphaTest](../testsuite/coinrender/CoinRenderShadowAlphaTest.cpp):
slot 7, referência 0,5625, viewport 256×256, attachment RGBA32F, texel (154,128).
O shader legado codifica no alpha `2 * fract(64 * l * l) - 1`; esse componente
não é a opacidade original do material.

## Entradas e primeira diferença observada

A reprodução recuperou exatamente os valores históricos. Os fluxos reais de
quatro vértices GL_QUADS de cada um dos oito mapas são iguais bit a bit entre
GPUs, assim como as matrizes model-view/projeção capturadas antes do desenho.
Os shaders GLSL originais são idênticos. Os uniforms efetivos, comparados por
nome, também coincidem: nearval=0,90796947479248047 e
farval=9,8867654800415039. Viewport, formato, referência e estados consultados
de rasterização/clamp coincidem; ambas reportam oito bits subpixel.

Um controle de repetição do VS original por transform feedback, usando os
vértices reais e as mesmas matrizes, produziu `light_vec` e `gl_Position`
bit a bit iguais nas duas GPUs: quatro vértices por mapa, oito mapas, sem erro
GL e com imagem do observador preservada. Esse controle é uma repetição do
VS, não uma captura interna da execução original GL_QUADS.

A primeira diferença medida aparece em `light_vec` interpolado no fragmento:

| Etapa no texel (154,128) | AMD | NVIDIA |
| --- | --- | --- |
| light_vec.z | -4,2338285446166992 | -4,2338509559631348 |
| l | 0,37041255831718445 | 0,3704150915145874 |
| alpha original RGBA32F | 0,56229972839355469 | 0,56254005432128906 |

Exportar os intermediários manteve o alpha original bit a bit nos 81 pixels
da vizinhança 9×9, em ambas as GPUs. A partir do `l` próprio de cada GPU,
o cálculo independente float32 reproduziu l², fase e alpha em 81/81 pixels.
Há ainda um efeito aritmético secundário: no ponto central, a normalização
AMD original ficou um ULP abaixo da divisão float32 por etapas; a diferença
correspondente de alpha é 0,0000019073486328125. O controle que fornece Z
constante no fragment shader produziu resultados iguais entre GPUs para os
dois Z capturados, mas não reproduziu esse último bit do programa AMD
original. Portanto não se atribui toda a diferença exclusivamente à interpolação.

## Vizinhança e alteração controlada

Dois modelos de interpolação sem parâmetros ajustados foram confrontados
com os dados: coordenadas de viewport antes do arredondamento e coordenadas
arredondadas à grade de 1/256 de pixel. O segundo descreve melhor NVIDIA;
o primeiro descreve melhor AMD. Isso se manteve na vizinhança e após
translações X de -0,0001 e +0,0001 somente no caster do slot 7: 243 pixels
por GPU. A igualdade das entradas e das saídas do controle de VS foi
verificada em cada caso. Restam resíduos: o modelo racional com grade
aproxima NVIDIA a até dois ULP por componente; o modelo sem grade aproxima
AMD Z a até cinco ULP nas três cenas. Nenhum deles reproduz todos os bits.

Um experimento de software tornou explícita a grade de 1/256 apenas em XY
projetado do vertex shader, preservando light_vec, clip Z/W e comparadores:

| Diferença absoluta AMD–NVIDIA | Original | Grade explícita |
| --- | --- | --- |
| light_vec.z no ponto | 0,000022411346435546875 | 0,000000476837158203125 |
| alpha no ponto | 0,000240325927734375 | 0,0000057220458984375 |
| máximo de alpha nos 81 pixels | 0,000247955322265625 | 0,0000095367431640625 |

O controle sem a alteração reproduziu os dados anteriores. A redução com
uma alteração exclusivamente geométrica sustenta a interpolação/rasterização
subpixel como origem dominante. A grade explícita foi um experimento
descartável; nenhuma alteração foi aplicada aos shaders do produto.

## Decisão do comparador e classificação

Os oito comparadores foram medidos diretamente no shader original 32F com a
mesma referência 0,5625. NEVER descarta em ambas; ALWAYS aceita em ambas;
LESS/LEQUAL aceitam somente AMD; GREATER/GEQUAL aceitam somente NVIDIA;
EQUAL descarta em ambas; NOTEQUAL aceita em ambas. Não houve mudança de
referência, epsilon ou tolerância para produzir essa conclusão.

A diferença não começa na transmissão dos dados pelo Coin ao OpenGL. A
investigação localiza sua componente dominante na interpolação nativa e
caracteriza a amplificação pela codificação VSM, com resíduo aritmético
explícito. Não demonstra bug de driver, comportamento indefinido ou qual GPU
representaria um resultado universalmente correto. Os resultados se limitam
às cenas e aos drivers medidos, não a todos os triângulos ou implementações.

A [especificação OpenGL, capítulos 14 e 15](https://registry.khronos.org/OpenGL/specs/gl/glspec46.compatibility.pdf)
é a referência para rasterização/interpolação. A
[ARB_color_buffer_float, questões 28 e 37](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_color_buffer_float.txt)
não exige uma única precisão do AlphaTest nem igualdade entre alpha
armazenado e alpha usado pelo teste. A observação histórica NVIDIA/16F
(alpha armazenado 0,5625) não foi requalificada nesta investigação 32F;
não se infere dela a precisão interna do comparador.

## Decisão de aceite após a investigação

Em 18/10/2026, o responsável pelo projeto definiu a entrega correta e
determinística à GPU como garantia do caminho nativo; resultados de pixels
não precisam ser idênticos entre máquinas. B22 fica encerrado como variação
numérica nativa aceita no domínio 32F investigado, sem correção do renderizador.
A diferença de interpolação e o resíduo aritmético foram caracterizados com
entradas iguais; o experimento de grade explícita não será incorporado ao
produto para forçar igualdade entre GPUs.

As comparações estritas anteriores permanecem divergentes e são preservadas
como diagnóstico, deixando de bloquear o aceite por esse motivo. Isso não
converte os resultados históricos em PASS nem encerra outras lacunas de C07.
O perfil portátil/canônico conserva seu contrato independente quando pedido.
A observação histórica 16F continua sem requalificação nesta investigação;
não é necessária para a decisão tomada sobre o caso 32F.

O alpha codificado do VSM e a opacidade autoral continuam sendo propriedades
distintas. Uma eventual mudança dessa semântica seria uma evolução própria,
não uma correção necessária para igualar o resultado das duas GPUs.
