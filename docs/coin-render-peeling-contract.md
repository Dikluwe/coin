# P10 — camadas limitadas e weighted OIT

CoinRender oferece peeling configurável de **1 a 8 camadas**, com padrão de
quatro. CPU, CoinBgfx e CoinWgpu compartilham a política sobre o frame capturado;
cada Infra executa seu mecanismo. Weighted OIT é uma extensão explícita de
CoinBgfx e CoinWgpu; não há seleção automática de weighted OIT.

## Donos e configuração

Wiring expõe `CoinRenderAction::setSortedLayersNumPasses(int)` e
`setTransparencyBufferBudget(uint64_t)`, com getters correspondentes. Captura
as opções no plano e as propaga para actions filhas de `SoSceneTexture2`.
Alterar uma opção invalida o reuso do plano, inclusive camera patch.

`CoinRenderTransparencyCore.h` é o dono dos limites e do cálculo conservador.
O orçamento padrão é **256 MiB por pedido de attachments de transparência**.
Peeling exige `width * height * (16 * layers + 8) * samples` bytes; os produtos usam
checagem de overflow. Contagem fora de 1..8 ou orçamento zero são inválidos.
Orçamento insuficiente retorna `UNSUPPORTED` antes da alocação/publicação,
preservando imagem e serial. Um pedido válido posterior volta a funcionar.

Esse orçamento não mede toda a memória do dispositivo: geometria, texturas,
alvos principais, tickets, overhead do driver e alocadores CPU estão fora dele.
Não é um limite agregado de todos os alvos ou de todas as texturas de cena.
A CPU mantém no máximo N fragmentos por pixel, mas o armazenamento dos vetores
tem overhead próprio. Na extensão weighted OIT, a estimativa comum é
`width * height * 24 * samples`, independente da contagem configurada de peeling. Samples é 1 no perfil CPU; não há fallback CPU ou redução silenciosa de MSAA.

## Contrato das camadas

Selecionam-se as N profundidades transparentes distintas mais próximas por
pixel e compõem-se de trás para frente sobre a passagem opaca. O fundo opaco
permanece mesmo quando existem mais superfícies transparentes que camadas.
Fragmentos alpha zero ocupam profundidade e consomem uma camada; não encerram
a busca das camadas seguintes. Coincidências de profundidade constituem uma
camada, com o último fragmento vencedor no estado LEQUAL padrão. Não há epsilon
fixo para descartar superfícies próximas: vale a precisão do depth armazenado.

BGFX e wgpu usam cor intermediária RGBA16F e depth D32F, com composição final
RGBA8. wgpu usa máscaras R8 e snapshot da profundidade opaca. BGFX usa máscara
R16F por camada e amostra o attachment D32F da passagem opaca, também nos
produtores RTT diretos e na passagem intermediária de janela. Seus attachments
de camada somam 14 bytes por pixel, dentro da estimativa comum de 16. A CPU
arredonda a conversão para bytes, evitando perda acumulada por truncamento.
Não se promete paridade bit a bit, HDR de saída ou preservação de distâncias
menores que a precisão do attachment de profundidade.

O depth de saída padrão continua sendo o depth real da passagem opaca. Um
opaco sem escrita de profundidade não pode virar oclusor na reconstrução das
camadas. As oito funções capturadas são avaliadas contra essa profundidade
opaca, independentemente do teste LEQUAL interno que seleciona as camadas.
Teste desativado também desativa escrita de depth, mesmo com write=true.
A composição publica o depth da camada selecionada mais próxima que tenha
teste e escrita ativos; sem essa camada, conserva o depth opaco. A máscara
pertence ao fragmento vencedor, permitindo estados distintos no mesmo pixel.
CPU, wgpu e BGFX executam esses estados; a qualificação atual fica em C06 da
[matriz](coin-render-feature-matrix.md).

SoDepthBuffer não implementa override genérico por setOverride no CoinGL:
campos explícitos de um filho prevalecem; campos ignored herdam o estado atual.
A captura segue essa semântica. Os outros modos e anotações seguem P09.

A revisão privada wgpu atual é **59** (CoinWgpuFfi.h); C06 não muda essa ABI.
O histórico a seguir descreve o layout de orçamento introduzido na revisão 28
e mantido na revisão 41, anterior às ampliações seguintes. `CoinWgpuFrameView` passa de 160 para 176 bytes:
contagem em 160, reservado zero em 164 e orçamento uint64 em 168. Vértice de
100 bytes, estado de 2280 e draw de 56 permanecem. C++ e Rust devem ser
reconstruídos juntos; a ABI pública de libCoin não muda. O bridge aplica o limite conservador `width * height * (16 * layers + 8) * samples`
também para chamadas diretas da ABI privada. Os attachments concretos wgpu
somam `width * height * (13 * layers + 4) * samples`, antes do overhead do
alocador e dos alvos principais. Esse controle não reinterpreta o estado Coin.


## Peeling com MSAA — R27.7

**Perfil inclinado B48/B49 qualificado no Linux disponível:** a campanha final passa 119/119 execuções nas sete células, com 1×/2×/4×, oito comparadores, range/offset, RTT e janela. Um oráculo geométrico independente calibra os samples cobertos também nas bordas; OpenGL BGFX usa profundidade explícita por sample no opaco comum para evitar a divergência do caminho nativo nessa transição. Vulkan default preserva depth hardware. Restauração no mesmo perfil é exata; a comparação externa entre formatos/caminhos usa somente o limite explícito de quantização. Isso não encerra B34 nem os demais cruzamentos e plataformas da matriz.

O caminho nativo RGBA8 linear executa 1×/2×/4× sem resolver as camadas intermediárias. Cada sample seleciona suas próprias 1–8 profundidades distintas, testa as oito funções capturadas contra o snapshot opaco original e conserva a máscara de escrita do fragmento vencedor. Alpha zero continua consumindo camada; empate e truncamento mantêm o contrato acima. Cor e depth são compostos por sample antes do resolve final: cor por média, depth pelo mínimo. Mapas VSM permanecem single-sample. A qualificação por combinação/plataforma e as pendências atuais estão em R27.7 da [matriz](coin-render-feature-matrix.md); implementar o mecanismo não qualifica automaticamente todos os cruzamentos.

A seleção tipada `CoinRenderOptions::transparency=COIN_RENDER_TRANSPARENCY_PEELING` escolhe o mecanismo de source-over adiado. Para misturar ADD/DELAYED_ADD locais, o modo global da action deve permitir esses estados, por exemplo SORTED_OBJECT_BLEND. O modo global SORTED_LAYERS_BLEND preserva a regra nativa CoinGL de impor override de transparência; nós locais não o substituem. Essa distinção não altera a ordem dos aditivos quando seus estados locais são efetivos.

O preflight verifica formato, renderização/amostragem, contagem real de samples e orçamento antes de produtores RTT. BGFX também contabiliza as views do grafo inteiro; excesso retorna UNSUPPORTED sem publicar produtores. Janela e produtor RTT GPU-only mantêm depth apto a amostragem desde sua criação, sem readback adicional. No OpenGL, máscaras de cobertura emitidas pelo shader evitam contaminar o clear MRT da próxima camada com a máscara fixa do draw anterior; todos os samplers declarados recebem bindings com tipos corretos, inclusive slots de sombra inativos.

BGFX/Vulkan depende de sampleRateShading habilitado no dispositivo. O SDK stock usado nesta máquina o desabilita; nessa configuração o pedido 2×/4× continua recusado antes da codificação. A [extensão controlada e versionada](../cmake/patches/README.md) negocia a feature e publica a capacidade sem alterar layouts; seu uso exige recompilar o consumidor com os headers correspondentes. Não há exceção por fabricante nem ativação global de sample shading em todos os pipelines. O caminho wgpu consulta suas capacidades próprias. Async, demais formatos/mips, bump translúcido, highlight com OIT e demais cruzamentos mantêm seus critérios separados.

## Extensão weighted OIT

`COIN_BGFX_TRANSPARENCY=auto` preserva a modalidade Coin. A seleção explícita
`weighted_oit` acumula apenas source-over adiado da camada base; draws imediatos
e aditivos mantêm suas passagens. Aditivos adiados são compostos depois de OIT.
Essa ordem não promete equivalência para toda mistura arbitrária de modalidades
locais com o renderer GL.

Para alpha A e depth normalizado d, o peso é
`min(8, 8*A + .01) * min(16, 16*(1-d)^3 + .1)`. Acumulam-se RGB*A*peso e
A*peso; a revelação é o produto de `(1-A)`. A cor média ponderada usa opacity
`1-revelação` e source-over sobre a passagem opaca. É uma aproximação declarada,
não um substituto silencioso de SORTED_LAYERS_BLEND. CoinWgpu executa o mesmo cálculo com dois attachments e resolve separado.
A seleção tipada `CoinRenderOptions::transparency=COIN_RENDER_TRANSPARENCY_WEIGHTED_OIT` vale nos dois
executores; CPU continua rejeitando essa extensão.

## Qualificação local — 2026-09-28

`CoinRenderPeelingTest` usa cenas atravessadas pela action, fast path ligado e
desligado, 10 superfícies em ordens invertidas, materiais por face na mesma
shape e reconfiguração 1/2/4/8/1. Verifica truncamento, alpha 0/.0001/1/255/.5/
.9999/1, cadeias com alpha zero, interseção dentro da shape, superfícies quase
coplanares, saturação, ausência de halos, PHONG direcional com combine/textura,
oclusão opaca com e sem depth write, alpha final, depth real e rejeição/recuperação
de configurações e orçamento. RGB/alpha usam expectativas numéricas independentes
com tolerância de seis níveis de byte; depth usa tolerância .003.

A comparação GL obrigatória usa seis superfícies transparentes e oito passes,
verificando que o GL manteve SORTED_LAYERS_BLEND sem fallback. O GL legado conta
a passagem opaca no orçamento de passes e pode perder esse fundo ao truncar;
o perfil comum conserva a base opaca e usa N camadas transparentes. Por isso o
truncamento de dez superfícies usa o oráculo numérico, não o resultado GL legado.
As exceções de textura/combine e anotações da referência constam do
[contrato P09](coin-render-transparency-contract.md).

Weighted OIT tem matriz numérica própria de muitas superfícies, alpha extremo,
ordem/material por face, luz/textura, oclusão, alpha final e mistura com ADD e
DELAYED_ADD. O perfil local é offscreen RGBA8 sob Xvfb, CPU/wgpu e BGFX
Vulkan/OpenGL, com referência GL Mesa. A regressão BGFX Release passou
**52/52 CTest**, incluindo peeling e weighted OIT nos dois renderers. A campanha
final wgpu Debug passou **23/23 CTest**, incluindo o orçamento no reuso após
resize. Nenhuma das duas campanhas retornou skip. Rust offline passou
**13 testes unitários e dois testes de shaders**.

O [contrato P11](coin-render-selection-contract.md) oferece capacidades e seleção tipadas. FreeCAD, drivers
adicionais, MSAA, polygon offset/clamp e viewports parcialmente externos exigem
suas campanhas próprias. A rota direta BGFX reutiliza os compositores com anexos próprios;
peeling/OIT staged e direct agora estão qualificados com sombras no perfil
[P27.4](coin-render-p27-shadows.md). Isso não encerra todos os perfis de P02.

## Qualificação com sombras — 2026-10-02

Peeling e weighted OIT executam com 1–8 mapas no perfil PHONG de P27.4,
com material, textura alfa e RTT ALPHA_BLEND staged/direct.
`CoinRenderShadowReferenceTest --shadow-oit N M` usa M=1 para peeling e M=2
para weighted OIT, com referência GL obrigatória para a recepção de sombras.
`CoinRenderPeelingTest --shadow N M` mede independentemente a composição
de seis superfícies, ordem invertida, materiais por face, truncamento 2/8,
oclusão e rejeição/recuperação do orçamento. Weighted OIT permanece aproximação
explícita; seu oráculo numérico não é SORTED_LAYERS_BLEND do GL.

BGFX usa entradas distintas da paleta para limpar VSM e os anexos de
transparência. Sua variante de oito mapas retira samplers de unidades da cena
proibidas pelo perfil, preservando o limite de dezesseis samplers de reflexão.
O shader de sombras Coin/GL agora executa o descarte pela profundidade da
camada anterior quando substitui o programa ARB de peeling; a fixture seleciona
SORTED_LAYERS_BLEND na action GL e rejeita fallback. Oito mapas GL nativos
continuam pendentes em outro contexto, conforme o tracker de plataformas.

## C06 — depth explícito no Linux disponível

Qualificação concluída em 18/10/2026 nas sete células AMD/NVIDIA: BGFX
Vulkan/OpenGL e wgpu Vulkan/AMD OpenGL. Os testes independentes
`CoinRenderPeelingDepthStateTest` e `CoinRenderPeelingDepthWindowTest` passam
56/56 processos nas políticas Native e CANONICAL_8BIT. São 384 casos por
célula/política: oito funções, teste/escrita ligados e desligados, 1/2/4 camadas,
ordem normal/inversa e escrita opaca ligada/desligada. Controles adicionais
verificam estados distintos por fragmento, herança por campos ignored, range,
alpha final, anotações e preservação de RGBA/depth/serial em recusa e recuperação.

RTT staged/direct consome o resultado do produtor; no direto, outro target
opaco permanece vivo e íntegro durante publicação, recusa e recuperação.
A inspeção de capacidade depende da inicialização do próprio backend, não
somente da existência do runtime compartilhado. O candidato anterior reproduziu
a recusa prematura. Janela usa XGetImage físico com carimbo por frame, transições
opaco/peeling/opaco, resize 64→96→65→64 e serial crescente, sem depender de
readback da swapchain. As políticas efetivas são conferidas nos targets.

O baseline canônico nas mesmas entradas passa nas 12 execuções wgpu e reprova
nas 16 BGFX por recusa dos estados admitidos; o candidato passa todas as 28.
As 63 regressões proporcionais também passam, incluindo peeling/OIT, depth,
RTT e sombras de um, quatro e oito mapas. Quatro mutações dirigidas foram
detectadas: ignorar a função capturada, omitir escrita transparente, repetir
a primeira camada e escrever depth com teste desativado. O candidato restaurado
passou a campanha final. Esse perfil não qualifica outros formatos/recursos
de C09/C17, wgpu/NVIDIA OpenGL indisponível nem as plataformas externas V.
