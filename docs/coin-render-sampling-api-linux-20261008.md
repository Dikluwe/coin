# Política de sampling — continuação Linux em 2026-10-08

A API por alvo foi qualificada em janela Xlib neste PC, com BGFX/wgpu, AMD
Vulkan/OpenGL e NVIDIA Vulkan. O perfil FreeCAD aquecido está completo neste PC. Native permanece
padrão. A opção portátil tem custo GPU mensurável; ela oferece o contrato
isotrópico de NEAREST_MIPMAP_LINEAR, não uma promessa de mais FPS.
A entrega continua exclusivamente em `codex/coin-portable-sampling-study`.
Não houve integração em master/coin-render, push, alteração do driver instalado
ou aplicação do workaround Mesa externo nesta campanha.

## Implementação e revisões

- `0719d4621e2b9faa7ac587f1b13fdb97d26c37c6`: opções explícitas nos
  construtores de CoinRenderSceneManager/CoinRenderManagerAdapter, benchmark,
  fixture Xlib e patch experimental do consumidor FreeCAD por viewport.
- `f9e606d4c3021bad9aaeebc67b9be632c9b25ac8`: admissão portátil evita varrer
  estados/unidades quando não há sampler incompatível; declara qualificação
  Linux offscreen/Xlib nos perfis testados e mantém as recusas/recovery.
- `3c517922e35ee16f83f67c59f7feda0b8b4c63e0`: nome do adaptador efetivamente submetido em diagnóstico wgpu
  e estabilização do fixture FreeCAD; BGFX permite recibo GL sem tracing completo
  e informa instancing também na janela. Essas últimas mudanças são diagnóstico.
  O shader e o algoritmo de sampling
  permaneceram iguais à revisão f9; o diff e identidades dos shaders ficam no ledger.

A classificação rápida percorre a tabela de samplers. Só percorre os estados
que referenciam unidades quando encontra filter=2/anisotropia incompatível.
Sampler incompatível sem uso continua permitido; unidade ativa incompatível
continua recusada antes de publicar serial/pixels/pointer. Testes exercitam ambos.

## Gates funcionais

| Campanha | Resultado | Escopo |
| --- | --- | --- |
| CPU/API | 2/2 PASS | admissão e recuperação sem GPU |
| Xlib/API/selection/publication/ownership | 30/30 PASS | seis perfis físicos, alvos simultâneos |
| Portátil deep/RTT direto/viewport/procedural | 24/24 PASS | seis perfis, drivers instalados |
| FreeCAD DPR 1 native e portable | 12/12 PASS | seis perfis, host privado aquecido |
| FreeCAD DPR 2 portable | 6/6 PASS | seis perfis, escala Qt forçada |
| Rust/Naga | 46/46 PASS | ponte e shaders |
| Runner Qt | 81/81 PASS | gates incluindo GPU GL physical/Other |
| Probe C11 exportado | PASS | capabilities v4=584, prefixo v3=536 |

Os 74 processos funcionais PASS acima não incluem pilotos, repetições ou benchmarks.
Após os últimos diagnósticos, API/window foi repetido nos seis perfis: 12/12 PASS,
com o runtime final. Essa repetição fica fora do subtotal 74.
A qualificação Xlib cobre dois alvos simultâneos, oracle texel/mip, unidades
0/7, independência native/portable, recusa preservando publicação, recuperação,
resize, suspensão 0×0 e remap. O teste aguarda extent/MapNotify; wgpu/OpenGL usa
pixels X11 do cliente por ausência de COPY_SRC nessa superfície. Os demais
perfis usam RGBA do renderer. O bit de qualificação expressa esse perfil
Linux implementado/testado, não certifica automaticamente todo driver/dispositivo.

No FreeCAD o [patch do host](../examples/coinrender/freecad_sampling_policy.patch)
recebe a propriedade Qt por viewport `coinRenderPortableSampling`, recria o
adapter ao trocar a política e publica `coinRenderActiveSamplingPolicy`.
Cada processo verifica a política efetivamente admitida. DPR 2 é escala Qt
forçada (`QT_SCALE_FACTOR=2`), não prova de DPI físico distinto entre monitores.
Foram usados object,
transparência padrão, oito nós: SoText2, SoImage, labels reais FreeCAD,
alpha-test, UV projetiva, complexity bbox e markers; visibilidade, mutação,
remoção, picking, resize/reteste e idle exato de 1,2 s.
O caso UV usa imagem POT 256×256/qualidade 0,5 para exercer filter=2 com
minificação no host. Isso não é comparação de pixels native/CoinGL/portable:
os oracles de texel/mip são os testes API/Xlib e deep, separados da integração.
Não se certificam todos os workbenches, weighted transparency ou primeiro
frame absoluto de cold start: dois redraws estabelecem a câmera inicial e um ciclo visível/oculto prepara
o recurso de texto antes do baseline. O teste exige dois renders/leituras
estáveis, com prazo limitado, antes de aplicar os mesmos gates.

A macro espera duas leituras de tela idênticas em no máximo 0,5 s e até 5 s
para estabilizar a fila de cleanup antes do gate idle inalterado. Menus/tooltip
do próprio host são fechados, sua janela recebe ABOVE por EWMH sem recriar a superfície Qt e input interativo
é bloqueado só nesse processo
privado. Isso elimina contaminação de tela/câmera sem reduzir o gate de remoção
(≤30 pixels), visibilidade ou mutação. Amostras de apresentação diferentes
ficam salvas. AMD Vulkan usa Xwayland privado; GL e NVIDIA usam o desktop,
pois a sessão privada não comprovou DRI3 GL e o WM NVIDIA encerrou antes do host.
Preferências, host recompilado e perfis são privados; FreeCAD/fonte originais
não foram alterados. wgpu/GLES pode declarar Other/device 0: aceitação exige
vendor conhecido e nome físico do contexto submetido, rejeitando CPU/software/
contexto ausente. Não se usa o inventário Qt como prova de execução física.
As três macros ficam congeladas em cada execução, com manifest SHA-256. WM,
`wmctrl` e `xprop` são requisitos desse fixture Linux.

## Desempenho delimitado

Campanhas independentes, ordem ABBA: **144/144** processos CPU válidos,
**96/96** sondas GPU válidas (4.320 timestamps medidos) e **120/120** controles
native versus baseline válidos. Cenas POT 1/4/8 unidades, NPOT 63×65/8 unidades,
cidades 40 mil/1 milhão; janela 1280×720 sem readback. CPU usa 90 frames/30 warmup,
GPU 45/15; o milhão BGFX usa 12/4 em políticas e baseline. O [índice de campanhas](validation/sampling-api-linux-20261008/campaign-index.json)
identifica as linhas qualificadas; os controles GL CPU/baseline originais sem
recibo de contexto físico foram substituídos por repetições explícitas.
Comandos, amostras,
medianas de cada repetição e intervalos estão nos JSON/CSV do ledger.
A mediana apresentada é a mediana das duas medianas por política.
O A/B GL do baseline usa tracing completo em ambos os lados porque o runtime
antigo exigia esse flag para imprimir o driver. Seu custo CPU é instrumentado;
a campanha CPU GL comum usa apenas o recibo de setup. Não misturar os dois
escopos nem comparar valores absolutos instrumentados com a campanha comum.

CPU mede chamada render/present com possível backpressure, não latência de
monitor ou GPU concluído. wgpu/OpenGL usa surface-default/vsync porque não
oferece Immediate/Mailbox. A campanha GPU usa instrumentação e sincronização;
seu tempo CPU não substitui a campanha CPU comum. GPU wgpu mede renderpass,
BGFX mede frame: comparar native/portable dentro do backend, não valores
absolutos entre backends.

Um milhão, ms por chamada CPU (— significa não medido):

| Perfil | Native | Portable corrigido | Portable antes da correção |
| --- | ---: | ---: | ---: |
| bgfx-amd-gl | 1076.316 | 1073.043 | — |
| bgfx-amd-vulkan | 1179.071 | 1174.666 | 1245.995 |
| bgfx-nvidia-vulkan | 1140.677 | 1141.592 | — |
| wgpu-amd-gl | 18.442 | 18.619 | 99.939 |
| wgpu-amd-vulkan | 16.404 | 15.409 | 100.548 |
| wgpu-nvidia-vulkan | 9.630 | 12.032 | 98.705 |

O custo extra de ~80–90 ms em wgpu era a varredura de um milhão de estados,
repetida em admissão. O fast path a removeu. BGFX continua com ~1,1–1,2 s por
chamada nessa cena, inclusive native e baseline. A sonda instrumentada confirmou
1.000.001 instâncias de 160 bytes, um draw/24 vértices compartilhados e pool GPU
para 1.048.576 instâncias. Após warmup: lowering mediano 1.024,61 ms native/
1.025,91 ms portable; upload CPU 125,65/128,37 ms; resource_cache_hit=0 e
geometry_buffer_reused=1. Não é medição de tempo GPU.
`retainForReuse` conta 160.000.160 bytes de instâncias CPU antes de outros
metadados, superando o orçamento explícito de 128 MiB; recusa a retenção do plano
e o backend volta a lowering/upload no frame seguinte. O código e os traces
sustentam esse diagnóstico. Otimizar o plano residente/payload ou separar
metadados de replay permanece estudo; não aumentar o limite silenciosamente.
[Profiling](validation/sampling-api-linux-20261008/bgfx-million-profile/summary.json). Na coorte longa NVIDIA
wgpu portable ficou ~25% acima de native (12,03 vs 9,63 ms); no piloto ABBA
isolado ambos ficaram perto de 10 ms. Não se afirma custo CPU zero ou igualdade
universal: variação de frequência/carga/backpressure exige estudo separado.
A repetição isolada ABBA 120 frames/30 warmup obteve 9,04 ms native e 9,16 ms
portable, intervalos 8,98–9,10/9,08–9,24 ms. Essa repetição não apaga a coorte
longa; reforça que não se deve atribuir toda diferença agregada à política.
[Repetição NVIDIA](validation/sampling-api-linux-20261008/nvidia-repeat-aggregate.json).

Oito unidades POT, ms GPU:

| Perfil | Native | Portable | Razão |
| --- | ---: | ---: | ---: |
| bgfx-amd-gl | 0.305 | 0.945 | 3.10× |
| bgfx-amd-vulkan | 0.271 | 0.893 | 3.30× |
| bgfx-nvidia-vulkan | 0.750 | 1.797 | 2.40× |
| wgpu-amd-gl | 0.537 | 1.290 | 2.40× |
| wgpu-amd-vulkan | 0.870 | 1.289 | 1.48× |
| wgpu-nvidia-vulkan | 0.701 | 1.560 | 2.22× |

A reconstrução portátil custa de 1,48× a 3,30× nesse fixture. O caminho de
uma amostra POT não elimina derivadas, cálculo de mip e centralização do texel;
NPOT usa duas amostras explícitas. Native continua apropriado quando sua
semântica é suficiente. Não recomendar portátil como otimização de FPS.
O A/B contra a revisão API anterior `5de802e9e6` não apresentou regressão
native consistente nesta campanha: nenhuma família excedeu 1,15× e os
intervalos do milhão BGFX/AMD se sobrepõem. São duas repetições por política,
não garantia para outras cargas. O baseline congelou executáveis e bibliotecas
antes das modificações; oito hashes foram conferidos novamente.

## Evidência, falhas anteriores e limites

Ledger: [manifest](validation/sampling-api-linux-20261008/manifest.json),
[CPU](validation/sampling-api-linux-20261008/cpu-aggregate-final.json),
[GPU](validation/sampling-api-linux-20261008/gpu-aggregate-final.json),
[baseline](validation/sampling-api-linux-20261008/baseline-aggregate-final.json),
[FreeCAD: índice de resultados qualificados](validation/sampling-api-linux-20261008/freecad-qualified-index.json).
Receitas: [runner](../testsuite/reproducers/sampling-api/README.md).
Artefatos grandes permanecem no diretório durável
`/mnt/Laranja/Git/externos/coin-portable-sampling-artifacts/20261008-sampling-api-linux`;
logs, comandos, receitas, patches, fontes e capturas qualificadas foram
versionados. Capturas de trials permanecem no root local com manifest de hashes;
imagens ocluídas por outra aplicação não são copiadas ao Git.
Cookies de autenticação, preferências pessoais, binários/objetos/SDKs não entram no Git.

O runtime final tem hashes próprios. A campanha principal de desempenho usou f9; os controles GL foram repetidos
com recibo físico obrigatório após correção apenas do diagnóstico BGFX;
seus hashes ELF de bibliotecas não foram congelados antes do rebuild diagnóstico.
Os hashes finais não certificam retrospectivamente esses ELF. Comandos,
hashes dos executáveis/cenas, fonte f9, diff diagnóstico e shaders idênticos
preservam a proveniência disponível. Os builds por symlink são mutáveis;
nunca usá-los como substitutos de binários históricos congelados.

Pilotos com corridas de WM, COPY_SRC indisponível, ausência de modo de present,
WM privado indisponível, GL software, sessão bloqueada, menu sobreposto,
gestos de navegação, superfície recriada por uma tentativa de flag Qt,
oclusão por outra aplicação e frames de apresentação anteriores permanecem como
FAIL/SKIP separados. Campanhas interrompidas têm checkpoints/terminação
preservados e não entram nos totais finais. O final-summary da primeira
orquestração registra FreeCAD incompleto; somente os resultados PASS
selecionados no índice qualificam o perfil aquecido.
A matriz aquecida principal contém dez PASS e dois SKIPs por sessão bloqueada;
DPR2 isolado AMD contém dois PASS. As duas células DPR1 bloqueadas foram
reexecutadas no desktop e passaram, seguidas dos quatro perfis DPR2 de desktop.
Esses processos novos completam o índice 12+6. Nenhum SKIP foi convertido em PASS.

A divergência nativa CoinGL AMD/LOD continua no driver instalado. A API
portátil atende seu próprio contrato e não corrige o driver. O relatório
[das 28 falhas anteriores](coin-render-failure-closure-20261008.md) conserva
suas condições de Mesa externo; recorte/iluminação/raster CoinGL seguem estudos.
Na matriz anterior sem aquecimento explícito de texto houve **11/12 PASS** e
um FAIL NVIDIA/wgpu portable: 1.476 pixels da borda do documento mudaram
(10,10,10→146,152,158), sem glyphs restantes. A sonda adicional de picking e
primeiro glyph passou, com deltas zero; não demonstrou a causa. Essa transição
inicial permanece **estudo aberto**, não foi corrigida ou reclassificada como
PASS. O perfil aquecido usa outro baseline e declara `cold_start_qualified=false`;
seu fechamento não certifica cold start, troca de política fria ou ausência de
mudança inicial da borda. Todos os gates posteriores de remoção e picking
permanecem inalterados. Traces/capturas locais e diagnóstico estão preservados.

Windows, Android desta API, Intel física, outras superfícies/consumidores e
promoção para coin-render permanecem pendentes. Continuar BGFX milhão e
variação NVIDIA como estudos de desempenho, conservando native como padrão.
