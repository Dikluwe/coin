# SoPBRMaterial — qualificação final M5

O primeiro perfil implementado é **Linux, BGFX/Vulkan, materiais opacos,
HDR e ambiente global pré-calculado**, consumido pelo CoinRender. M0–M5
estão organizados no [ledger](coin-render-pbr-milestones.md). A API permanece
experimental. A execução usa AMD RADV RENOIR 1002:1638, Mesa 25.2.8, em Release.
As imagens e números abaixo vêm do renderer real.

## Base Coin e atualização de recursos

SoPBRMaterial participa do estado/travessia Coin. Câmeras, geometria e baker
usam `SbVec*`, `SbMatrix`, `SbRotation`, `SbBox*`, `SbBSPTree`, `SbList`,
`SbByteBuffer` e `SbString`; esta última usa `cc_string`. Tempo e variáveis de
ambiente usam `cc_time` e os utilitários Coin. A prova de recursos usa
`SoNodeList`, `SbList`, `SbPList` e o `SbHash` interno. As estruturas já
existentes do FramePlan e da API de readback conservam seus tipos. Não foi
introduzido outro grafo, biblioteca matemática ou camada arquitetural.

Uma edição de baseColor, metallic, roughness ou emissive pode reaproveitar a
captura geométrica. A prova exige root/câmera qualificados, campos totalmente
autoriais, sem conexão/ignored/override, fatores com fonte inequívoca e ausência
de texturas/shadows. Notificações não qualificadas, subclasses, callbacks,
fontes distintas com fatores internados iguais ou alteração da propriedade
de slots seguem captura completa. A otimização não muda a semântica desses
casos. Callback nodes são inspecionados sem executar callbacks de busca.

A atualização usa o undo/commit já existente. Falha mantém a publicação e a
última captura válida. Testes comparam o plano otimizado com uma captura
independente completa e contam chamadas reais a generatePrimitives. Na GPU,
o caso qualificado registra `geometry_capture=0`, `geometry_buffer_reused=1`,
`material_patch=1`, `material_patch_ranges=0` e `material_patch_vertices=0`.
Fatores PBR são uniforms; atributos legados sem consumo pelo shader PBR são
canonicalizados no lowering. Emissão HDR conserva valores acima de um.

`CoinRenderTarget::setPbrExposureStops()` aceita stops finitos [-16,16].
`setPbrEnvironmentParameters()` aceita rotação finita em radianos sobre +Z
mundial e intensidade finita [0,64]. Os setters não notificam a cena, não
recapturam geometria nem recarregam o ambiente. O próximo frame usa os valores;
entradas inválidas preservam configuração, pixels e serial. Alterar exposição
preserva a radiância HDR anterior. Trocar o conteúdo do pacote exige outro
target; a cópia CPU congelada permanece válida após remover o arquivo.

## SDK e ciclo de vida

O consumidor [público](../examples/coinrender/sdk-consumer/pbr-main.cpp) foi
configurado a partir do diretório **instalado**, com apenas o include/pacotes
do SDK. Não inclui headers privados do baker/backend. Verifica controle
SoMaterial, SoPBRMaterial, ambiente, replay, fatores, exposição e suspensão/
resize. O SDK instala o baker, viewer e helpers necessários às views CAD.
O baker instalado reproduziu os pacotes studio/asymmetric 512, dez níveis,
LUT 256, com hashes SHA-256 idênticos às entradas de M3.

Nos SDKs Recording e Rust, o consumidor exige `UNSUPPORTED` para PBR e pixels/
serial legados preservados. Isso qualifica a rejeição; shading PBR nesses
executores continua fora do perfil.

A suspensão de tamanho zero é tratada antes de capturar um viewport sem área.
O teste nativo Xlib verifica resize, suspensão/resume, replay e perda de device
**controlada por injeção no executor**, seguida de reconstrução da janela e
de um peer offscreen com arquivos de origem removidos. O teste IBL também
reconstrói peers com ambientes distintos após aposentadoria do domínio comum.
A publicação CPU anterior permanece disponível após a perda; os recursos GPU
são aposentados. Captura de janela só pode ser solicitada após o redraw que
recupera TARGET_READY. A prova não representa um reset físico do driver.

Os testes de apresentação mantêm drawables Xlib com tamanho/visibilidade
controlados. Isso impede interferência do window manager sem remover as
asserções de pixels, resize, múltiplos alvos ou recuperação. SoInteraction é
inicializado para registrar corretamente SoSelection nas ferramentas CAD.

## Resultado funcional

- **33/33 BGFX**, incluindo núcleo Coin, materiais/recursos PBR, saída, IBL,
  qualidade CAD, publicação, profundidade, readback e múltiplos alvos.
- **13/13 Recording e 14/14 Rust** nas regressões selecionadas, mais os
  consumidores dos três SDKs instalados.
- **106/106 casos do viewer instalado**: 72 views principais em 1280×960,
  280 imagens, 52.170 probes geométricos, duas órbitas de 36 frames, ferramentas,
  cortes e montagem real FreeCAD. Pixels idênticos aos da campanha M4.
- **26/26 baseline M0**, com hashes de pixels preservados. Os runners M2/M3
  selecionam explicitamente seus controles históricos: 27/27 e 22/22.
  M2 preserva os pixels. Cinco casos M3 diferem em até um nível de canal em
  poucos pixels, após a versão de shader M4; o controle matemático independente
  de interação única continua passando. As diferenças estão quantificadas
  em [deltas M3](validation/pbr/m5/m3-pixel-deltas.json).

Os contratos e controles matemáticos M1–M4 continuam em
[material](coin-render-pbr-material.md), [shading](coin-render-pbr-shading.md),
[ambiente](coin-render-pbr-environment.md) e [CAD](coin-render-pbr-cad-quality.md).
As evidências finais estão em [M5](validation/pbr/m5/summary.json).

## Release: CPU, GPU e recursos

Cada caso usa câmera iso-perspective fixa, janela 1280×960 e 144 frames,
sem readback por frame. São descartados 16 frames para as estatísticas
aquecidas, restando 128. GPU total vem de timestamps BGFX associados ao frame
submetido. Tempos de passes individuais ficaram indisponíveis com identidade
de frame/view estrita; não são substituídos por valores de outros frames.

CPU abaixo é a soma dos **intervalos instrumentados** de traversal/FramePlan,
validação/preparo e lowering/upload/encode, excluindo submit e espera pelas
consultas. Não equivale ao consumo CPU total do processo. Valores em ms:

| Carga | CPU mediana / p95 | GPU frame mediana / p95 | Primeira chamada render |
|---|---:|---:|---:|
| Assembly legada | 0,0131 / 0,0218 | 0,2230 / 0,2322 | 59,31 |
| Assembly PBR | 0,0179 / 0,0280 | 0,8991 / 0,9474 | 334,31 |
| Housing PBR com ferramentas | 0,0207 / 0,0328 | 1,1930 / 1,2365 | 337,22 |
| Montagem FreeCAD convertida | 0,0110 / 0,0210 | 1,0950 / 1,1469 | 477,74 |

A primeira chamada inclui preparação/upload do ambiente e pipelines. Na
Assembly PBR, o submit CPU mediano foi 0,0054 ms; a drenagem das consultas GPU,
1,1389 ms, separada no relatório. O viewer finito sem captura não dorme entre
frames, mas a instrumentação avança frames adicionais para obter queries.
Esses números não são uma taxa de apresentação/FPS. O baseline M0 fixa cenas
e câmeras, sem gate numérico de FPS; M5 registra a medição sem inventar um
limiar retroativo. Há 128/128 hits de plano/geometria aquecidos em cada caso,
um upload de ambiente por executor e programas estáveis em regime aquecido.
Não há alegação de cache de compilação em disco Vulkan.

O pacote CPU tem 29.884.508 bytes; a textura média derivada adiciona 2.048.
Os texels GPU do ambiente somam 29.886.448 bytes (SH9 é uniforme). O alvo HDR
RGBA16F+D32F ocupa nominalmente 12 bytes/pixel: 14.745.600 em 1280×960.
O alvo SDR offscreen com profundidade ocupa mais 8 bytes/pixel; apresentação
e staging têm alocações próprias. O profiler BGFX reportou 197.738.496 bytes
de memória GPU usada na Assembly PBR, 29.935.604 de texturas e 14.745.600 de
render targets. Esses contadores incluem o runtime/allocator, não só PBR.
O RSS máximo do processo foi 161.420 KiB; RSS não mede VRAM. A GPU é integrada.

Limites verificáveis: payload do pacote 64 MiB; prova de materiais até 1.024
fontes/65.536 slots; inventário de callbacks até 65.536 nós, com fallback;
16 views por target e 11 views técnicas disponíveis; readback comum até
128 MiB/16 tickets. A LUT/prefilter versionados, entradas inválidas e excesso
de layers são rejeitados antes de publicar o candidato. Contadores completos,
amostras brutas, pico RSS e comandos estão no
[relatório de medição](validation/pbr/m5/benchmark/results.json).

## Alcance do perfil

IBL split-sum, SH9 e residual difuso sob radiância angularmente variável são
aproximações documentadas. Não há GI dinâmica. Layers técnicos são opacos
BASE_COLOR legados, compostos após a saída com profundidade; SoText3 usa
triângulos. O pick do viewer é determinístico para a qualificação, sem um
editor CAD completo. Texturas PBR/tangentes, alpha-mask/blend, shadows PBR,
mistura de materiais legados na base, readback PBR assíncrono e outras APIs/
plataformas continuam fora deste perfil e não são contabilizados como aprovados.
