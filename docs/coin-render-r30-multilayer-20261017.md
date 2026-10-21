# R30 — duas superimpositions frontais

O perfil frontal sem teste/escrita de profundidade, sem limpeza de depth e com
transparência herdada agora tem controle de **duas camadas ativas**. A primeira
desenha um quadrado verde de 144 pixels; a segunda, adicionada depois, cobre
os 36 pixels centrais em azul. Desabilitar ou remover a segunda restaura
exatamente a imagem da primeira; reabilitá-la reproduz exatamente a imagem
de duas camadas. A faixa verde externa permanece intacta.

O teste `CoinRenderProductTest` passou em sete células físicas Linux: BGFX
OpenGL/Vulkan AMD e NVIDIA; wgpu Vulkan AMD e NVIDIA e OpenGL AMD. Em todas,
o oráculo CoinGL encontrou os mesmos 36 pixels azuis, sem diferença RGB na
camada. O teste também repete o frame sem alteração e verifica a recuperação
após ativar/desativar a primeira camada. [Logs e hashes](validation/coin-render-r30-multilayer-20261017/verification.json).

Esse resultado qualifica a ordem de duas camadas neste perfil. Flags de
background, teste/limpeza de depth, transparência própria, modos diferentes
de `SCENE`/`AS_IS`, composição integral do fundo e plataformas externas
continuam abertos ou recusados conforme a [matriz](coin-render-feature-matrix.md).
