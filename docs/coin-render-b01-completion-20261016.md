# B01 — correção concluída e qualificação Linux

**A correção do B01 e seus cruzamentos estão encerrados nas sete células Linux
executáveis deste host.** O contrato escolhido é a política explícita
`COIN_RENDER_RASTER_CANONICAL_8BIT`. O padrão `NATIVE` conserva a rasterização
do driver e seus resultados conhecidos; seus testes CoinGL não foram aprovados
artificialmente. Qualificação de Intel, Windows, Android e Metal continua nos
IDs V correspondentes. Este relatório não encerra as etapas 2–4 completas.

| Requisito acompanhado | Evidência final |
| --- | --- |
| Cobertura, diagonais, winding/culling, viewport e clipping | 188 casos offscreen por célula, cobertura CPU/GPU exata |
| Z/W, near/far, funções e range de depth | Máximo observado 1,2517×10⁻⁶; gate preservado ≤10⁻⁵; oito funções, range normal/invertido e escrita desligada |
| Alpha e UV/LOD | Transparência atravessando near/far; nearest/linear e dois filtros mip; cobertura exata e último mip de cor independente |
| Instancing | 400 instâncias reais com near/far e transformação afim/perspectiva; controles B02 nativo/canônico nas quatro células BGFX |
| Janela e resize | 192 casos por célula, incluindo resize 32→256→32 e 32→64→32; RGB observado em sete células, RGBA nativo em seis |
| Tickets | Cor/depth assíncronos iguais ao síncrono; consumo único; janela recusa ticket sem alterar serial/publicação |
| Câmera e RTT | 18 frames por célula com máscara CPU idêntica; gates B01.1 anteriores e RTT staged/direct repetidos |
| Sombras | 56/56 processos: spot/directional, viewport, paths/overrides, qualidades e até oito mapas, RTT, peeling e weighted OIT |
| P20 completa disponível | 24/24 células CoinRender canônicas contra CPU; 12/12 comparações entre GPUs; oito pares Vulkan RGBA exatos |
| Regressão e ABI | 4/4 CTests CPU/ABI, 5/5 testes Rust de shaders/layout; B12 wgpu nas três células executáveis |

Células: BGFX Vulkan/OpenGL AMD e NVIDIA; wgpu Vulkan AMD e NVIDIA; wgpu
OpenGL AMD. wgpu/OpenGL NVIDIA retorna 77 e permanece **indisponível**, sem
contar como passe. Kernel Linux 6.17.0-42, Mesa 25.2.8, NVIDIA 615.71.09.
Há 39 processos finais de geometria/câmera/RTT/instancing/bump, mais 56 de
sombras; a P20 executa outros 64 processos de captura, entre controle nativo
e rodada canônica. Comparações visuais nativas reprovadas permanecem FAIL.

Campanha iniciada em 16/10 e concluída em 17/10/2026. A janela cobre 190
casos geométricos mais dois cruzamentos RTT staged/direct por célula.

## O que foi corrigido

1. **Referência CPU e W pequeno.** O teste inicial reproduziu 36 pixels
   diferentes em todas as quatro células BGFX para W=5×10⁻⁶. A CPU cortava
   em W=10⁻⁵ e aplicava o snap aos vértices depois do clipping. A política
   agora quantiza antes da rasterização e preserva a interpolação original,
   sem introduzir esse plano de olho artificial no perfil canônico.
2. **Vértices gerados pelo clipper e profundidade.** O estresse assimétrico
   revelou erros de depth de até aproximadamente 4,6×10⁻⁴, mesmo quando a
   cor e a cobertura coincidiam. BGFX e wgpu agora carregam o Z/W original,
   mantêm Z de rasterização dentro do frustum e executam o recorte near/far
   e a escrita de profundidade no fragmento. Isso mantém o plano original
   de interpolação em vez de depender de vértices de clipping diferentes.
   A CPU segue a mesma semântica com implementação independente.
3. **Sombras canônicas.** A restrição inicial foi removida. Os mapas de
   momentos e os receptores aplicam a política; o viewport de cada mapa é
   enviado explicitamente. O uniform Rust/WGSL de sombras mede 352 bytes,
   validado por Naga e pelo teste de layout. A ABI privada C++/Rust permanece
   52/464 bytes; o transporte de shader reaproveita dois componentes livres,
   mantendo o limite de 16 locations.
4. **Instancing e resize na AMD.** A campanha canônica B02 encontrou um
   pixel com RGB 111 de diferença em `(167,168)` depois do resize. A posição
   instanciada agora usa a ordem explícita de multiplicações/somas no modo
   canônico, com `invariant` GLSL e `precise` no caminho SPIR-V. O binário
   SPIR-V contém seis decorações `NoContraction`. Os oito processos BGFX
   nativo/canônico passaram com instancing ativo; não houve opt-out corretivo.

A grade continua `Q(x)=(floor(128*x)+0.5)/128`, passo 1/128 e deslocamento
lógico máximo 1/256 de pixel por eixo. O snap XY usa W>10⁻⁶ e coordenadas
intermediárias de janela com módulo ≤32768, em vez do limite inicial NDC±2.
Fora desse domínio numérico a posição XY é preservada; não se promete
identidade entre drivers nesse domínio. Z/W original é mantido para o depth
visível. Linhas/pontos públicos passam pela expansão comum em triângulos,
que foi testada em tamanhos 1/2,5/8; planos internos raw LINE_LIST/POINT_LIST
continuam recusados pelo contrato canônico.

O recorte por fragmento é uma opção de qualidade/consistência. Seu custo,
incluindo possíveis efeitos sobre early-Z, continua em B10. Não foi promovido
a padrão nem tratado como otimização de desempenho.

## Oráculos, gates e apresentação real

Os casos geométricos usam equações inteiras independentes para triângulos,
além da referência CPU completa. As funções de depth também verificam uma
amostra com o vencedor vermelho/azul calculado diretamente de sua semântica.
O último mip usa uma cor saturada conhecida, sem tomar a imagem de outro
executor como esperado. Cor/depth/câmera/RTT antigos mantêm seus gates.

A fixture nova de UV inicialmente exigia uma diferença de apenas um nível,
emprestada do caso sem textura. A NVIDIA apresentou máximo 2 na filtragem
linear, com cobertura idêntica. O controle passou a usar o contrato de
sampling **já existente** em `CoinRenderTextureSamplingTest`: máximo ≤4,
MAE ≤1,5, mantendo cobertura exata e o esperado independente do último mip.
Essa diferença não foi apresentada como correção de shader.

A superfície wgpu/OpenGL AMD não oferece COPY_SRC e aceita apenas FIFO para
a apresentação usada. Seu teste de janela observa os pixels RGB realmente
apresentados com XGetImage; alpha não é observado por essa rota. Um pixel de
borda reservado e uma cor de clear única por revisão identificam a chegada
do novo frame. O teste espera somente esse carimbo e depois compara a imagem
inteira, impedindo que uma imagem antiga coincidente aprove o frame novo.
As demais seis células usam readback RGBA nativo. A falta de readback dessa
superfície continua em R17/V04; não foi ocultada por um passe de API.

As APIs não oferecem uma imagem universal de bordas entre fornecedores.
As regras e a liberdade de precisão de clipping/rasterização estão nas
[especificações Vulkan](https://docs.vulkan.org/spec/latest/chapters/primsrast.html)
e no [ARB_clip_control do Khronos](https://github.com/KhronosGroup/OpenGL-Registry/blob/main/extensions/ARB/ARB_clip_control.txt).
A consequência adotada aqui é um contrato canônico explícito, comprovado
nos executores disponíveis, com a comparação nativa registrada separadamente.

## Resultado nativo e fechamento por eixo

O controle de câmera nativo BGFX repete três passes e a falha AMD/Vulkan já
conhecida (frame 5, MAE 0,149582, 49 pixels >3). Na P20 nativa, 22/24
comparações passam e as duas AMD/Vulkan transparentes offscreen permanecem
com máximo 17 e quatro pixels >3. O runner nativo mantém retorno de falha.
O próprio CoinGL escolhe lados diferentes em GPUs distintas nos recibos
anteriores; a política NATIVE não promete equivalência pixel a pixel com
esses oráculos. O perfil canônico é a solução qualificada para esse requisito.

| Eixo | Estado |
| --- | --- |
| Implementação B01 e regressões locais | Concluídas no contrato descrito |
| Qualificação B01 nas GPUs Linux disponíveis | Concluída, sete células |
| CoinGL nativo | Diagnóstico/contrato definidos; falhas dos gates preservadas, sem declaração de paridade |
| Intel/Windows/Android/Metal | Qualificação pendente nos V; sem propagação de passes Linux |
| Custo, recursos ainda ausentes e managers | Permanecem nas etapas/B/C/R correspondentes |

## Usar e reproduzir

Selecionar `options.rasterizationPolicy = COIN_RENDER_RASTER_CANONICAL_8BIT`
antes de criar o alvo/manager. Os benchmarks aceitam
`--rasterization-policy canonical`; `native` continua como padrão.

[Teste ampliado](../testsuite/coinrender/CoinRenderAdvancedCoverageTest.cpp),
[matriz](coin-render-feature-matrix.md),
[estado estruturado](inventories/coin-render-b01-final-matrix.json),
[manifesto](validation/coin-render-b01-advanced-20261016/manifest.json),
[verificação](validation/coin-render-b01-advanced-20261016/verification.json),
[resultados GPU](validation/coin-render-b01-advanced-20261016/gpu-results.json),
[sombras](validation/coin-render-b01-advanced-20261016/shadow-results.json),
[sombras ampliadas](validation/coin-render-b01-advanced-20261016/shadow-extended-results.json)
e [P20 final](validation/coin-render-b01-advanced-20261016/p20-final/portable-summary.json).
Scripts, logs, controles reprovados e experiências intermediárias foram
preservados no diretório de evidência; apenas `accepted-*`, os JSONs finais,
a P20 final e os controles listados no manifesto compõem o fechamento.

Os recursos e combinações que continuam recusados no perfil nativo, como os
casos C02/C07/C21, permanecem na etapa 2 e nos R/C próprios. Esses mecanismos
não receberam um passe implícito desta campanha de rasterização.

Executar `CoinRenderAdvancedCoverageTest` para GPU offscreen, `--window`
para X11 e `--cpu` para o oráculo CPU. Fixar ICD/GLX/EGL/PRIME com os runners
salvos na evidência. Validar o pacote com
`python3 scripts/coinrender/audit_b01_completion.py EVIDENCE_DIR`.
Se o checkout já avançou para outras frentes, acrescentar
`--source-snapshot EVIDENCE_DIR/b01-sources.tar.gz`: o arquivo preserva
os 53 fontes alterados/instrumentados, com hashes idênticos ao fechamento.

Os hashes de executáveis e bibliotecas são os registrados pelos runners
durante cada teste. O manifesto preserva a revisão dos fontes e das evidências
antes do avanço para B03; mudanças posteriores devem ser comparadas com
essa revisão, sem reatribuir os resultados antigos a fontes novos.
