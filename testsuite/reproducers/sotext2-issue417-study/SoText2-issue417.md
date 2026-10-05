# SoText2 e base da correção da issue 417

Estado: correção validada no escopo dos testes da issue 417 na branch `codex/fix/issue417-sotext2`, em 5 de outubro de 2026. A falha da macro de captura foi resolvida e a comparação visual no FreeCAD passou. Resultados finais: 38 casos Coin, 79 testes CTest e 44 verificações da comparação FreeCAD, sem falhas na correção. Veja a última seção para evidências e limites de abrangência.

O objetivo é melhorar o Coin usado pelo FreeCAD. O FreeCAD local contém uma adaptação explícita à issue 417 em `SoColorBarLabel`; este estudo descreve a correção na origem e a retirada posterior dessa adaptação.

## Base e estudos anteriores

- Coin `origin/master`: `674e74267df863dbaf50416c477bc7f918a826d8`.
- FreeCAD local: `228c679d78845c3fb6f5eb3d1a27f48aceab68b5`, com alterações locais. O inventário descreve esse checkout, sem alegar equivalência à master pública atual.
- [SoFonts](SoFonts.md) já estudou `SoGlyphCache`, caches globais, dimensões dos bitmaps, fallback e integração em `SoText2`. Sua base é `9cb3cb87a944`, distinta desta reprodução. Seus achados precisam de reconfirmação antes de serem classificados como pendências atuais.
- [SoShapeGeometry](SoShapeGeometry.md) registra contratos e riscos de bounding boxes, travessia e caches. Também tem base histórica distinta.
- Coin `docs/BUGS.txt`, item 219: View All com texto, registrado em 2006. Item 099 e o arquivo de 2004 sobre SoText2 nunca descartado registram o problema relacionado de culling e caches.
- Não havia arquivo dedicado à issue 417 nos estudos consultados. Isso corrige a lacuna da busca anterior, que não incluiu esta pasta.

O estudo de fontes já aponta dimensões em 16 bits e multiplicações do buffer RGBA. O novo caso de projeção acrescenta outra fronteira: a posição da âncora fora da tela é convertida para `short` antes de calcular seus limites. Corrigir essa posição não corrige automaticamente largura, altura e alocação dos glifos.

## Reprodução e evidência

Compilação limpa por `git archive origin/master`, GCC 13.3, Release, Xvfb e GLX direto por software, viewport 640 por 480. SoXt não está disponível: preservamos seu exemplo original, mas renderizamos o mesmo grafo por `SoOffscreenRenderer`. Essa primeira etapa usou somente Coin; a validação posterior dentro do FreeCAD está registrada ao final.

| Cenário | Master | Remoção global do descarte | Base proposta |
| --- | --- | --- | --- |
| Cena original, texto em 0.25, 0, 0.25 e View All | Caixa vazia, 0 pixels | 683 pixels | 683 pixels |
| Texto em x igual a 5 e View All | Caixa vazia, 0 pixels | 683 pixels | 683 pixels |
| Texto em x igual a 100 e View All | Caixa vazia, 0 pixels | Câmera x igual a menos 12.998, 0 pixels | Câmera x igual a 100.110, 683 pixels |
| Câmera ajustada manualmente para o texto distante | 683 pixels | 683 pixels | 683 pixels |
| Texto visível com View All, perspectiva ou ortográfica | 683 pixels | 683 pixels | 683 pixels |

Nove cenários por variante também cobrem cena mista com cubo. As contagens na cena mista incluem geometria; não provam isoladamente a presença do texto. Texto na origem com câmera padrão sem View All continua invisível em todas as variantes por clipping no plano próximo; esse caso não deve ser atribuído automaticamente à issue 417.

## Contrato que fundamenta a correção

`getQuad` calcula o retângulo do texto em coordenadas 3D a partir de métricas em pixels, transformação, câmera e viewport. Hoje devolve falso se o retângulo está fora da visão. Esse retorno é usado por três consumidores:

1. Cálculo de bounding box: precisa conhecer a extensão de texto que pode estar fora da visão, inclusive para reposicionar a câmera.
2. Renderização: pode descartar texto fora da visão, além do clipping do OpenGL.
3. Seleção por raio: precisa preservar o contrato de texto visível e as verificações próprias da action.

`SoCamera::viewAll` retorna se a caixa é vazia. Logo, usar a câmera atual para eliminar a extensão impede que View All encontre o texto. O commit `54c3cdc81f`, de 2009, introduziu esse descarte compartilhado.

A base experimental separa essas responsabilidades sem mudar header público ou layout da classe pública:

- Um parâmetro privado seleciona se o quad deve ser filtrado pela visibilidade.
- O `SoText2::computeBBox` público solicita a extensão sem esse filtro.
- Os caminhos privados usados por desenho e seleção mantêm o filtro.
- A âncora projetada usa coordenadas em ponto flutuante, com truncamento de pixel para preservar a regra existente dentro da faixa. Ela não atravessa mais `SbVec2s` e `short`.
- Verificações preliminares rejeitam viewport não positivo e projeção não finita.

Essa base resolveu os três cenários de View All acima. O patch foi executado somente na cópia temporária e depois revertido. Ele é um experimento, não uma promessa de cobertura completa: matriz singular, âncora no plano da câmera, profundidade negativa, fonte patológica, overflow intermediário e precisão extrema exigem contratos próprios.

## O que poderá ser retirado do FreeCAD

O alvo direto é `src/Gui/SoLabelNodes.cpp`, helper `computeText2BBoxWithCoinWorkaround`, aproximadamente linhas 72 a 95. Ele chama o cálculo herdado, testa `!box.hasVolume()`, amplia o view volume com `scaleWidth(1.02f)`, altera `SoViewVolumeElement` e calcula novamente. O comentário cita a issue 417.

Após o FreeCAD exigir uma versão do Coin com a correção validada, a retirada deve abranger:

1. O helper completo, incluindo ampliação de 2%, segunda avaliação e alteração do estado da câmera.
2. O corpo especial de `SoColorBarLabel::computeBBox` que chama esse helper. Usar o cálculo herdado. Se necessário preservar o símbolo exportado durante uma transição de ABI, manter temporariamente um override que apenas delega.
3. A declaração desse override em `src/Gui/SoLabelNodes.h`, quando a política de compatibilidade permitir removê-la.
4. O include de `SoViewVolumeElement` nesse arquivo, se ficar sem usos após a retirada. Atualmente suas ocorrências estão concentradas no workaround.
5. O comentário que descreve a compensação, substituído no histórico/PR por referência à correção e à versão mínima efetiva do Coin.

Não retirar a classe `SoColorBarLabel`, seu registro, seus campos herdados ou consumidores. A necessidade de uma classe identificável é uma decisão distinta da necessidade desse override.

### Evidência específica para a retirada

Foi reproduzido o mesmo algoritmo do workaround em um pequeno subtipo de SoText2, sem dependência do build inteiro do FreeCAD. Câmera ortográfica em 0,0,5, viewport 640 por 480 e texto Label em x igual a 1.34, ligeiramente além da borda direita:

| Variante | Texto padrão | Texto com workaround |
| --- | --- | --- |
| Master | Caixa vazia | Caixa não vazia |
| Base proposta | Caixa não vazia | Caixa não vazia, mas com limites alterados |

Na base proposta, os limites x do texto padrão são 1.337500 a 1.587500; com workaround, 1.339515 a 1.599615. O teste usa o algoritmo copiado, não a classe FreeCAD compilada.

O motivo é importante: uma caixa plana pode ser válida e ter volume zero. `hasVolume()` não equivale a `isEmpty()`. Assim, mesmo com os limites corrigidos, o workaround continua executando e alterando o estado. Trocar apenas esse predicado não substitui a retirada completa quando a versão mínima do Coin permitir.

Se o FreeCAD ainda suportar Coin antigo, preservar a compensação apenas para versões comprovadamente anteriores à correção. Não usar simplesmente Coin 4 como condição: a master reproduzida já é dessa família. A versão ou capacidade efetiva será determinada pela entrega upstream.

## Comportamentos do FreeCAD que devem permanecer

- A exclusão intencional de bounding box por `SoSkipBoundingGroup::EXCLUDE_BBOX`, encontrada nos rótulos de `SoAxisCrossKit`.
- Câmeras e estados próprios dos overlays do viewer.
- `renderCaching=OFF` nos textos dinâmicos do Sketcher, até haver benchmark separado que justifique alteração.
- Limites próprios de `SoFCBoundingBox`, calculados por `minBounds` e `maxBounds`.
- O caminho de `SoStringLabel` com textura e geometria próprias em espaço de tela.
- Anotações World com SoAsciiText e os caminhos de SoText3, que não foram cobertos pela reprodução.

Esses mecanismos não são automaticamente contornos da issue 417. Também não remover font caches, infraestrutura de textura ou conversões do Sketcher como consequência deste conserto.

## Validação necessária para entrega

- Bounding box e View All com perspectiva e ortográfica; LEFT, RIGHT, CENTER; múltiplas linhas, fontes e tamanhos normais.
- Âncora dentro, nas bordas, longe da visão, atrás e no plano da câmera; near/far, viewport degenerado e transformações.
- Confirmar que desenho e picking de texto fora da visão continuam obedecendo aos contratos.
- Verificar atualização de limites após mover câmera, alterar viewport, fonte ou string; avaliar invalidação de cache sem redesenho desnecessário de geometria estática.
- Separar do escopo de projeção as verificações de tamanho, pitch, dimensões e buffer RGBA apontadas em SoFonts, reconfirmando seus achados na revisão atual.
- No FreeCAD, comparar SoColorBarLabel com e sem o workaround e validar legendas, anotações Screen, rótulos de datum, Sketcher e overlays, preservando exclusões intencionais de Fit All.
- Registrar versão mínima do Coin antes de retirar compatibilidade com bibliotecas antigas.

## Artefatos e próximo passo

Fontes, resultados, imagens e dois patches experimentais estão em `/mnt/Laranja/Git/externos/coin/testsuite/reproducers/sotext2-issue417-study/`. `STUDY.txt` mantém o inventário detalhado do FreeCAD e comandos de reprodução; `probe.cpp` contém o grafo e controles; `edge-probe.cpp` reproduz a compensação de 2%.

`diagnostic-only.patch` remove o filtro globalmente e demonstra sua insuficiência. `base-design-only.patch` separa o filtro por consumidor e evita o short na posição projetada. Ambos são experimentais. A cópia temporária do Coin foi restaurada à master. A correção foi depois aplicada ao Coin em um worktree separado; o código do FreeCAD não foi modificado.

A validação visual do exemplo no FreeCAD foi concluída na última seção. A remoção do workaround, e as verificações de SoColorBarLabel nativo, datum e Sketcher, continuam como trabalhos específicos adicionais antes de alterar o FreeCAD.


## Branch e testes no FreeCAD — 5 de outubro de 2026

Branch `codex/fix/issue417-sotext2`, baseada em `674e74267df863dbaf50416c477bc7f918a826d8`, no worktree `/home/dikluwe/.codex/worktrees/issue417-sotext2/coin`.

A correção separa a obtenção dos limites públicos do filtro de visibilidade usado pelo desenho e pela seleção. Também mantém a posição projetada em ponto flutuante, com truncamento ao pixel, evitando a conversão para short. Recusa viewports sem dimensão positiva e posições projetadas não finitas.

`CoinSoText2BoundsTest` cobre 38 casos: câmeras perspectiva e ortográfica, os três alinhamentos, seis posições de texto, repetição de limites e View All; dois controles adicionais de seleção preservam o acerto visível e a rejeição fora da tela. Resultado: 0 falhas na correção, 36 falhas na master. A suíte CTest teve 78 testes aprovados e um GLX ignorado sem display; esse GLX foi executado separadamente sob Xvfb e aprovado.

Foi iniciado o executável real `/mnt/Laranja/Git/externos/freecad-build/bin/FreeCAD`, versão 26.3.0, revisão compilada `d8d85f05ffad8277db968d6872c4a7a9b733c1b3`. Essa revisão difere do checkout de fontes inventariado acima. Duas execuções usaram LD_PRELOAD para comparar Coin master e corrigida. `/proc/self/maps`, preservado nos JSONs, confirma qual biblioteca foi carregada. A Coin instalada no FreeCAD não foi substituída.

| Verificação dentro do processo FreeCAD | Coin master | Coin corrigida |
| --- | --- | --- |
| Grafo original: limites antes/depois de View All | Vazios; câmera permanece (0,0,1) | Não vazios; câmera enquadra a posição do texto |
| Grafo com texto em x=100 | Vazios; câmera permanece (0,0,1) | Não vazios; câmera x=100.1102 |
| Controle com câmera manual | Limites não vazios | Limites não vazios |
| App::Annotation, DisplayMode Screen, Fit All | Texto visível na captura | Texto visível na captura |
| Part::Box junto com anotação | Cubo e anotação visíveis; volume 1000 | Cubo e anotação visíveis; volume 1000 |

As capturas dos grafos Coin adicionados manualmente ao viewer ficaram sem texto em ambas as variantes, inclusive no controle com câmera manual. Portanto essas capturas não são prova de correção visual nem de regressão: a inserção/renderização desse grafo na macro ainda precisa ser investigada. A evidência de desenho do grafo mínimo permanece a reprodução independente por SoOffscreenRenderer descrita acima. Os limites e as câmeras foram medidos por ações Coin dentro do processo FreeCAD; as capturas de Annotation e Part foram inspecionadas.

Houve erros preexistentes de carregamento PySide6/Qt em workbenches auxiliares (BIM, Draft, Help, Measure e Tux). As operações de Annotation e Part usadas aqui concluíram. Isso limita a abrangência da integração: não foram validados Sketcher, datum, overlays nem SoColorBarLabel compilado com o workaround removido. O estudo anterior do workaround continua sendo uma reprodução do algoritmo copiado.

Macro, JSONs, logs, capturas e resultados CTest estão em `freecad-validation/`. A retirada de código do FreeCAD descrita acima permanece condicionada à versão mínima efetivamente corrigida e à validação de suas classes nativas.


## Auditoria dos 36 casos e imagens

Na auditoria, o teste foi corrigido para percorrer SoText2::LEFT (1), RIGHT (2) e CENTER (3). A versão inicial percorria 0, 1 e 2, incluindo um valor inválido e omitindo CENTER. Após a correção, o resultado foi novamente 36 falhas na master e zero na correção, em 38 casos. Os logs de freecad-validation/master-native-tests.txt e fixed-native-tests.txt foram atualizados.

As 36 falhas correspondem a 2 câmeras × 3 alinhamentos × 6 âncoras: (0.25,0,0.25), (5,0,0), (100,0,0), (-100,0,0), (0,100,0) e (0,-100,0). Não representam 36 bugs independentes. Cada caso exige limites não vazios, centro finito próximo da âncora, estabilidade ao repetir a ação e limites não vazios após View All. Os dois controles de picking passam nas duas versões.

As imagens audit-before/ e audit-after/ foram geradas novamente pelo mesmo probe.cpp, respectivamente com a biblioteca master e a biblioteca compilada da branch, por SoOffscreenRenderer em 640×480. A conversão PPM→PNG apenas orienta as linhas do buffer para exibição. Na cena original, antes: 0 pixels não pretos; depois: 683. O controle com câmera manual produz 683 pixels nas duas versões. São renderizações isoladas da Coin; não são capturas do viewer FreeCAD.


## Resultado final: correção da macro e validação visual FreeCAD

A pendência visual registrada nas seções anteriores foi resolvida. A macro adicionava o texto diretamente ao grafo geral retornado por getSceneGraph(), mas o caminho saveImage()/savePicture() constrói o grafo de captura a partir de pcViewProviderRoot, câmeras, luzes e decoração. O subgrafo manual não estava no conjunto de objetos do documento capturado. Isso explicava também o controle manual vazio. Não foi necessário alterar novamente a implementação Coin para resolver essa falha da macro.

A macro agora cria App::FeaturePython com um ViewProvider Python que registra o SoSeparator do texto usando addDisplayMode. Assim, o exemplo é parte de um objeto real do documento FreeCAD e entra no caminho de captura. A câmera, o texto, as posições e as bibliotecas comparadas permanecem os mesmos. As execuções finais estão em freecad-validation/freecad-master-vp/ e freecad-fixed-vp/; as capturas antigas permanecem somente como registro histórico da macro incorreta.

| Captura real do FreeCAD | Master | Correção |
| --- | --- | --- |
| Exemplo original após View All | Sem texto | Hello World! visível |
| Texto distante x=100 após View All | Sem texto | Hello World! visível |
| Controle com câmera manual | Hello World! visível | Hello World! visível |
| Anotação Screen e Part::Box | Visíveis | Visíveis |

O verificador verify-freecad.py exige: macro concluída; apenas a biblioteca Coin esperada carregada; limites antes/depois e movimento da câmera coerentes; imagens 640×480; texto visível nos cenários esperados; controle manual visível nas duas variantes; modo Screen; volume 1000 do cubo; capturas da anotação e do cubo não vazias. A região do teste de texto é y>=150, excluindo o cubo de navegação; pixels claros RGB>180: 0 na master nos dois casos problemáticos, 434 na correção e no controle manual de ambas. Resultado: 44 verificações, zero falhas.

A suíte CTest foi repetida sob Xvfb: 79 testes aprovados, nenhum ignorado. O teste específico, agora com os três alinhamentos válidos, continua com 38 casos e zero falhas na biblioteca corrigida. As 36 falhas da master são o controle negativo esperado e mostram por que a correção é necessária; não são falhas remanescentes na branch. Logs finais: branch-ctest-final.txt, freecad-viewprovider-verification.json e fixed-native-tests.txt.

Este resultado valida a issue 417 nos cenários executados e resolve a antiga pendência de captura. Não equivale a uma validação completa de todos os usos SoText2 do FreeCAD. SoColorBarLabel nativo com o workaround removido, datum, Sketcher e overlays ainda não foram exercitados especificamente; o código FreeCAD e sua Coin instalada não foram alterados. Os erros PySide6/Qt em workbenches auxiliares continuam registrados nos logs. A retirada do workaround permanece um trabalho condicionado à versão mínima suportada.


## Limites adicionais no mesmo caminho — análise, sem alteração do FreeCAD

O FreeCAD é apenas ferramenta de análise. Não há proposta de modificar seus fontes nesta etapa. Os ViewProviders Python usados nos testes são objetos temporários criados pela macro, não mudanças no programa. A inspeção adicional abaixo não alterou o código de produção da Coin; somente acrescentou fontes de sondagem e evidências ao estudo.

Base: branch com correção da #417, após a6ace5e3a8. Comparação negativa: master 674e74267df863dbaf50416c477bc7f918a826d8. Probes em additional-limits/; C++11, bibliotecas já compiladas, sem renderizar buffers gigantes. Os resultados são sondagens diagnósticas, não novos testes aprovados da suíte.

### 1. Limites e posições dos glifos ainda usam short — reproduzido

SoText2P mantém SbBox2s bbox e SbList<SbVec2s> positions. buildGlyphCache converte posições acumuladas em int para short; a extensão final também converte maxwidth+maxoverhang para short. A correção da #417 eliminou somente a conversão da âncora projetada, não essas outras reduções.

Com Times-Roman tamanho 12, câmera perspectiva (0,0,5), viewport 640×480 e uma linha de letras M: 1000 caracteres produzem largura 94.92395 unidades; 2000, 189.84790; 3000, 565.44470 e centro x=-0.03882; 4000 e 6000 repetem a mesma largura incorreta. O esperado é crescimento aproximadamente linear, com o texto à direita da origem. O limite depende da largura em pixels, não de um número fixo de caracteres.

Mil linhas produzem altura 103.51888; duas mil, 207.07227; três mil, 565.47052 e centro y=0. O mesmo defeito ocorre com três linhas e spacing=10000, com a caixa aparecendo acima da origem apesar de as linhas avançarem para baixo. Os mesmos resultados de largura/altura aparecem na master: são defeitos preexistentes.

Direção de correção: coordenadas internas de pelo menos 32 bits, validação das somas, e tratamento explícito de caixas grandes antes de desenho e seleção. Mudar só SbBox2s é insuficiente: posições, tamanhos, índices e buffer precisam permanecer coerentes.

### 2. Buffer RGBA faz produtos em int — confirmado por inspeção, sem crash reproduzido

GLRender calcula numpixels=bbsize[0]*bbsize[1], aloca numpixels*4, zera esse produto e calcula offsets também em int. Por exemplo, dimensões positivas 32767×32767 têm 1073676289 pixels, mas 4294705156 bytes RGBA, acima de INT_MAX. Não foi forçada uma alocação dessa magnitude. A possibilidade aritmética e a ausência de checagens são observáveis no código; não foi demonstrada corrupção de memória nesta análise.

Direção de correção: dimensões validadas, produtos em size_t com checagem, limites explícitos de alocação e offsets coerentes. Deve acompanhar qualquer ampliação das caixas internas, para não liberar tamanhos maiores em um buffer sem proteção.

### 3. Seleção mistura bytes UTF-8 com posições por glifo — reproduzido

buildGlyphCache percorre codepoints UTF-8 e adiciona uma posição por glifo. rayPick percorre string.getLength(), que nos exemplos devolve o número de bytes, e usa esse índice para acessar positions. Para um texto de um único é, há uma posição e dois bytes; para 世, uma posição e três bytes. Assim, o laço pode acessar posições além do número lógico de elementos e devolver detalhes incoerentes.

No probe, é devolve characterIndex=1 e 世 devolve 2, embora cada string contenha só um caractere. éA e Aé devolvem índice 2 em regiões que pertencem ao segundo glifo. Master e correção têm o mesmo comportamento. Houve também pontos sem acerto dentro da caixa de textos ASCII; sem um oráculo do contorno/avanço do glifo, esses pontos não foram classificados aqui como um defeito adicional confirmado.

Direção de correção: usar contagem e índices coerentes com os glifos; definir o contrato de characterIndex em UTF-8; nunca acessar positions além de seu tamanho. Acrescentar casos ASCII e multibyte com oráculos de detalhe da seleção, além de verificar apenas a existência do hit.

### 4. Âncora exatamente no plano da câmera — limite reproduzido

Câmera perspectiva em z=5: texto ancorado exatamente em z=5 tem caixa vazia, inclusive após a correção. Em z=4.9999 e z=5.0001 a caixa é não vazia, porém de largura aproximadamente 0.00002281; no plano exato, a projeção é singular. As verificações de finitude evitam continuar com valores inválidos, mas não criam um fallback que permita View All encontrar essa âncora.

Isso exige uma decisão de contrato: devolver uma caixa mínima/âncora para enquadramento, usar uma escala de referência ou documentar a projeção singular como não suportada. Não basta retirar o clipping. O caso do plano exato já falhava na master; a nova correção passa a devolver limites também para âncoras finitas atrás da câmera.

### 5. Precisão em coordenadas muito grandes — reproduzido

Em x=1000 a largura do texto de 12 letras M é 1.13910; em x=1000000, 1.125; em x=1000000000, zero, com centro x=999999936, diferença de 64 em relação à âncora. A conversão para short da âncora foi removida, mas o armazenamento final e operações intermediárias continuam usando float. Nessa escala, a largura do texto é menor que a resolução numérica das coordenadas. A master também falha nesse extremo e ainda desloca a caixa para perto da origem.

Direção de análise: intermediários de maior precisão e cálculo relativo à câmera podem reduzir perda, mas SbBox3f/SbVec3f ainda impõem limites à representação final. Não é uma correção isolada de casting.

### 6. Viewport e métricas do backend — limites de API e riscos inspecionados

SbViewportRegion recebe width/height como short. Passar 40000 já perde representabilidade antes de SoText2; a branch recusa a dimensão não positiva resultante. Isso é uma fronteira da API, não prova de regressão para um viewport válido. Viewport com largura zero também produz caixa vazia, como previsto pelo novo guard.

cc_glyph2d mantém larguras/alturas em unsigned short e offsets em short. A fronteira FreeType ainda copia rows*pitch sem validações completas e mistura pitch assinado com representação unsigned. Os trechos apontados em SoFonts permanecem presentes nesta revisão, mas pitch negativo, dimensões extremas e falhas de alocação não foram novamente reproduzidos com backend simulado. São pendências de investigação/validação, não falhas observadas do teste de integração FreeCAD.

Prioridade para ampliar o escopo: primeiro limites internos de glifos/caixas junto com proteção do buffer; seleção UTF-8 com teste independente; e uma decisão explícita para a âncora no plano da câmera. O limite de precisão em coordenadas extremas e a largura do viewport envolvem contratos mais amplos da biblioteca. Os 38 casos originais continuam cobrindo a correção delimitada da #417, não esses novos extremos.
