# R30 — foreground com transparência BLEND herdada

O controle visual usa cena opaca vermelha e uma superimposition frontal azul
com material de transparência 0,5. A camada herda `BLEND` do manager, sem
override próprio e sem teste de depth. O centro compõe vermelho e azul;
o frame inteiro foi comparado com CoinGL offscreen corrigido.

As sete células GPU Linux passaram com erro máximo de **um nível RGB por
canal**: soma 36 na AMD e 72 na NVIDIA, preservada nos executores BGFX/wgpu.
Os centros CoinRender foram `(128,0,128)` na AMD e `(128,0,127)` na NVIDIA.
[Logs e hashes](validation/coin-render-r30-translucent-20261017/verification.json).

Esse gate qualifica uma camada frontal simples com `BLEND` herdado. A
[continuação](coin-render-r30-two-translucent-20261017.md) qualificou duas
camadas em ordem. Ainda faltam transparência configurada **por camada**,
interação com depth/clear, outros tipos de transparência,
janela e plataformas externas.
