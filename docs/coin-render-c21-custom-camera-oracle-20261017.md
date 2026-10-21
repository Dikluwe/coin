# C21 — câmera dentro de `shadowMapScene`

O CoinGL constrói cada mapa com a câmera da luz antes de inserir os filhos de
`shadowMapScene`. Uma câmera nessa cena, portanto, pode substituir a projeção
dos casters seguintes. O CoinRender captura a cena independente, mas hoje
recusa essa câmera antes da submissão; os executores calculam a projeção de
todos os casters com a matriz única do passe da luz.

A fixture de cenas independentes passou a renderizar o mesmo caso no CoinGL
com e sem uma `SoOrthographicCamera` própria, antes de conferir a recusa
atômica do CoinRender e a recuperação. Em AMD e NVIDIA físicas, ambas via
BGFX/Vulkan para a parte CoinRender, a suíte completa passou. O delta máximo
de luminância CoinGL na região central foi **360 para spot e 333 para
direcional** nas duas GPUs. Logo, ignorar a câmera não preservaria o caso
válido. Os controles CoinRender continuaram a exigir status `UNSUPPORTED`,
serial e pixels anteriores intactos, e sucesso após remover a câmera.

Próxima implementação: a captura precisa marcar quais casters usam a câmera
própria e transportar sua view/projection ao mapa. O BGFX já calcula MVP por
draw, mas atualmente o faz com `pass.view/projectionCoin`; a ponte wgpu também
precisa de transporte por caster. A câmera da luz continua sendo a base do
receiver, e a normalização dos momentos precisa ser comparada com CoinGL.
Há que testar câmera antes/depois de casters, câmeras múltiplas, separadores,
mutação e rollback em spot/direcional antes de remover a recusa.

[AMD/Vulkan](validation/coin-render-c21-camera-20261017/coinrender-c21-amd-vulkan.log),
[NVIDIA/Vulkan](validation/coin-render-c21-camera-20261017/coinrender-c21-nvidia-vulkan.log),
[fixture](../testsuite/coinrender/CoinRenderShadowReferenceTest.cpp).
