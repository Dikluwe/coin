# Perfil WebGPU experimental do Coin 4

Esta implementação é opt-in (`COIN_BUILD_WGPU=ON`) e fica em
`CoinWgpuExperimental`, separada de `libCoin`. A ação e o alvo de renderização
estão em `experimental/include` para uso na árvore de build; não são API nem
ABI pública instalada do Coin 4. A ponte C++/Rust é privada e versionada
(`COIN_WGPU_BRIDGE_PROTOCOL_REVISION=15` nesta revisão). Não promova esses
headers a consumidores externos como se fossem estáveis.

## Perfil implementado

| Área | Suportado agora | Fora do perfil / rejeição esperada |
| --- | --- | --- |
| Geometria | Triângulos de callback e `SoIndexedFaceSet`; linhas e pontos sem textura, inclusive `SoIndexedLineSet` | Topologias sem caminho implementado não têm paridade prometida |
| Materiais | `BASE_COLOR`, iluminação e alpha uniforme por draw; `SoTransparencyType::SORTED_OBJECT_BLEND` em triângulos transparentes | Outros modos de transparência; alpha heterogêneo por vértice; linhas/pontos transparentes |
| Luzes | Direcional, pontual e spot, até oito ativas por draw | Excesso de oito luzes deve retornar `UNSUPPORTED` |
| Textura | `SoTexture2` na unidade 0, UV explícita, 1–4 componentes incluindo alpha de imagem, `MODULATE`, `REPEAT`/`CLAMP`, qualidade 0 ou 0,5 | Unidades adicionais, UV procedural/default, `REPLACE`/`DECAL`/`BLEND`, linhas/pontos texturizados |
| Composição | Opacos primeiro e transparentes em pass separado, ordenados estavelmente por profundidade média em view space; depth write desligado para transparentes | Ordenação por triângulo, interseções e transparência independente da ordem |
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
antes do pai. No backend Rust offscreen, `COIN_WGPU_RTT_GPU_DIRECT=1`
habilita a rota experimental GPU→GPU: o filho produz uma texture view
amostrada pelo pai, sem transferência intermediária de pixels. Outros formatos
e estados de `SoSceneTexture2` continuam fora do perfil. A ponte privada Rust oferece
submit/query/poll/cancel de readback assíncrono, com token, geração, serial,
formatos, pitch e publicação atômica de cor/profundidade.
`SoWgpuRenderAction::applyAsync` devolve o ticket; as funções estáticas do
`SoWgpuRenderTarget` consultam, publicam ou cancelam o resultado sem depender
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
publicado depois da construção e validação completa do seu FramePlan.

`WgpuSceneTextureTest` cobre amostragem superior/inferior (orientação),
mutação da subcena entre frames, alpha sobre fundo colorido e subcena
sem draws, duas dependências aninhadas, troca de target, rejeição no segundo
pass sem alterar o frame publicado, ciclo e recuperação após resize em Rust e
Recording/CPU. Este caminho é funcional, mas a cópia GPU→CPU→GPU por
frame não é a solução final de recursos GPU compartilhados.

`WgpuSceneTextureBudgetTest` verifica limite agregado em passes aninhados,
aceitação exata de 64 MiB, rejeição antes do quinto subpass, preservação do
frame publicado e orçamento novo no apply seguinte. Qualidade zero e override
de imagem não consomem esse orçamento.


## Caminho RTT GPU→GPU direto (4F experimental)

Com `COIN_WGPU_RTT_GPU_DIRECT=1` e target offscreen Rust, a subcena
`SoSceneTexture2` produz attachment RGBA8 com `TEXTURE_BINDING`. Dois
consumidores compartilham o produtor somente quando tamanho e snapshot
capturado coincidem integralmente; estados distintos não são colapsados.
O pai amostra a view GPU e o shader ajusta V para manter a orientação Coin.
O target filho não aloca color/depth CPU; não há readback, staging ou upload
**intermediário**. Readback final do target pai continua disponível.
Tokens são liberados ao fim do apply e retidos no bridge até o serial GPU
seguro. O clear opaco prova alpha opaco; nos demais casos a composição trata
a textura como potencialmente transparente.

`WgpuSceneTextureDirectTest` cobre imagem, alpha, aninhamento, ciclo,
resize, dois consumidores equivalentes ou distintos, zero bytes de upload
intermediário e perda do device antes do filho, entre filho e pai e durante
o submit pai. OOM é injetado antes e depois do filho; frame anterior e
recursos são preservados/aposentados, com recuperação explícita no próximo
apply ou em novo target. Falhas de criação de textura, view, depth e bind
group também exigem rollback. `WgpuAsyncActionDirectStressTest` executa 1.024
submits com tickets sobrepostos, mutação, troca de target e cancel/poll;
`WgpuAsyncActionDirectTest` esvazia o cache de geometria com ticket pendente.
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

`WgpuAsyncReadbackTest` exerce duas solicitações fora de ordem, polling não
bloqueante, buffers pequenos e sobrepostos, cancelamento, resize com pedido
anterior pendente, equivalência exata de cor/profundidade com o readback
síncrono, falha de mapeamento injetada e perda de dispositivo com invalidação
dos demais tickets e recuperação em nova geração. O staging pertence ao
runtime Rust até poll/cancel; os callbacks não retêm ponteiros do target nem do
frame. Um ensaio local na NVIDIA GeForce RTX 3060 Laptop GPU (Vulkan) usou
65.536 bytes de staging para duas capturas RGBA8+Depth32 de 64×64 e levou
271,4 ms no roundtrip completo do teste ampliado; não é medida isolada de
latência de readback nem SLA. `WgpuAsyncActionTest` submete uma cena com cone
pela action, faz resize e destrói action/target antes do poll, compara cor e
profundidade exatamente ao caminho síncrono e verifica cancelamento, ticket
consumido e dois polls em threads distintas com resultados independentes.
O mesmo teste encadeia `SoSceneTexture2` ao `applyAsync`: cor e profundidade
coincidem com o frame síncrono mesmo após mutar a subcena antes do poll; o
readback síncrono anterior fica inválido, e um pass fora do perfil não emite
ticket nem avança o serial publicado do alvo pai.

## Evidência de integração da 4E

`WgpuPerformanceTest` mede em cada execução criação/reuso do pipeline de
blend, serial de dois passes staged, bytes de upload intermediário e staging
do ticket assíncrono, além do tempo entre o retorno de `applyAsync` e o poll
pronto. Executar com `ctest --test-dir build-wgpu -V -R '^WgpuPerformanceTest$'`.
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
`Debug`, C++11, biblioteca compartilhada e `COIN_BUILD_WGPU=OFF`.
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
O exemplo na árvore de build é `examples/wgpu/wgpu_window_cone.cpp`, ativado
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
`COIN_BUILD_WGPU=OFF` do baseline `a8b55d0dc5` e deste HEAD tiveram 100%
de compatibilidade binária e de fonte, sem problemas nem avisos. Isso não
promete compatibilidade com os três headers WebGPU experimentais que existiam
no baseline e foram removidos no isolamento 3B.

O teste estrito de janela X11 sob Xvfb passou com frames repetidos, resize,
suspensão zero-size, fault injection de superfície e recuperação de device
lost. `WgpuStabilizationTest` já cobre falha transacional de alocação do cache;
`WgpuOffscreenTest` e `WgpuSurfaceTest` cobrem OOM e estados do alvo.

## Medição quantitativa da 3E

O comando abaixo executa a cena de 64×64 em 31 frames: aquecimento, repetição
estática, 20 mutações da textura 4×4 e nove frames após remover a textura.

```sh
ctest --test-dir build-wgpu --repeat until-fail:3 -V -R '^WgpuPerformanceTest$'
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
