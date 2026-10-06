# CoinRender: checklist das próximas frentes

Atualizada em 2026-10-06, após a rodada P02/P04/P05/P06 `b8fba0e530` e o fechamento do primeiro perfil P07, na branch
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
compressão, mipmaps/formatos/unidades/modelos adicionais de RTT e qualificação
Windows/FreeCAD. Esses escopos não reabrem os itens do primeiro perfil.

Fechamento: expectativas numéricas e cenas mínimas comuns, CPU/BGFX/wgpu,
referência CoinGL no domínio válido e capacidades/documentação atualizadas.

## 2. Paridade de geometria e viewport — P02/P04/P05/P06

- [x] Resolver viewport externo no wgpu com projeção/interseção no Core;
  comparar CPU/BGFX/wgpu/CoinGL, bordas/cantos, interseção vazia, resize e
  clear de depth das annotations em CPU/wgpu no [perfil ampliado](coin-render-geometry-viewport-contract.md).
- [x] Corrigir normais indexadas de face/part e a ordem não indexada no fast
  path; verificar fallback atômico e vetores esperados independentes.
- [x] Ampliar FaceSet/IndexedFaceSet: cinco bindings, alpha heterogêneo,
  normais fornecidas/geradas e fast path ligado/desligado (80 cenas).
- [x] Ampliar luzes directional/point/spot simultâneas com quatro modos de fog,
  range [.2,.8] e viewport externo (16 cenas); conservar Gouraud clássico.
- [x] Ampliar estilos com DEFAULT/Plane e multitextura: IndexedFaceSet/Cube
  CPU/GPU/CoinGL; Sphere CPU/GPU; Cone/Cylinder na captura CPU.
- [ ] **Estudo de melhoria futura:** investigar cobertura, interpolação, UV/LOD e
  seleção de profundidade nas junções curvas/coincidentes do CoinGL, conforme
  o [estudo de raster](coin-render-raster-junctions-study.md). Comparar abordagens
  e custos antes de escolher uma implementação; a reprodução do raster nativo
  não bloqueia o contrato portátil CPU/BGFX/wgpu escolhido para esta entrega.
- [ ] Concluir a qualificação portátil de Cone/Cylinder e dos estilos ampliados;
  divergências entre CPU e os executores GPU continuam bloqueando essas células
  e não são dispensadas pelo estudo de compatibilidade CoinGL.
- [ ] Completar estilos por shape com clipping, offset, materiais e todas as
  combinações UV; os contornos convexos continuam delimitados por contrato.
- [ ] Ampliar depth clamp/range/offset fora do perfil atual, múltiplas regiões,
  transparência/sombras/RTT com viewport externo e demais bordas de raster.
- [ ] Completar bindings/índices de materiais e normais nos demais shapes.
- [ ] Completar a matriz numérica de luzes/fog por shape e estado; manter
  iluminação por fragmento identificada como extensão.

Fechamento: fixtures compartilhadas, restauração de estado, recuperação após
rejeição e diferenças de raster diagnosticadas sem relaxar o esperado.

## 3. Nós, FreeCAD e recursos além do perfil — P03/P15/P16/P24/P28

- [ ] Qualificar texto, imagens, marcadores, alpha test, UV projetivo e caixas
  de Complexity recentes no Windows e no viewport real FreeCAD.
- [ ] Adaptar subclasses e nós GL-only conhecidos no host; verificar conteúdo
  visível, callbacks, picking e seleção por workbench.
- [ ] Ampliar RTT além de RGBA8/unidade 0: formatos, estados, transparência,
  dimensões e rota direta de janela, com orçamento e publicação transacional.
- [ ] Definir o contrato portátil de shaders próprios antes de implementar
  tradução e recursos em BGFX/wgpu.
- [ ] Definir planos comuns para texturas 3D, cube maps e RTT de cubo.
- [ ] Definir MSAA/resolve e multipass na action/target, com memória e
  comportamento de resize/readback explícitos.

Fechamento: cenário real no host quando pertinente; ausência de omissões
silenciosas nos tipos adaptados; rejeição explícita fora do perfil publicado.

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
