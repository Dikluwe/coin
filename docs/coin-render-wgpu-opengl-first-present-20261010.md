# Primeira apresentação wgpu/OpenGL no X11

No recibo `light-final-wgpu-opengl-native-terrain-million`, a primeira
captura XGetImage tinha cobertura visível zero, enquanto o render offscreen
da mesma cena tinha 401.466 pixels visíveis. A captura final passou RGB 0.
O erro é intermitente: sete repetições posteriores do terreno passaram
RGB 0 inicial/final, quatro delas com a espera anterior a XGetImage reduzida
de 50 ms para zero em uma cópia temporária do consumidor. Essa instrumentação
foi removida após o diagnóstico.

A tentativa de ler a própria superfície wgpu/OpenGL retornou
`Window surface does not support COPY_SRC readback`. Assim, o recibo físico
depende de XGetImage; ele não distingue ainda se a imagem preta veio de uma
apresentação omitida ou de uma leitura antes de a janela atualizar. A falha
continua aberta. O próximo diagnóstico precisa registrar uma recorrência
com o estado da apresentação e capturas X11 repetidas sem novo render.

Os logs locais estão em
`/mnt/Laranja/Git/externos/coin/build-coinrender-terrain-results/` com
prefixos `light-final-wgpu-opengl-native-terrain-million`,
`retry-wgpu-gl-terrain-`, `retry-full-wgpu-gl-terrain-` e
`zero-delay-wgpu-gl-`.
