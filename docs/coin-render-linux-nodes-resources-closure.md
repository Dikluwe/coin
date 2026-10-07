# P03/P15/P16/P24/P28 — continuação Linux

Esta entrega amplia o perfil neste PC. As campanhas anteriores continuam em
seus diretórios; [a nova evidência](validation/resources-linux-20261006/README.md)
identifica o código, os binários e as configurações desta rodada: 48 células
nativas da matriz e três células focadas de rejeição, todas PASS.

## Subclasses registradas depois da action

`SoCallbackAction` conserva as inscrições e expande suas cinco tabelas quando
novos tipos aparecem. Inscrições herdadas mantêm ordem e multiplicidade;
registro de outro tipo não duplica os observadores já expandidos. A revisão dos
callbacks continua representando inscrições do usuário; crescimento do registro
não invalida observadores de tipos internos nem desliga seus atalhos.

O gate CPU registra subclasses de SoShape e SoShaderProgram depois das actions,
verifica pre/post/primitivas, PRUNE, ABORT, captura e rejeição/recuperação do
shader ativo. ABORT conserva o comportamento nativo dos callbacks posteriores.
A correção fica no pimpl, sem alteração da ABI pública Coin.

O Polygon volta a usar `SoShape::callback` e seu `generatePrimitives`, com os
limites já publicados. Isso evita a reconstrução de uma subcena temporária de
SoLineSet e conserva observadores do tipo real. O estudo de registro tardio está
resolvido para este caminho; depth clamp geométrico do bbox continua estudo.

## Consumidores Part::Spline

A fixture cria Bezier, BSpline curva e BSpline superfície reais. Para os dois
novos controles usa respectivamente 5×1 e 5×4 polos, ativa/desativa ControlPoints,
modifica um polo, regenera Shape, remove o objeto e verifica pixels e idle no
viewport. Isso qualifica esse ciclo do consumidor Part, sem certificar editores,
arraste por eventos ou todos os workbenches.

## RTT direto de janela e recuperação no host

O consumidor pode ser uma janela GPU. Os produtores permanecem attachments
privados no dispositivo da janela, com identificação de dono, geração e fences;
o modo direto continua sem staging de pixels. O teste nativo verifica alteração
do produtor, resize do produtor/janela, formato recusado, imagem anterior
preservada na janela e recuperação na mesma integração.

Quarter mantém uma surface saudável que já publicou um quadro quando a action
recusa a candidata com UNSUPPORTED. A primeira falha sem publicação e falhas de
surface/dispositivo continuam no tratamento existente. `wgpuFrameStatus` e
`wgpuFrameError` permitem verificar o diagnóstico. Um resize de janela que
repete o tamanho atual conserva publicação e geração; um tamanho diferente
invalida o serial até a próxima publicação.

A ponte wgpu informa o serial de publicação por surface, independente de outros
alvos e candidatos recusados. A consulta privada adiciona um símbolo; os layouts
e a versão 49 do transporte não mudam. Recompilar CoinRender e a ponte juntos,
incluindo o export Windows, é obrigatório. Windows ainda precisa de execução.

O [patch incremental FreeCAD](../examples/coinrender/freecad_linux_rtt_recovery.patch)
é aplicado **depois** do patch Linux retido anterior e da integração P16 local.
Validar com `git apply --check --ignore-space-change`; recompilar FreeCADGui e
MeshGui contra este build Coin/CoinRender. O patch anterior isolado continua
usando o contorno retido, portanto não representa esta continuação.

## Mipmaps staged de RTT RGBA8

SoSceneTexture2 usa linear/base em `0 < q ≤ 0,5` e trilinear em
`0,5 < q ≤ 0,85`, distinguindo o limiar dos filtros de imagens armazenadas.
Qualidade zero desliga o consumo e a reativação recaptura o recurso. O Core
gera a cadeia box RGBA8 a partir dos pixels staged; Infra só faz upload.
Mips requerem dimensões POT, até 2.048 por eixo, e entram no orçamento nominal
64 MiB do grafo, incluindo usos nos produtores e o débito conservador existente.

Na referência CoinGL, cruzar o limiar de qualidade invalida o buffer/imagem do
produtor. Pbuffer usa o mesmo filtro linear/trilinear do FBO. O controle reduz
um checker vermelho/azul, compara os mipmaps com CoinGL, passa repetidamente pelo
limiar e verifica NPOT recusado, publicação preservada e recuperação. Não
certifica anisotropia arbitrária nem equivalência de raster sem mipmaps.

RTT direto com mipmaps é recusado durante admissão do grafo, antes de submeter
qualquer produtor. Não muda para staged silenciosamente. Geração GPU de mips,
formatos float/HDR/depth/sRGB e dimensões maiores continuam implementação futura.

## Configuração e pendências

A qualificação de janela usa Vulkan AMD e BGFX OpenGL com GLX NVIDIA/EGL Mesa
AMD, provado pelo contexto atual do backend. O cache de programas BGFX está
explicitamente desativado nesta campanha. Seu timeout anterior em Part::Spline
com cache ativo permanece estudo, sem alegação de correção. O oráculo offscreen
FBO/pbuffer usa a configuração NVIDIA já qualificada neste PC; a sessão isolada
não conseguiu criar esse offscreen e não conta como comparação aprovada.

Windows, outros dispositivos e input completo permanecem separados. Shaders
próprios, volume/cubo e MSAA/multipass têm
[contratos definidos](coin-render-portable-resources-contract.md), mas ainda não
possuem executores. Essas pendências exigem implementação e validação; não são
restrições do PC nem itens fechados pela definição do contrato.
