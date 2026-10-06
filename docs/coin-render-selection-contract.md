# P11 — opções, mecanismos e capacidades

CoinRender separa os fatos de hardware, os mecanismos implementados e a
evidência de qualificação. A política mecânica fica em
`CoinRenderSelectionCore.h`; Wiring captura as escolhas no plano, Infra fornece
os fatos e executa o mecanismo, e Shell interpreta os defaults textuais.

## Configuração tipada

As fábricas de `CoinRenderTarget` aceitam `CoinRenderOptions` e disponibilizam
`getOptions()`. As opções são imutáveis durante a vida do alvo:

```cpp
CoinRenderOptions options;
options.renderer = COIN_RENDER_RENDERER_VULKAN;
options.transparency = COIN_RENDER_TRANSPARENCY_COIN;
options.sceneTexture = COIN_RENDER_SCENE_TEXTURE_STAGED;
auto * target = CoinRenderTarget::createOffscreen(SbVec2i32(640, 480), options);
```

Sem opções explícitas, Shell lê os defaults **na criação do alvo**:
`COIN_BGFX_RENDERER`, `COIN_BGFX_TRANSPARENCY` e `COIN_RENDER_RTT_GPU_DIRECT`
(com alias legado wgpu para RTT). Renderer/transparência BGFX são interpretados
apenas nesse build. Valores inválidos são rejeitados na entrada da action, antes da travessia e dos
subpasses; a action não interpreta
strings de ambiente. As fábricas com opções explícitas ignoram esses defaults,
inclusive valores textuais inválidos. Mudar o ambiente não reconfigura alvos
existentes: criar outro alvo e desvincular a action antes de destruir o anterior.
Controles de profiling, falhas injetadas, caches e pipeline de readback continuam
específicos de Infra e suas campanhas P18/P19; não decidem modalidades Coin.

Renderer UNKNOWN usa o default do conector. BGFX escolhe Vulkan ou OpenGL. No
wgpu, um renderer explícito deve coincidir com o adaptador ativo; a API não
troca esse runtime compartilhado nem aplica fallback para outro renderer.

| Transparência | Regra |
|---|---|
| COIN | Preserva a modalidade Coin capturada; não escolhe OIT pelo número de draws. |
| OBJECT | Executa as modalidades de objetos; rejeita SORTED_LAYERS_BLEND. |
| PEELING | Extensão explícita que usa camadas para source-over adiado da camada base. |
| WEIGHTED_OIT | Aproximação explícita BGFX/wgpu para esse subconjunto de draws; CPU rejeita quando exigida. |

Passagens imediatas, aditivas e anotações conservam o contrato P09. Sem draws
elegíveis, o caminho de objetos é suficiente; escolher uma extensão não exige
alocar seus attachments para um frame opaco. Peeling conserva o orçamento e
os limites P10. O modo integra a igualdade/invalidação do plano. Os alvos filhos
staged e diretos herdam as opções do pai. A rota direta BGFX reutiliza peeling/OIT com attachments privados, qualificados
com sombras em P27.4.

## Capacidades versão 3

`coin_render_query_capabilities` aceita os tamanhos exatos V1/V2 e devolve o
prefixo correspondente, sem escrever além dele. V3 acrescenta:

- `known_hardware_facts` e `known_formats`: distinguem campo desconhecido de
  recurso ausente. Renderer, IDs, formatos, limites e features vêm do probe
  nativo. D24S8 não é conhecido no wgpu; Depth24PlusStencil8 não certifica esse
  formato físico. Zero só significa ausência quando o bit de conhecimento está presente.
- `implemented_mechanisms`: o que o conector implementa, independentemente do
  adaptador. CPU oferece objetos e peeling; BGFX/wgpu acrescentam weighted OIT.
- `available_mechanisms`: mecanismos executáveis após o probe. No wgpu, formatos
  usados na execução consideram as features habilitadas no device, separadas
  das possibilidades físicas do adaptador.
- `qualified_profile_mechanisms` e `qualified_profiles`: evidência dos perfis
  delimitados P08/P09/P10 por conector, renderer e rota offscreen. Não certificam
  um novo device/driver, todo estado Coin ou uma janela real. Consultas de janela
  mantêm essas máscaras zeradas; seu probe verifica adaptador, não apresentação.
- `max_peel_layers`: limite implementado de oito, distinto da disponibilidade.

`features` continua descrevendo o perfil compilado legado; não é uma máscara
de hardware nem prova de qualificação. O máximo de unidades de textura foi
corrigido para oito também no wgpu/CPU. O bit
`COIN_RENDER_FEATURE_PROCEDURAL_TEXTURE_COORDINATES` descreve o
[perfil inicial P07](coin-render-p07-procedural-textures.md), sem alterar os
tamanhos V1/V2/V3 ou certificar todos os geradores/devices. `gpu_available` não demonstra suporte
a weighted OIT, a depth overrides ou a um estado particular de RTT.

O probe BGFX ignora o modo de transparência para medir hardware; um texto
inválido ou requisitos de uma extensão não escondem o adaptador. Um runtime
ocupado por outro renderer/thread retorna BUSY. A variante
`coin_render_query_capabilities_for_renderer` permite consulta explícita,
independente dos defaults. A sonda continua podendo inicializar o runtime e
alocar recursos; não é uma operação de apresentação em uma janela real.

## Seleção e rejeição

`coin_render_select_mechanism(caps, mechanism, require_qualified_profile)` é
pura, pede um mecanismo e devolve `CoinRenderSelection`: mecanismo, motivo e
evidência de perfil. Os motivos distinguem pedido inválido, implementação
ausente, hardware indisponível, probe ainda sem resposta e perfil não qualificado.
Qualificação é uma condição opcional para a consulta: não é inferida da presença
de hardware. A escolha de modalidade capturada e o conflito OBJECT/layers usam
o mesmo Core; BGFX adapta essa decisão aos nomes concretos de seu executor.

Não há substituição silenciosa. A action retorna UNSUPPORTED para pedidos fora
do mecanismo implementado, preservando a imagem/serial e aceitando um pedido
válido posterior. Essa API estrutura rejeições de seleção; não converte todos
os erros de travessia, recursos e device loss em um único enum de capacidades.
O chamador ainda precisa observar o status do alvo e o resultado de apply.

## Evidência local — 2026-09-28

`CoinRenderSelectionTest` verifica seleção com fatos sintéticos independentes,
precedência de pedidos inválidos, runtime ocupado, versão e limites, probe
concreto, fronteiras de escrita V1/V2, ausência de qualificação de janela,
renderer explícito, modos tipados, RGB produzido, conflito sem publicação,
recuperação e opções inválidas. BGFX testa também probe independente de texto
inválido de transparência e precedência das opções explícitas sobre o ambiente.

A campanha usa CPU e wgpu Debug, BGFX Release Vulkan/OpenGL sob Xvfb e os
mesmos contratos numéricos P08/P09/P10. wgpu passou **28/28 CTest**, incluindo
RTT staged/direto, orçamento aninhado e async com perda de dispositivo. Rust
offline passou **15 testes unitários e dois testes de shaders**; o header público
de capacidades também passou na compilação C. A campanha BGFX teve 58 casos:
56 passaram na execução inicial. Duas fixtures de superfície ainda pediam
SORTED_LAYERS_BLEND com OBJECT forçado e esperavam a conversão antiga. Elas
passaram a pedir SORTED_OBJECT_BLEND para esse mecanismo; a rejeição de
OBJECT/layers continua no teste P11. A repetição dos dois casos passou: os
**58 casos BGFX estão aprovados**, incluindo Vulkan/OpenGL, superfícies e RTT.
Nenhum caso das duas campanhas retornou skip. Outros drivers, plataformas, estados
de RTT e qualificação física seguem P12–P14/P20–P23.

A campanha ampliada corrigiu a herança de opções nas actions filhas que
planejam RTT sem alvo próprio. O orçamento direto aninhado volta a contar
attachments diretos. A fixture antiga de pai inválido usava um viewport menor
que o alvo, que é válido; agora usa uma modalidade Coin desconhecida após a
captura do produtor. Viewport externo convertido para coordenadas negativas
pode ser rejeitado pelo wgpu após submits de produtores: o preflight completo
desse caso continua em P04/P13, sem ser certificado pelo fechamento P11.
