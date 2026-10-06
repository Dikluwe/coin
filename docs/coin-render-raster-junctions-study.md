# Estudo: raster nas junções curvas e coincidentes

Registrado em 2026-10-06 por solicitação do usuário. O critério escolhido para
P02/P04/P05/P06 é o **contrato portátil CPU/BGFX/wgpu**, com diferenças do
raster CoinGL delimitadas. A reprodução específica do raster deste driver é
um estudo de melhoria futura; não bloqueia o primeiro perfil portátil.
Este registro não declara concluída a validação de P02/P04/P05/P06: diferenças
entre CPU e BGFX/wgpu continuam exigindo correção e qualificação.

## Evidência atual

Os [logs diagnósticos preservados](validation/raster-junctions-study-20261006/README.md)
registram o reproducer e o controle FILLED, com o alcance das observações.

A fixture `CoinRenderProceduralTextureTest --probe-cone` reproduzia MAE 2,93584
e máximo 120 RGB entre CPU e wgpu em LINES/DEFAULT. A lateral e a tampa
emitiam bordas coincidentes em sentidos opostos, com UVs distintos. Dar uma
orientação comum à aritmética das bordas sólidas preserva seus atributos e a
ordem de publicação e reduziu o máximo a 1. Padrões de stipple conservam a
orientação autoral, pois ela determina sua fase.

A esfera conserva concordância CPU/wgpu de máximo 1, mas o reproducer nativo
`--probe-sphere` mede máximo 9 contra CoinGL no perfil com mipmaps P07.
O testemunho de profundidade mediu, no pixel (26,23), aproximadamente
0,32754022 na expansão comum e 0,30524847 no CoinGL. O contexto nativo fornece
D24, mas a diferença é muito maior que uma unidade D24. Isso demonstra uma
seleção diferente de fragmentos nas junções; não demonstra isoladamente que
um dos dois rasterizadores esteja errado.

Uma execução FILLED de controle conserva concordância RGB e, no mesmo pixel,
nenhum dos dois cobre a face. Experimentos com interpolação perpendicular,
alinhamento por células e extrapolação dos caps não eliminaram todas as
junções e introduziram outras diferenças. Esses experimentos **não fazem parte da
implementação mantida na branch**. O perfil conserva a expansão comum validada e
não amplia tolerâncias para fazer o oráculo nativo passar.

## Perguntas e próximos experimentos

- Identificar a borda e o polígono que fornecem cor, UV e profundidade em cada
  pixel divergente, com testemunhos por segmento e captura de estado GL.
- Separar cobertura, interpolação, caps, ownership de profundidade e LOD;
  usar segmentos isolados, cruzamentos mínimos e valores numéricos autorais.
- Comparar larguras pares/ímpares, sentidos de emissão, câmeras ortográficas/
  perspectivas, clipping, alpha e texturas com contraste controlado.
- Repetir em APIs/drivers e GPUs diferentes antes de escolher uma melhoria
  portátil ou uma rota específica de compatibilidade CoinGL.
- Medir custo de preparação, geometria expandida e submissão em campanha
  isolada; os tempos CTest não são benchmark.

O estudo deverá propor alternativas concretas com imagens e métricas, custos,
limites e efeito sobre estado/rejeição/recuperação. O esperado portátil permanece
fixo enquanto a abordagem futura não for escolhida e qualificada.

## Como reproduzir

Usar os builds Release e o ambiente GPU da campanha integrada da branch
`codex/coin-render`. O reproducer da esfera continua retornando erro quando o
oráculo nativo ultrapassa MAE 1/máximo 3, para manter a diferença observável.

```sh
CoinRenderProceduralTextureTest --probe-sphere
CoinRenderProceduralTextureTest --probe-cone
COIN_PROBE_FILLED=1 CoinRenderProceduralTextureTest --probe-sphere
```

Os gates de estilos verificam CPU/BGFX/wgpu. Referências CoinGL brutas são
mantidas nas células qualificadas e identificadas nos logs; aprovação portátil
não significa igualdade pixel a pixel nas junções nativas.
