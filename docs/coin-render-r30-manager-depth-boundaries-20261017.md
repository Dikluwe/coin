# R30 — fronteiras de depth das camadas do manager

O adaptador agora transmite à action as fronteiras dos separadores que ele
cria para o manager. A captura atribui um `renderLayer` por passagem e marca
`clearDepthBefore` no primeiro draw quando a superimposition usa
`CLEARZBUFFER`. Quando há backgrounds, a cena principal também recebe uma
fronteira com limpeza de depth, como em `SoRenderManager::render` no CoinGL.
As fronteiras são associadas aos ponteiros dos separadores pertencentes ao
adaptador, sem interpretar nomes ou IDs de shapes do usuário.

Foram validadas quatro combinações com imagem RGB integral idêntica ao CoinGL
nas sete células GPU Linux:

| Combinação | Controle decisivo |
| --- | --- |
| Foreground `ZBUFFERON|CLEARZBUFFER` | Camada verde atrás da cena vermelha aparece após limpar depth |
| Background `ZBUFFERON` | Cena vermelha aparece sobre fundo azul mais próximo porque depth é limpo antes da cena |
| Dois backgrounds `ZBUFFERON` | O primeiro azul oculta o segundo verde na área de sobreposição; a cena vermelha ainda aparece |
| Background, cena e foreground | Ordem visual correta mesmo com registro da camada frontal antes da de fundo |

BGFX OpenGL/Vulkan AMD e NVIDIA, wgpu Vulkan AMD e NVIDIA e wgpu OpenGL AMD
passaram 7/7. `CoinRenderCompositionTest` passou no controle CPU.
[Logs e hashes](validation/coin-render-r30-manager-depth-boundaries-20261017/verification.json).

A [continuação com camada vazia](coin-render-r30-empty-clear-20261017.md)
carrega a limpeza para o primeiro draw seguinte no mesmo viewport, também
com paridade RGB integral nas sete células.
Transparência própria, outros modos de manager, janela/resize e plataformas
externas seguem abertos.
