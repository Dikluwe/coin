# CoinRender: plano de trabalho e fechamento

Plano organizado em 2026-09-28. A [checklist de responsabilidades](rendering-responsibilities-checklist.md)
registra estados e evidências; este documento define sequência e dependências.
As caixas abaixo representam entregas ainda não fechadas.

A [checklist das próximas frentes](coin-render-next-fronts-checklist.md) detalha
as pendências após a integração de 2026-10-06 e os critérios de cada entrega.

## Base já entregue

CoinRender identifica o contrato comum; CoinBgfx e CoinWgpu identificam os
mecanismos específicos. Nomes e composição comum estão fechados nos escopos
A01/A03 e M01–M04. Isso não encerra o suporte GPU de todas as modalidades Coin.

No BGFX já existem benchmark em janela sem readback, timestamps por passagem,
buffers persistentes, atualização parcial de material, agrupamento de draws,
readback duplo/triplo, RTT GPU→GPU, múltiplos alvos e recuperação com falhas
injetadas. Reutilizar e qualificar esses caminhos. O wgpu também tem RTT direto,
readback assíncrono e infraestrutura de múltiplos dispositivos/alvos, com
escopos próprios de validação.

## 1. Remover bloqueios básicos do viewport

- [x] **P01 — SoClipPlane (F03):** contrato Coin/GL, captura, espaços de coordenadas,
  representação comum e execução nos dois backends. Fechado até oito planos no
  perfil registrado em [contrato de clipping](coin-render-clipping-contract.md);
  qualificação visual GL/FreeCAD e matriz ampliada permanecem abertas.
- [ ] **P02 — SoDrawStyle (F04):** LINES, POINTS e INVISIBLE por shape, resolvidos
  sobre geometria comum; linhas/pontos dedicados não encerram esse item.
  INVISIBLE e LINES/POINTS de contornos convexos implementados no perfil
  documentado, incluindo offset da face plana original e units D32Float;
  padrão contínuo, recortes/cantos e matriz explícita de contornos já verificados.
  O primeiro perfil portátil amplia estilos, alpha, funções autorais e
  multitextura na matriz de 1.290 cenas e no gate procedural de 311 cenas;
  junções curvas/coincidentes CoinGL ficam como
  [estudo de melhoria futura](coin-render-raster-junctions-study.md), conforme o
  critério portátil CPU/BGFX/wgpu escolhido. Combinações de offset fora desse perfil e
  qualificação completa permanecem no [contrato de estilo](coin-render-draw-style-contract.md).
- [ ] **P03 — SoText2 (F05):** fontes, âncora, tamanho, clipping e composição;
  captura/layout comum de tipos nativos exatos implementados em Linux em
  2026-10-05, com glifos do Coin e transporte comum de raster. Qualificação
  Windows/FreeCAD ampliada continua aberta no [inventário](coin-render-node-inventory.md).
- [ ] **P04 — Viewport/scissor e depth (F13/F12):** retângulos parcialmente externos,
  projeção, múltiplas regiões, resize, range/clamp/offset e bordas.
  O primeiro perfil portátil inclui viewport externo/vazio, múltiplas regiões,
  resize, annotations, RTT, sombras, range normal/reverso/colapsado e clamp de
  entrada. Depth clamp geométrico e formatos/drivers adicionais seguem as
  ampliações do [perfil P02/P04/P05/P06](coin-render-geometry-viewport-contract.md).

Fechamento: cenas reproduzíveis, expectativas comuns, execução GPU e referência
Coin/GL quando aplicável. Aplicar A02/A05 às funções alteradas: Wiring captura,
Core transforma snapshots. Não exigir uma refatoração global prévia.

## 2. Fechar geometria, materiais, iluminação e texturas

- [ ] **P05 — Bindings/materiais (F09):** matriz por shape e índices, múltiplos
  materiais, cores/alpha heterogêneo, normais fornecidas e geradas.
  A matriz anterior de faces e os sete pares em dez tipos adicionais integram
  o primeiro perfil portátil. Corrigidos índices/ocorrências de linhas, bindings
  de QuadMesh/strips e vetores esperados independentes. Os 49 cruzamentos entre
  bindings distintos, subclasses e outros shapes seguem a ampliação.
- [ ] **P06 — Iluminação/fog (F10):** fórmulas e matriz numérica comuns para luzes
  direcionais/pontuais/spot, componentes do material e limites. Preservar o modelo
  Coin: PHONG no GL normal usa iluminação por vértice/Gouraud; iluminação por
  fragmento deve ser distinguida como extensão. O primeiro perfil inclui luzes
  assimétricas, quatro modos de fog e normais fornecidas/geradas por shape;
  flat com sombras/per-fragmento continua fora da qualificação.
- [ ] **P07 — UV/texturas (F06/F11):** procedural/default, matriz, modelos
  MODULATE/REPLACE/DECAL/BLEND, wrap, filtros, qualidade e formatos.
  [Primeiro perfil de DEFAULT/FUNCTION entregue](coin-render-p07-procedural-textures.md);
  [primeiro perfil de sampling/RTT fechado](coin-render-p07-sampling-rtt-contract.md).
  NPOT, SRGB/HDR/compressão, RTT além do perfil e plataformas adicionais
  continuam nas campanhas próprias.
- [x] **P08 — Multitextura/strokes (F07/F08):** oito unidades, SoTextureCombine,
  UVs/matrizes independentes e execução comum em CPU/BGFX/wgpu; raster aliased,
  largura/tamanho arredondados, padrão, cor/alpha, perspectiva, clipping,
  rejeição sem publicação e recuperação qualificados no
  [contrato P08](coin-render-multitexture-contract.md). Referência Coin/GL
  obrigatória passou em wgpu e BGFX Vulkan/OpenGL. UV procedural/default,
  formatos/qualidade e matriz geral de bindings seguem P07/P05; modalidades de
  transparência foram qualificadas no perfil P09, sem encerrar P02 integralmente.

Fechamento: mesmas entradas e expectativas, decisões com um dono comum, shaders
específicos. P08 depende dos contratos de P02/P05/P07. P05–P07 podem avançar por
escopos delimitados.

## 3. Fechar composição e transparência funcional

- [x] **P09 — Modos Coin (F01/F02):** onze modalidades no perfil funcional
  comum CPU/BGFX/wgpu; alpha final, depth, ordem imediata/adiada, anotações,
  sorting de triângulos, screen door e quatro camadas. Referência GL obrigatória
  e expectativas numéricas independentes no [contrato P09](coin-render-transparency-contract.md).
  A ampliação de camadas e controles consta de P10; FreeCAD e raster ampliado seguem suas etapas.
- [x] **P10 — OIT/peeling (F16):** peeling comum de 1..8 camadas, orçamento de
  attachments, alpha zero/precisão, luz/textura, interseções, oclusão e depth
  qualificados no [contrato P10](coin-render-peeling-contract.md). Weighted OIT
  é extensão explícita BGFX/wgpu; consulte também o contrato P11 e P27. Overrides de depth fora do
  mecanismo BGFX são rejeitados com preservação. Outros drivers, MSAA e a rota
  direta BGFX de textura de cena seguem as campanhas próprias.
- [x] **P11 — Seleção/capacidades (A07/A08):** opções tipadas por alvo para
  renderer/transparência/RTT; capacidades V3 distinguem fatos conhecidos, mecanismo
  implementado/disponível e evidência de perfil qualificado. Compatibilidade V1/V2,
  conflitos sem fallback e limites no [contrato P11](coin-render-selection-contract.md).
  A consulta de janela não substitui sua qualificação física ou de apresentação.

Fechamento: preservar modalidades Coin; weighted OIT é extensão explícita.
Unsupported preserva imagem/serial e permite próximo pedido válido. P09 reutiliza
A03; P10 depende dos materiais/texturas. P11 acompanha cada capacidade; não introduz seleção automática
entre mecanismos ou aproximações visuais.

## 4. Integrar recursos, RTT e ciclo de vida

- [x] **P12 — Plano/ownership (A04/A06/F14):** grafo comum staged/direto,
  referências lógicas separadas de tokens GPU, revisão do produtor, owner/device,
  gerações, retenção e invalidação. Captura sem execução de produtores; recursos
  concretos pertencem à Infra. Alpha automático pode depender de resolução
  staged no [contrato P12](coin-render-rtt-ownership-contract.md); a política
  explícita Coin do SoSceneTexture2 foi corrigida em P13. Execução/publicação
  qualificadas em P13; formatos/estados ampliados continuam F14 e recuperação
  geral permanece P14.
- [x] **P13 — RTT/publicação (F14/F15/A09), perfil RGBA8:** staged/direto,
  orientação, dependências/ciclos, preflight dos limites concretos, falhas,
  tickets e publicação transacional qualificados. Política Coin NONE corrigida
  e ALPHA_BLEND suportado no Core; cor/depth/serial/revisão/borrow preservados
  em falha. [Contrato e checklist P13](coin-render-rtt-publication-contract.md).
  Formatos/estados ampliados e RTT direto de janela continuam em F14;
  recuperação geral e orçamento de múltiplos alvos pertencem a P14.
- [x] **P14 — Múltiplos alvos/recuperação (F17), perfil experimental atual:**
  isolamento e execução ordenada, filas 16/128 MiB, views BGFX, resize/destruição,
  tickets pendentes e perda/reconstrução qualificados. RTT staged mantém o runtime
  BGFX vivo; uma perda aposenta peers/tickets da geração compartilhada.
  [Contrato e checklist P14](coin-render-multi-target-contract.md). Seleção pública
  de dispositivos por alvo e matriz física/plataformas não são certificadas aqui.

Fechamento: recursos de um alvo/geração não contaminam outro; resultados inválidos
não são publicados. P12 precede P13; P14 usa esse contrato nos casos com RTT/readback.
Esta frente pode avançar junto das entregas funcionais, reutilizando testes existentes.

## 5. Fechar integração real no FreeCAD

- [x] **P15 — Inventário de nós (F19/F20), checkout local Gui/Mod:** 33 classes,
  47 overrides GL e 33 criações especiais revisadas, com dono/bloqueio por caso.
  SoImage/Text2/shaders/GL-only caracterizados; candidatos a P16 e oito frentes de
  fechamento registradas no [inventário e checklist P15](coin-render-node-inventory.md).
  Addons externos e suporte funcional amplo F19/F20 permanecem fora desse fechamento.
- [x] **P16 — Qt/manager/FreeCAD (F18/F01), perfil Part/BRep:** mesmas cenas e
  ações em GL/BGFX OpenGL/BGFX Vulkan/wgpu Vulkan: expose, resize, seleção,
  grade/on-top, links aninhados, arrays, documentos e montagem App::Part.
  [Contrato, matriz, reprodução e limites](coin-render-freecad-viewport.md).
  Outros workbenches e bloqueios F19/F20 continuam no inventário P15.

Fechamento: viewport real utilizável no perfil declarado, com evidência de interação.
Cena exportada não substitui esse teste. P15 começa cedo para orientar prioridades;
P16 fecha depois das capacidades exigidas pelas cenas escolhidas.

## 6. Medir e otimizar com evidência

- [x] **P17 — Campanha comparável:** janela sem readback e offscreen separados;
  GL/BGFX OpenGL/BGFX Vulkan/wgpu nos modos suportados; cenas opacas com estados
  intercalados e cenas transparentes; mediana/p95, throughput, warmup e vsync.
  [Protocolo, 112 execuções e limites P17](coin-render-p17-campaign.md);
  [amostras individuais](inventories/coin-render-p17-runs.csv).
- [x] **P18 — Instrumentação/memória (A09):** lacunas por passagem, GPU versus
  espera CPU, submits/transições, picos de buffers/texturas/framebuffers/staging.
  [Protocolo, 56 perfis e limites](coin-render-p18-profiling.md);
  [matriz de amostras](inventories/coin-render-p18-profile.csv).
- [x] **P19 — Reuso/readback (F15):** câmera/material, persistência, agrupamento,
  pipeline 1/2/3, latência/backpressure/memória e ausência de readback desnecessário.
  [Protocolo, 54 execuções e limites](coin-render-p19-reuse-readback.md);
  [amostras individuais](inventories/coin-render-p19-runs.csv).

Fechamento: resultados reproduzíveis, device/build/resolução identificados e A/B
que preservem semântica. Não estimar timestamps indisponíveis nem usar RSS como
memória GPU. Medições podem começar nos perfis existentes; otimizar custos medidos.
Base de execução: [benchmark e mecanismos existentes](coin-render-window-benchmark.md).

## 7. Ampliar hardware e plataformas

- [ ] **P20 — Matriz física (A10/F21):** AMD/RADV/radeonsi, Intel e NVIDIA,
  OpenGL/Vulkan, fixtures comuns, skips e tolerâncias por célula.
  [Primeira matriz física AMD/NVIDIA, células e lacunas](coin-render-p20-physical-matrix.md);
  Intel e oráculos fora das fixtures qualificadas continuam abertos;
  NVIDIA BGFX/OpenGL e seu oráculo foram fechados na campanha P27 Linux.
- [ ] **P21 — Windows:** Win32, D3D11/D3D12 e APIs disponíveis, resize/DPI/multiwindow.
  [Windows/NVIDIA GTX 1060 qualificado em wgpu D3D12 e Vulkan](coin-render-p21-windows-validation.md),
  99/99 testes por API. BGFX/D3D11/D3D12, mudança de DPI entre monitores e
  perda/recriação real de superfície/device em janela seguem abertos.
- [ ] **P22 — macOS/Wayland:** Cocoa/Metal e Wayland nativo, superfícies,
  coordenadas, apresentação e ciclo de vida próprios.
  [Wayland nativo executado; AppKit/Metal preparado](coin-render-p22-macos-wayland.md).
- [ ] **P23 — Android:** após desktop, pause/resume e recriação de superfície/recursos.
  [NDK r30 instalado; ponte wgpu e objetos Android compilados](coin-render-p23-android.md);
  Coin base ainda exige GL desktop; link final e dispositivo pendentes.

Fechamento por combinação backend/API/driver/plataforma/alvo. Xvfb ou um dispositivo
Vulkan não encerram a matriz física nem qualificam outras plataformas.

## 8. Recursos modernos após a base

- [ ] **P24 — Qualidade (F21):** MSAA/multipass configurável e linear/sRGB/HDR.
  [Contrato de action/target, resolve, passes e orçamento](coin-render-portable-resources-contract.md)
  definido; implementação e qualificação ainda pendentes.
- [ ] **P25 — Efeitos (F20):** sombras, SSAO e texturas 3D/cube maps;
  compatibilidade Coin separada das extensões novas. A
  [checagem de efeitos](coin-render-p25-effects.md) rejeita cenas ativas ainda
  sem executor antes de publicar; os efeitos funcionais seguem abertos.
- [ ] **P26 — Execução avançada:** compute/culling/preparação de geometria,
  indirect/instancing; ray tracing por último, como extensão explícita.
  [Contrato e critérios](coin-render-p26-advanced-execution.md); nenhum
  mecanismo novo qualificado.
- [ ] **P27 — Sombras Coin:** BGFX e wgpu executam os perfis qualificados de
  até oito mapas; cenas próprias, grupos irmãos/aninhados e anotações opacas
  compõem offscreen e RTT staged/direct. Alfa RTT NONE/ALPHA_BLEND está
  qualificado até oito mapas, assim como alfa de material e textura estática.
  ALPHA_TEST acompanha o comportamento atual Coin/GL (composição transparente,
  sem descarte automático). Peeling/OIT e qualidade ampliada estão qualificados.
  P27.4 mantém a referência GL nativa com oito mapas pendente; P27.5 mantém
  GPU Intel e macOS/Metal. Windows/NVIDIA D3D12 e Vulkan, assim como
  NVIDIA/OpenGL PRIME e seu oráculo, estão qualificados.
  [Seis células físicas Linux e Wiring fechados](coin-render-p27-linux-validation.md).
  [Referência e trabalho pendente](coin-render-p27-shadows.md).
- [ ] **P28 — Texturas espaciais e SSAO:** volume/cube maps seguem o contrato
  Coin; SSAO é extensão opt-in. [Critérios](coin-render-p27-p30-tracker.md).
- [ ] **P29 — Compute/culling:** planejamento comum, execução opcional e A/B
  equivalente. [Critérios](coin-render-p27-p30-tracker.md).
- [ ] **P30 — Instancing/indirect:** lotes e fallback convencional equivalente,
  com ganho medido. [Critérios](coin-render-p27-p30-tracker.md).

P25/P26 são frentes gerais; P27–P30 são entregas funcionais que as desdobram,
sem duplicar interpretação do Coin. Dependem de capacidades, ownership e medições;
não devem adiar os bloqueios básicos.

## Acompanhamento

As evidências que exigem outro SO, processador ou GPU física ficam no
[registro de validação externa](coin-render-platform-validation-pending.md).

Cada Pxx usa o registro da checklist: contrato Coin e caminho GL; dono comum;
captura; saída do Core; execução BGFX/wgpu; testes, device/formato/tolerâncias;
limites; documentação e commit de evidência. Registrar implementação, execução e
qualificação separadamente. Rejeição explícita fecha diagnóstico, não suporte
funcional. Atualizar Pxx e Axx/Fxx associados somente no escopo comprovado.

**Entrega atual: P23 — Android.** A rota NDK/wgpu e o smoke de lifecycle estão
preparados; NDK r30 e target Rust arm64 estão instalados, e a ponte e os objetos
Android passaram na compilação cruzada. Coin base ainda depende de GL desktop,
impedindo o link completo; falta dispositivo para qualificação. O
[registro externo](coin-render-platform-validation-pending.md) reúne essas
células, junto da validação Intel que o usuário pode executar em outro
computador e da célula macOS sem host disponível. **P22 — macOS/Wayland:**
Wayland/wgpu Vulkan passou em Weston headless; AppKit/Metal aguarda macOS.
**P21 — Windows:** a [campanha nativa GTX 1060](coin-render-p21-windows-validation.md)
qualificou wgpu D3D12 e Vulkan, com 99/99 testes por API. Os limites de DPI e
perda real de superfície permanecem explícitos. P20 segue aberto para GPU Intel
e oráculos fora das fixtures qualificadas. As campanhas
[P17](coin-render-p17-campaign.md), [P18](coin-render-p18-profiling.md) e
[P19](coin-render-p19-reuse-readback.md) fixam cenas, métricas e A/B na Radeon.
A [primeira execução P20](coin-render-p20-physical-matrix.md) amplia a evidência
para NVIDIA Vulkan e explicita falhas/skips sem extrapolar esses números. O
[perfil P16](coin-render-freecad-viewport.md) é a base de interação comprovada.
Texto, imagem, rótulos e geometria GL-only seguem F19/F20/P15; viewport
parcialmente externo continua P04; RTT ampliado F14; captura explícita de janela RGBA8 foi fechada em [F15](coin-render-window-readback.md).

## Fechamento do primeiro perfil P07 em 2026-10-06

Os três itens de sampling/formatos, combinações de unidades/modelos/publicação
e matriz RTT da checklist foram fechados no
[contrato P07](coin-render-p07-sampling-rtt-contract.md). O identificador P07
no plano geral conserva a expansão futura para NPOT, anisotropia, formatos
mais amplos e RTT fora do perfil; a primeira implementação está completa.
