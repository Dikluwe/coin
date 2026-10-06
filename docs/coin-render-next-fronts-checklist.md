# CoinRender: checklist das próximas frentes

Atualizada em 2026-10-06, sobre a integração `601ccdf2e5`, na branch
`codex/coin-render`. Os itens abaixo são pendências; capacidades já implementadas
precisam de ampliação ou qualificação apenas nos escopos indicados.
O [plano geral](coin-render-work-plan.md) conserva os identificadores P/F/A.

## 1. Contrato comum de texturas — P07, primeira implementação

- [x] Definir o perfil inicial de UV DEFAULT/FUNCTION com textura ativa,
  funções autorais, espaços de coordenadas e transformação projetiva no
  [contrato P07 inicial](coin-render-p07-procedural-textures.md). Matrizes ampliadas por shape permanecem abertas.
- [x] Capturar as coordenadas na camada comum e entregar snapshots aos executores;
  qualificar callbacks, fast path e subclasses de faces no perfil P07 inicial.
- [ ] Ampliar qualidade, filtros, mipmaps e formatos com limites explícitos.
- [ ] Cobrir unidade 0 e multitextura, modelos legados, textura desligada,
  reativação, erro sem publicação e recuperação.
- [ ] Definir o contrato da matriz de textura herdada por produtores RTT:
  diagnosticar a diferença FBO/pbuffer antes de escolher o esperado.

Fechamento: expectativas numéricas e cenas mínimas comuns, CPU/BGFX/wgpu,
referência CoinGL no domínio válido e capacidades/documentação atualizadas.

## 2. Paridade de geometria e viewport — P02/P04/P05/P06

- [ ] Ampliar LINES/POINTS/INVISIBLE por shape: contornos, clipping, offset,
  materiais, multitextura e interação com UV procedural.
- [ ] Resolver viewport parcialmente externo no wgpu com projeção/scissor no
  Core; comparar a mesma entrada em BGFX e CoinGL.
- [ ] Ampliar depth range/clamp/offset, bordas, múltiplas regiões e resize.
- [ ] Completar a matriz de bindings/índices de materiais e normais por shape,
  inclusive alpha heterogêneo e normais geradas.
- [ ] Ampliar a matriz numérica de luzes e fog; preservar Gouraud no perfil
  PHONG clássico e identificar iluminação por fragmento como extensão.

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
