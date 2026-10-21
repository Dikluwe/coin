# B10 — cache de cubos sob dimensões dinâmicas

Na cidade de 40 mil cubos com geometria 100% animada, o trace anterior contou
39.969 expulsões do cache de 32 templates. Testei interromper a aprendizagem
após 1.024 expulsões quando estas superavam em 16 vezes os reusos de range.
Isso preservaria o caminho de captura normal; era uma hipótese de redução do
custo de substituição do cache, não uma mudança de imagem.

Quatro processos A/B/B/A usaram BGFX AMD/Vulkan, 512², 10 frames de aquecimento
e 20 medidos. A imagem final foi idêntica nos quatro processos (SHA-256
`15e0deb6b2b394e7c1b277d0b8ea3c9463529762e554db872df4a808242e041e`).

| Processo | Mediana total | p95 total |
| --- | ---: | ---: |
| A1, cache atual | 180,993 ms | 187,682 ms |
| B1, corte de aprendizagem | 182,166 ms | 185,136 ms |
| B2, corte de aprendizagem | 199,488 ms | 235,497 ms |
| A2, cache atual | 189,799 ms | 212,594 ms |

As duas medianas B pioraram; a cauda é instável. **O candidato foi revertido.**
`CoinBgfxCoreTest` passou após reconstruir a biblioteca original. A campanha
não demonstra redução de latência nem autoriza qualificação em outras GPUs.
[Métricas, logs e imagens](validation/coin-render-b10-cube-churn-20261017/verification.json).

Uma malha unitária compartilhada, com dimensões transferidas para a matriz de
modelo, é uma alternativa estrutural a investigar. O caminho atual de overlay
em `CoinRenderActionP::prepareTranslationOverlay` atualiza somente posições de
vértices para um `SoCube` alterado. Antes de adotar a malha unitária, esse
caminho deve atualizar também a escala dos estados de renderização, ou recusar
o overlay desses cubos e reconstruir o plano. A qualificação precisa cobrir
mudanças sucessivas de dimensão, translação, material e câmera, além do
rollback, e preservar normais, orientação, transparência e ordenação. Sem essa
migração, o primeiro quadro poderia estar correto e os seguintes obsoletos.
