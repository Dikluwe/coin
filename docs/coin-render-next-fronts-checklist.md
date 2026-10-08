# CoinRender: checklist das próximas frentes

Atualizada em 2026-10-07, incluindo a continuação Windows, com a entrega
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
- [x] Mipmaps RTT gerados dentro do GPU: wgpu POT/NPOT e BGFX POT.
  OpenGL/wgpu usa cópias internas GPU para isolar views; orçamento conservador,
  tokens tipados, falha sem publicação e recuperação.
- [ ] Views independentes para samplers base/mips da mesma imagem em BGFX/OpenGL;
  alias de LOD diagnosticado e combinação recusada sem publicação.
- [ ] Redução por área NPOT direta no BGFX: autogeração nativa divergente
  diagnosticada e recusada antes dos produtores.
- [ ] Rescaling/SoTextureScalePolicy legado, codec de melhor qualidade,
  ETC2/ASTC/BC1/BC5/BC7, saída HDR/tone mapping, Windows/Android/FreeCAD.
- [x] Isolar o sampling projetivo AMD: GPU/CoinGL idênticos em wgpu e BGFX Vulkan;
  quantização nearest e footprint explicam o diagnóstico, com contracontrole NVIDIA.
- [ ] Fechar a paridade portátil nearest projetiva AMD/CPU sem ampliar MAE≤1,5/max≤4;
  o modelo que passa AMD falha NVIDIA. Gate original preservado e estudo reproduzível.
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
- [ ] Fechar Windows com DPI físico distinto entre monitores, captura visual
  wgpu/OpenGL sem COPY_SRC e perda real de dispositivo em campanha controlada.
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
- [ ] Recompilar os SDKs Windows após o ajuste do export compartilhado e
  executar o novo consumidor portátil; a campanha Windows anterior continua
  preservada e não qualifica esse ajuste CMake posterior.

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
