# R30 — limpeza offscreen por frame e depth das camadas

**Continuação:** [fronteiras de depth do manager](coin-render-r30-manager-depth-boundaries-20261017.md)
qualificaram depois `CLEARZBUFFER` frontal e background com depth nas sete
células. Os limites ao final deste relatório são os da primeira rodada.

O callback de `SoOffscreenRenderer` limpava cor e depth em **cada** `apply()`
da `SoGLRenderAction`. `SoRenderManager` aplica a mesma action separadamente
à cena principal e às superimpositions; também aplica várias vezes em modos
de linha. Assim, o oráculo offscreen apagava o depth e a cor entre passagens.
O callback agora se remove após a primeira limpeza de cada frame offscreen.

O efeito corrigiu três conclusões baseadas nas capturas anteriores:

| Controle | Captura antiga | Captura após correção |
| --- | --- | --- |
| HIDDEN_LINE, esfera 64×64 | CoinGL tinha 619 pixels de linha, igual a WIREFRAME | CoinGL tem 408; WIREFRAME tem 619; diferença de máscara 211 |
| HIDDEN_LINE, cubo inclinado | CoinGL 230 pixels em ambos os modos | CoinGL 181 ocultos, 230 em WIREFRAME; CoinRender 180/229 |
| Foreground verde | Erro RGB integral 159.120; 624 canais de fundo mudados no CoinGL | Erro RGB integral zero; nenhum canal de fundo mudou |

Com o oráculo correto, o adaptador admite uma camada frontal com
`ZBUFFERON`, sem `CLEARZBUFFER`. O plano mantém o depth da cena principal,
liga teste/escrita na camada e inclui os flags na chave do root composto.
No controle ortográfico, a camada verde atrás da base vermelha fica oculta;
à frente fica visível. As duas imagens inteiras têm erro RGB **zero** contra
CoinGL nas sete células físicas Linux. O gate do teste exige essa igualdade,
além de provar ocultação em HIDDEN_LINE e paridade integral do foreground
sem depth. [Logs, hashes e métricas](validation/coin-render-r30-offscreen-fix-20261017/verification.json).

As sete células: BGFX AMD/NVIDIA OpenGL e Vulkan; wgpu AMD/NVIDIA Vulkan e
AMD OpenGL. `CLEARZBUFFER`, `BACKGROUND`, transparência própria da camada,
janela/resize e outras plataformas ainda precisam de implementação ou
qualificação. Os recibos antigos permanecem arquivados como histórico da
falha do oráculo, sem representar o estado atual.
