# Renderização Coin: responsabilidades e checklist de fechamento

Referência de acompanhamento criada em 2026-09-28 a partir do checkout e da
auditoria funcional. Atualizada com a implementação e validação da composição
comum; evidências e limites em [contrato de composição](coin-render-composition-contract.md).

Sequência, dependências e entregas em [plano de trabalho CoinRender](coin-render-work-plan.md).
Esta checklist conserva os estados e evidências por responsabilidade/capacidade.

## Regra de organização

Coin define o comportamento. Wiring captura os fatos da cena e coordena o ciclo
de vida. Core transforma esses fatos e produz decisões explícitas. Infra traduz
essas decisões para recursos e comandos concretos. Shell lê configuração textual
e apresenta resultados, diagnósticos e profiling.

A organização é por responsabilidade, sem exigir quatro diretórios ou
bibliotecas. Um componente específico de backend também pode pertencer ao Core:
por exemplo, empacotar vértices para o layout BGFX é mecânico; criar o vertex
buffer é Infra. Não incluir headers GPU não basta para tornar um algoritmo comum.

A interpretação de transparência, bindings, anotações ou depth do Coin deve ter
um único dono. Os executores podem validar seu payload e calcular detalhes
mecânicos, mas não devem escolher novamente o significado da cena.

Fluxo desejado:

`Coin/SoState → captura Wiring → snapshots → planejamento Core → plano resolvido → adaptação/execução Infra → resultado estruturado → publicação Wiring / apresentação Shell`

Os recursos GPU pertencem à Infra. O plano comum pode referenciar recursos por
identidade opaca com contrato explícito de produtor, device, geração e validade;
não deve depender do valor ou do tipo do handle nativo. Wiring coordena a vida
do pedido, enquanto Infra cria, retém e libera os recursos concretos.

## Como está o checkout

| Componente atual | Responsabilidade real | Compartilhamento e pendência |
|---|---|---|
| `CoinRenderAction`, callbacks e replay de paths | Wiring | Captura comum; a action ainda conhece BGFX/Rust, libera tokens concretos e lê configuração textual de RTT. |
| `CoinRenderFramePlanBuilder` | Wiring + Core | Lê `SoCallbackAction`/`SoState` e também transforma geometria e monta o plano. Separar funções de captura e funções mecânicas, sem exigir separar o arquivo inteiro. |
| `CoinRenderFramePlan` e snapshots em `CoinRenderFramePlan.h` | Contrato comum | Compartilháveis; `gpuToken`, limites e convenções precisam de significado independente do executor. |
| `CoinRenderIndexedGeometryCore` | Core comum | Bons limites explícitos: arrays e fatos capturados, sem travessia nem recursos GPU. |
| `CoinRenderFrameReuseCore` | Core comum | Classificação e atualização de câmera compartilháveis; Infra decide se seus buffers podem materializar o reuso. |
| `CoinRenderImageCore` | Core comum | Transformação mecânica de linhas; readback e publicação são responsabilidades distintas. |
| `CoinRenderComposition.h` | Core comum | Dono único de alpha, modalidade Coin, ordenação, screen door e profundidade efetiva; `exactCoin` removido. |
| `CoinBgfxLowering` | Core específico + decisões comuns | Layout BGFX pode permanecer específico. Políticas de transparência/depth extraídas; outras operações reaproveitáveis continuam pendentes. |
| `CoinWgpuFfiFrame`, `CoinWgpuFfi.h` | Adaptação específica Rust/wgpu | Empacotamento e ABI privados do conector; transportam draws ordenados, blend e variantes de depth resolvidos, na ABI privada 28 (frame view 176, vértice 100, estado 2280, draw 56 bytes). |
| `rust_bridge/src/composition.rs` | Adaptação Infra | Valida flags, referências e sequência recebida; não reclassifica alpha nem reordena draws. |
| `CoinBgfxBackend`, recursos e shaders BGFX | Infra BGFX | Devem permanecer específicos; recebem o plano resolvido e executam mecanismos compatíveis. |
| `rust_bridge/src/lib.rs`, recursos e shaders WGSL | Infra wgpu, com lógica mecânica misturada | Execução específica; separar validação/adaptação de decisões semânticas. |
| `CoinRenderTarget`, scene manager e adapter | Fachada comum + Wiring + Infra | Separar ciclo/publicação de operações nativas por método/colaborador. A API comum não precisa expor a classe concreta. |
| `CoinRenderCapabilities` | Infra + Core + Shell | Probe nativo é Infra; seleção mecânica tem dono comum; V3 separa fatos, execução e evidência de perfil; defaults textuais são Shell. [P11](coin-render-selection-contract.md). |
| `CoinRenderDiagnosticShell` | Shell comum e extensões específicas | Mensagens/status/tempos comuns compartilháveis; contadores BGFX ou da ponte Rust devem conservar identificação específica. |

Evidências principais: [captura e plano](../src/rendering/coinrender/CoinRenderFramePlanBuilder.cpp),
[action e RTT](../src/actions/CoinRenderAction.cpp),
[composição C++](../src/rendering/coinrender/CoinRenderComposition.h),
[composição Rust](../src/rendering/coinwgpu/rust_bridge/src/composition.rs),
[adaptação BGFX](../src/rendering/coinbgfx/CoinBgfxLowering.cpp),
[ciclo do manager](backend-independent-frame-preparation.md).

## O que compartilhar e como nomear

A migração CoinRender/CoinBgfx/CoinWgpu já foi aplicada. Alguns colaboradores
abaixo ainda são propostas de extração futura. O nome identifica domínio/backend;
o sufixo identifica função. Compatibilidade: [migração de nomes](coin-render-naming-migration.md).

| Conteúdo | Dono | Nome/destino sugerido |
|---|---|---|
| Estado capturado, geometria, materiais, luzes, fog, texturas, câmera, viewport e clipping | Contrato comum | `CoinRenderFramePlan`, snapshots comuns. |
| Integração de actions, travessia, callbacks, paths e captura de elementos | Wiring comum | Componentes de captura Coin; uma entrada BGFX/wgpu pode apenas selecionar/configurar o executor. |
| Bindings, normais geradas, indexação, expansão de linhas/pontos e transformações de imagem | Core comum | `CoinRenderIndexedGeometryCore`, `CoinRenderStrokeCore`, `CoinRenderImageCore`. |
| Composição Coin, camadas, barreiras, ordenação, estados efetivos de depth e dependências RTT | Core comum | `CoinRenderCompositionCore` e plano de execução resolvido. |
| Identidade, revisão, classificação de reuso e invalidação | Core comum | `CoinRenderFrameReuseCore`. Buffers e fences permanecem específicos. |
| Formato e orientação pública do readback, status/tickets e regras de publicação | Contrato comum + Wiring | Fachada comum; cópia, alinhamento, polling e sincronização em cada Infra. |
| Testes de semântica, fixtures, expectativas e tolerâncias justificadas | Validação comum | Testes por capacidade Coin, executados por BGFX, wgpu e referência quando aplicável. |
| Layout de vértices/uniforms e agrupamento por pipeline BGFX | Core específico | `CoinBgfxLowering`, `CoinBgfxDraw`, `CoinBgfxVertex`; extrair cálculos comuns quando possível. |
| Views, programs, handles, framebuffer, submit, blit e readback BGFX | Infra BGFX | `CoinBgfxBackend`, recursos e shaders BGFX. |
| ABI C/Rust, structs `repr(C)`, empacotamento e validação da ponte | Adaptação específica | `CoinWgpuFfiFrame`, `CoinWgpuFfi`, identificando Rust bridge quando necessário. |
| Pipeline, bind groups, command encoder, surfaces e staging wgpu | Infra wgpu | `CoinWgpuBackend`/`CoinWgpuRustBridge`; WGSL específico. |
| Probe de formatos/limites/timestamps e tradução de convenções de clip/depth | Infra de cada backend | Nome BGFX/wgpu. A convenção Coin de entrada e a política de compatibilidade são comuns. |
| Parsing textual, mensagens e formato de profiling | Shell comum | `CoinRenderDiagnosticShell`; parsers/contadores específicos identificados por backend. |
| Escolha entre mecanismos equivalentes, como recursos de OIT e orçamento de passes | Política mecânica + Infra | Core seleciona entre capacidades estruturadas; Infra materializa. Uma aproximação visual exige modo explícito. |

Não reutilizar silenciosamente `SCREEN_DOOR` ou sorting por triângulo como nomes
para weighted OIT. OIT pode ser uma extensão melhor em determinados cenários,
mas sua seleção e os limites de equivalência devem ficar explícitos.

Compartilhar fórmulas, convenções e testes de iluminação não exige compartilhar
literalmente o mesmo arquivo de shader entre WGSL e BGFX. As linguagens e bindings
podem diferir; o significado e as expectativas não devem divergir por acidente.

## Estados e critério de fechamento

- **Aberto:** trabalho ainda não fechado ou sem evidência suficiente.
- **Parcial:** existe implementação ou validação de parte do contrato/perfil.
- **Fechado no escopo:** escopo e evidências estão definidos; não significa paridade universal.
- **Extensão:** comportamento adicional, configurável e distinguível da compatibilidade Coin.

Uma checkbox só pode ser marcada quando a entrega descrita estiver concluída.
Implementação, execução e qualificação são acompanhadas separadamente. A ausência
de suporte pode fechar o diagnóstico/contrato de rejeição, mas não fecha a entrega
funcional da capacidade.

Para cada pendência, usar estes critérios:

- [ ] Identificar API/elementos/nós Coin e definir o comportamento observado pelo usuário.
- [ ] Estudar o caminho GL no Coin, incluindo defaults, exceções e configuração da action.
- [ ] Registrar o dono único da decisão e os dados que Wiring precisa capturar.
- [ ] Definir saída do Core e limites/variantes do perfil, sem comandos GPU.
- [ ] Implementar no BGFX ou rejeitar explicitamente antes de publicar um novo frame.
- [ ] Implementar no wgpu ou rejeitar explicitamente antes de publicar um novo frame.
- [ ] Testar captura → planejamento → adaptação → execução, com referência GL quando relevante.
- [ ] Atualizar capacidades, documentação e profiling; registrar backend, device, formato e tolerância usados.

Essas checkboxes são o modelo por item, não oito trabalhos globais já concluídos.

## Checklist de arquitetura

- [x] **A01 — Nome neutro para o contrato comum.** API, tipos, módulo, exemplos e testes migrados para `CoinRender`; componentes específicos usam `CoinBgfx`/`CoinWgpu`. Headers/pacote antigos encaminham a fonte; consumidores precisam ser recompilados. Evidência: [migração de nomes](coin-render-naming-migration.md).
- [ ] **A02 — Limite Wiring/Core no builder.** Funções de transformação recebem snapshots/arrays Coin, sem acessar actions, paths ou `SoState`.
- [x] **A03 — Composição com dono único.** Classificação de alpha, modalidade Coin, ordenação, screen door e estados efetivos estão no Core comum; interpretação paralela de C++/Rust removida. Fechado nesse escopo; suporte GPU e qualificação GL continuam em F01/F02. [Evidência](coin-render-composition-contract.md).
- [x] **A04 — Plano de execução comum, perfis P09/P12/P13.** Sequência, camadas, barreiras, blend/depth efetivos e grafo RTT staged/direto compartilham o Core. Wiring captura; Infra resolve e executa. Preflight e publicação qualificados no [P13](coin-render-rtt-publication-contract.md); ampliação de formatos/estados RTT continua F14.
- [ ] **A05 — Extrair o comum de `CoinBgfxLowering`.** Manter layout/agrupamento BGFX específicos; mover decisões Coin e cálculos reutilizáveis para Core comum. **Parcial:** transparência, screen door e depth efetivo extraídos nesta etapa.
- [x] **A06 — Recursos opacos com ownership definido, escopo P12.** Action usa IDs lógicos; criação/liberação de tokens está na Infra. Owner, produtor, device, gerações, retenção e invalidação são verificados no contrato comum. Dispositivo default wgpu e runtime compartilhado BGFX no perfil atual. Recuperação compartilhada qualificada no [P14](coin-render-multi-target-contract.md); seleção pública de dispositivos por alvo permanece fora do perfil. [Contrato](coin-render-rtt-ownership-contract.md).
- [x] **A07 — Configuração estruturada, escopo P11.** Renderer/transparência/RTT em opções tipadas e imutáveis por alvo; Shell interpreta defaults. Wiring/Core não interpretam texto/env para essas escolhas. Controles de instrumentação/caches/readback continuam nas campanhas P18/P19. [Contrato](coin-render-selection-contract.md).
- [x] **A08 — Capacidades por contrato e alvo, escopo P11.** V3 separa fatos conhecidos, mecanismos implementados/disponíveis e evidência de perfis offscreen; seleção retorna motivos estruturados. V1/V2 preservados. Janela, novos drivers e erros gerais de recursos não são certificados pelo probe. [Contrato](coin-render-selection-contract.md).
- [ ] **A09 — Resultados e profiling.** **Publicação fechada no perfil P13:** Infra retorna candidatos; contrato comum valida resultados/tickets; cor/depth/serial/revisão/borrow só mudam após sucesso. Shell formata códigos/diagnósticos. [Evidência P13](coin-render-rtt-publication-contract.md). Instrumentação e memória completas continuam P18; A09 geral permanece aberta.
- [ ] **A10 — Matriz compartilhada de testes.** Mesmas fixtures e expectativas para cada executor; skips, aproximações e tolerâncias ficam visíveis. **Parcial:** composição comum executada em CPU, wgpu e BGFX; matriz geral ainda aberta.

## Checklist funcional priorizada

| ID | Fechamento acompanhado | Dono da decisão comum | BGFX atual | wgpu atual | Próxima evidência necessária |
|---|---|---|---|---|---|
| F01 | Anotações: camada, ordem e barreira de depth | Core de composição | Caminho existente | Transporte e testes específicos fechados | Planejamento comum fechado em M04; on-top real Part/BRep qualificado em GL/BGFX/wgpu no [P16](coin-render-freecad-viewport.md). Casos ampliados seguem seus perfis. |
| F02 | Onze modalidades de transparência Coin e alpha final | Core de composição | Onze modos no perfil funcional, Vulkan/OpenGL | Mesmo plano comum; aditivo, stipple, triângulos e 1..8 camadas | P09 fechado no [perfil e matriz](coin-render-transparency-contract.md); P10 amplia camadas/depth e orçamento. Seleção tipada e capacidades no perfil P11. |
| F03 | `SoClipPlane` | Captura Wiring + clipping Core | Até oito planos; object/OIT/layers ensaiados em GL/Vulkan | Até oito planos; captura/CPU/GPU ensaiados | P01 fechado no perfil: [contrato e limites](coin-render-clipping-contract.md). Qualificação visual GL, bordas/strokes, RTT e FreeCAD permanecem abertas. |
| F04 | `SoDrawStyle`: LINES, POINTS, INVISIBLE | Wiring de visibilidade; Core de geometria/estilo | INVISIBLE e LINES/POINTS convexos; padrão contínuo; offset plano original; textura/fog ensaiados | Mesmo perfil, padrão contínuo, offset/units D32Float e textura explícita/fog em strokes | [Contrato e checklist](coin-render-draw-style-contract.md): P08 qualifica raster diagonal/fracionário; offset fora do perfil plano/precisão e qualificação ampliada GL/FreeCAD continuam abertos. Padrão contínuo, recortes/cantos/alpha simples e rejeição de contornos fora do perfil ensaiados. |
| F05 | `SoText2` | Captura Wiring + layout/rasterização Core | Sem caminho completo identificado | Sem caminho completo identificado | Estudar GL/fontes, âncora, tamanho, clipping e composição; implementação de atlas específica. |
| F06 | UV procedural/default | Captura Wiring + coordenadas Core | Rejeições e documentação contraditória | Rejeições identificadas | Resolver funções/defaults, espaços de coordenadas e matriz por shape. |
| F07 | Multitextura e `SoTextureCombine` | Core de textura | Oito unidades e combine normalizado, Vulkan/OpenGL qualificados | Mesmo contrato, oito unidades e ABI privada 26 | P08 fechado no [perfil e matriz](coin-render-multitexture-contract.md); UV procedural/default e formatos/qualidade seguem P07. |
| F08 | Linhas/pontos texturizados e atributos de stroke | Core de geometria/stroke | Expansão, continuidade, raster aliased, alpha, clipping, multitextura e fog comuns | Mesmo Core; atributos homogêneos e UVs extras introduzidos na ABI 26 | P08 fechado no [perfil e matriz](coin-render-multitexture-contract.md); modalidades qualificadas no perfil P09; drivers/MSAA e FreeCAD seguem qualificação geral. |
| F09 | Bindings, índices, materiais e alpha por vértice | Core de geometria/material | Implementações e ensaios existentes | Captura comum, requalificação pendente | Matriz completa por shape e bindings, incluindo normais geradas. |
| F10 | Iluminação Coin/Gouraud, luzes e fog | Core de estado + contrato matemático | Comparações existentes | Shaders/caminhos existentes | Matriz numérica comum; limites de luzes e distinção de Phong por fragmento. |
| F11 | Modelos de textura, filtros e qualidade | Core de textura | Modelos ensaiados; perfil limitado | Modelos presentes; docs antigas | Atualizar perfil, filtros/qualidade, formatos e comparações. |
| F12 | Depth test/write/function/range, offset e clamp | Core de estados efetivos | Implementado no perfil; lacunas em range/clamp | Implementado no perfil; teste GPU depth passou | Defaults/overrides Coin, range invertido, clamp, precisão e raster de bordas. |
| F13 | Viewports/scissor parcialmente externos | Core de viewport | Suporte parcial externo | Rejeita fora do alvo | Contrato comum da projeção/clipping; escolher adaptação ou rejeição conforme capacidade. |
| F14 | RTT staged e GPU→GPU direto | Core de dependências/composição + lifecycle Wiring | Perfil RGBA8 staged/direto qualificado, produtores diretos OBJECT | Perfil RGBA8 staged/direto qualificado | Ownership P12; NONE/ALPHA_BLEND, orientação, preflight, falhas, tickets e publicação no [P13](coin-render-rtt-publication-contract.md). F14 parcial: formatos/estados ampliados, ALPHA_TEST e direto de janela; recuperação entre alvos no [P14](coin-render-multi-target-contract.md). |
| F15 | Readback cor/depth, tickets e janela | Contrato comum + lifecycle Wiring | Cor/depth, async e publicação transacional qualificados | Cor/depth, async e publicação transacional qualificados | Tickets completos com pitches específicos da Infra, origem e serial no [P13](coin-render-rtt-publication-contract.md). Admissão 16/128 MiB no [P14](coin-render-multi-target-contract.md); readback de janela, memória física e persistência seguem P18/P19. |
| F16 | Weighted OIT e peeling | Core de política de extensão | Peeling 1..8 e weighted OIT explícito | Peeling 1..8; weighted OIT não oferecido | P10 fechado no [perfil e matriz](coin-render-peeling-contract.md): precisão/alpha zero, luz/textura, orçamento, depth e rejeição de overrides BGFX. Seleção tipada e capacidades no perfil P11. |
| F17 | Múltiplos alvos/janelas | Lifecycle Wiring + admissão Core | Runtime compartilhado, views isoladas e aposentadoria coordenada | Default compartilhado e bridge de dispositivos extras | P14 fechado no [perfil atual](coin-render-multi-target-contract.md): agendamento ordenado, orçamento, RTT/tickets, resize/destruição e recuperação. Matriz física/FreeCAD segue P16/P17/P20. |
| F18 | Qt/manager/FreeCAD, overlays e seleção | Wiring comum; adaptações Coin/host e sincronização da política do manager | OpenGL/Vulkan qualificados no perfil Part/BRep | Vulkan qualificado no mesmo perfil | [P16 fechado no perfil declarado](coin-render-freecad-viewport.md): expose/resize, grade/on-top, Face/Edge/Vertex por input, links aninhados, arrays, documentos e montagem App::Part. Outros workbenches/GL-only seguem F19/F20. |
| F19 | Nós de workbenches que só fazem GLRender | Wiring/adaptação comum dos nós | Inventário Gui/Mod P15: 33 classes/47 overrides | Mesma captura comum, mesmos bloqueios | [P15 fechado como inventário](coin-render-node-inventory.md): candidatos/parciais/bloqueados por classe e workbench. Geometria GL-only e qualificação no host seguem I04/P16; travessia não certifica suporte. |
| F20 | SoImage, shaders customizados, sombras, 3D/cube maps | Captura Wiring + contrato Core + execução Infra | SoImage/shader têm lacunas de captura/diagnóstico caracterizadas | Mesmas lacunas anteriores à Infra | [P15](coin-render-node-inventory.md): 13 criações SoImage e 1 SoTexture3; CAM shader fora do grafo Coin. I02/I03/I07/I08 permanecem funcionais abertas; não assumir suporte por SUCCESS. |
| F21 | Antialiasing, multipass e superfícies/plataformas | Core de política + Infra | Equivalência incompleta | Equivalência incompleta | Contrato de qualidade/action, mecanismos e matriz física Intel/NVIDIA/plataformas. |

A matriz resume o checkout/auditoria; não deve ser lida como nova certificação de
hardware. Dawn/native permanece protótipo, sem entrar como executor equivalente.

## Marcos já fechados no escopo

- [x] **M01 — Metadados de anotações na ponte Rust.** `render_layer` e `clear_depth_before`, introduzidos na ABI privada 20 (atual 28), testes de empacotamento, reuso e camera patch.
- [x] **M02 — Execução offscreen de anotações no wgpu.** Testes de pixels para ordem opaco/transparente, depth write explícito, limpeza restrita à viewport, preservação do frame rejeitado e frame vazio; CTest sem skip na etapa anterior.
- [x] **M03 — Registro dos limites dessa entrega.** [Contrato de anotações](coin-wgpu-annotation-contract.md) documenta o escopo e aponta a correção da suíte geral de composição.
- [x] **M04 — Anotações com planejamento comum.** Ordem e depth efetivo compartilham o Core e as fixtures de composição. Isso fecha A03; A04 passou a incluir dependências RTT no P12; a qualificação ampliada de F01 continua aberta. [Evidência](coin-render-composition-contract.md).

- [x] **M05 — Clipping comum (P01/F03).** Captura de planos em mundo, equações em Core, strokes recortados antes da expansão e execução BGFX/wgpu; ABI privada 25. [Contrato, evidências e limites](coin-render-clipping-contract.md).

## Registro a preencher a cada fechamento

```text
ID:
Escopo Coin e referência GL (arquivos/funções):
Dono da decisão comum:
Dados capturados por Wiring:
Saída resolvida do Core:
Mecanismo BGFX / suporte ou rejeição:
Mecanismo wgpu / suporte ou rejeição:
Testes e resultados (sem omitir skips/falhas):
Device, API, alvo/formato e tolerâncias:
Documentação/capacidades atualizadas:
Limites e extensões explícitas:
Commit/PR ou artefato de evidência:
Estado final: aberto / parcial / fechado no escopo / extensão
```

Ordem atual: bloqueios de viewport; geometria, materiais, iluminação e texturas;
composição funcional; recursos/RTT e integração real; desempenho e matrizes.
Dependências e frentes que podem avançar juntas estão no [plano de trabalho](coin-render-work-plan.md).
A01 e A03 estão fechados nos escopos registrados; não substituem o fechamento funcional.
