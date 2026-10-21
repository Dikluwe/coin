# P03/P15/P16/P24/P28 — contratos além do primeiro perfil

Este documento define os planos de shaders próprios, texturas espaciais e MSAA/multipass. A execução é delimitada pela [matriz de recursos](coin-render-feature-matrix.md): R23.1 implementa o primeiro perfil de volumes POT em CPU, BGFX e wgpu; R27 possui perfis MSAA. Shaders autorais, cubos e multipass geral continuam com execução pendente. Uma qualificação de perfil não cobre automaticamente os recursos e cruzamentos restantes.

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
na UI normal. Não executar código externo só por que um nó contém uma string.

Fechamento de implementação: mesmo módulo com cor constante e parametrizada,
UV projetivo, oito bindings, resize/mutação, clipping/fog/alpha/composição e RTT;
IR malformado, assinatura incompatível, limite, compilação, perda de dispositivo
sem publicação e recuperação. Comparar CPU/IR e BGFX/wgpu; CoinGL só é oráculo
quando a adaptação demonstra equivalência com o programa original.

## Texturas 3D, cube maps e RTT de cubo

O perfil R23.1 usa atlas RGBA8 2D contendo todas as fatias e todos os níveis espaciais, com metadados lógicos `volumeWidth/Height/Depth/Columns/SourceComponents` e `volumeMipmapped`. `width/height` descrevem a textura física; `mipmapped=false` impede mipmaps 2D do atlas. CPU e shaders fazem nearest/trilinear e a seleção/interpolação entre mips 3D, com wrap S/T/R e divisão por q no perfil PROJECTIVE após interpolação perspectiva. Um sampler por unidade preserva a coexistência de imagens 2D/3D sem aumentar o número de bindings; não há redução do volume a uma fatia.

O atlas usa colunas em potência de dois suficientes para as fatias e empilha os níveis verticalmente. No domínio POT 1..256, as 729 extensões possíveis têm máximo físico de 4.096×5.471×4=89.636.864 bytes, incluindo padding e mips; o teto de 128 MiB é por imagem publicada. Imagem da cena e temporários de conversão são memória CPU adicional. As dimensões físicas também respeitam o limite real de textura 2D do executor. Identidade, deduplicação, reuso e validação incluem os metadados e bytes; cache de atlas é restrito à captura e inclui revisão do nó. O teste público cobre o máximo 256³ com/sem mips; não usa esse passe como prova de formatos, NPOT ou combinações ainda ausentes.

As 14 execuções Linux de R23.1 passaram com CPU/GPU/CoinGL em sete células e duas políticas de sampling, incluindo filtros intermediários e footprint apenas em R. A transição CoinGL 0,6→0,8 no mesmo objeto já mipmapped mantém o filtro anterior; C14 conserva esse contrato em aberto. B14 registra a correção do upload nativo, que confundia dimensão da imagem com estado habilitado anterior ao trocar 3D→2D. Novas plataformas e cruzamentos seguem V/C17.

R23.2 amplia o mesmo atlas RGBA8 linear para todos os eixos inteiros de 1 a 256, preservando a extensão e os voxels autorais. Cada mip usa `max(1, floor(eixo/2))`; cada voxel novo integra a área completa das células do nível anterior nos três eixos e arredonda ao inteiro mais próximo, com empate para cima, uma única vez por nível. Linhas/fatias finais ímpares participam; volumes POT mantêm os bytes anteriores. Padding físico deve ser zero. O máximo físico acima também vale no domínio completo de 16.777.216 extensões.

O protocolo privado wgpu 60 conserva os tamanhos das estruturas e codifica, no formato 11, W−1 nos bits 0–7, H−1 em 8–15, D−1 em 16–23 e presença de mips no bit 24; bits 25–31 são zero. Comprimento, dimensões, padding e coordenadas R/Q são validados antes do cache e da publicação, inclusive para linhas/pontos. Nenhuma extensão inválida é reduzida silenciosamente. A matriz R23.2 delimita os perfis aprovados; B63 sincroniza a primeira captura de janela BGFX. Não há exigência de pixels idênticos entre máquinas.

C17.2 preserva coordenadas STRQ independentes em misturas de até oito unidades
2D/volume/cubo junto de sombras. O perfil shadowed DIRECT_ST usa STR transformado
sem divisão por Q; um volume PROJECTIVE sem sombras divide STR por Q, enquanto
o cubemap usa a direção STR e ignora Q finito, inclusive zero. Entradas não
finitas, direção zero/singular dentro da primitiva e Q projetivo inválido são
recusados antes da publicação. Os filtros/LODs são avaliados sobre os atributos
do triângulo efetivo; o fan preciso de QuadMesh conserva seus pesos legados,
que não são a média afim dos atributos. A matriz C17.2 delimita a qualificação
offscreen, janela e RTT; não encerra as demais combinações de C17.

A representação comum para extensões futuras terá `dimension` (2D/3D/cube), extensão xyz, formato, mip levels,
face/layer e bytes/pitches por subrecurso. A identidade inclui todos os descritores
e payloads; a conversão de L/LA/RGB/RGBA para RGBA8 acontece uma única vez no
Core. O primeiro perfil espacial será RGBA8 linear; compressão, depth, sRGB e
float/HDR aguardam P24. Limites iniciais de admissão: volume 256 por eixo,
cubo 2048 por face, 128 MiB somando subrecursos/mips por imagem. Os limites reais
do executor podem ser menores e devem ser verificados antes de alocar.

Wiring captura `SoTexture3` e suas coordenadas 3D; nunca reduz volume a uma
fatia 2D silenciosa. Coordenadas s/t/r/q mantêm interpolação perspectiva. No perfil PROJECTIVE, STR é
dividido por q antes de amostrar; zero/não finito segue a rejeição comum P07.
Wrap S/T/R explícito, qualidade e mip selection têm o mesmo dono de P07.
O perfil R23.2 admite os eixos inteiros 1..256 e recusa geração incompleta;
filtragem entre fatias usa interpolação nas três dimensões. Upload/flip deve
preservar os eixos Coin, demonstrados por um volume com oito cantos diferentes.

O núcleo R24.1 (`CoinRenderCubeTextureCore`) prepara seis faces POT 1..2048 em RGBA8 linear, com cadeia de mips isolada por face. O atlas usa largura 2N e sequência linear de níveis/faces/linhas; a altura é o teto do total de texels dividido pela largura e o padding final é zero. No máximo 2048, a cadeia completa ocupa 4096×8192×4=134.217.728 bytes, incluindo padding. Projeção e amostragem CPU validam os descritores antes de ler e preservam a saída em caso de erro. O desempate portátil de direção prioriza X/Y/Z; a filtragem inicial limita cada face às próprias bordas. A integração pública R24.2 liga esse núcleo à captura, ao plano, à CPU e aos executores BGFX/wgpu; a matriz delimita a qualificação e os cruzamentos ainda abertos. Seamless permanece ausente.

R24.2 captura as seis imagens de `SoTextureCubeMap`, na ordem +X, -X, +Y, -Y, +Z, -Z. As faces têm mesmo tamanho e componentes originais L/LA/RGB/RGBA; o atlas físico é RGBA8 linear. MODULATE, REPLACE, DECAL e BLEND conservam os componentes originais; REPLACE RGB preserva alpha primário e DECAL L/LA é recusado. O sampler limita cada face à borda, como o caminho CoinGL atual; não aplica wrap entre faces. Anisotropia, compressão, HDR e sRGB ficam fora deste perfil.

A direção STR recebe a matriz de textura 4×4 uma única vez e mantém interpolação perspectiva antes da seleção da face; Q finito zero/negativo não divide STR. Não há normalização por vértice. Direção zero/não finita, inclusive singularidade dentro da primitiva, falha antes da publicação. O perfil inicial usa `SoTextureCoordinate3` explícito; defaults geométricos e geradores adicionais exigem qualificação própria, sem Reflection implícito. Face ausente/mista, filename pendente e bytes/metadados inválidos são recusados; Separator, unidade, revisão e reuso preservam o estado. Validação precede cache de mesma revisão nos ingressos CPU/C++/Rust.

A orientação segue a tabela 3.19 do OpenGL 2.1 e passou nos controles interiores assimétricos CoinGL AMD/NVIDIA. O CoinGL atual fixa MIN/MAG em LINEAR sem gerar mips em `SoGLCubeMapImage`; CoinRender amplia esse caminho com quatro bandas de qualidade e mips por face, verificadas por oráculo numérico. O LOD usa derivadas da projeção na face escolhida; bilinear e interpolação de níveis são manuais. A verificação de cobertura MSAA k/N vale para uma face resolvida observada sem nova mistura de texels no consumidor. Quando produtor e consumidor resolvem N amostras e o consumidor combina texels diferentes, podem surgir frações k/N²; esse resultado não exige correção de pixels por fabricante. Os testes devem isolar cada resolução ou calcular a composição completa. BGFX usa endereço inteiro e `texelFetch`, wgpu usa endereço u32, sem converter a direção em imagem CPU para executar na GPU. O protocolo wgpu privado 61 na implementação R25 usa formato12 e metadados de tamanho/componentes/mips, sem alterar o tamanho do FrameView. Empates de eixo e filtragem nas costuras não recebem alegação de seamless; a divergência nativa AMD/NVIDIA para Q está registrada em B29.

RTT de cubo é um produtor lógico com seis passes e câmeras descritas no Core,
com dependências anteriores, limite de oito níveis e ciclos rejeitados. Só
publica o cubo quando as seis faces estão completas na mesma geração. Um erro
na sexta face mantém o cubo/quadro/tickets anteriores; recursos candidatos são
aposentados por fence. Cache inclui câmera por face, cena, formato, dimensões,
política de transparência e revisões. Não confundir seis passes com seis
produtores independentes publicáveis. Janela não é produtor de face: recebe
um consumidor do grafo, com ownership do dispositivo da janela.

Em R25, a ABI privada wgpu 61 conserva os layouts públicos existentes.
No formato 12, os bits 0–3 indicam o expoente do tamanho da face, 4–6 os
componentes de origem, 7 a cadeia de mips e 8 um recurso GPU; os demais bits
são inválidos. O cubo RTT GPU usa quatro componentes, ponteiro nulo e comprimento
CPU zero; o campo privado content_digest transporta o token do recurso.
Dimensões físicas do atlas, domínio, geração e cadeia são validados antes do
cache. O token do cubo montado possui seus próprios recursos e permanece válido
após aposentar os seis tokens de face. No grafo DIRECT, essa aposentadoria
ocorre após a montagem e retira somente os registros das faces. A liberação
física continua obedecendo ao fence de seu último uso; memória ainda pendente
na GPU permanece no envelope de orçamento. A capacidade é verificada pelo
pico de registros, não pela soma de todas as faces de todos os produtores:
com k produtores anteriores retidos, um cubo exige k+7 registros durante a
montagem e deixa k+1 ao concluir. O registro wgpu tem 64 posições, reduzidas
pelos tokens externos vivos; esse limite não é atribuído ao BGFX. Zero, geração antiga, alias de faces,
metadados inconsistentes e payload CPU em um recurso GPU são recusados sem
publicação. A matriz R25 discrimina a execução dos testes desse ingresso.


Para R25, o clear de cada face combina `backgroundColor` RGB com alpha 1: esse valor é explícito no `prerendercb` de `SoSceneTextureCubeMap` nativo. Não herdar alpha de outro tipo de RTT. A ordem lógica é +X, -X, +Y, -Y, +Z, -Z, independente dos índices do enum nativo; cada câmera deve preservar os parâmetros autorais, substituindo a orientação na captura sem alterar os campos da cena. O override herdado é respeitado apenas na unidade zero; o próprio `isOverride()` também estabelece override de imagem apenas na unidade zero, como o caminho ativo de `SoTexture2`. Unidades extras não estabelecem override global. Isso é uma diferença deliberada do `SoSceneTextureCubeMap` nativo, que não aplica o próprio override; os setters presentes em `SoTextureCubeMap::doAction` e `SoSceneTexture2::doAction` estão desativados e não servem como referência ativa. Esse contrato orienta a implementação; a qualificação permanece discriminada em R25 na matriz.

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

O perfil CPU integral de RTT usa a configuração pública `COIN_RENDER_BACKEND=RECORDING`: a fábrica seleciona o rasterizador auxiliar tanto na raiz quanto nos produtores STAGED. Não há fallback automático de GPU para CPU. Substituir somente o consumidor por uma referência CPU em um build GPU não comprova esse perfil. O perfil STAGED RGBA8 linear 1× passou 33/33 testes públicos RECORDING sem display ou drivers GPU: seis faces, câmera, sampler, alpha, grafo, callbacks, compartilhamento, estéreo e limites 1/2/2048 com/sem mips. Após OOM na submissão raiz, com o atlas CPU completo já preparado, a publicação anterior permanece intacta e TARGET_ERROR bloqueia novas submissões. A recuperação usa resize público explícito nas mesmas dimensões: invalida a publicação/serial e permite renderizar novamente no mesmo alvo e backend, com RGBA/depth A/B/A exatos. Esse controle não injeta falha nativa na sexta face CPU. DIRECT, MSAA 2×/4×, janela, async, sombras e bump mantêm as recusas anteriores desse conector, sem serem apresentados como recursos CPU implementados.

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
