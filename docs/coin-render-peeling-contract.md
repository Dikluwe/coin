# P10 — camadas limitadas e weighted OIT

CoinRender oferece peeling configurável de **1 a 8 camadas**, com padrão de
quatro. CPU, CoinBgfx e CoinWgpu compartilham a política sobre o frame capturado;
cada Infra executa seu mecanismo. Weighted OIT é uma extensão explícita do
BGFX. O perfil wgpu oferece peeling; não há seleção automática de weighted OIT.

## Donos e configuração

Wiring expõe `CoinRenderAction::setSortedLayersNumPasses(int)` e
`setTransparencyBufferBudget(uint64_t)`, com getters correspondentes. Captura
as opções no plano e as propaga para actions filhas de `SoSceneTexture2`.
Alterar uma opção invalida o reuso do plano, inclusive camera patch.

`CoinRenderTransparencyCore.h` é o dono dos limites e do cálculo conservador.
O orçamento padrão é **256 MiB por pedido de attachments de transparência**.
Peeling exige `width * height * (16 * layers + 8)` bytes; os produtos usam
checagem de overflow. Contagem fora de 1..8 ou orçamento zero são inválidos.
Orçamento insuficiente retorna `UNSUPPORTED` antes da alocação/publicação,
preservando imagem e serial. Um pedido válido posterior volta a funcionar.

Esse orçamento não mede toda a memória do dispositivo: geometria, texturas,
alvos principais, tickets, overhead do driver e alocadores CPU estão fora dele.
Não é um limite agregado de todos os alvos ou de todas as texturas de cena.
A CPU mantém no máximo N fragmentos por pixel, mas o armazenamento dos vetores
tem overhead próprio. Na extensão BGFX weighted OIT, a estimativa comum é
`width * height * 24`, independente da contagem configurada de peeling.

## Contrato das camadas

Selecionam-se as N profundidades transparentes distintas mais próximas por
pixel e compõem-se de trás para frente sobre a passagem opaca. O fundo opaco
permanece mesmo quando existem mais superfícies transparentes que camadas.
Fragmentos alpha zero ocupam profundidade e consomem uma camada; não encerram
a busca das camadas seguintes. Coincidências de profundidade constituem uma
camada, com o último fragmento vencedor no estado LEQUAL padrão. Não há epsilon
fixo para descartar superfícies próximas: vale a precisão do depth armazenado.

BGFX e wgpu usam cor intermediária RGBA16F e depth D32F, com composição final
RGBA8. wgpu usa também máscaras R8 e snapshot da profundidade opaca. A CPU
arredonda a conversão para bytes, evitando perda acumulada por truncamento.
Não se promete paridade bit a bit, HDR de saída ou preservação de distâncias
menores que a precisão do attachment de profundidade.

O depth de saída padrão continua sendo o depth real da passagem opaca. Um
opaco sem escrita de profundidade não pode virar oclusor na reconstrução das
camadas. CPU/wgpu executam os overrides qualificados de escrita transparente,
teste desativado e GREATER. BGFX limita a camada base a test=true, write=false,
LESS/LEQUAL/NEVER: outros estados são rejeitados antes da submissão, preservando
o resultado anterior. Os overrides dos outros modos e anotações seguem P09.

A revisão privada wgpu é **28**. `CoinWgpuFrameView` passa de 160 para 176 bytes:
contagem em 160, reservado zero em 164 e orçamento uint64 em 168. Vértice de
100 bytes, estado de 2280 e draw de 56 permanecem. C++ e Rust devem ser
reconstruídos juntos; a ABI pública de libCoin não muda. O bridge verifica sua
alocação física `width * height * (13 * layers + 4)` também para chamadas diretas
da ABI privada. Esse controle concreto não reinterpreta o estado Coin.

## Extensão weighted OIT BGFX

`COIN_BGFX_TRANSPARENCY=auto` preserva a modalidade Coin. A seleção explícita
`weighted_oit` acumula apenas source-over adiado da camada base; draws imediatos
e aditivos mantêm suas passagens. Aditivos adiados são compostos depois de OIT.
Essa ordem não promete equivalência para toda mistura arbitrária de modalidades
locais com o renderer GL.

Para alpha A e depth normalizado d, o peso é
`min(8, 8*A + .01) * min(16, 16*(1-d)^3 + .1)`. Acumulam-se RGB*A*peso e
A*peso; a revelação é o produto de `(1-A)`. A cor média ponderada usa opacity
`1-revelação` e source-over sobre a passagem opaca. É uma aproximação declarada,
não um substituto silencioso de SORTED_LAYERS_BLEND. O orçamento fixo e o teste
numérico da extensão não transformam weighted OIT em capacidade do wgpu.

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
suas campanhas próprias. A rota direta BGFX de textura de cena ainda usa OBJECT;
sua ampliação pertence a P12/P13. Este fechamento não qualifica peeling BGFX
nessa rota direta, nem encerra o contrato integral de todas as luzes de P02.
