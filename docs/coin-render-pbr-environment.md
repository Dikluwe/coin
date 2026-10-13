# SoPBRMaterial — ambiente global pré-calculado (M3)

O perfil BGFX/Vulkan pode iluminar faces PBR opacas com um ambiente HDR global.
A radiância continua linear no attachment RGBA16F e recebe exposição, Neutral e
sRGB no passe de saída. M3 usa interação única; compensação de múltiplo
espalhamento e filtragem especular em movimento ficam para M4.

## Uso

```sh
coin_render_pbr_baker --source studio --output studio.cibl
coin_render_pbr_baker --source asymmetric --output asymmetric.cibl
coin_render_viewer --fixture assembly --view iso-perspective --offscreen \
  --environment studio.cibl --direct-intensity 0 --frames 2 \
  --capture-prefix assembly-ibl
```

No SDK experimental, `CoinRenderOptions::pbrEnvironmentBundle` é um `SbString`.
Vazio mantém a rota direta M2. `pbrEnvironmentRotation` é um ângulo em radianos
em torno de +Z mundial; a CLI usa graus. `pbrEnvironmentIntensity` aceita [0,64]
e multiplica somente a iluminação indireta. `pbrExposureStops` continua [-16,16].
As opções pertencem ao target. Um pacote carregado é imutável durante seu
lifetime; substituir um arquivo sob o mesmo caminho não altera frames ativos.
Para trocar seu conteúdo, criar outro target com o pacote desejado.

## Modelo e matemática

O baker CPU usa `SbVec*`, `SbColor`, `SbList`, `SbByteBuffer` e `SbString`.
Câmera e transformação usam `SbMatrix`/`SbRotation`; o tempo usa `cc_time`.
As estruturas nativas existentes de CoinRender permanecem em uso. O pacote é
um recurso privado, sem grafo ou renderer paralelo.

O ambiente é procedural e contínuo: studio tem fontes amplas neutras,
asymmetric tem fontes de intensidades e cores diferentes. WHITE e AXIS são
controles analíticos de teste. Não há dependência de imagens HDR licenciadas.
Cubemap fonte, prefilter GGX e irradiância são calculados a partir da mesma
radiância. Cada nível especular recebe integração GGX ponderada por N·L com
V=N; mipmaps comuns não participam da construção.

A LUT usa amostragem de normais visíveis GGX e Smith correlacionado; a
referência dos testes usa integração independente em double. A técnica de
amostragem está descrita em [Heitz 2018](https://jcgt.org/published/0007/04/01/).
A implementação define `alpha=max(roughness,0.045)^2`, coerente com M2.

A aproximação split-sum avalia `Lpref(R,r) * (F0*A+B)`, com
`F0=mix(0.04,baseColor,metallic)`. A parcela difusa é
`E_SH(N)/pi * baseColor * (1-metallic) * (1-F0) * (1-C)`.
C é a integral de Schlick Fc ponderada pelo cosseno. Isso reproduz a parcela
difusa do BRDF sob ambiente branco; sob radiância variável permanece uma
aproximação angular. SH9 conserva baixas frequências, podendo produzir valores
negativos truncados para zero. A formulação de SH e split-sum é discutida em
[Filament](https://google.github.io/filament/main/filament.html).

A LUT é RGBA16F: A, B, C e Eavg. Eavg é a integral hemisférica de A+B,
reservada para a compensação em M4. Esse formato amplia a proposta inicial RG
e evita LUTs distintas para as parcelas do mesmo modelo. A textura é indexada
por N·V e rugosidade perceptual, com coordenadas nos centros dos texels.
O nível zero avalia o piso 0,045; os outros usam i/(níveis-1). O LOD interpola
explicitamente entre 0,045 e o primeiro nível para não ampliar metais lisos.

Normais e reflexão saem do espaço de vista usando a inversa `SbMatrix` da
câmera. A rotação do ambiente aplica Rz(-ângulo) às direções de amostragem.
IBL exige uma câmera rígida, com diagnóstico para vistas não rígidas.

## Pacote e validação

`.cibl` é little-endian, magic COINIBL1, versão/modelo/orientação 1.
O header tem 72 bytes; cada descritor, 40. Contém as seis faces fonte na ordem
+X,-X,+Y,-Y,+Z,-Z, todas as faces e níveis GGX, LUT RGBA16F e nove coeficientes
RGB f32 de irradiância. Os fatores de convolução SH são pi, 2pi/3 e pi/4.
O manifest JSON registra dimensões, amostras, offsets, bytes e identidade.

O loader valida a disposição canônica completa, limites, versões, orientação,
checksum FNV-1a64, truncamento e dados excedentes. Cores são finitas e não
negativas, alpha é 1, a LUT tem domínio limitado e SH deve ser finito.
Checksum identifica conteúdo e corrupção acidental; não é autenticação.
Falha mantém intacto um pacote anterior e não publica um frame candidato.

O limite de payload é 64 MiB; cubemaps aceitam potências de dois de 8 a 512.
Studio e asymmetric usam 512, dez níveis, LUT 256, 256 amostras de prefilter
(64 no nível zero), 2048 na LUT e 65536 para SH. Cada payload tem 29.884.508
bytes, aproximadamente 28,50 MiB. As três texturas usam nominalmente
29.884.400 bytes; os 108 bytes restantes são SH. Alignment/alocador e
attachments HDR são custos adicionais. O target retém outro payload CPU do
mesmo tamanho para reconstrução; não é uma medição de memória física GPU.

## Lifetime e evidência

O target conserva uma cópia CPU const; handles pertencem ao executor e domínio
do dispositivo. Resize recria attachments, preservando o ambiente. Aposentadoria
do dispositivo destrói handles e mantém o pacote; o próximo executor o envia
novamente, mesmo após remoção do arquivo. O teste exerce essa reconstrução
controlada, sem alegar simular todas as causas de perda física do dispositivo.
Replay aquecido mantém um upload e reutiliza o programa. Após publicação
legada, o diagnóstico privado de radiância não expõe HDR antigo.

A execução Release na AMD RADV RENOIR passou 20 CTests BGFX, 11 Recording e
12 Rust. O teste GPU compara 18 white furnaces com integração independente,
orientações nas seis faces, bordas/cantos, rotação, intensidade, continuidade do
primeiro LOD e o último nível físico. O loader é testado com corrupção que
recalcula o checksum para exercitar a validação semântica.

A matriz real do viewer passou 22/22: 20 casos positivos, 40 imagens de primeiro
frame/replay, igualdade janela/offscreen, dois ambientes, rotação e intensidade;
duas entradas inválidas foram recusadas. Os 26 controles legados mantiveram
os hashes dos pixels. [Resultados](validation/pbr/m3/summary.json),
[comandos](validation/pbr/m3/commands.json) e [galeria](validation/pbr/m3/gallery.png).
Os binários grandes são reproduzidos pelo baker; manifests e hashes ficam no
repositório. M4 ainda deve qualificar ferramentas CAD, energia e estabilidade;
M5 deve concluir instalação, budgets e campanha de performance.
