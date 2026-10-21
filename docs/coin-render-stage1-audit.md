# Encerramento da etapa 1 do CoinRender

Em 16/10/2026, o inventário e a triagem da etapa 1 estão concluídos no escopo de planejamento: **284 classes públicas**, **58 nomes de métodos públicos do manager** e **120 processos visuais históricos** foram indexados. Outras **142 declarações públicas** de actions, scene manager e offscreen estão no inventário de APIs gráficas. Implementação, correções e requalificação continuam abertas na [matriz](coin-render-feature-matrix.md). O encerramento desta etapa não certifica paridade completa ou imagem do HEAD.

Código CoinRender `24ddf557af4c8956b008d51a53446f4671f33a3b`; master `ca247c1321bce07199ec0f034493977761b3218e`. A coleta é estática e reconcilia evidências preservadas, sem build ou campanha GPU nova. A [base JSON de nós](inventories/coin-render-master-node-inventory.json) registra herança, declarações, métodos GL/callback e referências da captura. Presença por callback é um candidato de integração, não prova de aparência equivalente.

## Inventário dos nós do master

Escopo: headers públicos com classes So derivadas em nodes, VRMLnodes, draggers, manips, nodekits e FXViz/nodes. Foram lidos 273 headers e localizadas 284 classes; bases abstratas estão incluídas. O JSON permite verificar cada caminho e linha, cadeia de herança e métodos do master. APIs de campos, matemática, IO, áudio e ações não gráficas não são novas obrigações de renderer; continuam na biblioteca Coin. Elementos/estados gráficos e APIs de action entram na triagem abaixo, sem inferir suporte de um include.

| Área | Encaminhamento de acompanhamento | Pendência funcional |
| --- | --- | --- |
| Shapes e derivados | R02/R04; C01–C05 | Primitivas e bindings têm perfis; extensão GL-only precisa adaptação |
| Estado e grupos | R01/R03/R05–R12 | Herança/override/estado efetivo e cruzamentos precisam seguir os contratos |
| VRML | R33 e C22 | Inventário separado de fundos, geometria/texturas e comportamento específico; nenhuma certificação global VRML |
| Draggers/manips/nodekits | R21 e C16 | Grafo e callbacks não certificam consumidor/input real |
| Programas e texturas espaciais | R22–R25/C17 | Rejeições existem; implementar o comportamento portátil |
| Bump | R26/B12 | Callback legado inerte e captura ausente; efeito pode ser ignorado |

## Auditoria do manager

O [inventário por assinatura](inventories/coin-render-manager-api-inventory.json) relaciona cada nome de método público externo a estado, ID e achado. Construtor/destrutor da fonte continuam no host; lifetime do adaptador exige destruição antes da janela. `prepareFrame/finishFrame` são adições de CoinRender, não métodos do master.

| Grupo | Estado confirmado na leitura | Próximo trabalho |
| --- | --- | --- |
| Root, fundo RGBA e viewport | Copiados em syncFromRenderManager | Preservar esse contrato nas ampliações |
| Transparência | Lida da GLRenderAction fonte | Demais opções dessa action não são copiadas |
| Sensores, pre/post, áudio e realTime | prepareFrame/finishFrame, inclusive conclusão em erro | Callbacks host não podem depender de inicialização GL |
| Câmera/autoclip | Manager fonte atualiza clipping da câmera externa, mas renderiza apenas o grafo; o adaptador preserva essa distinção | [C23 qualificado no Linux](coin-render-c23-external-camera-20261017.md): câmera fornecida via setCamera fora do root |
| Texturas habilitadas e render mode | Não encaminhados | R30: AS_IS, WIREFRAME, POINTS, WIREFRAME_OVERLAY, HIDDEN_LINE, BOUNDING_BOX, SHADED_HIDDEN_LINES |
| Estéreo | Não encaminhado | R29: MONO, ANAGLYPH, SEPARATE_OUTPUT/QUAD_BUFFER, linhas/colunas intercaladas e offset |
| Superimpositions | Não enumeradas/renderizadas pelo adaptador | R30: ZBUFFERON/CLEARZBUFFER/AUTOREDRAW/BACKGROUND, enabled e transparência própria |
| Overlay color, RGB/index e double buffer | Não encaminhados | Definir equivalência/indisponibilidade por surface, evitando ignorar pedido |
| Antialiasing, passes e clear flags | Não encaminhados | R27/R28: overloads render/initmatrices/clearwindow/clearzbuffer e smoothing/numPasses |
| Redraw, render callback, dimensão/origem e activate/deactivate | Fonte/host, sem substituição integral | Eventos, resize e viewport explícitos; validar agendamento |
| reinitialize | Não propagado diretamente | Relacionar com reconstrução/gerações do target R18 |

Reprodução da coleta: `python3 scripts/coinrender/audit_stage1_inventory.py --source . --output /tmp/coinrender-stage1-recheck --archive "/home/dikluwe/Área de trabalho/Estudo coin/consolidacao-frentes-20261016/renderer-history/docs/validation"`. A saída deve ser comparada antes de atualizar o registro, pois master/HEAD podem avançar.

Fontes: [manager](../include/Inventor/SoRenderManager.h), [adaptador](../src/rendering/coinrender/CoinRenderManagerAdapter.cpp), [scene manager](../include/Inventor/rendering/CoinRenderSceneManager.h), [lifecycle](../src/rendering/SoRenderManager.cpp).

## Auditoria de bump e APIs da action

`SoBumpMap::GLRender` preenche estado GL usado no caminho legado. Seu `callback` tem a chamada de doAction comentada; CoinRenderAction/FramePlanBuilder e os executores desta branch não capturam SoBumpMap/SoBumpMapElement. Assim, R26 passa de “auditar” a **ausente**, com B12 para risco de efeito ativo ignorado. Corrigir primeiro a detecção de capacidade ou implementar o efeito; esta auditoria não alterou o runtime. Fonte: [SoBumpMap](../src/nodes/SoBumpMap.cpp), [action](../src/actions/CoinRenderAction.cpp).

O [inventário de APIs gráficas](inventories/coin-render-public-render-api-inventory.json) enumera as declarações públicas de SoGLRenderAction, highlight actions, SoSceneManager e SoOffscreenRenderer. A correspondência de nomes não certifica semântica. A triagem inclui ainda a configuração pública de [SoGLRenderAction](../include/Inventor/actions/SoGLRenderAction.h): viewport, transparência, sorting, depth e callbacks têm caminhos próprios no CoinRender; `setSmoothing`, `setNumPasses`, pass callback/update, update area, abort callback e estados cache-context/remote não herdam equivalência apenas pela existência da nova action. R34/C24 registram esse contrato e a auditoria funcional por opção a implementar na etapa seguinte. Highlight actions (box/line) e passes auxiliares de seleção ficam em R35/C25; picking geométrico existente não prova o highlight visual. Componentes L/LA/RGB/RGBA, exportação RGB/PostScript/codecs e opções específicas de contexto do offscreen ficam em R36/C26 para definir correspondência com o target/readback CoinRender; a API legada permanece no CoinGL.

## Reconciliação visual por processo

O [ledger JSON](inventories/coin-render-visual-case-ledger.json) conserva um ID B04.nnn para cada processo, resultado original, plataforma, API, política, cena, manifesto, índice e hash do log quando presente. Não inclui variáveis de ambiente. Cada processo tem documento de continuação e próximo passo; nenhum FAIL foi promovido a PASS sem execução do mesmo caso. Correção posterior tem estado descritivo separado.

| Origem | Processos | PASS original | FAIL original | Requalificação do HEAD |
| --- | --- | --- | --- | --- |
| Linux visual 20261010 | 48 | 16 | 32 | Não executada |
| Windows visual 20261010 | 72 | 30 | 42 | Não executada |
| Total | 120 | 46 | 74 | Não executada |

Linux: cidades receberam correções de coordenadas/câmera; terreno recebeu upload particionado/luz; BGFX/OpenGL recebeu alvo intermediário e patch LEQUAL. Esses controles posteriores são ligados individualmente, mas o ledger mantém o FAIL da campanha original. Wgpu/OpenGL continua com B03. Windows permanece aberto por caso: correção Linux não comprova comportamento Windows. B02 (instancing) é um controle separado e não é apagado pela correção de janela.

Os hashes dos seis manifestos originais estão no ledger; o nome/índice de cada registro permite recuperação. Os recibos completos permanecem no arquivo permanente indicado nos campos source/log. Copiar o índice para o repositório não publica nem altera essas evidências históricas.

## Pendências novas identificadas

| ID | Achado | Destino |
| --- | --- | --- |
| R33/C22 | Cobertura visual VRML específica não pode ser inferida da herança | Classificar e completar por nó/efeito na matriz funcional |
| R34/C24 | APIs gráficas da action além dos perfis atuais | Capturar/implementar opções ou rejeitar pedidos sem equivalência |
| R35/C25 | Highlight actions e composição de seleção | Comparação box/line highlight com transparência, clipping e layers |
| R36/C26 | Correspondência de componentes, exportação e opções offscreen | Contrato de saída/conversão e integração do consumidor |
| C23 | Câmera setada no manager fora do root | [Contrato e equivalência local qualificados](coin-render-c23-external-camera-20261017.md); execução externa permanece nos V |
| B12 | SoBumpMap ativo sem avaliador/rejeição equivalente | Detectar e corrigir na etapa 2 ou implementar R26 |

## Critério de encerramento da etapa 1

- [x] Matriz de recursos/combinações/plataformas com IDs estáveis e fontes.
- [x] Inventário reproduzível das classes públicas do master no escopo gráfico declarado, sem prometer suporte por herança.
- [x] Auditoria do manager e bump com lacunas concretas incorporadas.
- [x] Ledger individual dos 120 processos, com 46 PASS/74 FAIL preservados e continuações separadas.
- [x] Lacunas descobertas receberam IDs e próximo fechamento funcional.
- [x] Separação entre etapa de inventário concluída e etapas de implementação/GPU abertas.

A próxima etapa é resolver B01–B12 e ampliar C01–C26 conforme dependências. R33–R36 representam áreas de implementação/qualificação abertas; sua enumeração não as certifica. Cada ampliação pode descobrir novos casos e deve acrescentar IDs. Uma triagem completa do conjunto delimitado não equivale a verificar toda combinação possível de campos, callbacks e drivers.
