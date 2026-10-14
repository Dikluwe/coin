# P03/P15/P16/P24/P28 — contratos além do primeiro perfil

Este documento fecha a definição dos planos de shaders próprios, texturas
espaciais e MSAA/multipass. Não certifica execução desses recursos. O perfil
publicado continua RGBA8 2D, uma amostra por pixel e programas internos do
CoinRender. `SoShaderProgram`, volume e cubo ativos continuam UNSUPPORTED.
Implementar estes planos requer mudanças no transporte privado e testes dos
dois executores; um bit de hardware não habilita conteúdo.

## Shaders próprios: primeiro contrato portátil

A entrada será um módulo tipado do produto, versionado e imutável, com assinatura
verificada no Core. GLSL legado (`SoShaderProgram`, ARB/Cg, built-ins `gl_*`), WGSL
ou binário BGFX não são essa entrada. Wiring identifica o programa Coin ativo e
seus parâmetros; uma integração futura deve fornecer uma adaptação explícita
para o módulo. Sem adaptação, rejeita antes da publicação. Tradução automática
arbitrária de fonte GL permanece fora do primeiro contrato.

O primeiro módulo altera apenas a cor de fragmento de triângulos. Posição,
clipping, interpolação perspectiva, fog, alpha test, depth, composição, seleção,
camadas e sombras conservam os donos atuais. Sua entrada é a cor primária após
iluminação e antes de fog/composição, mais coordenadas UV homogêneas capturadas
por Wiring. Sua saída é RGBA finito, sem pré-multiplicação; o Core decide
classificação transparente por metadado explícito conservador. O módulo não
escreve depth, não modifica a posição e não pode ocultar primitivas de picking.
Uma futura operação discard requer contrato separado para depth/alpha/seleção.

| Elemento | Primeiro limite e semântica |
| --- | --- |
| Expressões | AST acíclica com constantes f32, vetores 2/3/4, uniformes, add/sub/mul/div, min/max/clamp, dot e amostragem 2D; sem laços, recursão, compute ou efeitos laterais |
| Uniformes | 4 KiB por módulo; tipos e tamanhos exatos; campos ausentes, extras e NaN/Inf diagnosticados; divisão somente por escalar constante/uniforme validado não zero; padding é decisão do lowering |
| Complexidade | 256 expressões, profundidade 32, até oito amostragens e oito imagens 2D; recursos ligados por identidade lógica e geração |
| Coordenadas | Unidade explícita 0..7; divisão por q segundo P07; funções Coin resolvidas em Wiring/Core, sem chamada ao Coin na Infra |
| Sampler | Perfil P07 comum; qualidade não é reinterpretada pelo módulo |
| Identidade | Digest de versão, AST, assinatura e perfil; parâmetros e imagens têm revisões próprias; cache inclui dispositivo/geração/formato/amostras |
| Composição | Alpha do módulo declarado como potencialmente variável força scheduling conservador; programas opacos precisam prova estrutural, nunca inferência dos pixels de um quadro |
| Falha | Validação/compilação incompatível falha antes do submit dos produtores; erro de runtime posterior preserva a publicação anterior e permite recuperação |

Core valida AST/assinatura, ordena operações e produz um IR único. Infra emite
WGSL e as variantes de shader BGFX a partir desse IR, verifica limites reais e
retém programas até o fence. Não haverá dois intérpretes de semântica Coin.
Shell relata módulo, recurso/operação e fase da falha, sem expor fonte de shader
na UI normal. Não executar código externo só porque um nó contém uma string.

Fechamento de implementação: mesmo módulo com cor constante e parametrizada,
UV projetivo, oito bindings, resize/mutação, clipping/fog/alpha/composição e RTT;
IR malformado, assinatura incompatível, limite, compilação, perda de dispositivo
sem publicação e recuperação. Comparar CPU/IR e BGFX/wgpu; CoinGL só é oráculo
quando a adaptação demonstra equivalência com o programa original.

## Texturas 3D, cube maps e RTT de cubo

O plano comum terá `dimension` (2D/3D/cube), extensão xyz, formato, mip levels,
face/layer e bytes/pitches por subrecurso. A identidade inclui todos os descritores
e payloads; a conversão de L/LA/RGB/RGBA para RGBA8 acontece uma única vez no
Core. O primeiro perfil espacial será RGBA8 linear; compressão, depth, sRGB e
float/HDR aguardam P24. Limites iniciais de admissão: volume 256 por eixo,
cubo 2048 por face, 128 MiB somando subrecursos/mips por imagem. Os limites reais
do executor podem ser menores e devem ser verificados antes de alocar.

Wiring captura `SoTexture3` e suas coordenadas 3D; nunca reduz volume a uma
fatia 2D silenciosa. Coordenadas s/t/r/q mantêm interpolação perspectiva e são
divididas por q antes de amostrar; zero/não finito segue a rejeição comum P07.
Wrap S/T/R explícito, qualidade e mip selection têm o mesmo dono de P07.
O primeiro perfil exigirá potências de dois e reject de geração incompleta;
filtragem entre fatias usa interpolação nas três dimensões. Upload/flip deve
preservar os eixos Coin, demonstrados por um volume com oito cantos diferentes.

Cubo tem seis faces quadradas, mesmo tamanho, formato e cadeia de mips; ordem
lógica +X, -X, +Y, -Y, +Z, -Z. Coordenadas são uma direção, não UV 2D, e devem
ser resolvidas pelo contrato Coin de `SoTextureCubeMap`. Orientação de cada face
será congelada por referência GL com eixos/setas e amostras interiores e nas
costuras; não escolher flips pela convenção de uma API. Empates de eixo e
filtragem nas costuras exigem ensaio separado antes de declarar seamless.
Face faltante, tamanhos mistos, direção zero/não finita e mip incompleto falham.

RTT de cubo é um produtor lógico com seis passes e câmeras descritas no Core,
com dependências anteriores, limite de oito níveis e ciclos rejeitados. Só
publica o cubo quando as seis faces estão completas na mesma geração. Um erro
na sexta face mantém o cubo/quadro/tickets anteriores; recursos candidatos são
aposentados por fence. Cache inclui câmera por face, cena, formato, dimensões,
política de transparência e revisões. Não confundir seis passes com seis
produtores independentes publicáveis. Janela não é produtor de face: recebe
um consumidor do grafo, com ownership do dispositivo da janela.

A contabilidade deve incluir seis cores, depth temporário, cadeia de mips,
resolve e staging efetivamente simultâneos. O limite atual de 64 MiB do RTT
2D não será reutilizado como uma estimativa de memória total. Core contabiliza
bytes nominais com multiplicação/addição verificadas; Infra informa overhead e
capacidades adicionais. Readback expõe ordem de faces, pitches e origem por
subrecurso; readback RGBA 2D não representa um cubo.

Fechamento: oito cantos, seis faces assimétricas, wrap/filter/mips, mutação e
qualidade desligada/reativação, callbacks e referência GL; staged/direct,
transparência, resize, faces incompletas, orçamento/ciclo/geração, falha tardia
e recuperação em CPU/BGFX/wgpu. `SoTexture3` tem consumidor FreeCAD real no
inventário; presença do nó não basta para certificar esse ViewProvider.

## MSAA/resolve, cor e multipass — P24

Configuração futura tipada do target: formato de cor, espaço de cor e número
de amostras. Default conserva RGBA8 linear/uma amostra. Primeiro MSAA propõe
1/2/4; 8 e sample shading ficam fora do perfil inicial. Action produz passes
sem escolher contagens diferentes do target. Mudança de configuração/resize
avança a geração e invalida attachments/pipelines/tickets dependentes.
As duas Infra consultam capacidade por formato e número exato de amostras.
Não arredondar ou reduzir MSAA silenciosamente.

Resolve de cor ocorre uma vez após o último passe que escreve o attachment,
antes de amostrar RTT, readback ou present. Conteúdo linear é resolvido em
linear; conversão sRGB é explícita na fronteira de publicação. HDR exige formato
float e contrato de exposição/tone mapping antes de readback RGBA8. Rejeitar
HDR/MSAA até essa implementação em vez de clamp implícito.

Depth readback MSAA fica UNSUPPORTED no primeiro perfil: não há equivalência
universal com depth da amostra central nem com min/max. Sombras continuam
single-sample. Weighted OIT/peeling e alpha-to-coverage precisam planos próprios
e não serão habilitados automaticamente pelo número de amostras. Picking Coin
continua geométrico; MSAA não altera identidade de seleção.

Cada passe comum descreve attachment/subrecurso, load (clear/load), store,
viewport/scissor, depth e dependências de leitura/escrita. Proibir read/write
simultâneo do mesmo subrecurso; Core ordena o DAG e rejeita ciclos. Anotações,
foreground, sombras e RTT existentes devem baixar para esses descritores sem
mudar sua ordem. Multipass não significa repetir a travessia com efeitos de
callbacks a cada amostra. Captura uma vez; produtores executam conforme grafo.

Orçamento nominal: `w*h*(bytesColor*N + bytesDepth*N + bytesResolve)` para
attachments presentes, mais mips/OIT/peeling/staging/tickets simultâneos.
Aritmética conferida antes de criar recursos; informar por target e por grafo.
Resize durante trabalho pendente aposenta a geração antiga somente após fence;
resultado candidato de tamanho/geração antiga não pode ser publicado no alvo
novo. Suspensão zero não aloca; restauração reconstrói antes de apresentar.

Fechamento: arestas com cobertura parcial vs controle uma amostra, cor/alpha e
resolve, leitura RTT depois do resolve, clear/load/store e ordem de passes,
janela/offscreen, resize/suspend, limites/pitches/overflow, falha de alocação ou
device loss com pixels/serial/tickets preservados e recuperação. Planos de
amostras/cor precisam chegar ao protocolo privado; ABI pública Coin preservada,
ABI experimental versionada se for necessário acrescentar campos.

## Host, subclasses e certificação

A integração deve fornecer callback que prepare conteúdo frio e atualize câmera,
viewport/DPR, material e revisões. Não chamar `GLRender` para preparar caches.
Uma subclasse não herda certificação visual do tipo nativo: callbacks próprios
permanecem observáveis. GL-only conhecido precisa callback/adaptação ou rejeição
explícita no host; C++ não oferece introspecção segura de todos os overrides.

P03/I01/I02 estão implementados nos tipos nativos Linux. I03/I04/I06 e os
consumidores de Sketcher/Draft/Fem/Measure/Mesh precisam casos próprios do
FreeCAD. Para cada caso registrar inicialização sem frame GL anterior,
conteúdo visível, resize/câmera/DPR, mutação/remoção, picking/seleção quando
pertinentes e backend efetivamente apresentado. Teste sintético no Coin e
exportação do grafo não substituem essa evidência. Windows fica em célula
separada: esta estação Linux não pode certificar código Windows novo.
