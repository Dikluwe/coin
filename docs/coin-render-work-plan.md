# CoinRender: plano de trabalho e fechamento

Plano organizado em 2026-09-28. A [checklist de responsabilidades](rendering-responsibilities-checklist.md)
registra estados e evidências; este documento define sequência e dependências.
As caixas abaixo representam entregas ainda não fechadas.

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
- [ ] **P03 — SoText2 (F05):** fontes, âncora, tamanho, clipping e composição;
  layout comum, atlas e recursos próprios de cada Infra.
- [ ] **P04 — Viewport/scissor e depth (F13/F12):** retângulos parcialmente externos,
  projeção, múltiplas regiões, resize, range/clamp/offset e bordas.

Fechamento: cenas reproduzíveis, expectativas comuns, execução GPU e referência
Coin/GL quando aplicável. Aplicar A02/A05 às funções alteradas: Wiring captura,
Core transforma snapshots. Não exigir uma refatoração global prévia.

## 2. Fechar geometria, materiais, iluminação e texturas

- [ ] **P05 — Bindings/materiais (F09):** matriz por shape e índices, múltiplos
  materiais, cores/alpha heterogêneo, normais fornecidas e geradas.
- [ ] **P06 — Iluminação/fog (F10):** fórmulas e matriz numérica comuns para luzes
  direcionais/pontuais/spot, componentes do material e limites. Preservar o modelo
  Coin: PHONG no GL normal usa iluminação por vértice/Gouraud; iluminação por
  fragmento deve ser distinguida como extensão.
- [ ] **P07 — UV/texturas (F06/F11):** procedural/default, matriz, modelos
  MODULATE/REPLACE/DECAL/BLEND, wrap, filtros, qualidade e formatos.
- [ ] **P08 — Multitextura/strokes (F07/F08):** SoTextureCombine, limites por unidade
  e linhas/pontos texturizados no wgpu; largura, padrão, cor, alpha e interpolação.

Fechamento: mesmas entradas e expectativas, decisões com um dono comum, shaders
específicos. P08 depende dos contratos de P02/P05/P07. P05–P07 podem avançar por
escopos delimitados.

## 3. Fechar composição e transparência funcional

- [ ] **P09 — Modos Coin (F01/F02):** onze modalidades, alpha final, depth,
  ordem imediata/atrasada, anotações e sorting; completar mecanismos ausentes no
  wgpu: aditivo, triângulos, camadas e screen door.
- [ ] **P10 — OIT/peeling (F16):** luzes/texturas, muitas camadas, alpha extremo,
  interseções dentro do objeto, oclusão opaca, saturação/halos, orçamento e camadas
  configuráveis; decidir e declarar o perfil oferecido pelo wgpu.
- [ ] **P11 — Seleção/capacidades (A07/A08):** opções tipadas; distinguir hardware
  disponível, mecanismo implementado e contrato Coin qualificado por alvo.

Fechamento: preservar modalidades Coin; weighted OIT é extensão explícita.
Unsupported preserva imagem/serial e permite próximo pedido válido. P09 reutiliza
A03; P10 depende dos materiais/texturas. P11 acompanha cada capacidade e precede
qualquer seleção automática entre mecanismos equivalentes.

## 4. Integrar recursos, RTT e ciclo de vida

- [ ] **P12 — Plano/ownership (A04/A06/F14):** dependências RTT comuns; identidade
  opaca, produtor, device, geração, retenção e invalidação; retirar criação/liberação
  de tokens concretos da action.
- [ ] **P13 — RTT/publicação (F14/F15/A09):** staged versus direto, formatos,
  orientação, dependências aninhadas, ciclos, falhas, tickets e publicação transacional.
- [ ] **P14 — Múltiplos alvos/recuperação (F17):** isolamento, agendamento, orçamento,
  resize/destruição e perda/reconstrução do dispositivo em ambos os executores.

Fechamento: recursos de um alvo/geração não contaminam outro; resultados inválidos
não são publicados. P12 precede P13; P14 usa esse contrato nos casos com RTT/readback.
Esta frente pode avançar junto das entregas funcionais, reutilizando testes existentes.

## 5. Fechar integração real no FreeCAD

- [ ] **P15 — Inventário de nós (F19/F20):** workbenches que dependem de GLRender,
  SoImage e shaders próprios; suporte e bloqueios por caso.
- [ ] **P16 — Qt/manager/FreeCAD (F18/F01):** mesmas cenas e ações em GL/BGFX/wgpu:
  expose, resize, seleção, overlays, links, arrays, documentos e montagens.

Fechamento: viewport real utilizável no perfil declarado, com evidência de interação.
Cena exportada não substitui esse teste. P15 começa cedo para orientar prioridades;
P16 fecha depois das capacidades exigidas pelas cenas escolhidas.

## 6. Medir e otimizar com evidência

- [ ] **P17 — Campanha comparável:** janela sem readback e offscreen separados;
  GL/BGFX OpenGL/BGFX Vulkan/wgpu nos modos suportados; cenas opacas com estados
  intercalados e cenas transparentes; mediana/p95, throughput, warmup e vsync.
- [ ] **P18 — Instrumentação/memória (A09):** lacunas por passagem, GPU versus
  espera CPU, submits/transições, picos de buffers/texturas/framebuffers/staging.
- [ ] **P19 — Reuso/readback (F15):** câmera/material, persistência, agrupamento,
  pipeline 1/2/3, latência/backpressure/memória e ausência de readback desnecessário.

Fechamento: resultados reproduzíveis, device/build/resolução identificados e A/B
que preservem semântica. Não estimar timestamps indisponíveis nem usar RSS como
memória GPU. Medições podem começar nos perfis existentes; otimizar custos medidos.
Base de execução: [benchmark e mecanismos existentes](coin-render-window-benchmark.md).

## 7. Ampliar hardware e plataformas

- [ ] **P20 — Matriz física (A10/F21):** AMD/RADV/radeonsi, Intel e NVIDIA,
  OpenGL/Vulkan, fixtures comuns, skips e tolerâncias por célula.
- [ ] **P21 — Windows:** Win32, D3D11/D3D12 e APIs disponíveis, resize/DPI/multiwindow.
- [ ] **P22 — macOS/Wayland:** Cocoa/Metal e Wayland nativo, superfícies,
  coordenadas, apresentação e ciclo de vida próprios.
- [ ] **P23 — Android:** após desktop, pause/resume e recriação de superfície/recursos.

Fechamento por combinação backend/API/driver/plataforma/alvo. Xvfb ou um dispositivo
Vulkan não encerram a matriz física nem qualificam outras plataformas.

## 8. Recursos modernos após a base

- [ ] **P24 — Qualidade (F21):** MSAA/multipass configurável e linear/sRGB/HDR.
- [ ] **P25 — Efeitos (F20):** sombras, SSAO e texturas 3D/cube maps;
  compatibilidade Coin separada das extensões novas.
- [ ] **P26 — Execução avançada:** compute/culling/preparação de geometria,
  indirect/instancing; ray tracing por último, como extensão explícita.

Dependem de capacidades, ownership e medições; não devem adiar os bloqueios básicos.

## Acompanhamento

Cada Pxx usa o registro da checklist: contrato Coin e caminho GL; dono comum;
captura; saída do Core; execução BGFX/wgpu; testes, device/formato/tolerâncias;
limites; documentação e commit de evidência. Registrar implementação, execução e
qualificação separadamente. Rejeição explícita fecha diagnóstico, não suporte
funcional. Atualizar Pxx e Axx/Fxx associados somente no escopo comprovado.

**Próxima entrega concreta: P02 — SoDrawStyle / F04.** Mapear o comportamento
Coin/GL por shape e resolver LINES, POINTS e INVISIBLE no Core comum.
