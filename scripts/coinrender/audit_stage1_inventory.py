import json,re,subprocess,hashlib,collections
from pathlib import Path
import argparse
parser=argparse.ArgumentParser(description='Inventario estatico e reconciliacao historica da etapa 1; nao executa GPU')
parser.add_argument('--source',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--archive',type=Path,default=Path('/home/dikluwe/Área de trabalho/Estudo coin/consolidacao-frentes-20261016/renderer-history/docs/validation'))
args=parser.parse_args();root=args.source.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
def git(*args):return subprocess.check_output(['git','-C',str(root),*args],text=True)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
master=git('rev-parse','master').strip();head=git('rev-parse','HEAD').strip()
paths=git('ls-tree','-r','--name-only',master,'include/Inventor').splitlines()
headers={p:git('show',master+':'+p) for p in paths if p.endswith('.h') and any('/'+x+'/' in p for x in ['nodes','VRMLnodes','draggers','manips','nodekits'])}
# Class identity and public inheritance, not a claim of semantic/visual support.
classes={}
for p,s in headers.items():
 for m in re.finditer(r'class\s+(?:COIN_DLL_API\s+)?(So\w+)\s*:\s*public\s+(So\w+)',s):
  classes[m[1]]={'header_field_declarations':re.findall(r'\b(So(?:SF|MF)\w+)\s+(\w+)\s*;',s),'name':m[1],'base':m[2],'header':p,'line':s[:m.start()].count('\n')+1,'header_sha256':hashlib.sha256(s.encode()).hexdigest()}
# Reuse all master source definitions to locate GL and callback responsibilities.
sourcepaths=git('ls-tree','-r','--name-only',master,'src').splitlines()
methods=collections.defaultdict(dict)
for p in sourcepaths:
 if not p.endswith(('.cpp','.h')):continue
 s=git('show',master+':'+p)
 for m in re.finditer(r'\b(So\w+)::(callback|doAction|generatePrimitives|GLRender(?:BelowPath|InPath|OffPath)?)\s*\(',s):
  methods[m[1]].setdefault(m[2],[]).append({'path':p,'line':s[:m.start()].count('\n')+1})
current_paths=['src/actions/CoinRenderAction.cpp','src/rendering/coinrender/CoinRenderFramePlanBuilder.cpp']
cur={p:(root/p).read_text() for p in current_paths}
explicit_missing={'SoTexture3':('R23','recusa explicita'),'SoTextureCubeMap':('R24','recusa explicita'),'SoSceneTextureCubeMap':('R25','recusa explicita'),'SoShaderProgram':('R22','recusa explicita'),'SoBumpMap':('R26','captura ausente e callback legado inerte'),'SoBumpMapCoordinate':('R26','estado de bump sem avaliador equivalente'),'SoBumpMapTransform':('R26','matriz de bump sem avaliador equivalente')}
records=[]
for name,v in sorted(classes.items()):
 chain=[];n=name
 while n in classes and n not in chain:chain.append(n);n=classes[n]['base']
 chain.append(n)
 mentions=[{'path':p,'lines':[i for i,l in enumerate(s.splitlines(),1) if re.search(r'\b'+name+r'\b',l)]} for p,s in cur.items() if re.search(r'\b'+name+r'\b',s)]
 if name in explicit_missing:rid,status=explicit_missing[name]
 elif 'SoCamera' in chain:rid,status='R03','camera por estado/callback; perfil qualificado somente conforme matriz'
 elif 'SoShape' in chain:rid,status='R02','geometria por primitivas; extensoes GL-only e combinacoes exigem qualificacao'
 elif 'SoLight' in chain:rid,status='R05','luz por captura comum; dominio depende de campos e perfil'
 elif '/VRMLnodes/' in v['header']:rid,status='R33','VRML: comportamento especifico requer classificacao funcional; hierarquia nao comprova equivalencia'
 elif any(x in v['header'] for x in ['/draggers/','/manips/','/nodekits/']):rid,status='R21','composicao/interacao por grafo e callbacks; consumidor real requer qualificacao'
 else:rid,status='R01','estado/grupo por callbacks; sem certificacao automatica por presenca'
 records.append({**v,'inheritance':chain,'master_methods':methods[name],'coinrender_mentions':mentions,'resource_id':rid,'classification':status,'visual_status':'nao inferido por analise estatica'})
assert records and len({r['name'] for r in records})==len(records)
(out/'coin-render-master-node-inventory.json').write_text(json.dumps({'schema_version':1,'master':master,'coinrender':head,'scope':'Classes publicas So com heranca em nodes, VRMLnodes, draggers, manips, nodekits e annex FXViz/nodes; fontes master de metodos callback/doAction/generatePrimitives/GLRender. Interfaces abstratas e bases incluidas; arquivos sem declaracao de classe nao sao nos.','headers_scanned':len(headers),'classes':records},ensure_ascii=False,indent=2)+'\n')
# Read-only historical process ledger; no environment dump or mutation of receipts.
base=args.archive.resolve()
ledger=[];sources=[]
def later(platform,backend,api,scene,policy,passed):
 if passed:return ('passe historico preservado; sem revalidacao do HEAD','docs/coin-render-branch-guide.md','nenhuma falha historica neste processo; ampliar conforme matriz')
 if platform=='windows':return ('falha historica aberta na celula Windows; correcoes Linux nao transferem qualificacao','docs/coin-render-windows-visual-scenes-20261010.md','B04; reexecutar o caso na revisao final Windows')
 if backend=='wgpu' and api=='opengl':return ('correcoes locais posteriores e repeticoes sem reproduzir quadro preto; causa intermitente aberta','docs/coin-render-wgpu-opengl-first-present-20261010.md','B03/B04; qualificar celula completa e primeira apresentacao')
 if backend=='bgfx' and api=='opengl':return ('correcao posterior de alvo janela/LEQUAL em perfis Linux; campanha original nao reexecutada integralmente','docs/coin-render-bgfx-lequal-camera-patch-20261010.md','B04; conferir controle equivalente, preservar B02 separado')
 if scene.startswith('city'):
  p='docs/coin-render-wgpu-camera-world-20261010.md' if backend=='wgpu' else 'docs/coin-render-bgfx-camera-world-20261010.md'
  return ('correcao de coordenadas de camera com controles posteriores Linux; sem converter resultado original',p,'B04; repetir caso final se controle posterior nao cobrir fixture/revisao')
 if scene=='terrain-million':return ('correcoes posteriores de upload/luz no Linux; controles Vulkan registrados','docs/coin-render-camera-light-overlay-20261010.md','B04; conferir upload particionado e imagens na celula final')
 return ('falha sem fechamento individual comprovado','docs/coin-render-branch-guide.md','B04; diagnosticar e repetir caso')
for platform in ['linux','windows']:
 files=list((base/'linux-visual-20261010').glob('results-*.json')) if platform=='linux' else list((base/'windows-visual-20261010').glob('*/summary.json'))
 for p in sorted(files):
  sources.append({'path':str(p),'sha256':sha(p)})
  for idx,d in enumerate(json.loads(p.read_text())):
   if platform=='linux':name=d['name'];cohort=d['cohort'];backend=d['backend'];api=d['api'];scene=d['scene'];policy=d['policy'];passed=d['pass'];log=base/'linux-visual-20261010/logs'/backend/(name+'.log')
   else:
    name=d['label'];cohort=p.parent.name;cmd=d['command'];api,policy,scene=cmd[1:4];backend='bgfx' if '-bgfx-' in name else 'wgpu';passed=d['pass_gate'];log=p.parent/d['log']
   state,follow,nextstep=later(platform,backend,api,scene,policy,passed)
   row={'id':f'B04.{len(ledger)+1:03}','platform':platform,'cohort':cohort,'name':name,'backend':backend,'api':api,'policy':policy,'scene':scene,'original_result':'PASS' if passed else 'FAIL','exit':d['exit'],'source':str(p),'source_index':idx,'log':str(log),'log_exists':log.exists(),'later_status':state,'followup_document':follow,'next_step':nextstep,'head_retest':'nao executado'}
   if platform=='linux':row['original_oracle']=d.get('oracle');row['gpu_receipt']=d.get('receipt');row['gl_adapter']=d.get('gl_adapter')
   if log.exists():row['log_sha256']=sha(log)
   if 'dll_sha256' in d:row['original_dll_sha256']=d['dll_sha256']
   ledger.append(row)
counts={p:dict(collections.Counter(x['original_result'] for x in ledger if x['platform']==p)) for p in ['linux','windows']}
assert counts=={'linux':{'PASS':16,'FAIL':32},'windows':{'PASS':30,'FAIL':42}},counts
(out/'coin-render-visual-case-ledger.json').write_text(json.dumps({'schema_version':1,'coinrender':head,'criterion':'historico: igualdade RGB janela/offscreen, nao paridade com CoinGL','source_manifests':sources,'counts':counts,'processes':ledger},ensure_ascii=False,indent=2)+'\n')
# Read master manager method declarations, including overloads and nested class; record each signature.
manager=git('show',master+':include/Inventor/SoRenderManager.h')
manager=re.sub(r'/\*.*?\*/','',manager,flags=re.S);manager=re.sub(r'//[^\n]*','',manager)
public_names=['render','addSuperimposition','removeSuperimposition','setSceneGraph','getSceneGraph','setCamera','getCamera','setAutoClipping','getAutoClipping','setNearPlaneValue','getNearPlaneValue','setTexturesEnabled','isTexturesEnabled','setDoubleBuffer','isDoubleBuffer','setRenderMode','getRenderMode','setStereoMode','getStereoMode','setStereoOffset','getStereoOffset','setRenderCallback','isAutoRedraw','setRedrawPriority','getRedrawPriority','scheduleRedraw','setWindowSize','getWindowSize','setSize','getSize','setOrigin','getOrigin','setViewportRegion','getViewportRegion','setBackgroundColor','getBackgroundColor','setOverlayColor','getOverlayColor','setBackgroundIndex','getBackgroundIndex','setRGBMode','isRGBMode','activate','deactivate','setAntialiasing','getAntialiasing','setGLRenderAction','getGLRenderAction','setAudioRenderAction','getAudioRenderAction','enableRealTimeUpdate','isRealTimeUpdateEnabled','getDefaultRedrawPriority','addPreRenderCallback','removePreRenderCallback','addPostRenderCallback','removePostRenderCallback','reinitialize']
api=[]
for name in public_names:
 sigs=re.findall(r'\b'+name+r'\s*\([^;{}]*\)\s*(?:const)?\s*;',manager,re.S)
 assert sigs,name
 if name in ['setSceneGraph','getSceneGraph','setViewportRegion','getViewportRegion','setBackgroundColor','getBackgroundColor']:state='encaminhado pelo adaptador';rid='R20';detail='syncFromRenderManager copia root, viewport e fundo; setSceneGraphOverride opcional'
 elif name in ['setAutoClipping','getAutoClipping','setNearPlaneValue','getNearPlaneValue','setCamera','getCamera']:state='parcial via manager fonte';rid='R20';detail='prepareFrame atualiza clipping do manager fonte; camera independente do grafo nao e copiada pelo adaptador'
 elif name in ['addPreRenderCallback','removePreRenderCallback','addPostRenderCallback','removePostRenderCallback','setAudioRenderAction','getAudioRenderAction','enableRealTimeUpdate','isRealTimeUpdateEnabled']:state='ciclo comum preservado';rid='R20';detail='prepareFrame/finishFrame executam pre/post, audio e realTime; callbacks devem ser independentes de GL'
 elif 'Stereo' in name:state='sem encaminhamento';rid='R29';detail='modos MONO/ANAGLYPH/SEPARATE_OUTPUT/INTERLEAVED_ROWS/COLUMNS e offset nao chegam ao plano'
 elif 'Superimposition' in name or name in ['setOverlayColor','getOverlayColor','setRenderMode','getRenderMode']:state='sem equivalencia integral';rid='R30';detail='camadas SoAnnotation e override de root existem; nao implementam a API completa de modos/superimpositions'
 elif name in ['setAntialiasing','getAntialiasing']:state='sem encaminhamento';rid='R28';detail='smoothing e numPasses da fonte nao sao copiados'
 elif name in ['setGLRenderAction','getGLRenderAction']:state='configuracao parcialmente lida';rid='R20';detail='somente transparencia e lida; action GL e demais opcoes nao sao executadas pelo adaptador'
 elif name=='render':state='entrada alternativa parcial';rid='R20';detail='adapter.render nao equivale aos overloads clearwindow/clearzbuffer/initmatrices/action externa'
 elif name in ['setTexturesEnabled','isTexturesEnabled','setRGBMode','isRGBMode','setBackgroundIndex','getBackgroundIndex','setDoubleBuffer','isDoubleBuffer']:state='sem encaminhamento';rid='R30';detail='estado nao e copiado pelo adaptador; apresentar via surface nao prova equivalencia'
 elif name=='reinitialize':state='sem equivalencia direta';rid='R18';detail='resize/device-loss do target tem contrato proprio; reinitialize da fonte nao e propagado'
 else:state='responsabilidade do host/fonte';rid='R20';detail='agendamento, ativacao, render callback e dimensoes continuam no host; resize e viewport exigem encaminhamento explicito'
 api.append({'method':name,'signatures':[' '.join(x.split()) for x in sigs],'state':state,'resource':rid,'finding':detail})
(out/'coin-render-manager-api-inventory.json').write_text(json.dumps({'schema_version':1,'master':master,'coinrender':head,'scope':'58 nomes de metodos publicos da classe externa SoRenderManager; construtor/destrutor e API aninhada Superimposition tratados na auditoria Markdown','methods':api},ensure_ascii=False,indent=2)+'\n')
# Public rendering action/manager/offscreen options, kept separate from node traversal.
api_headers={
'include/Inventor/actions/SoGLRenderAction.h':'R34',
'include/Inventor/actions/SoBoxHighlightRenderAction.h':'R35',
'include/Inventor/actions/SoLineHighlightRenderAction.h':'R35',
'include/Inventor/SoSceneManager.h':'R20',
'include/Inventor/SoOffscreenRenderer.h':'R36',
}
option_records=[]
for path,rid in api_headers.items():
 s=git('show',master+':'+path)
 clean=re.sub(r'/\*.*?\*/','',s,flags=re.S);clean=re.sub(r'//[^\n]*','',clean)
 public=clean.split('public:',1)[1].split('protected:',1)[0].split('private:',1)[0]
 for m in re.finditer(r'\b([A-Za-z_]\w*)\s*\(([^;{}]*)\)\s*(?:const\s*)?(?:override\s*)?;',public,re.S):
  name=m[1]
  if name.startswith('SO_'):continue
  found=bool(re.search(r'\b'+re.escape(name)+r'\s*\(', (root/'include/Inventor/actions/CoinRenderAction.h').read_text()))
  state='triagem de fronteira; equivalencia por contrato/caso, nao inferida por assinatura'
  if rid=='R34':state='assinatura homonima na action alternativa; sem prova de equivalencia geral' if found else 'sem assinatura homonima na action alternativa; adaptar contrato'
  if rid=='R35':state='action GL especializada; selecao/picking do host nao comprova esta action'
  if rid=='R36':state='captura CoinRender propria; componentes/exportacao/contexto legado exigem correspondencia'
  option_records.append({'header':path,'method':name,'signature':m.group(0),'resource':rid,'state':state,'master_header_sha256':hashlib.sha256(s.encode()).hexdigest()})
(out/'coin-render-public-render-api-inventory.json').write_text(json.dumps({'schema_version':1,'master':master,'coinrender':head,'headers':api_headers,'declarations':option_records},ensure_ascii=False,indent=2)+'\n')

# Human-readable companion.
md=f'''# Encerramento da etapa 1 do CoinRender

Em 16/10/2026, o inventário e a triagem da etapa 1 estão concluídos no escopo de planejamento: **{len(records)} classes públicas**, **{len(api)} nomes de métodos públicos do manager** e **{len(ledger)} processos visuais históricos** foram indexados. Outras **{len(option_records)} declarações públicas** de actions, scene manager e offscreen estão no inventário de APIs gráficas. Implementação, correções e requalificação continuam abertas na [matriz](coin-render-feature-matrix.md). O encerramento desta etapa não certifica paridade completa ou imagem do HEAD.

Código CoinRender `{head}`; master `{master}`. A coleta é estática e reconcilia evidências preservadas, sem build ou campanha GPU nova. A [base JSON de nós](inventories/coin-render-master-node-inventory.json) registra herança, declarações, métodos GL/callback e referências da captura. Presença por callback é um candidato de integração, não prova de aparência equivalente.

## Inventário dos nós do master

Escopo: headers públicos com classes So derivadas em nodes, VRMLnodes, draggers, manips, nodekits e FXViz/nodes. Foram lidos {len(headers)} headers e localizadas {len(records)} classes; bases abstratas estão incluídas. O JSON permite verificar cada caminho e linha, cadeia de herança e métodos do master. APIs de campos, matemática, IO, áudio e ações não gráficas não são novas obrigações de renderer; continuam na biblioteca Coin. Elementos/estados gráficos e APIs de action entram na triagem abaixo, sem inferir suporte de um include.

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
| Câmera/autoclip | Manager fonte atualiza clipping; adaptador não copia câmera independente | C23: câmera fornecida via setCamera fora do root |
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
| C23 | Câmera setada no manager fora do root | Contrato explícito de câmera e validação da equivalência |
| B12 | SoBumpMap ativo sem avaliador/rejeição equivalente | Detectar e corrigir na etapa 2 ou implementar R26 |

## Critério de encerramento da etapa 1

- [x] Matriz de recursos/combinações/plataformas com IDs estáveis e fontes.
- [x] Inventário reproduzível das classes públicas do master no escopo gráfico declarado, sem prometer suporte por herança.
- [x] Auditoria do manager e bump com lacunas concretas incorporadas.
- [x] Ledger individual dos 120 processos, com 46 PASS/74 FAIL preservados e continuações separadas.
- [x] Lacunas descobertas receberam IDs e próximo fechamento funcional.
- [x] Separação entre etapa de inventário concluída e etapas de implementação/GPU abertas.

A próxima etapa é resolver B01–B12 e ampliar C01–C26 conforme dependências. R33–R36 representam áreas de implementação/qualificação abertas; sua enumeração não as certifica. Cada ampliação pode descobrir novos casos e deve acrescentar IDs. Uma triagem completa do conjunto delimitado não equivale a verificar toda combinação possível de campos, callbacks e drivers.
'''
(out/'coin-render-stage1-audit.md').write_text(md)
# Full process index readable without JSON viewer.
lines=['# Ledger das campanhas visuais do CoinRender','','Índice dos processos B04 da etapa 1. Resultado original preservado; nenhum novo teste foi executado. Detalhes, hashes e recibos no [JSON](inventories/coin-render-visual-case-ledger.json).','','| ID | Plataforma | Campanha e caso | Original | Continuação e próximo fechamento |','| --- | --- | --- | --- | --- |']
for r in ledger:lines.append(f"| {r['id']} | {r['platform']} | {r['cohort']} / {r['name']} | {r['original_result']} | [{r['later_status']}]({r['followup_document'].removeprefix('docs/')}); {r['next_step']} |")
(out/'coin-render-visual-case-ledger.md').write_text('\n'.join(lines)+'\n')
print(json.dumps({'classes':len(records),'headers':len(headers),'manager_names':len(api),'processes':len(ledger),'counts':counts,'logs_missing':sum(not r['log_exists'] for r in ledger)},ensure_ascii=False))
