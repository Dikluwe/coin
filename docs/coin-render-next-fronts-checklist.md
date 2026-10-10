# CoinRender: checklist das próximas frentes

Atualizada em 2026-10-09, incluindo a API de sampling e sua continuação Linux, com a entrega
P03/P15/P16/P24/P28 após o fechamento
P02/P04/P05/P06 e do primeiro perfil P07, na branch
`codex/coin-render`. Os itens abaixo são pendências; capacidades já implementadas
precisam de ampliação ou qualificação apenas nos escopos indicados.
O [plano geral](coin-render-work-plan.md) conserva os identificadores P/F/A.

O checkout de continuação deste PC fica em
`/mnt/Laranja/Git/externos/coin-render`, na branch `codex/coin-render`.
Esta checklist, fontes e evidências em `docs/validation` são versionados e
publicados no fork. Não usar `/tmp` como checkout de desenvolvimento nem como
única cópia de evidências; os paths `/tmp` nos logs anteriores são históricos.
Builds, SDKs e arquivos ainda necessários à continuação devem ficar em armazenamento
permanente. O antigo checkout temporário foi recuperado do commit `20f987b7c9`
em 2026-10-07; seus binários não versionados precisam ser reconstruídos.

## 1. Contrato comum de texturas — P07, primeira implementação

- [x] Definir o perfil inicial de UV DEFAULT/FUNCTION com textura ativa,
  funções autorais, espaços de coordenadas e transformação projetiva no
  [contrato P07 inicial](coin-render-p07-procedural-textures.md). Matrizes ampliadas por shape permanecem abertas.
- [x] Capturar as coordenadas na camada comum e entregar snapshots aos executores;
  qualificar callbacks, fast path e subclasses de faces no perfil P07 inicial.
- [x] Ampliar qualidade, filtros, mipmaps e formatos no
  [perfil P07 fechado](coin-render-p07-sampling-rtt-contract.md): nearest/linear,
  dois filtros de mipmaps, L/LA/RGB/RGBA POT, qualidade ≤ 0,85 e 128 MiB por cadeia.
- [x] Cobrir unidade 0 e multitextura, modelos legados, textura desligada,
  reativação, erro sem publicação e recuperação; 231 cenas/seis rejeições por
  execução GPU, com referência CoinGL. DECAL L/LA tem rejeição explícita.
- [x] Diagnosticar FBO/pbuffer e definir matrizes identidade na entrada de cada
  produtor, matrizes próprias/aninhadas e restauração do consumidor. Corrigidos
  a herança FBO e o filtro pbuffer na fronteira 0,5; RTT permanece linear/base.

### Texturas avançadas — extensão P07/P24 de 2026-10-07

- [x] NPOT 2D sem rescaling implícito e mipmaps que incluem bordas ímpares.
- [x] Anisotropia com limites explícitos: CPU/wgpu 1/2/4/8/16;
  BGFX 1/máximo nativo, com recusa atômica de fatores 2/4/8.
- [x] SRGB opt-in do alvo, decode antes do filtro e alpha linear;
  modelos legados em unidades 0/3, identidade de formatos no cache.
- [x] Compressão BC3 determinística, base alinhada a 4×4, mips comprimidos,
  admissão nativa do GPU e referência numérica independente.
- [x] HDR RGBA16F em produtor RTT direto e consumidor, sem clamp para RGBA8
  antes da composição; staged/HDR32 recusados, com recuperação.
- [x] Mipmaps RTT gerados dentro do GPU: wgpu POT/NPOT e BGFX POT/NPOT no Linux.
  OpenGL/wgpu usa cópias internas GPU para isolar views; orçamento conservador,
  tokens tipados, falha sem publicação e recuperação.
- [x] Views independentes para samplers base/mips da mesma imagem armazenada em
  BGFX/OpenGL: duplicação apenas do nível base, sem alias de LOD, inclusive
  replay do cache; [controle AMD](coin-render-advanced-textures-mixed-views-20261008.md).
- [x] Views independentes para tokens RTT diretos compartilhados por samplers
  base/mips em BGFX/OpenGL: cópia do nível base no GPU, sem readback e com
  [controle AMD nativo/portátil](coin-render-advanced-textures-direct-views-20261008.md).
- [ ] Fechar a qualificação da redução NPOT direta no BGFX: implementação GPU
  por área e 16/16 processos Linux PASS em AMD/NVIDIA Vulkan/OpenGL, com
  dimensões até 2047², HDR e rollback de token. O cenário combinado de oito
  sombras, transparência e mips NPOT passou em oito perfis AMD/NVIDIA
  Vulkan/OpenGL; NVIDIA/OpenGL exige EGL surfaceless neste PC. O custo de
  captura sincronizada NPOT sem/com mips foi medido exploratoriamente.
  O custo dos cinco frames GPU de mips foi isolado em 8/8 perfis, com mediana
  0,0477–0,1558 ms neste fixture; [método e ledger](coin-render-bgfx-npot-mip-gpu-20261009.md).
  Windows passou funcionalmente em D3D12/Vulkan/OpenGL, oito sombras e
  peeling/weighted OIT; custo GPU completo passou em Vulkan/OpenGL. Falta
  coletar todos os cinco timestamps por cadeia em D3D12, conforme a [campanha Windows de 2026-10-10](coin-render-sampling-api-windows-20261010.md).
  O seletor não oferece D3D11: dx11 é o nome do shader DXBC usado pelo
  D3D12, e sua compilação não qualifica execução D3D11.
  [Implementação](coin-render-bgfx-npot-direct-mips-plan-20261008.md) e
  [continuação do gate](coin-render-bgfx-npot-shadow-oit-20261009.md).
- [ ] Rescaling/SoTextureScalePolicy legado, codec de melhor qualidade,
  ETC2/ASTC/BC1/BC5/BC7, saída HDR/tone mapping, Windows/Android/FreeCAD.
  O perfil NPOT pequeno de `USE_TEXTURE_QUALITY`/`SCALE_DOWN`/`SCALE_UP`
  passou 4/4 processos AMD/NVIDIA Vulkan/OpenGL; `FRACTURE` agora é recusada
  sem publicação quando a textura está ativa. Rescaling completo e recorte de
  subtexturas continuam abertos. Sob o override legado que desativa NPOT,
  resize POT por vizinho para qualidade de escala <0,5 passou a sonda 17×19
  em Vulkan AMD/NVIDIA e OpenGL NVIDIA/Mesa; qualidade alta usa a mesma
  `simage_resize` do CoinGL quando disponível, com erro máximo de 1 canal nas
  mesmas rotas e em imagens de 1 a 4 componentes (24 comparações por rota).
  O primeiro resize agora fica em cache por revisão de upload de `SoTexture2`;
  mudança de `model`, transição de política/qualidade e notificações de wrap/imagem
  passaram em AMD/NVIDIA Vulkan e OpenGL NVIDIA/Mesa, com erro máximo de 1
  canal. A dica `enableCompressedTexture` agora segue a escolha do primeiro
  upload e o estado BC3 persistente após notificação de wrap; as sondas
  passaram em AMD/NVIDIA Vulkan e NVIDIA/Mesa OpenGL com erro GPU/CoinGL
  zero. A captura agora consulta validade e flags persistentes do `SoGLImage`
  no nó; a sonda de troca de contexto com política, qualidade de escala,
  notificação de wrap e compressão passou em AMD/NVIDIA Vulkan e
  NVIDIA/Mesa OpenGL. `textureQuality` alterada sem notificação agora conserva
  a qualidade do upload para filtro e extensão POT; a sonda 300×300 cobre
  256×256 antes da notificação e 512×512 após ela (máximo 2 para
  escala/qualidade, 0 para compressão). No caminho NPOT nativo, a qualidade
  do primeiro upload agora também persiste sem CoinGL e entre contextos:
  sonda 0,3→0,1 com reupload após wrap passou nos quatro perfis locais
  (GPU/CoinGL máximo 1). O fallback GLU 2D agora usa reamostragem CPU por
  interpolação linear e caixa. Com simage oculto, a sonda POT passou em
  AMD/NVIDIA Vulkan e NVIDIA/Mesa OpenGL (24 combinações por perfil,
  GPU/CoinGL máximo 2); sem simage nem GLU, o fallback por vizinho passou
  em AMD/Vulkan (máximo 1).
  O oráculo nativo compara `legacyResizeGlu` com GLU em RGBA 17×19 para
  16×16 e 32×32, além do vetor de borda. Os quatro jobs do
  [gate multiplataforma](coin-render-texture-pot-resize-20261009.md)
  passaram em Ubuntu, Windows e macOS ARM/Intel. A integração CoinGL
  completa nos runners macOS segue bloqueada pelo contexto CGL offscreen;
  o runtime estático Windows excedeu o timeout da captura anterior.
  A ampliação Linux verificou 12 redimensionamentos GLU nativos (máximo 1)
  e 72 comparações GPU/CoinGL em cada perfil AMD/NVIDIA Vulkan/OpenGL
  (máximo 2), incluindo texturas pequenas e não quadradas; o
  [ledger](validation/glu-pot-expanded-20261009/summary.json) guarda os logs.
  Os 12 redimensionamentos também passaram no
  [CI Windows/macOS](https://github.com/Dikluwe/coin/actions/runs/37968646516).
  [Contrato e ledger NPOT](coin-render-texture-scale-policy-20261009.md) e
  [resize POT](coin-render-texture-pot-resize-20261009.md).
- [x] Isolar o sampling projetivo AMD: GPU/CoinGL idênticos em wgpu e BGFX Vulkan;
  quantização nearest e footprint explicam o diagnóstico, com contracontrole NVIDIA.
  O gate GPU completo de sampling passou 8/8 processos BGFX AMD/NVIDIA
  Vulkan/OpenGL, native/portable, com o Mesa privado corrigido. O Mesa instalado
  ainda diverge; [gate, runner e condição](coin-render-full-gpu-sampling-gate-20261009.md).
- [x] Testar LOD explícito + fetch/centro como estudo isolado: nearest CPU–GPU
  MAE0/max0 em AMD/NVIDIA e wgpu/BGFX, oracle EGL sem Coin, alpha independente,
  texturas avançadas e benchmarks GPU/janela, incluindo cidades40mil/1milhão.
  [Avaliação e decisão](coin-render-portable-sampling-study-20261007.md): manter
  native padrão; contrato portátil nearest como opção futura. Fetch completo é mais caro.
- [x] Verificar o candidato POT de uma amostra `base`/`base_uniform`:
  contraexemplo AMD em mips profundos, inclusive LOD calculado das UV originais;
  [sonda sem Coin e alternativa pelo mip fino ativo](coin-render-base-sampling-counterexample-20261008.md).
  Uniformizar tamanho não corrige a escolha; candidato `fine` passou o oracle inicial.
- [x] Integrar e medir `fine`/`fine_uniform` no estudo isolado: uma amostra em POT,
  fallback NPOT de duas e magnificação linear; mip profundo/formatos/RTT explícito/
  viewport deslocado, seis configurações, erro máximo1 nos controles novos.
  [Resultados e decisão](coin-render-fine-sampling-study-20261008.md): `fine_uniform`
  reduz custo GPU frente a dois centros (≈34% AMD/60% NVIDIA com8 unidades), mas
  native continua mais rápido. Produção recebe somente docs/evidências.
- [x] Separar perfis nativos e experimentais no laboratório e investigar NVIDIA/
  1milhão: prefixo wgpu3040 bytes/instancing nativo e fragmentos BGFX separados;
  162 controles (134 PASS/28 FAIL conhecidos), Rust46 PASS e84 medições de janela.
  [Relatório e limites](coin-render-native-sampling-path-study-20261008.md): custo
  extra AMD/wgpu removido nesta amostra;36 controles válidos do milhão e144 hashes/
  timestamps de diagnóstico não reproduzem regressão persistente do seletor.
  Coin original e código de produção intactos; política pública continua pendente.
- [x] Tratar as 28 falhas da campanha original no estudo: quatro corrigidas em
  strokes/profundidade BGFX e 24 passam com workaround Mesa **externo e isolado**;
  campanha original162/162 PASS, gates preservados.
  [Correções, ledger e condições](coin-render-failure-closure-20261008.md).
  Driver instalado e código de produção continuam sem essa correção de sampling.
- [x] Fechar o gate estrito CoinGL AMD no Renoir/Mesa 25.2.8:
  oito FAIL de estilos estritos também no baseline; contrato portátil8/8 PASS.
  [Sonda da borda de plano de usuário](coin-render-raster-amd-followup-20261008.md):
  preenchimento e borda original presentes, borda nova ausente em CoinGL
  mesmo com deslocamentos do plano. O controle OpenGL sem Coin reproduziu
  a ausência somente no radeonsi AMD: NVIDIA e llvmpipe desenham a borda;
  na AMD, o polígono pré-recortado e a linha explícita também a desenham.
  `gl_ClipDistance` no shader confirma a mesma perda na AMD, enquanto llvmpipe
  preserva a borda. CoinGL agora complementa a borda ou os pontos criados pelo
  recorte nesse perfil de driver e quad; o gate estrito completo passou em
  CPU e BGFX/Vulkan AMD, com llvmpipe como controle. A fase do stipple é
  verificada separadamente para cada polígono, conforme a especificação GL.
  [Correção e evidência](coin-render-raster-amd-strict-gate-20261009.md).
  A diferença de iluminação da célula de geometria completa
  foi atribuída à triangulação: CoinRender usa diagonal 0–2; `GL_QUADS` no
  radeonsi Renoir usa 1–3, confirmado por oracle de cores não afins. Sondas de
  luz/material isoladas sustentam essa causa. A regra interna do clipper/flags
  de aresta do radeonsi ainda precisa de correção comprovada no Mesa; o contrato
  portátil continua obrigatório. O
  [oracle Mesa estrito e os ensaios de registradores](coin-render-mesa-clip-boundary-20261009.md)
  mostram a borda e os pontos ausentes no radeonsi, com controle llvmpipe;
  o protótipo de geometry shader gera 18/18 pixels de borda e 4+4 pixels nas
  extremidades, para plano fixo e `gl_ClipDistance`. Com o quad inteiro como
  entrada, elimina o ponto da diagonal interna. Um patch NIR experimental no
  Mesa privado fez o caso isolado `GL_POLYGON`/`LINE` passar na AMD. A
  primeira variante `GL_TRIANGLES` causou reset de GPU; após corrigir a
  transição GS→NULL, a variante restrita a seis vértices e `LINE` passou em
  quatro ensaios físicos sem reset. A variante `POLYGON`/`POINT` também
  passou em GPU física; as três variantes combinadas passaram com e sem
  `AMD_DEBUG=nongg`. O shader do aplicativo marcava `CLIP_DIST0` como
  `no_varying` porque foi ligado sem GS; um patch NIR experimental preservou
  essa saída para o GS suplementar. Com ele, o oracle OpenGL isolado passou
  completo na AMD física nos dois caminhos do radeonsi, sem reset. A
  integração geral e a validação CoinGL original no Mesa candidato continuam
  abertas, com [patches e limites registrados](coin-render-mesa-clip-boundary-20261009.md).
  Não promover o workaround Mesa sem avaliar custo, limites e esses estudos.
- [x] Implementar API por alvo native/portable e capacidades v4 na branch de estudo:
  retirar seletor/packing experimental, declarar derivadas/formatos/anisotropia,
  herdar política em RTT e recusar incompatibilidade sem publicação. Linux offscreen:
  170/170 processos PASS, Rust46/46 e C11 PASS; driver instalado qualifica portátil,
  comparações CoinGL AMD delimitam uso do Mesa externo.
  [Contrato, API e ledger](coin-render-texture-sampling-api.md).
- [x] Qualificar a API em janela Xlib Linux neste PC: 30/30 processos
  janela/API/ownership e 24/24 portátil stock; Rust46/46, runnerQt81/81 e C11 PASS.
  Opções por manager/adapter e GPU física comprovada. [Continuação Linux e limites](coin-render-sampling-api-linux-20261008.md).
- [x] Qualificar FreeCAD Linux aquecido: DPR1 native/portable 12/12 PASS e
  portátil DPR2 6/6 PASS. Recibos físicos, picking, minificação, resize, remoção
  e idle preservados. SKIPs anteriores ficam separados; cold start não qualificado.
- [x] Medir native/portable sem readback e custo GPU separadamente: 144 processos
  CPU, 96 GPU/4.320 timestamps e 120 A/B native/baseline válidos. Corrigir varredura
  desnecessária da geometria; um milhão wgpu portátil ~100→10–19 ms por chamada.
  Oito texturas portáteis custam 1,48–3,30× GPU no fixture; native segue padrão.
- [ ] Estudar a transição inicial da borda do documento no FreeCAD/NVIDIA/wgpu
  portable: matriz sem aquecimento de texto 11/12 PASS, um FAIL de 1.476 pixels
  de borda, sem glyphs restantes. O [recorte do diff e os controles](coin-render-freecad-cold-border-study-20261008.md)
  isolam a moldura de 370×370 e a transição após mutar SoText2; a causa
  permanece aberta. Quatro controles AMD físicos tiveram diff final zero;
  a primeira tentativa NVIDIA ficou sem adaptador por incompatibilidade do
  driver. Após reinicialização, NVIDIA 615.71.09 e BGFX/Vulkan físico 380/380
  PASS. O shell `kiosk-shell.so` derrubou o Weston ao abrir o FreeCAD; com
  `desktop-shell.so`, oito controles frios NVIDIA native/portable e dois gates
  padrão passaram, sem diferença na borda. A transição histórica não reapareceu
  e o gate ainda registra `cold_start_qualified=false` ([ledger e limites](coin-render-freecad-cold-border-study-20261008.md)).
- [x] Diagnosticar BGFX milhão: 1.000.001 instâncias/160 bytes excedem o cache
  CPU de 128 MiB; plano não retido, lowering ~1,025 s e upload ~126 ms repetidos,
  apesar de reuso dos buffers GPU. Native e portable têm o mesmo gargalo.
- [x] Otimizar payload/replay BGFX mantendo orçamento explícito: após upload,
  liberar as instâncias CPU que impediam retenção; replay/cache hit no milhão
  em AMD/Vulkan native/portable. [Código, gates e A/B delimitado](coin-render-bgfx-million-replay-20261008.md).
- [ ] Estudar variação NVIDIA/wgpu (coorte longa ~25%, repetição isolada ~1,3%)
  e ampliar o replay BGFX a outras GPUs/cargas. [Novo A/B após o driver](coin-render-nvidia-million-after-driver-20261008.md):
  oito processos físicos wgpu/BGFX Vulkan válidos; a diferença de 25% não
  reapareceu e a variação wgpu dentro da mesma política excedeu a diferença
  agregada. Traces confirmam replay BGFX na NVIDIA, native/portable. Outras
  cargas e a causa da variação seguem abertas; não prometer FPS universal.
- [ ] Qualificar esta API em Windows/Android, outros consumidores/dispositivos e
  avaliar promoção para coin-render. Windows/GTX 1060 passou 102/102 gates
  funcionais native/portable e 12/12 consumidor instalado, com rejeição sem
  publicação e recuperação. Perfil geral Windows e promoção permanecem
  pendentes; a consulta estrita continua UNQUALIFIED_PROFILE. Ver a [campanha Windows de 2026-10-10](coin-render-sampling-api-windows-20261010.md). Sampling CoinGL AMD instalado continua
  divergente; gate MAE≤1,5/max≤4 preservado, estudos de raster abertos.
  [Decisão de 2026-10-08](coin-render-sampling-promotion-decision-20261008.md):
  manter native padrão e a API na branch de estudo até fechar os gates listados.
- [x] Reconciliar LargeBindings/MultiDevice: 25.600 draws independentes com clipping
  aceitando toda a geometria; upload exato GPU 96 bytes/instância, imagens/depth e
  reuso preservados; nove passes AMD Vulkan/OpenGL e NVIDIA Vulkan, incluindo stress.
  Ausência de adaptador agora produz SKIP explícito, sem falso PASS.
- [x] Atualizar a expectativa procedural de qualidade 0,95 para anisotropia suportada;
  manter recusa sem publicação em qualidade 1,01 e recuperação. Limites RGB preservados.

Diagnóstico, integração e limites desta continuação:
[Sampling AMD e instancing](coin-render-sampling-instancing-validation-20261007.md).

Escopo, controles e resultados estão no [contrato avançado](coin-render-advanced-textures-profile.md)
e no [relatório desta máquina](coin-render-advanced-textures-validation-20261007.md).
Esses limites não reabrem os itens históricos do primeiro perfil.

Fechamento: expectativas numéricas e cenas mínimas comuns, CPU/BGFX/wgpu,
referência CoinGL no domínio válido e capacidades/documentação atualizadas.

## 2. Paridade de geometria e viewport — P02/P04/P05/P06

Primeiro perfil portátil fechado: 204 CTests wgpu e 270 CTests BGFX
qualificados, com os dois skips conhecidos em cada perfil. Rust: 40 passes.
Recording: dez passes e dois skips das partes GPU, após executar seus controles CPU.
O [contrato ampliado](coin-render-geometry-viewport-contract.md) define o alcance.

- [x] Resolver viewport externo/vazio no Core, resize, múltiplas regiões,
  composição, annotations e preservação da projeção original.
- [x] Capturar/validar ranges normais, reversos, colapsados e clamp de entrada;
  rejeitar NaN/Inf sem publicação e recuperar a mesma action. Resolver slope e
  units sobre os primitivos originais do perfil, com controle numérico CPU/GPU.
- [x] Ampliar a matriz a 1.290 cenas: cinco bindings anteriores em faces,
  sete pares nos dez tipos adicionais, normais fornecidas/geradas, alpha,
  fast path, directional/point/spot e quatro modos de fog.
- [x] Corrigir bindings de QuadMesh/strips e normais/ocorrências de linhas
  indexadas; preservar fallback atômico e expectativas numéricas independentes.
- [x] Ampliar LINES/POINTS com UV DEFAULT/Plane/função autoral, unidade 0/
  multitextura, clipping e alpha: 311 cenas e 69 referências CoinGL por GPU.
- [x] Definir cobertura portátil top/left em passos de 1/256 de pixel;
  preservar ownership de bounding boxes, atributos de perspectiva, stipple e
  cache estático com digest do payload após expansão. Conservar os limites RGB.
- [x] Ampliar RTT com viewport externo em capture/FBO/pbuffer/staged/direto e
  sombras com controle crop ativo/inativo; fixtures sem expectativas circulares.
- [x] Concluir a qualificação integrada BGFX Vulkan/OpenGL e Recording;
  guardar [contagens, hashes e logs finais](validation/geometry-viewport-closure-20261006/summary.json).
  Dois asserts wgpu foram ajustados ao deslocamento raster; o timeout BGFX
  de sombras passou isolado com fonte/limite inalterados. Os eventos ficam nos logs.

Melhoria futura, fora deste primeiro perfil:

- [x] **Estudo inicial de rasterização:** isolar cobertura por sentido de linha,
  extrapolação/clamp dos caps e quantização subpixel AMD/Vulkan–CoinGL;
  preservar [sondas, imagens, métricas e alternativas](coin-render-raster-study-20261007.md).
  Essa conclusão fecha a investigação controlada desta rodada, não a melhoria
  de produção nem a igualdade nativa de todos os estilos.
- [ ] **Estudo:** reprodução das junções curvas/coincidentes e endpoints
  transparentes do CoinGL, conforme o [roteiro e evidências](coin-render-raster-junctions-study.md).
  Gate procedural OpenGL da esfera/linhas/UV DEFAULT também reproduzido em
  `6c91ed26`: máximo 4 contra limite 3; conservar a falha e as
  [evidências de continuação](coin-render-sampling-instancing-validation-20261007.md).
  Comparar abordagens e custos antes de escolher a melhoria. Isso não dispensa
  diferenças CPU/BGFX/wgpu: as divergências portáteis encontradas nesta rodada
  recebem correção e gates completos, sem exclusão OpenGL ou tolerância ampliada.
- [ ] Ampliar os 49 cruzamentos de bindings, subclasses/shapes adicionais,
  contornos côncavos, offset não planar e combinações de UV/estado além da matriz.
- [ ] Estudar depth clamp geométrico, precisão/formatos de depth adicionais e
  mais combinações de transparência/sombras/RTT; clamp de range já implementado
  não equivale a GL_DEPTH_CLAMP.
- [ ] Qualificar flat com sombras/iluminação por fragmento e o modo não padrão
  `COIN_QUADMESH_PRECISE_LIGHTING`; manter extensões identificadas.
- [ ] Repetir o perfil em outros drivers/dispositivos, Windows e FreeCAD.

Fechamento por perfil: fixtures comuns, restauração de estado, recuperação após
rejeição e diferenças nativas delimitadas no estudo, sem promessa universal.

## 3. Nós, FreeCAD e recursos além do perfil — P03/P15/P16/P24/P28

Entrega e limites: [perfil ampliado](coin-render-p03-p24-p28-profile.md) e
[continuação Linux neste PC](coin-render-linux-nodes-resources-closure.md).

- [ ] Ampliar a qualificação recente para Windows e consumidores reais de cada
  workbench; manter a célula Linux do viewport separada.
- [x] Adaptar StringLabel/DatumLabel, bbox, control points, Polygon/MeshGrid,
  ShapeScale/TransformDragger e preparo frio de ColorBar no perfil
  [Linux retido](coin-render-retained-linux-profile.md), com limites e rejeição.
- [ ] Qualificar consumidores completos de workbenches e arraste por input;
  repetir os adaptadores no [Windows](coin-render-retained-windows-checklist.md).
  Tipos reais no viewport não fecham todos
  os consumidores.
- [x] Ensaiar o consumidor Bezier Part::Spline: ControlPoints, Shape regenerada
  e remoção em DPR 1/2 nas três rotas; OpenGL no perfil explícito com cache de
  programas desativado.
- [x] Ensaiar BSpline curva e superfície Part::Spline, com polos 5×1/5×4,
  regeneração/remoção, object/weighted OIT e DPR 1/2 nas três rotas físicas.
  Operações completas de edição e consumidores de outros workbenches seguem abertos.
- [ ] Estudar os timeouts de Part::Spline/OpenGL DPR 1 com cache ativo;
  preservar a configuração qualificada e as duas tentativas sem captura.
- [x] Corrigir registro tardio de subclasses em SoCallbackAction sem duplicar
  callbacks/observadores; qualificar Polygon pelo generatePrimitives no viewport.
- [ ] **Estudo:** bbox com geometric depth clamp; o perfil qualificado continua
  com os cantos no volume de profundidade.
- [x] Disponibilizar delegação comum para subclasses SoText2/SoImage, preservando
  callbacks/observadores e falha sem publicação; adaptar SoColorBarLabel e
  SoFrameLabel com preparo frio no host.
- [x] Qualificar oito controles de conteúdo no viewport FreeCAD Linux:
  BGFX Vulkan e wgpu em object/weighted OIT e DPR 1/2; BGFX OpenGL em DPR 1.
  Adaptar imagens RGBA NPOT geradas pelo GUI e validar os 12 controles de
  NaviCube nas três rotas, com sete orientações, máscaras e picking.
- [x] Concluir BGFX OpenGL DPR 2 no perfil Linux com GLX NVIDIA/EGL Mesa,
  identificando AMD no contexto atual do backend. A prova GLX da janela não
  identifica a GPU EGL; combinações com timeout/falha de surface seguem estudo.
- [x] Ampliar RTT RGBA8 a unidades 0..7, quatro modelos e política explícita da
  subcena; definir/executar alpha convencional do produtor no Core e executores.
  Corrigir restauração FBO e sobrescrita de política pbuffer na referência.
- [x] Implementar RTT direto de janela com ownership do dispositivo consumidor,
  mutação/resize e rejeição/recuperação na mesma janela nas três rotas.
- [x] Implementar mips staged RGBA8 POT no Core, orçamento 64 MiB e filtro
  linear/trilinear RTT; qualificar transições FBO/pbuffer e recuperação.
- [ ] Implementar mips GPU diretos, formatos adicionais e dimensões maiores,
  preservando orçamento, propriedade e publicação transacional.
- [x] Definir o contrato portátil de shaders próprios antes de implementar
  tradução e recursos em BGFX/wgpu.
- [x] Definir planos comuns para texturas 3D, cube maps e RTT de cubo.
- [x] Definir MSAA/resolve e multipass na action/target, com memória e
  comportamento de resize/readback explícitos.

Os três últimos itens fecham definição de contrato, não execução. Shaders,
volume/cubo e MSAA continuam sem executor no perfil publicado. Referência:
[contratos dos recursos](coin-render-portable-resources-contract.md).

Fechamento por perfil: cenário real no host quando pertinente; ausência de
omissões silenciosas nos tipos adaptados; rejeição explícita fora do perfil.

## 4. Desempenho — continuação de P17/P18/P19

- [x] Congelar controles CoinGL/CoinRender e medir sem builds ou testes GPU
  concorrentes, separando janela sem readback e offscreen.
- [x] Medir primeiro quadro e regime aquecido: estático, câmera, materiais,
  transformações e geometria parcial/total, com mediana e p95.
- [x] Identificar o custo dominante entre captura, validação/composição,
  lowering/FFI, preparação GPU, submissão e publicação.
- [x] Implementar uma otimização delimitada com mecanismo de comparação A/B,
  preservando conteúdo, rejeições e caminhos de recuperação.
- [x] Revalidar reuso, caches, instancing, readback, memória e caudas de latência;
  registrar regressões e limites junto dos ganhos.

Fechamento: execuções repetidas/intercaladas, hashes dos controles, mesma
cena/resolução/driver e evidência visual. Tempos CTest não são benchmark.

Fechado localmente no Linux/RTX 3060 Laptop/NVIDIA 610.57.04: [relatório e
limites](coin-render-performance-continuation-linux.md), 252 processos
qualificados, 126 pares PPM idênticos e 36 pares auxiliares de digest de janela.
Tentativa de janela com monitor desligado ficou excluída e arquivada; a
repetição ativa auditou DPMS entre processos e restaurou o estado original.
A reserva temporária mantém os limites existentes. O p95 BGFX/Vulkan offscreen
piorou nesta amostra e permanece registrado junto dos ganhos de mediana.
Validação incremental, reuso de lowering, timestamps de janela wgpu e ampliação
de hardware seguem como estudos futuros; Windows e outras GPUs não são
qualificados por este fechamento.

- [x] Executar um piloto A/B Windows offscreen na revisão corrigida: seis
  casos e seis combinações backend/API, 252 pares de imagens idênticos,
  234 processos de medição qualificados e regressões de p95 registradas.
- [ ] Ampliar Windows para janela sem readback e campanha de latência maior,
  controlando variação do host/clocks e investigando as regressões observadas.
  Em 2026-10-10, fixture 64×64 passou 24/24 ABBA, 60 warmup + 600 frames
  medidos por processo, com clocks/P-state registrados e sem readback no
  trecho medido. Retorno CPU render/present; não fecha workloads maiores,
  clock fixado ou latência de display. Ver a [campanha Windows de 2026-10-10](coin-render-sampling-api-windows-20261010.md).

O [relatório Windows](coin-render-windows-continuation-validation-20261007.md)
preserva dados, exclusões e limites do piloto; não demonstra ganho uniforme
nem encerra uma campanha completa de desempenho Windows.

## 5. Hardware, superfícies e sombras — P20/P21/P22/P23/P27

- [x] Requalificar sombras Linux nas duas GPUs físicas: BGFX Vulkan/OpenGL e
  wgpu Vulkan, com GPU/oráculo reais e sem skips no perfil.
- [x] Diagnosticar o pixmap AMD e comparar 32 capturas X11 janela/offscreen;
  opacidade e janela qualificadas. Transparência offscreen Vulkan mantém
  quatro pixels de borda fora do gate CoinGL, documentados como estudo.
- [x] Definir expectativa independente para oito mapas: conservação de
  intensidade, contribuição da oitava luz, nona rejeitada sem publicação e
  recuperação exata. O limite CoinGL de oito unidades foi demonstrado nas
  duas GPUs; sua referência nativa de oito mapas continua externa.
- [x] Executar `BumpProgramGLX` nas duas GPUs, com visual double-buffer no PRIME.
  Executar câmera com CoinGL na NVIDIA e AMD/OpenGL, e contrato portátil nas
  três APIs AMD. Diferenças de borda AMD/Vulkan permanecem explicitamente abertas.
- [x] Ampliar Wayland/wgpu Vulkan: AMD/NVIDIA, escala **observada** 1/2,
  pixels iguais ao offscreen/CoinGL, duas janelas, resize, suspensão e três
  recriações reais de superfície com isolamento da janela sobrevivente.
- [x] Recompilar ponte Rust e objetos Android arm64/API 26; retirar GL da ação
  comum/profiler quando o renderer legado está desligado.
- [ ] Melhorar a seleção de fragmentos nas bordas AMD/Vulkan de P20/câmera,
  mantendo o gate atual. A [câmera branca foi diagnosticada](coin-render-raster-study-20261007.md)
  por quantização subpixel; validar snap comum em câmera/viewport/RTT e aplicar
  os controles à transparência P20 antes de declarar a mesma causa.
- [x] Concluir o primeiro **build/link e APK Android x86_64 neste Linux**:
  perfil CPU do Coin sem libGL/libGLES, renderer legado recusado, pacote
  assinado/instalado e apresentação wgpu/OpenGL ES no AVD.
- [ ] Remover completamente os fontes/referências GL remanescentes do Coin base;
  a primeira fronteira Android conserva ABI com referências fracas indisponíveis.
- [ ] Qualificar Vulkan nesta imagem Android: Goldfish é não conforme e a
  execução de diagnóstico falha em `vulkan.ranchu.so` no submit. Sem fallback.
- [ ] Fechar a célula Intel física e repetir os perfis nas GPUs/APIs previstas.
- [ ] Qualificar CoinGL nativo com oito mapas em contexto com nove unidades
  utilizáveis; a expectativa portátil local não encerra essa célula.
- [x] Recompilar MSVC BGFX/wgpu na revisão atual, instalar SDKs experimentais
  isolados e executar consumidores públicos nas três APIs de cada backend.
- [x] Repetir os gates Windows BGFX D3D12/Vulkan/OpenGL e wgpu dx12/Vulkan/gl,
  com GPU/oráculo obrigatórios, correções e resultados por rodada registrados.
- [x] Ampliar Win32 nesta revisão: duas janelas, resize/minimização, três
  recriações reais de HWND, seriais isolados e ticket offscreen sobrevivente.
  Pixels janela/offscreen iguais nas três APIs BGFX e em wgpu D3D12/Vulkan;
  wgpu/OpenGL qualifica apresentação/rejeição de captura, sem comparar a janela.
- [x] Capturar visualmente o fixture de sampling Win32 wgpu/OpenGL sem COPY_SRC:
  GDI dos pixels apresentados, native/portable, resize/remap/manager, mesmo
  oracle de mip e limite RGB 1. Gates de janela: 6/6 smokes + 6/6 sampling na
  [campanha Windows de 2026-10-10](coin-render-sampling-api-windows-20261010.md); não generaliza todos os consumidores visuais.
- [ ] Fechar Windows com DPI físico distinto entre monitores e perda real de
  dispositivo em campanha controlada. Em 2026-10-10 ambos retornaram 96 DPI;
  fault injection não foi contado como perda física real.
- [ ] Qualificar AppKit/Metal e Wayland em compositor físico, escala fracionária,
  mudança de monitor, formato de swapchain e demais perfis visuais; BGFX/Wayland
  continua sem mecanismo neste conector.
- [x] Validar no AVD x86_64 a primeira execução wgpu/OpenGL ES: apresentação,
  captura offscreen 64×64, ausência de captura da janela, seriais crescentes,
  HOME/retomada, rotação 1080×2400 ↔ 2400×1080 e encerramento `OK`.
- [x] Ampliar o AVD x86_64/GLES para 40.000 prédios: asset no APK, launcher padrão,
  giro/zoom e loop contínuo; três recriações reais de janela com offscreen vivo,
  rotação, duas reaberturas após BACK no mesmo processo e controle do cubo.
- [x] Liberar runtime Android ocioso antes da thread da Activity terminar;
  epochs avançam e alvos/recursos externos vivos impedem esse teardown.
- [ ] Ampliar Android para duas janelas, API automática, tickets/RTT entre threads
  de Activities e demais fixtures/formatos; preservar o zoom entre recriações.
- [x] Perfilar e otimizar os 40.000 no AVD/GLES: duas rodadas A/B sem input,
  cerca de 1,1 → 60 FPS, cache de viewport/plano e SCREEN_DOOR realmente opaco.
- [x] Apresentar 1.000.000 prédios reais + chão no AVD com 8 GiB temporários:
  um draw instanciado, buffer GPU de 96 bytes por ocorrência, contagem real,
  giro/zoom, seleção persistente e captura com um plano CPU grande por vez;
  HOME com TERM_WINDOW real e retorno no mesmo PID, geração 1 → 2.
- [ ] Estudo: reduzir estados CPU, abertura e custo durante arrastes do milhão;
  ampliar memória/caudas de latência e retomada. A cena parada chegou perto de
  30 FPS; interação varia e o AVD de 2 GiB não está qualificado.
- [ ] Qualificar o layout GPU compacto em outros drivers/Metal/D3D12 e estudar
  a diferença de perspectiva AMD/Vulkan/CoinGL já presente no shader anterior.
- [ ] Qualificar APK arm64 e Android físico: pause/resume, rotação,
  recriação de superfície e drivers. A emulação x86_64 permanece evidência separada.

[Primeiro APK e validação emulada](coin-render-p23-apk-validation-20261007.md).
[Cidade interativa e continuação Android](coin-render-p23-android-city-20261007.md).
[FPS e milhão: evidência, exclusões e estudos](coin-render-p23-fps-million-20261007.md).

[Evidência e limites desta rodada](coin-render-hardware-surfaces-linux.md).
Fechamento por backend/API/driver/plataforma/alvo; manter skips, diferenças
visuais e dispositivos indisponíveis separados dos passes. A documentação
Windows foi reconciliada com BGFX/D3D12 já implementado. A
[campanha Windows de 2026-10-07](coin-render-windows-continuation-validation-20261007.md)
requalifica o código novo em GTX 1060/581.08, com os limites indicados acima;
os dois monitores físicos disponíveis têm DPI observado 96.
O [roteiro Windows de 2026-10-07](coin-render-windows-continuation-20261007.md)
detalha pré-requisitos, APIs, gates e prioridades para a continuação no outro PC.

- [x] Integrar por fast-forward a entrega Windows na branch única
  `codex/coin-render` e revalidar captura/reúso, RTT FBO/pbuffer/mipmaps,
  publicação/ownership e MultiDevice nas células Linux selecionadas.
- [x] Ampliar Linux com SDKs relocados BGFX/wgpu e consumidor público portátil:
  sete células offscreen com RGB igual ao CoinGL, seleção explícita de API,
  rejeição sem fallback e sem dependência BGFX no cliente instalado.
- [x] Recompilar os SDKs Windows após o ajuste do export compartilhado e
  executar o consumidor público native/portable: 12/12 processos físicos em
  BGFX/wgpu × D3D12/Vulkan/OpenGL na GTX 1060, mais 2/2 sondas C11, DLLs
  carregadas dos prefixes isolados e hashes coincidentes. Falhas iniciais e
  pixels preservados na [campanha Windows de 2026-10-10](coin-render-sampling-api-windows-20261010.md). A campanha anterior permanece separada.

[Integração e evidência Linux própria](coin-render-windows-integration-linux-20261007.md).
Os estudos e limites acima permanecem abertos.

## 6. Critérios para cada entrega

- [ ] Documentar entrada, semântica esperada, limites e dono da decisão:
  Wiring captura, Core decide, Infra executa, Shell explica.
- [ ] Implementar ou rejeitar o mesmo contrato em BGFX e wgpu, sem duplicar
  interpretação Coin nos shaders/conectores.
- [ ] Testar sucesso, limites, estado anterior preservado em falha e recuperação;
  verificar ABI/protocolo quando o transporte mudar.
- [ ] Aplicar a [política de bugs do CoinGL](coin-render-compatibility-policy.md):
  uma diferença sem diagnóstico permanece aberta.
- [ ] Atualizar capacidades, inventário e contratos; guardar revisão, comandos,
  ambiente, logs e contagens de passes/skips/falhas na branch consolidada.

Referência da base: [retomada e validações integradas](coin-render-development-20261006.md).
Ordem de execução sugerida: frente 1, frente 2 e qualificação recente da frente 3;
desempenho em campanhas isoladas; hardware conforme disponibilidade. Shaders,
3D/cubo e MSAA exigem contratos próprios e não bloqueiam esses primeiros passos.
