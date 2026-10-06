# CoinRender — P12: plano e ownership de RTT

A execução e publicação deste contrato foram qualificadas em
[P13](coin-render-rtt-publication-contract.md), incluindo correção da política
Coin NONE e suporte a ALPHA_BLEND. A evidência abaixo registra a entrega P12.

P12 fecha o planejamento comum e o ownership no perfil existente de
`SoSceneTexture2`: unidade 0, RGBA8, MODULATE, REPEAT/CLAMP, função de
transparência NONE, sem `sceneTransparencyType`, dimensões de 1..2048 por eixo,
até oito níveis e orçamento declarado de 64 MiB por apply. Não encerra F14,
publicação/tickets de P13, recuperação geral de P14 ou a matriz física de P20.

## Contrato Coin e referência GL

`SoSceneTexture2` fornece uma cena produtora e aplica seu resultado como uma
textura às geometrias subsequentes. Fundo, tamanho, sampler e revisão da cena
fazem parte da captura. Qualidade zero e override de imagem impedem a captura
do produtor e seu débito no orçamento.

O caminho GL em `src/nodes/SoSceneTexture2.cpp` usa FBO/`SoGLImage` ou contexto
pbuffer e, quando necessário, readback. A criação, reconstrução e exclusão
estão associadas ao contexto de cache GL; a exclusão pode ser agendada pelo
`SoGLCacheContextElement`. Essa associação é o comportamento a preservar:
resultado, produtor e contexto vivo devem corresponder. Um token, framebuffer,
pbuffer ou contexto GL concreto não constitui o contrato comum.

P12 não amplia formatos, estados ou modelos do nó. BGFX e wgpu mantêm mecanismos
próprios e recebem os mesmos snapshots e dependências.

## Responsabilidades e representação

- **Wiring:** lê o nó/`SoState`, captura a revisão Coin (`getNodeId()`), aplica a
  action filha somente para capturar sua cena e entrega o grafo ao executor.
  Publica o resultado da action conforme seu status. Não prepara backends,
  cria texturas GPU, transforma IDs em tokens ou libera tokens RTT.
- **Core:** `CoinRenderRttPlan` define ordem topológica, identidade de produtor,
  deduplicação, dimensões, orçamento e rejeição de ciclos/referências inválidas.
  `CoinRenderRttResources` verifica owner/device/gerações e resolve descrições.
  O Core não atravessa a cena nem chama a GPU.
- **Infra:** `CoinRenderRttExecution` conduz os passes a partir do plano e retém
  seus resultados até a submissão do consumidor. `CoinWgpuBackend` e
  `CoinBgfxBackend` implementam os hooks de domínio, submissão direta e
  aposentadoria. A exigência BGFX de liberar o backend pai antes dos filhos
  staged também está na execução, fora da action.
- **Shell:** continua responsável por defaults textuais e apresentação de
  diagnósticos; as opções tipadas de P11 chegam aos filhos sem nova interpretação.

`producerId` é uma referência lógica, numerada a partir de 1, válida somente
no grafo capturado de um apply. Zero indica uma imagem sem dependência RTT.
A referência nunca é um ponteiro de nó ou token GPU. Cada produtor tem tamanho,
revisão da origem Coin e um `CoinRenderFramePlan` próprio. As dependências são
as referências de textura dos planos; só podem apontar para produtores anteriores.

A mesma origem/revisão, dimensões e payload pode compartilhar um produtor.
Origens distintas recebem identidades distintas, mesmo com pixels equivalentes.
Mudança de revisão ou payload exige outro produtor. Reuso físico de conteúdo
entre origens poderá ser implementado na Infra sem alterar essa identidade.

A captura mantém `gpuToken=0`. A execução resolve uma cópia do plano em pixels
staged ou em um handle opaco do conector. O campo `gpuToken` permanece privado,
apenas nessa cópia de execução e na adaptação para a ABI existente. O Core não
interpreta seu valor. FFI wgpu, lowering BGFX e raster CPU rejeitam referências
lógicas que não tenham sido resolvidas.

## Owner, dispositivo, geração e retenção

Cada alvo recebe um owner monotônico no processo, independente do seu endereço.
Recording sem alvo recebe um owner próprio por escopo. Cada resultado retido
registra ID lógico, revisão do produtor, owner, geração do alvo, domínio de
dispositivo e geração do dispositivo.

- Resize avança a geração de recursos do alvo, separada da geração pública dos
  tickets. Perda do dispositivo durante a execução do alvo também a invalida.
- BGFX identifica seu runtime compartilhado e avança a geração a cada nova
  inicialização. Os recursos diretos continuam pertencendo ao backend do alvo.
- O executor C++ wgpu usa o dispositivo default do bridge. Uma consulta privada,
  sem inicialização, informa seu epoch vivo; zero significa indisponível/perdido.
  Dispositivos extras da API FFI não passam a ser selecionáveis por alvo CoinRender.
- Resultados staged retidos são bytes CPU; seu domínio GPU é zero. O dispositivo
  do produtor não precisa permanecer vivo depois do readback.

A resolução rejeita outro owner, outro dispositivo, resize, nova geração ou
resultado ausente, preservando a saída do chamador. Captura e resolução não
alteram o plano original. A tabela é invalidada ao encerrar o escopo.

Os tokens retornados, inclusive uma alocação parcial retornada junto com erro,
são retidos pelo executor. A liberação ocorre em todos os caminhos de saída,
depois de submeter o consumidor ou abandonar a execução. wgpu aposenta as
texturas conforme o serial/fila e tolera a liberação de uma geração descartada.
BGFX marca recursos usados como reutilizáveis e remove os que não foram usados;
destruir/reconstruir seu backend invalida o cache concreto. Aplicar uma cena sem
RTT também encerra o conjunto anterior de recursos BGFX.

`applyAsync` conserva a mesma duração dos recursos do produtor: a submissão
GPU retém o que precisa até completar. Tickets e publicação continuam sob seus
contratos existentes; o escopo RTT não transfere ponteiros de action/planos aos
callbacks.

O cache de captura da action é conservadoramente desabilitado quando o grafo
contém RTT, staged ou direto. Uma referência de um apply não é reutilizada no
grafo do apply seguinte. Caches de textura/geometria e recursos persistentes
específicos dos executores continuam disponíveis. O último frame staged pode
reter pixels CPU para o log Recording refletir o alpha realmente resolvido;
nenhum token GPU é retido no último plano da action.

## Preflight e alpha staged

Toda a cena produtora é capturada antes de executar qualquer produtor, também
no staged. O Core valida o grafo, dimensões, orçamento e os planos de perfil
antes do primeiro submit. Falha conhecida de captura/perfil não publica o pai
nem executa os produtores já capturados.

No Core, uma imagem com política automática de alpha ainda pode depender de
readback para classificação. O preflight adia sua seleção de mecanismo até
resolver pixels, preservando conflitos sem fallback. P13 corrigiu a captura de
`SoSceneTexture2` para usar a política explícita do Coin: NONE ignora alpha da
textura no scheduling; ALPHA_BLEND o força. A inferência original de P12 pelos
pixels não era suficiente para esse nó. A fixture staged foi corrigida em P13.

Falhas após executar produtores continuam possíveis; publicação transacional,
limites concretos e tickets são qualificados no [P13](coin-render-rtt-publication-contract.md).
Viewport parcialmente externo wgpu é convertido no Core em P04; combinações
RTT ampliadas e recuperação geral permanecem abertas no
[perfil de geometria/viewport](coin-render-geometry-viewport-contract.md). BGFX
direto segue OBJECT; peeling/OIT dentro do produtor direto continua fora do perfil.

O orçamento continua sendo um débito contratual: staged cobra quatro bytes por
pixel por ocorrência; direto cobra oito bytes por pixel por produtor distinto.
Não mede picos reais de memória, cópias temporárias ou memória GPU do driver;
essa instrumentação pertence a P18.

## Evidência e fechamento

`CoinRenderRttOwnershipTest` cobre IDs, revisão/payload, isolamento de origens,
ordem topológica, ciclos, limite de oito níveis, dimensões, 64 MiB, owner/device,
resize/geração, invalidação, captura imutável e resolução que preserva a saída.
Um backend de fixture verifica dependências resolvidas, retenção até o consumidor,
liberação de alocação parcial, interrupção na troca de geração e ausência de
submit antes de rejeitar um grafo inválido. Uma verificação GPU em wgpu e BGFX
Vulkan/OpenGL garante que dois payloads distintos da mesma origem conservem
outputs independentes: BGFX não reutiliza um framebuffer ainda marcado em uso.
Também rejeita direto em janela e
Recording sem fallback staged.

`CoinRenderSceneTextureTest` verifica resultados, orientação, alpha, mutação,
nesting, troca de alvo, resize e recuperação. A verificação do serial global
antes de rejeitar o segundo produtor cobre staged e direto. A fixture de alpha
staged cobre clear transparente totalmente coberto por geometria opaca, conflito
quando a saída passa a ser translúcida e recuperação. O wgpu mantém os casos de
perda/falha intermediária e ausência de vazamento, além dos testes async/stress.
`CoinRenderSceneTextureBudgetTest` cobre o orçamento e produtores compartilhados.

A matriz CTest BGFX agora inclui explicitamente staged/direto em Vulkan e
OpenGL, para as fixtures de resultado e orçamento. Os registros locais de build
são `/tmp/coin-p12-{wgpu,bgfx,recording}-build.log`; as campanhas ficam em
`/tmp/coin-p12-{wgpu,bgfx}-regression.log` e
`/tmp/coin-p12-recording-tests.log`. São evidência da máquina desta execução,
sem extrapolação para outros drivers, dispositivos ou plataformas.


Validação desta entrega: campanha wgpu **29/29** (63,42 s), campanha BGFX
**67/67** (213,09 s), mais dois casos explícitos de ownership Vulkan/OpenGL e
a verificação final dos cenários afetados após a retenção do
snapshot staged; raster RECORDING/CPU **3/3** (0,42 s); Rust offline **15 testes
unitários e 2 de shaders**, todos aprovados. As campanhas GPU usam referência
Coin/GL obrigatória para suas fixtures de comparação, Mesa/GLX e Xvfb; as
fixtures de ownership/RTT também têm expectativas independentes de estado,
serial, retenção e pixels.

A primeira campanha BGFX teve 66 aprovações e uma falha de inicialização EGL
no readback OpenGL; uma campanha completa em Xvfb novo passou. Não houve skip
nas campanhas GPU nem nos três testes finais CPU. A consulta Selection de P11
foi executada separadamente no build CPU e encerrou com skip na fase que exige
GPU; esse caso não é contado como qualificação GPU do perfil RECORDING.
Registros da revisão final de logging: `/tmp/coin-p12-wgpu-final-focused.log`,
`/tmp/coin-p12-bgfx-final-focused.log` e
`/tmp/coin-p12-recording-final-tests.log`.

A revisão de retenção GPU está em `/tmp/coin-p12-bgfx-retention.log` e
`/tmp/coin-p12-wgpu-retention.log`. A primeira verificou 13 casos e teve três
falhas de inicialização EGL nos casos OpenGL de RTT; a repetição em Xvfb novo,
com os renderers explícitos da fixture de ownership, passou 7/7 em
`/tmp/coin-p12-bgfx-owner-renderers.log`. Esse diagnóstico intermitente é uma
limitação observada da campanha EGL/Xvfb, não uma qualificação de estabilidade
do driver. Os 69 casos BGFX distintos incluem a campanha 67/67 e os dois novos
casos de ownership, com as revisões afetadas verificadas novamente. A política de troca de alvo
existente foi encaminhada de Wiring para o target, preservando o comportamento
BGFX e invalidando sua geração de recursos quando o backend é liberado.

Na última revisão, a verificação wgpu de RTT/async passou **11/11** (12,45 s).
A verificação BGFX de RTT, seleção, log staged, readback e múltiplas janelas
passou **25/25** (61,43 s), antes da correção adicional de retenção, cujo gate e
repetição estão detalhados acima. Nenhum desses resultados amplia P14/P20.

## Continuação P14

A recuperação e o orçamento entre alvos do perfil experimental atual foram
qualificados no [contrato P14](coin-render-multi-target-contract.md). RTT staged
BGFX agora mantém o consumidor preparado durante os filhos; perda compartilhada
aposenta conectores e staging de tickets sem bloquear a próxima geração. As
ressalvas acima descrevem o fechamento original P12 e sua campanha histórica.
