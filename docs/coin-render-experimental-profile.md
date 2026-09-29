# Perfil WebGPU experimental do Coin 4

Esta implementação é opt-in (`COIN_BUILD_RENDER=ON`) e fica em
`CoinRender`, separada de `libCoin`. A ação e o alvo de renderização
estão em `experimental/include`. Desde a Onda 6, esses headers e a biblioteca
podem ser instalados com `COIN_INSTALL_RENDER_EXPERIMENTAL=ON`, mas continuam
experimentais, fora da API e ABI públicas estáveis de `libCoin` no Coin 4.
A ponte C++/Rust é privada e versionada
(`COIN_WGPU_BRIDGE_PROTOCOL_REVISION=26` nesta revisão). Não promova esses
headers a consumidores externos como se fossem estáveis.

O guia de produto, a matriz de capacidades, o manager, os exemplos instaláveis,
o benchmark e a política de evolução para o Coin 5 estão em
[`wgpu-wave6-product.md`](coin-render-wave6-product.md).

## Cache privado e perfil por fase

`apply(SoNode *)` reutiliza um único `CoinRenderFramePlan` por action quando o ponteiro
da raiz e `SoNode::getNodeId()` continuam iguais. Notificações de descendentes,
mudança de viewport, fundo ou fast path invalidam o plano. O cache é
deliberadamente desligado na rota RTT GPU→GPU direta, cujos tokens pertencem
a um único apply. O log textual de Recording é gerado de forma preguiçosa ao
consultar `getRecordingLog()`; o backend Recording sem target continua com o
mesmo conteúdo determinístico.

Uma revisão de CoinRenderFramePlan, privada e monotônica no processo, permite evitar
validação estrutural duplicada no mesmo target e na ponte Rust. Revisão zero
significa plano externo/manual e força validação completa. Resize zera a
revisão validada. Esse campo motivou a revisão 16 da ponte privada; nenhum
header público estável ou símbolo de `libCoin` mudou.

Para diagnóstico, `COIN_RENDER_TRACE_PHASES=1` escreve tempos de traversal,
construção do CoinRenderFramePlan, empacotamento da ponte, validação Rust,
preparação/encode, submit, espera GPU e publicação de readback. A espera GPU
usa `device.poll(Maintain::Wait)` e inclui render mais cópias para staging;
não deve ser apresentada como timestamp de execução GPU isolada.

O mesmo trace agora emite `rust_cpu_detail`, repartindo o trabalho Rust em
criação de attachments, encode dos draws, snapshot de cena, preparação de
staging, submit, registro/espera do map, cópia de cor/profundidade,
publicação e reciclagem. O campo `total_ms` é a soma das fases CPU dessa
linha para um frame; as medianas de campos distintos não precisam somar.
`COIN_WGPU_GPU_TIMESTAMPS=1` junto ao trace solicita queries GPU somente
se o adapter suportar ambos os recursos de timestamp necessários; `rust_gpu`
informa `render_ms` e `copy_ms` ou `status=unsupported`. Queries, resolve e
readback da sonda afetam o frame medido e não são usados no caminho normal.

O experimento privado `COIN_WGPU_CAMERA_BINDINGS=1` reutiliza o buffer de
materiais, uniform buffers e bind groups apenas em patches de câmera opacos,
sem textura e já validados no device Rust. O padrão permanece desligado:
o A/B Release contrabalançado melhorou a mediana WebGPU em 512², mas não
o p95 em 1024² nem demonstrou vantagem geral sobre GL. O trace expõe
`camera_bindings_created`/`camera_bindings_reused`. O cache fica limitado
a 512 draws e 4 MiB de payload GPU calculado, sendo invalidado ao sair do
perfil ou trocar a geometria/device. Consulte o Prompt 008D e as medições em
`wgpu-freecad-examples-validation.md`.

O target offscreen preserva cor+profundidade como padrão. O método experimental
`CoinRenderTarget::setDepthReadbackEnabled(FALSE)` pede somente cor ao
conector Rust: o depth attachment e o depth test continuam ativos, mas não há
cópia/mapeamento/publicação de profundidade e `readbackDepth()` retorna vazio.
Em `applyAsync`, o ticket desse frame tem `depthFormat=0` e `depthBytes=0`,
independentemente de mudanças posteriores no target. Alternar a opção
invalida os accessors síncronos até o próximo render bem-sucedido. A opção
não se aplica a targets de janela ou passes RTT GPU-only. Recording/CPU
mantém o depth interno para rasterização, ocultando apenas sua saída.

O conector Rust mantém um pool privado de staging por device, com no máximo
16 MiB e 16 buffers livres, até dois por tamanho. Buffers acima de 8 MiB não
são retidos. Um buffer só retorna ao pool após completar o mapeamento,
descartar a view e desmapear. Tickets pendentes continuam donos dos buffers;
cancelamento não os disponibiliza antes de a GPU terminar. RGBA-only copia
diretamente do mapeamento para a saída do chamador; cor+depth conserva a
publicação atômica. Isto não altera qual frame o caminho síncrono retorna.
O trace opt-in expõe `staging_color_reused` para verificar o reuso.

No offscreen **síncrono** Rust, `COIN_WGPU_ATTACHMENT_CACHE=1` retém um par
de attachments RGBA8/depth por device e dimensões, até 32 MiB estimados.
Ele só é devolvido ao cache após o readback terminar; tickets assíncronos e
RTT direto mantêm attachments próprios. Resize, troca de tamanho e perda de
device impedem reuso de recursos incompatíveis. O trace expõe
`attachments_reused`. O cache permanece opt-in por enquanto.

Se o pitch de staging é exatamente `width * 4`, a cor usa uma única cópia
contígua; com padding, a cópia continua linha a linha. A variável de
diagnóstico `COIN_WGPU_FORCE_ROW_COPY=1` força o caminho antigo para A/B.
Em ambos os casos, a publicação conjunta de cor+depth continua atômica.
O accessor experimental `CoinRenderTarget::borrowRGBA()` evita a cópia
adicional de `readbackRGBA()` no C++ quando o consumidor pode usar os pixels
imediatamente. A view só existe após um render síncrono bem-sucedido e deixa
de ser válida no próximo render, resize, troca de política ou destruição do
target. A API com cópia mantém seu contrato.

O benchmark `coin_render_gl_benchmark` aceita `--rgba-output borrow` e
`--async-depth 2`. O segundo usa os tickets assíncronos existentes para
medir submit, latência até readback e throughput de dois frames em voo;
não muda o contrato de `apply()`, nem implica ganho de throughput.
Resultados e comandos reproduzíveis estão em
`wgpu-freecad-examples-validation.md`.

## Perfil implementado

| Área | Suportado agora | Fora do perfil / rejeição esperada |
| --- | --- | --- |
| Geometria | Triângulos de callback e `SoIndexedFaceSet`; linhas e pontos com textura explícita nas unidades 0–7, inclusive `SoIndexedLineSet` | Topologias sem caminho implementado não têm paridade prometida |
| Materiais | `BASE_COLOR`, iluminação e alpha uniforme por draw; onze modalidades Coin no perfil P09, inclusive strokes | Matriz geral de alpha heterogêneo por vértice e bindings P05; qualificação ampliada de camadas P10 |
| Luzes | Direcional, pontual e spot, até oito ativas por draw | Excesso de oito luzes deve retornar `UNSUPPORTED` |
| Textura | `SoTexture2` nas unidades 0–7, UV explícita, SoTextureCombine (P08), 1–4 componentes no perfil legado incluindo alpha de imagem, `MODULATE`/`REPLACE`/`DECAL`/`BLEND`, `REPEAT`/`CLAMP`, qualidade 0 ou 0,5 | UV procedural/default; formatos/qualidade ampliados em P07 |
| Composição | Onze modalidades resolvidas no Core, sorting de triângulos, screen door e peeling de quatro camadas por pixel; defaults/overrides de depth comuns | Peeling BGFX rejeita escrita transparente explícita e testes fora de LESS/LEQUAL/NEVER; muitas camadas, alpha zero/epsilon e orçamento em P10 |
| Ambiente | Fog `NONE`, `HAZE`, `FOG` e `SMOKE` em distância de view space; fog depois de luz/textura e antes da composição, sem alterar alpha | Fórmulas ou estados de fog fora desses quatro modos |
| Raster | Front face e backface culling de `SoShapeHints` em triângulos, inclusive reflexão | Culling de linhas/pontos (não aplicável ao pipeline dessas topologias) |
| Alvos | Offscreen com cor/profundidade e janela X11 no backend Rust; readback síncrono atômico; `applyAsync` com ticket e query/poll/cancel; `SoSceneTexture2` RGBA8 staged por padrão e GPU→GPU direto opt-in em offscreen Rust | Outros formatos/estados de `SoSceneTexture2`, RTT direto de janela, readback assíncrono de janela e outros sistemas de janela |

Recursos não suportados devem produzir `UNSUPPORTED` e diagnóstico, não uma
imagem aparentemente válida que ignore silenciosamente parte do estado. A
referência CPU e o Recording permitem testar o contrato sem GPU; a paridade de
pixels exige GPU real e, opcionalmente, Coin/GL.

A Onda 4 está **concluída no perfil mínimo delimitado**: 4A/4B,
composição e render-to-texture staged de 4C, readback assíncrono offscreen
de 4D e gates/medição de 4E. Não houve alteração da ABI pública do Coin 4.
Por padrão, a subcena de `SoSceneTexture2` passa por readback/upload
antes do pai. No backend Rust offscreen, `COIN_RENDER_RTT_GPU_DIRECT=1`
habilita a rota experimental GPU→GPU: o filho produz uma texture view
amostrada pelo pai, sem transferência intermediária de pixels. Outros formatos
e estados de `SoSceneTexture2` continuam fora do perfil. A ponte privada Rust oferece
submit/query/poll/cancel de readback assíncrono, com token, geração, serial,
formatos, pitch e publicação atômica de cor/profundidade.
`CoinRenderAction::applyAsync` devolve o ticket; as funções estáticas do
`CoinRenderTarget` consultam, publicam ou cancelam o resultado sem depender
da vida da action/target original. `apply` e readback síncronos permanecem.
Após `applyAsync`, os accessors síncronos do target devolvem vetores vazios
até outro `apply` síncrono, evitando apresentar o frame anterior como atual.
A query não aloca os vetores de saída enquanto o GPU readback está pendente.
O backend native/Dawn não possui pipeline transparente e rejeita esse perfil
explicitamente.

## Evidência da 4C (perfil SoSceneTexture2 staged)

`SoSceneTexture2` aceita somente unidade 0, `RGBA8`, `MODULATE`,
`REPEAT`/`CLAMP`, `transparencyFunction=NONE`, sem
`sceneTransparencyType`, com cena não nula e tamanho de 1 a 2048 por eixo.
Cada apply admite até 64 MiB de pixels RGBA8 intermediários e oito passes
aninhados; ciclos, formatos e estados fora desse perfil retornam
`UNSUPPORTED` com diagnóstico. O pass filho termina e fornece readback
antes de o pai carregar a imagem como textura; as linhas são invertidas
uma vez para conciliar as origens de framebuffer e imagem Coin. A textura
gerada participa das mesmas regras de alpha da 4B. O frame pai só é
publicado depois da construção e validação completa do seu CoinRenderFramePlan.

`CoinRenderSceneTextureTest` cobre amostragem superior/inferior (orientação),
mutação da subcena entre frames, alpha sobre fundo colorido e subcena
sem draws, duas dependências aninhadas, troca de target, rejeição no segundo
pass sem alterar o frame publicado, ciclo e recuperação após resize em Rust e
Recording/CPU. Este caminho é funcional, mas a cópia GPU→CPU→GPU por
frame não é a solução final de recursos GPU compartilhados.

`CoinRenderSceneTextureBudgetTest` verifica limite agregado em passes aninhados,
aceitação exata de 64 MiB, rejeição antes do quinto subpass, preservação do
frame publicado e orçamento novo no apply seguinte. Qualidade zero e override
de imagem não consomem esse orçamento.


## Caminho RTT GPU→GPU direto (4F experimental)

Com `COIN_RENDER_RTT_GPU_DIRECT=1` e target offscreen Rust, a subcena
`SoSceneTexture2` produz attachment RGBA8 com `TEXTURE_BINDING`. Dois
consumidores compartilham o produtor somente quando tamanho e snapshot
capturado coincidem integralmente; estados distintos não são colapsados.
O pai amostra a view GPU e o shader ajusta V para manter a orientação Coin.
O target filho não aloca color/depth CPU; não há readback, staging ou upload
**intermediário**. Readback final do target pai continua disponível.
Tokens são liberados ao fim do apply e retidos no bridge até o serial GPU
seguro. O clear opaco prova alpha opaco; nos demais casos a composição trata
a textura como potencialmente transparente.

`CoinRenderSceneTextureDirectTest` cobre imagem, alpha, aninhamento, ciclo,
resize, dois consumidores equivalentes ou distintos, zero bytes de upload
intermediário e perda do device antes do filho, entre filho e pai e durante
o submit pai. OOM é injetado antes e depois do filho; frame anterior e
recursos são preservados/aposentados, com recuperação explícita no próximo
apply ou em novo target. Falhas de criação de textura, view, depth e bind
group também exigem rollback. `CoinRenderAsyncActionDirectStressTest` executa 1.024
submits com tickets sobrepostos, mutação, troca de target e cancel/poll;
`CoinRenderAsyncActionDirectTest` esvazia o cache de geometria com ticket pendente.
Após a Onda 5, o teste direto também injeta perda depois de cada filho de um
DAG aninhado com dois consumidores do mesmo produtor, exigindo frame intacto,
recursos RTT esvaziados e recuperação completa. O teste assíncrono cobre
duas perdas sucessivas com ticket RTT pendente e buffers do chamador intactos.

Os passes filhos são capturados em ordem topológica; todos os planos
(filhos e pai) são pré-validados antes do primeiro submit. Serial permanece
inalterado após falha de pass ou perfil do pai. O limite de 64 MiB por
apply conta RGBA8 staged por ocorrência ou color+depth32 por produtor único
na rota direta. O caso de três ocorrências/dois produtores no limite e a
rejeição de um terceiro produtor distinto estão cobertos por teste.
O teto de oito níveis permanece; o bridge limita a 64 texturas RTT ativas.
A rota permanece opt-in e restrita ao perfil offscreen RGBA8. O gate local
passou 25/25 testes Rust e 14/14 Recording, build OFF, símbolos ON/OFF
idênticos e `abidiff` sem remoções ou alterações contra `libCoin.so.80`.
Quatro testes diretos passaram em ASan/UBSan com LeakSanitizer desativado:
há vazamento global preexistente de 32 bytes em `SoDB::init`/`SoSFTime`.

## Evidência da 4D (ponte privada e action experimental)

`CoinRenderAsyncReadbackTest` exerce duas solicitações fora de ordem, polling não
bloqueante, buffers pequenos e sobrepostos, cancelamento, resize com pedido
anterior pendente, equivalência exata de cor/profundidade com o readback
síncrono, falha de mapeamento injetada e perda de dispositivo com invalidação
dos demais tickets e recuperação em nova geração. O staging pertence ao
runtime Rust até poll/cancel; os callbacks não retêm ponteiros do target nem do
frame. Um ensaio local na NVIDIA GeForce RTX 3060 Laptop GPU (Vulkan) usou
65.536 bytes de staging para duas capturas RGBA8+Depth32 de 64×64 e levou
271,4 ms no roundtrip completo do teste ampliado; não é medida isolada de
latência de readback nem SLA. `CoinRenderAsyncActionTest` submete uma cena com cone
pela action, faz resize e destrói action/target antes do poll, compara cor e
profundidade exatamente ao caminho síncrono e verifica cancelamento, ticket
consumido e dois polls em threads distintas com resultados independentes.
O mesmo teste encadeia `SoSceneTexture2` ao `applyAsync`: cor e profundidade
coincidem com o frame síncrono mesmo após mutar a subcena antes do poll; o
readback síncrono anterior fica inválido, e um pass fora do perfil não emite
ticket nem avança o serial publicado do alvo pai.

## Evidência de integração da 4E

`CoinRenderPerformanceTest` mede em cada execução criação/reuso do pipeline de
blend, serial de dois passes staged, bytes de upload intermediário e staging
do ticket assíncrono, além do tempo entre o retorno de `applyAsync` e o poll
pronto. Executar com `ctest --test-dir build-wgpu -V -R '^CoinRenderPerformanceTest$'`.
Na NVIDIA GeForce RTX 3060 Laptop GPU (Vulkan), Debug/C++11, 64×64: um
pipeline blend novo e um hit no frame seguinte; dois passes RTT 32×32→64×64,
4.096 bytes de upload da imagem filha e 7,47 ms para o RTT; 32.768 bytes de
staging RGBA8+Depth32 e 1,16 ms do retorno de `applyAsync` ao poll pronto.
Os tempos são uma observação local, não SLA. O valor de staging deriva dos
pitches e dimensões retornados no ticket; os passes derivam do serial de
submissão da ponte.

O build Rust/GL de 23/09/2026 passou 30/30 testes sob Xvfb. O build
Recording passou 23/23, a janela X11 estrita passou e os símbolos exportados
de `libCoin.so` foram idênticos em builds Debug/C++11 com WebGPU ON/OFF.
Os 878 headers instalados são idênticos byte a byte; o consumidor externo
C++11 do pacote instalado compilou e executou nos dois modos.

O gate histórico foi reexecutado em 23/09/2026 com dois worktrees limpos:
`a8b55d0dc5` (Coin estável) e `71976f9c12` (fim da Onda 4), ambos
`Debug`, C++11, biblioteca compartilhada e `COIN_BUILD_RENDER=OFF`.
`abi-dumper` seguido de `abi-compliance-checker` reportou 100% de
compatibilidade binária e de fonte, zero problemas e zero avisos. Os símbolos
exportados também coincidiram exatamente. As alterações não commitadas do
checkout principal não participaram dos builds.

Esses gates cobrem o perfil declarado; não certificam GPU→GPU direto,
transparência fora de `SORTED_OBJECT_BLEND` ou plataformas não testadas.

## Build e testes

```sh
cmake -S . -B build-wgpu -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_STANDARD=11 \
  -DCOIN_BUILD_WGPU=ON -DCOIN_WGPU_BACKEND=RUST_BRIDGE \
  -DCOIN_BUILD_TESTS=ON
cmake --build build-wgpu -j
ctest --test-dir build-wgpu --output-on-failure
```

O backend Rust exige Cargo e usa `cargo build --locked`. Para a referência GL
estrita em Linux/X11, acrescente `-DCOIN_TEST_WGPU_GL_REFERENCE=ON` na
configuração; é necessário `xvfb-run`. Para exigir uma janela real visível,
use `-DCOIN_TEST_WGPU_STRICT_DISPLAY=ON` e disponibilize um display X11.
O exemplo na árvore de build é `examples/coinrender/coin_render_window_cone.cpp`, ativado
com `-DCOIN_BUILD_WGPU_WINDOW_EXAMPLE=ON`.

O gate de isolamento usa outra configuração com
`-DCOIN_BUILD_WGPU=OFF -DCOIN_BUILD_TESTS=ON -DCMAKE_CXX_STANDARD=11`.
Compare símbolos de `libCoin.so` somente entre builds equivalentes (inclusive
`CMAKE_BUILD_TYPE`); Debug e Release exportam conjuntos diferentes por causa
da otimização. A comparação ON/OFF Debug por `abi-compliance-checker` nesta
etapa deu 100% de compatibilidade binária e de fonte. Os 878 headers instalados
são idênticos em ON/OFF e não incluem WebGPU. Um consumer C++11 externo do
pacote Coin instalado compilou e executou.

## Evidência adicional da 3E

A comparação histórica do Coin estável também foi feita com
`abi-dumper`/`abi-compliance-checker`: builds limpos, Debug, C++11 e
`COIN_BUILD_RENDER=OFF` do baseline `a8b55d0dc5` e deste HEAD tiveram 100%
de compatibilidade binária e de fonte, sem problemas nem avisos. Isso não
promete compatibilidade com os três headers WebGPU experimentais que existiam
no baseline e foram removidos no isolamento 3B.

O teste estrito de janela X11 sob Xvfb passou com frames repetidos, resize,
suspensão zero-size, fault injection de superfície e recuperação de device
lost. `CoinRenderStabilizationTest` já cobre falha transacional de alocação do cache;
`CoinRenderOffscreenTest` e `CoinRenderSurfaceTest` cobrem OOM e estados do alvo.

## Medição quantitativa da 3E

O comando abaixo executa a cena de 64×64 em 31 frames: aquecimento, repetição
estática, 20 mutações da textura 4×4 e nove frames após remover a textura.

```sh
ctest --test-dir build-wgpu --repeat until-fail:3 -V -R '^CoinRenderPerformanceTest$'
```

Na NVIDIA GeForce RTX 3060 Laptop GPU (Vulkan, driver NVIDIA 610.43.02),
três execuções produziram os mesmos contadores: 21 uploads de textura
(um inicial + 20 misses por mutação), um hit estático, 1.344 bytes RGBA8
enviados, 12 evicções após as mutações, nove entradas ativas nesse ponto e
zero após remover a textura. O pipeline foi compilado uma vez e reutilizado
30 vezes. O tempo dos 31 frames foi 105,4–126,3 ms (mediana 112,3 ms);
é observação local, não limite de desempenho portátil. A política privada
retém texturas sem uso por até oito submissões e as aposenta pelo serial de
conclusão da GPU, evitando retenção indefinida após mudança de cena.

A evidência da 3E vale para esta configuração Linux/Vulkan/X11 e para o
perfil opaco descrito acima; outros adaptadores e sistemas de janela ainda
precisam da própria validação antes de qualquer promessa de suporte.

## Composição comum (2026-09-28)

A03 foi fechado no Core, com transporte resolvido para BGFX e wgpu e correção
da referência CPU. `CoinRenderCompositionTest` passou nos dois executores GPU;
a evidência acima por fase permanece histórica. O suporte ainda não cobre as
onze modalidades no wgpu. Consulte [contrato e limites atuais](coin-render-composition-contract.md).

## Clipping Coin

Até oito planos ativos, capturados em mundo e resolvidos por CoinRender, com
recorte de strokes antes da expansão. Limites, ABI privada 26 e evidências no
[contrato de clipping](coin-render-clipping-contract.md).

## Offset de polígonos em LINES/POINTS

CoinRender calcula a inclinação da face plana original antes da expansão.
BGFX aplica o bias resolvido no fragmento; wgpu usa uma variante com saída de
depth (ABI privada 26). O perfil wgpu combina inclinação com units fracionário na precisão D32Float;
combinações fora do perfil recebem diagnóstico explícito. Faces não planas,
precisão de units e qualificação visual continuam abertas no
[contrato e checklist de estilo](coin-render-draw-style-contract.md).

Expansão de strokes agora é comum em `CoinRenderStrokeCore.h`, com UV/cor
em perspectiva e fog por fragmento. O vértice wgpu tem 100 bytes na ABI privada
26; a matriz numérica e os limites estão no contrato de estilo.

## Fechamento P08

Oito unidades e SoTextureCombine agora têm interpretação comum e execução
CPU/BGFX/wgpu, inclusive em strokes. A matriz, os limites e a ABI privada 26
estão no [contrato P08](coin-render-multitexture-contract.md).

## P09 — transparência funcional

Onze modalidades compartilhadas, aditivo, screen door, sorting de triângulos
e peeling de quatro camadas foram qualificados em CPU, Rust/wgpu e BGFX
Vulkan/OpenGL. O plano de composição tem um dono no Core. A revisão privada
atual é 27, mantendo os tamanhos da revisão 26 registrada em P08.
O [contrato P09](coin-render-transparency-contract.md) declara as comparações
GL, as expectativas numéricas de alpha e os limites de camadas/depth. P10,
FreeCAD, raster ampliado e native/Dawn continuam fora desse fechamento.
