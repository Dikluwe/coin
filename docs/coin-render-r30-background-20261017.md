# R30 — superimposition BACKGROUND

**Continuação:** [fronteiras de depth do manager](coin-render-r30-manager-depth-boundaries-20261017.md)
qualificaram depois `BACKGROUND|ZBUFFERON` e `CLEARZBUFFER` frontal. O texto
abaixo descreve o primeiro perfil, sem depth no fundo.

O adaptador agora separa as camadas ativas pelo flag `BACKGROUND` e compõe
as de fundo antes da cena principal; as frontais vêm depois. A ordem de
registro é preservada dentro de cada grupo. O root composto continua
reutilizável quando cena, camadas e flags não mudam. O perfil admitido usa
transparência herdada, sem `CLEARZBUFFER`; `BACKGROUND|ZBUFFERON` ainda é
recusado até qualificação própria.

O teste de produto exercita fundo azul, cena vermelha e uma camada frontal
verde. Registra a frontal antes da de fundo para provar que o flag, e não a
posição global de registro, determina a composição. Um controle separado
usa só fundo e cena. As imagens inteiras das duas combinações são RGB
idênticas ao CoinGL corrigido nas sete células físicas Linux: BGFX
AMD/NVIDIA OpenGL/Vulkan e wgpu AMD/NVIDIA Vulkan mais AMD OpenGL.
[Logs e hashes](validation/coin-render-r30-background-20261017/verification.json).

`CLEARZBUFFER`, depth ligado em camada de fundo, transparência própria,
janela/resize e plataformas externas permanecem abertos. A representação
`clearDepthBefore` já existe no plano/execução; falta transmitir a fronteira
da camada do manager à captura antes de aceitar `CLEARZBUFFER`.
