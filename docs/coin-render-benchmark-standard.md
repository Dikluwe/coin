# Padrão de comparação de render no Coin

A **referência principal é o renderer OpenGL clássico do Coin3D**: `SoGLRenderAction`. No benchmark offscreen ele é exercitado por `SoOffscreenRenderer`, com `coin_render_gl_benchmark --backend gl`. No benchmark de janela, use `coin_render_window_benchmark --backend coin-gl`.

**BGFX/OpenGL é uma variante experimental**, separada da referência CoinGL. As comparações antes/depois dentro de um backend são medidas complementares de otimização e regressão; devem acompanhar a comparação principal com CoinGL.

## Regras da campanha

- Compare cada variante com CoinGL na mesma GPU física, sessão, cena, câmera, resolução, perfil de atualização e política de transparência. Uma referência NVIDIA não qualifica uma variante AMD.
- Use o mesmo escopo: janela/apresentação ou offscreen/readback. Para offscreen, mantenha readback síncrono de cor e cópia para o consumidor nos caminhos comparados. Fluxos assíncronos, borrowed output e leitura de profundidade precisam de campanhas separadas.
- Execute cada caminho em processo novo, em pelo menos três rodadas com ordem intercalada. Para a cidade estática deste host, use 30 quadros de aquecimento e 120 medidos. Registre mediana, p95 e logs por processo, sem builds ou outras medições GPU concorrentes.
- Separe primeiro quadro, tempo até a primeira imagem desde o início do programa e quadros aquecidos. O primeiro quadro do executável exclui parsing, enquadramento, construção dos targets e capability probe. Processo novo não significa caches do sistema/driver vazios.
- Confirme o renderer/GPU do CoinGL por uma execução diagnóstica separada com `COIN_DEBUG_GLGLUE=1`; desative a sonda durante a medição. Registre o tipo de contexto e as variáveis de ambiente necessárias.
- Mantenha os caches padrão de cada renderer e documente seu efeito. CoinGL pode usar display lists; CoinRender pode reutilizar o plano. Use campanhas dinâmicas separadas para verificar atualização da cena.
- Compare imagens RGB orientadas na mesma GPU, além de medir tempo. O PPM do executável já corrige a orientação GL. Hash RGBA bruto entre CoinGL e CoinRender não é um critério de igualdade: orientação e alpha do fundo diferem. Registre erros e diferenças observadas, sem declarar igualdade apenas por uma imagem reduzida.
- Registre revisão e hashes das bibliotecas. Declare se CoinGL vem da revisão local ou de um checkout upstream intocado. Não misture métricas de GPUs, revisões ou contratos de saída diferentes.

A [campanha CoinGL Linux de 2026-10-04](coin-render-coingl-reference-linux.md) aplica este padrão à cidade de 40 mil prédios.
