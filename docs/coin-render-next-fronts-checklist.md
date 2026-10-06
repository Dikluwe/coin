# CoinRender: checklist das próximas frentes

Atualizada em 2026-10-06, com a entrega P03/P15/P16/P24/P28 após o fechamento
P02/P04/P05/P06 e do primeiro perfil P07, na branch
`codex/coin-render`. Os itens abaixo são pendências; capacidades já implementadas
precisam de ampliação ou qualificação apenas nos escopos indicados.
O [plano geral](coin-render-work-plan.md) conserva os identificadores P/F/A.

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

Próximo perfil: NPOT/rescaling, anisotropia/limites configuráveis, SRGB/HDR/
compressão, mipmaps/formatos adicionais de RTT e qualificação
Windows/FreeCAD. Esses escopos não reabrem os itens do primeiro perfil.

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

- [ ] **Estudo:** reprodução das junções curvas/coincidentes e endpoints
  transparentes do CoinGL, conforme o [roteiro e evidências](coin-render-raster-junctions-study.md).
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

Entrega e limites: [perfil ampliado](coin-render-p03-p24-p28-profile.md).

- [ ] Ampliar a qualificação recente para Windows e consumidores reais de cada
  workbench; manter a célula Linux do viewport separada.
- [ ] Adaptar os nós GL-only restantes: StringLabel/DatumLabel, bbox, control
  points, Polygon/MeshGrid, kits/draggers e preparo de viewport por consumidor.
- [x] Disponibilizar delegação comum para subclasses SoText2/SoImage, preservando
  callbacks/observadores e falha sem publicação; adaptar SoColorBarLabel e
  SoFrameLabel com preparo frio no host.
- [x] Qualificar oito controles de conteúdo no viewport FreeCAD Linux:
  BGFX Vulkan e wgpu em object/weighted OIT e DPR 1/2; BGFX OpenGL em DPR 1.
  Adaptar imagens RGBA NPOT geradas pelo GUI e validar os 12 controles de
  NaviCube nas três rotas, com sete orientações, máscaras e picking.
- [ ] Concluir a qualificação física BGFX OpenGL DPR 2: timeout AMD antes da
  captura; tentativa de offload concluiu conteúdo, mas sem prova de GPU física.
  Manter os diagnósticos separados das células aprovadas.
- [x] Ampliar RTT RGBA8 a unidades 0..7, quatro modelos e política explícita da
  subcena; definir/executar alpha convencional do produtor no Core e executores.
  Corrigir restauração FBO e sobrescrita de política pbuffer na referência.
- [ ] Ampliar formatos/mipmaps/dimensões e RTT direto de janela, mantendo
  orçamento, propriedade de recursos e publicação transacional.
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

- [ ] Congelar controles CoinGL/CoinRender e medir sem builds ou testes GPU
  concorrentes, separando janela sem readback e offscreen.
- [ ] Medir primeiro quadro e regime aquecido: estático, câmera, materiais,
  transformações e geometria parcial/total, com mediana e p95.
- [ ] Identificar o custo dominante entre captura, validação/composição,
  lowering/FFI, preparação GPU, submissão e publicação.
- [ ] Implementar uma otimização delimitada com mecanismo de comparação A/B,
  preservando conteúdo, rejeições e caminhos de recuperação.
- [ ] Revalidar reuso, caches, instancing, readback, memória e caudas de latência;
  registrar regressões e limites junto dos ganhos.

Fechamento: execuções repetidas/intercaladas, hashes dos controles, mesma
cena/resolução/driver e evidência visual. Tempos CTest não são benchmark.

## 5. Hardware, superfícies e sombras — P20/P21/P22/P23/P27

- [ ] Diagnosticar e qualificar as divergências recentes do oráculo pixmap AMD.
- [ ] Fechar a célula Intel física e repetir os perfis nas GPUs/APIs previstas.
- [ ] Qualificar o oráculo CoinGL de oito mapas de sombra ou registrar uma
  expectativa independente e a divergência demonstrada.
- [ ] Ampliar Windows: APIs BGFX pendentes, DPI entre monitores, multiwindow
  e perda/recriação real de superfície/dispositivo.
- [ ] Qualificar AppKit/Metal e ampliar o perfil Wayland nativo.
- [ ] Fechar build/link Android e testar pause/resume e recriação em dispositivo.
- [ ] Resolver as condições dos skips `BumpProgramGLX` e
  `CoinRenderCameraReuseReferenceTest` em campanhas específicas.

Fechamento por backend/API/driver/plataforma/alvo; manter skips e dispositivos
indisponíveis separados dos passes. Windows anterior não qualifica código novo.

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
