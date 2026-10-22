# P03/P15/P16/P24/P28 — contratos além do primeiro perfil

Este documento define os planos de shaders próprios, texturas espaciais e MSAA/multipass. A execução é delimitada pela [matriz de recursos](coin-render-feature-matrix.md): R23.1/R23.2 implementam volumes POT/NPOT e R23.3 amplia o formato para sRGB em CPU, BGFX e wgpu; R24.3/R24.4 completam o perfil armazenado de cubemap linear/sRGB; R27 possui perfis MSAA. Shaders autorais, cubos e multipass possuem implementação; seus perfis de execução e pendências estão delimitados na matriz. Uma qualificação de perfil não cobre automaticamente os recursos e cruzamentos restantes.

## Shaders próprios: primeiro contrato portátil

A entrada é um módulo tipado do produto, versionado e imutável, com assinatura
verificada no Core. GLSL legado (`SoShaderProgram`, ARB/Cg, built-ins `gl_*`), WGSL
ou binário BGFX não são essa entrada. Wiring identifica o programa Coin ativo e
seus parâmetros; a aplicação associa a adaptação explicitamente por
`CoinRenderAction::setPortableShaderModule`. Sem adaptação, rejeita antes da publicação. Tradução automática
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

A assinatura canônica completa, incluindo nomes, tem limite inclusivo de
65.536 bytes. A função gerada tem limite de 262.144 bytes, excluindo o NUL
do transporte privado. Comprimentos são verificados antes de ler/copiar a
assinatura ou alocar a fonte anunciada pelo callback. Uma associação inválida
preserva a associação anterior; saídas auxiliares permanecem intactas na falha.

O transporte lógico de módulos tem orçamento inclusivo de 32 MiB por frame e
por grafo raiz/produtores. Cada instância armazenada custa `64 + 40*N + 4*P + S`
bytes: descritor, N nós IR, P floats de parâmetros e S bytes da assinatura.
Vários draws no mesmo slot contam uma vez; slots distintos e faces dos
produtores contam separadamente. O Core valida a soma completa antes de
compilar, executar produtores ou publicar. O ingresso privado Rust também
admite a tabela inteira antes de chamar o Core para o primeiro módulo. Não é
uma estimativa da memória do driver nem substitui o cache de programas e os
orçamentos próprios de texturas, attachments e multipass. A entrega R22 preservou a ABI65; R23.3 introduziu a revisão 66 e a corrente é 67 em R24.3.

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

O protocolo privado wgpu 60 original de R23.2 conserva os tamanhos das estruturas e codifica, no formato 11, W−1 nos bits 0–7, H−1 em 8–15, D−1 em 16–23 e presença de mips no bit 24; bits 25–31 são zero. Comprimento, dimensões, padding e coordenadas R/Q são validados antes do cache e da publicação, inclusive para linhas/pontos. Nenhuma extensão inválida é reduzida silenciosamente. A matriz R23.2 delimita os perfis aprovados; B63 sincroniza a primeira captura de janela BGFX. Não há exigência de pixels idênticos entre máquinas.

R23.3 estende `CoinRenderOptions::storedTextureColorSpace` às imagens autorais
`SoTexture3`. O atlas admite RGBA8 linear ou sRGB, preservando os bytes originais
do nível base. Cada voxel RGB sRGB é decodificado antes da interpolação espacial
e entre níveis; alpha permanece linear. Os mips RGB integram em espaço linear
as mesmas áreas completas, incluindo bordas NPOT ímpares, e codificam uma vez
por nível. O alpha conserva a média inteira e o arredondamento existentes;
o caminho linear mantém seus bytes. A cor de saída permanece RGBA8 linear.

A ABI privada 66 introduzida em R23.3, preservada na revisão 67, mantém o formato 11 e os tamanhos das estruturas, com sRGB no
bit 25 dos metadados do volume; bits 26–30 permanecem reservados e zero. O bit
31 conserva a declaração geral de opacidade, sem modificar o alpha autoral. Dimensões,
padding, comprimento e flags são validados antes do reuso por revisão. O formato
participa da identidade/cache; a revisão anterior 65 é recusada explicitamente.
O símbolo anterior de `packVolumeAtlas` conserva a semântica linear; a sobrecarga
com formato admite somente RGBA8 linear/sRGB e preserva a saída na falha.

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
Core. Os perfis armazenados de volumes e cubos admitem RGBA8 linear/sRGB. Compressão, depth e float/HDR espaciais exigem contrato próprio,
assim como anisotropia maior que 1; as solicitações sem suporte são recusadas. Limites iniciais de admissão: volume 256 por eixo,
cubo 2048 por face, 128 MiB somando subrecursos/mips por imagem. Os limites reais
do executor podem ser menores e devem ser verificados antes de alocar.

Wiring captura `SoTexture3` e suas coordenadas 3D; nunca reduz volume a uma
fatia 2D silenciosa. Coordenadas s/t/r/q mantêm interpolação perspectiva. No perfil PROJECTIVE, STR é
dividido por q antes de amostrar; zero/não finito segue a rejeição comum P07.
Wrap S/T/R explícito, qualidade e mip selection têm o mesmo dono de P07.
O perfil R23.2 admite os eixos inteiros 1..256 e recusa geração incompleta;
filtragem entre fatias usa interpolação nas três dimensões. Upload/flip deve
preservar os eixos Coin, demonstrados por um volume com oito cantos diferentes.

O núcleo R24.1 (`CoinRenderCubeTextureCore`) prepara seis faces POT 1..2048 em RGBA8 linear/sRGB, com cadeia de mips isolada por face. O overload anterior conserva o caminho linear; o overload tipado e o membro final de formato do atlas selecionam sRGB. Bytes autorais do nível base são preservados. Cada fetch decodifica RGB antes de interpolar; cada mip integra RGB em luz linear e codifica uma vez por nível. Alpha permanece linear e conserva a média inteira com empate para cima. O atlas usa largura 2N e sequência linear de níveis/faces/linhas; a altura é o teto do total de texels dividido pela largura e o padding final é zero. No máximo 2048, a cadeia completa ocupa 4096×8192×4=134.217.728 bytes, incluindo padding. Projeção e amostragem CPU validam os descritores antes de ler e preservam a saída em caso de erro. O desempate portátil de direção prioriza X/Y/Z; a filtragem inicial limita cada face às próprias bordas. A integração pública R24.2 liga esse núcleo à captura, ao plano, à CPU e aos executores BGFX/wgpu; a matriz delimita a qualificação e os cruzamentos ainda abertos. Seamless permanece ausente.

R24.2 captura as seis imagens de `SoTextureCubeMap`, na ordem +X, -X, +Y, -Y, +Z, -Z. As faces têm mesmo tamanho e componentes originais L/LA/RGB/RGBA; o atlas físico é RGBA8 linear/sRGB segundo `storedTextureColorSpace`. MODULATE, REPLACE, DECAL e BLEND conservam os componentes originais; REPLACE RGB preserva alpha primário e DECAL L/LA é recusado. O sampler limita cada face à borda, como o caminho CoinGL atual; não aplica wrap entre faces. Anisotropia maior que 1, compressão, HDR/depth e faces NPOT ficam fora deste perfil e são recusados. Seamless e SAMPLECUBE autoral não são implementados; outros cruzamentos/plataformas exigem qualificação própria.

A direção STR recebe a matriz de textura 4×4 uma única vez e mantém interpolação perspectiva antes da seleção da face; Q finito zero/negativo não divide STR. Não há normalização por vértice. Direção zero/não finita, inclusive singularidade dentro da primitiva, falha antes da publicação. O perfil qualifica `SoTextureCoordinate3`, direções variáveis de comprimentos diferentes e perspectiva com W distintos. Também qualifica o default geométrico bbox XY do Coin (no empate quadrado, S=Y/T=X), com matriz que mantém STR não nulo, e os callbacks CPU Object, Plane, ReflectionMap e Environment nas entradas registradas. Não há Reflection implícito. Default que alcance direção zero é recusado. O NormalMap geométrico deste Coin entrega somente um callback dummy zero e depende de hardware: é recusado mesmo se uma matriz deslocar esse dummy; o avaliador raster existente é preservado. TEXGEN sem callback CPU recusa quando a textura está ativa; qualidade zero conserva a desativação anterior. Face ausente/mista, filename pendente e bytes/metadados inválidos são recusados; Separator, unidade, revisão e reuso preservam o estado. Validação precede cache de mesma revisão e publicação nos ingressos Target/CPU/BGFX/C++/Rust. Formato, metadados e bytes do cubo participam do reuso BGFX, inclusive CAMERA_PATCH; metadados do formato 12 participam de TextureKey no Rust. Mesma revisão/digest/payload não encobre uma troca válida linear/sRGB/linear.

A orientação segue a tabela 3.19 do OpenGL 2.1 e passou nos controles interiores assimétricos CoinGL AMD/NVIDIA. O CoinGL atual fixa MIN/MAG em LINEAR sem gerar mips em `SoGLCubeMapImage`; CoinRender amplia esse caminho com quatro bandas de qualidade e mips por face, verificadas por oráculo numérico. O LOD usa derivadas da projeção na face escolhida; bilinear e interpolação de níveis são manuais. A verificação de cobertura MSAA k/N vale para uma face resolvida observada sem nova mistura de texels no consumidor. Quando produtor e consumidor resolvem N amostras e o consumidor combina texels diferentes, podem surgir frações k/N²; esse resultado não exige correção de pixels por fabricante. Os testes devem isolar cada resolução ou calcular a composição completa. BGFX usa endereço inteiro e `texelFetch`, wgpu usa endereço u32, sem converter a direção em imagem CPU para executar na GPU. O protocolo wgpu privado corrente 67 usa formato 12 com metadados de tamanho/componentes/mips e bit 9 sRGB, sem alterar o FrameView de 544 bytes. A revisão 66 é recusada. Empates de eixo e filtragem nas costuras não recebem alegação de seamless; a divergência nativa AMD/NVIDIA para Q está registrada em B29.

RTT de cubo é um produtor lógico com seis passes e câmeras descritas no Core,
com dependências anteriores, limite de oito níveis e ciclos rejeitados. Só
publica o cubo quando as seis faces estão completas na mesma geração. Um erro
na sexta face mantém o cubo/quadro/tickets anteriores; recursos candidatos são
aposentados por fence. Cache inclui câmera por face, cena, formato, dimensões,
política de transparência e revisões. Não confundir seis passes com seis
produtores independentes publicáveis. Janela não é produtor de face: recebe
um consumidor do grafo, com ownership do dispositivo da janela.

R25 introduziu o formato 12 na ABI privada wgpu 61; a revisão corrente 67 conserva os tamanhos das estruturas.
No formato 12, os bits 0–3 indicam o expoente do tamanho da face, 4–6 os
componentes de origem, 7 a cadeia de mips, 8 um recurso GPU e 9 sRGB armazenado.
Bits 10–30 são reservados zero; bit 31 é a declaração geral de opacidade, sem alterar alpha. Cubos RTT permanecem RGBA8 linear mesmo com a política de imagens autorais em sRGB: bit 9 deve ser zero para o produtor/token GPU. Não se decodificam novamente os bytes lineares de um resultado renderizado. O cubo RTT GPU usa quatro componentes, ponteiro nulo e comprimento
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

Revisão e digest são pistas de cache, não identidade do recurso: descritores, bytes, sampler e matriz continuam sujeitos à validação. A resolução confere novamente o layout retido e a declaração capturada; uma declaração de cubo sem mips pode consumir o atlas completo compartilhado, desde que ambos sejam canônicos. O executor recusa token repetido entre faces ou retornado como atlas antes de aposentar os temporários, mantendo a publicação anterior e a aposentadoria única pelo owner. Isso não cria uma API pública de exportação de cubemap.


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

O perfil CPU integral de RTT usa a configuração pública `COIN_RENDER_BACKEND=RECORDING`: a fábrica seleciona o rasterizador auxiliar tanto na raiz quanto nos produtores STAGED. Não há fallback automático de GPU para CPU. Substituir somente o consumidor por uma referência CPU em um build GPU não comprova esse perfil. O perfil STAGED RGBA8 linear 1× passou 33/33 testes públicos RECORDING sem display ou drivers GPU: seis faces, câmera, sampler, alpha, grafo, callbacks, compartilhamento, estéreo e limites 1/2/2048 com/sem mips. Após OOM na submissão raiz, com o atlas CPU completo já preparado, a publicação anterior permanece intacta e TARGET_ERROR bloqueia novas submissões. A recuperação usa resize público explícito nas mesmas dimensões: invalida a publicação/serial e permite renderizar novamente no mesmo alvo e backend, com RGBA/depth A/B/A exatos.

A falha nativa na sexta face também é qualificada: o hook interno, isolado por thread e compartilhado entre instâncias CPU, permite cinco submissões reais completas e falha na sexta antes de alocar ou publicar. A raiz mantém RGBA/depth/serial e permanece pronta; desarmar o hook permite publicar B imediatamente, seguido da restauração exata de A. Uma dependência já concluída não torna publicável o atlas parcial do produtor seguinte. A admissão percorre todas as faces antes de qualquer produtor; entradas inválidas e capacidades CPU recusadas, inclusive apenas na última face, não consomem a falha armada. DIRECT, MSAA 2×/4×, janela, async, sombras e bump mantêm as recusas anteriores desse conector, sem serem apresentados como recursos CPU implementados.

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

### Multipass da action — R28

`CoinRenderAction` e `CoinRenderSceneManager` oferecem `setNumPasses(int)` e
`getNumPasses()`. O padrão é 1; o valor pedido é preservado. No render/apply,
valores menores que 1 produzem `INVALID_SCENE`, maiores que 16 produzem
`UNSUPPORTED`, antes de executar produtores ou o consumidor. O adaptador do
manager encaminha a contagem nativa. `passUpdate` com mais de uma passagem é
recusado explicitamente: este perfil publica somente o resultado final.
Callbacks específicos por passagem e demais opções nativas continuam em R34.

A captura ocorre uma vez por atualização lógica, admitindo reuso exato do plano.
Cada amostra aplica o jitter nativo à projeção do viewport efetivo da ocorrência,
a partir da matriz original. Marker/Text2 são materializados depois desse
jitter e antes do snap/layout; SoImage mantém a âncora nativa não jitterada.
Produtores RTT e mapas VSM independentes da câmera consumidora executam uma vez.
BGFX/wgpu acumulam RGBA em float na GPU e publicam a média linear uma vez;
a profundidade é a da última amostra. O alvo conserva sua contagem MSAA 1/2/4.

`setMultipassBufferBudget(uint64_t)`/`getMultipassBufferBudget()` definem um
orçamento de 256 MiB por padrão. Zero admite uma passagem e recusa multipass
com `OUT_OF_MEMORY`. A admissão considera os recursos simultâneos da composição:
anexos de cena/MSAA, resolve, somas float, OIT/peeling, VSM, RTT/staging/mips,
saída/readback, tickets retidos e recursos pendentes na fila. Usa aritmética
checada e precede os produtores e o consumidor. Uma recusa preserva exatamente
RGBA, profundidade e serial anteriormente publicados; recuperação usa o mesmo
alvo, action ou manager. O orçamento não substitui limites próprios de texturas
residentes nem mede toda a memória do driver.

O transporte privado wgpu é ABI65, `FrameView` de 544 bytes. Acrescenta contagem,
reservado zero, orçamento e matrizes MVP por amostra/estado; a contagem das
matrizes é em floats. Uma passagem usa ponteiro nulo e contagem zero. Os bits
15..30 de `composition_flags` selecionam amostras (zero significa todas); o
bit 31 identifica raster já materializado. Bits inferiores conservam o contrato
anterior. Metadados inconsistentes são recusados antes de uploads/submissão.
Não se podem misturar headers/executáveis ABI63 com bibliotecas ABI65.
Os limites de qualificação permanecem na [matriz](coin-render-feature-matrix.md#r28--multipass-integrado--21102026).

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
