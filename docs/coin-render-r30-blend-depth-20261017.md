# R30 — BLEND com ZBUFFERON e CLEARZBUFFER

O teste de produto combina uma camada frontal azul com transparência 0,5,
override `BLEND` e o objeto base vermelho. Para a camada à frente, `ZBUFFERON`
mistura 36 pixels. Ao colocá-la atrás, a profundidade bloqueia todos os
pixels azuis. `CLEARZBUFFER` libera a camada traseira e volta a misturar
36 pixels. A comparação do frame RGB inteiro contra CoinGL admite no máximo
um nível por canal.

Os três casos passaram nas sete células GPU Linux disponíveis (21/21):
BGFX Vulkan/OpenGL em AMD/NVIDIA; wgpu Vulkan em AMD/NVIDIA e OpenGL em AMD.
Todos os sete processos `CoinRenderProductTest` terminaram com sucesso.
[Resultados, logs e hashes](validation/coin-render-r30-blend-depth-20261017/verification.json).

O contrato qualificado cobre uma camada frontal e um objeto base nesse
perfil. Políticas mistas no mesmo frame, janela/resize e demais combinações
do manager continuam abertas em R30/C19.
