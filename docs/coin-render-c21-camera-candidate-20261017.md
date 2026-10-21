# C21 — contracontrole da projeção por caster

Foi testado um candidato que marcava os estados após uma câmera dentro de
`shadowMapScene` e enviava view/projection próprios aos casters BGFX e wgpu,
preservando a câmera da luz para os receivers. Também foi testada a leitura das
matrizes do volume de visão do `SoCallbackAction`. O candidato publicou quadros
e reproduziu o delta máximo de luminância do CoinGL (spot 360; direcional 333)
na AMD/Vulkan, mas **não reproduziu a distribuição da sombra**.

Depois de inverter as linhas do readback GPU para alinhar com o CoinGL, o erro
médio absoluto da *mudança* de luminância nos 85×85 pixels centrais foi 64,356
para spot e 118,071 para direcional em BGFX/AMD/Vulkan; wgpu/AMD/Vulkan teve
63,973 e 118,123. O erro da imagem com câmera subiu de 24,136 para 41,929
na spot e de 32,167 para 97,722 na direcional, em relação à imagem-base.
As [quatro imagens do caso direcional](validation/coin-render-c21-camera-candidate-20261017/)
mostram as regiões discordantes. Os testes sem câmera continuaram passando,
portanto a igualdade dos dois deltas máximos era um gate insuficiente.

**A aceitação foi revertida.** A recusa atômica permanece até que a projeção
do mapa e a transformação de entrada do grupo sejam reproduzidas contra o
CoinGL com um oráculo espacial, incluindo câmeras antes/depois de casters e
mais de uma câmera no mesmo mapa. A próxima investigação deve comparar as
coordenadas projetadas dos casters e o mapa de momentos por pixel, não somente
a imagem final ou a amplitude máxima.
As bibliotecas BGFX e wgpu foram reconstruídas após a reversão; a suíte completa
`CoinRenderShadowReferenceTest` passou em AMD/Vulkan para ambas, com a câmera
própria novamente recusada e o oráculo CoinGL mantido.

[BGFX/AMD](validation/coin-render-c21-camera-candidate-20261017/coinrender-c21-candidate-bgfx-amd-vulkan.log),
[wgpu/AMD](validation/coin-render-c21-camera-candidate-20261017/coinrender-c21-candidate-wgpu-amd-vulkan.log).
[BGFX restaurado](validation/coin-render-c21-camera-candidate-20261017/coinrender-c21-restored-bgfx-amd-vulkan.log),
[wgpu restaurado](validation/coin-render-c21-camera-candidate-20261017/coinrender-c21-restored-wgpu-amd-vulkan.log).
