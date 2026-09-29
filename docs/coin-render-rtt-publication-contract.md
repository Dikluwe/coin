# CoinRender — P13: execução RTT e publicação

P13 qualifica o grafo de P12 no perfil RGBA8 existente, corrige a política de
alpha do Coin e protege a publicação de resultados. O fechamento é deste perfil;
F14/F15 completos e outros formatos continuam no plano. Recuperação entre alvos
do perfil atual foi qualificada depois no [P14](coin-render-multi-target-contract.md).

## Contrato Coin e referência GL

`SoSceneTexture2` produz uma imagem aplicada às geometrias seguintes. O caminho
GL pode usar FBO, pbuffer direto ou readback/upload. Esses mecanismos são
específicos da GPU; cena, tamanho, revisão, sampler, política de transparência e
resultado pertencem ao contrato Coin.

A leitura de `SoSceneTexture2.cpp`, nas flags de `SoGLImage`, revelou uma correção
necessária sobre P12: `NONE` força transparência falsa para a textura, enquanto
`ALPHA_BLEND` força transparência verdadeira. O alpha dos pixels continua
participando da amostragem. A transparência do material também continua válida
em MODULATE. Portanto, pixels translúcidos em `NONE` não bastam para provocar
blending no consumidor, e uma textura opaca em `ALPHA_BLEND` continua sendo
classificada como transparente. A decisão depende da modalidade Coin do draw.

Wiring captura `sceneTransparencyFunction`; o Core de composição interpreta a
política uma única vez. O builder, a igualdade de payload/reuso e a resolução de
RTT preservam esse campo. BGFX, wgpu, raster CPU e log Recording consomem a mesma
decisão. A prova física `gpuOpaque` permanece separada da política de scheduling.
Nenhum layout FFI ou número de protocolo foi alterado.

## Perfil e execução

| Item | Contrato qualificado |
| --- | --- |
| Captura | Unidade 0, RGBA8, MODULATE, REPEAT/CLAMP, NONE ou ALPHA_BLEND; cena não nula, sem sceneTransparencyType |
| Dimensões/grafo | 1..2048 por eixo, oito níveis, ciclos e referências futuras/ausentes rejeitados |
| Orçamento declarado | 64 MiB por apply; staged: quatro bytes/pixel por ocorrência; direto: oito por produtor distinto |
| Staged | Readback RGBA8 com origem superior, uma inversão para imagem Coin com origem inferior, upload pelo backend consumidor |
| Direto | Textura GPU retida até submeter o consumidor; orientação é responsabilidade do conector; requer alvo GPU offscreen |
| BGFX direto | Produtor com mecanismo OBJECT; necessidade de peeling/OIT é rejeitada no preflight |
| wgpu direto | Até 64 texturas retidas por dispositivo; preflight consulta a capacidade restante do dispositivo default, sem inicializá-lo e sem somar dispositivos extras |
| Fora do perfil | ALPHA_TEST, formatos RGB/float/depth, outros modelos/estados e direto de janela têm rejeição explícita; isso não equivale a suporte |

Infra pode inspecionar o grafo imutável antes de criar o dispositivo ou submeter
produtores. O preflight comum verifica planos/dependências/orçamento; o hook de
cada conector verifica seus limites concretos. O wgpu rejeita viewports usados
fora do alvo antes de executar qualquer produtor, em ambos os modos RTT. A
implementação de viewport parcialmente externo continua em P04.

Uma imagem comum com política automática de alpha ainda pode exigir resolução
staged antes de escolher o mecanismo. `SoSceneTexture2` usa sua política explícita
Coin; o alpha de seus pixels não substitui essa política. Falhas de alocação,
readback, runtime ou dispositivo podem ocorrer depois de trabalho GPU; nenhum
contrato promete ausência universal de submissões antes de falhar.

## Publicação transacional e tickets

`CoinRenderTargetP` é o dono da publicação de resultados. Backends escrevem em
buffers candidatos. Cor, depth habilitado, serial e revisão validada são
publicados juntos apenas após sucesso e conferência das dimensões completas.
Falha de preflight, prepare ou submit preserva a imagem anterior e as alocações
publicadas, inclusive o ponteiro de `borrowRGBA`. Status e diagnóstico refletem
a falha; gerações de recursos/dispositivo continuam avançando quando necessário.

Os buffers candidatos reutilizam armazenamento por alvo, sem copiar a imagem
anterior para fazer rollback. Isso mantém até um par adicional de buffers CPU:
quatro bytes/pixel de cor e, quando alocado, quatro de depth. Resize libera esse
armazenamento adicional. Esse custo não é incluído no débito nominal de RTT;
medição de pico pertence a P18; a admissão compartilhada é definida no [P14](coin-render-multi-target-contract.md).

A submissão async escreve primeiro em um ticket privado. O contrato comum confere
token, serial, tamanho, formato RGBA8, pitches e bytes de cor/depth antes de
entregá-lo ao chamador. Pitches descrevem staging e podem ter padding; bytes
publicados descrevem vetores compactos. O Core confere linhas suficientes e
alinhadas aos componentes; cada conector confere seu layout exato no polling. Em falha o ticket público permanece vazio; uma alocação
retornada junto com erro é cancelada. BGFX faz sua última checagem de runtime
antes de publicar o readback interno, e retorna ao dono comum um ticket parcial
que precise ser aposentado.

Uma submissão async bem-sucedida invalida o readback síncrono anterior. O ticket
retém sua imagem independentemente da action/alvo, até polling bem-sucedido ou
cancelamento. Poll inválido, pendente ou de geração perdida preserva os vetores
do chamador. Esse contrato e os metadados públicos continuam compatíveis com P12.

## Checklist de fechamento

- [x] Coin/GL estudado; NONE/ALPHA_BLEND corrigidos no Core comum.
- [x] Mesma captura/grafo staged e direto; ownership e retenção de P12 mantidos.
- [x] RGBA8 e orientação superior/inferior qualificadas nos executores.
- [x] Dependências, ciclos, orçamento e limites concretos em preflight.
- [x] Falhas tardias preservam cor, depth, serial, revisão e ponteiros publicados.
- [x] Resultado incompleto é rejeitado mesmo com status SUCCESS do backend.
- [x] Tickets completos, falha async, consumo/cancelamento e retenção testados.
- [ ] Formatos/estados ampliados e ALPHA_TEST: F14 continua parcial.
- [x] Múltiplos alvos, admissão e reconstrução no perfil experimental atual: [P14](coin-render-multi-target-contract.md). Matriz física e memória total permanecem P20/P18.
- [x] Profiling de passagens, recursos próprios e reuso/readback em pipeline: [P18](coin-render-p18-profiling.md) e [P19](coin-render-p19-reuse-readback.md).
- [ ] Leitura explícita de pixels de janela e memória total do driver: F15/qualificação futura.
- [ ] Matriz física de drivers/plataformas: P20.

## Evidência local

`CoinRenderPublicationTest` injeta escrita parcial antes de seis códigos de
falha, além de SUCCESS com buffer incompleto e recuperação. Verifica identidade
de alocação e dados, serial e revisão. Executa nos três builds.
`CoinRenderRttOwnershipTest` verifica a política de alpha independentemente dos
pixels, sua preservação na resolução e os limites específicos sem GPU iniciada.
`CoinRenderSceneTextureTest` verifica NONE/ALPHA_BLEND, orientação, mutação,
dependências e recuperação em staged/direto. No BGFX também compara pixels de
RTT async/sync e injeta perda depois da criação do ticket, exigindo aposentadoria
e recuperação sem vazamento de referência do runtime. `CoinRenderAsyncActionTest` inclui
rejeição de formato que preserva cor/depth/serial/borrow e deixa o ticket vazio;
continua cobrindo tickets sobrepostos, cancelamento, lifetime e perda de geração.

Validação local em Linux/X11/Xvfb:

| Campanha | Resultado |
| --- | --- |
| wgpu — composição, estados, reuso, RTT, publicação, manager, FFI, dispositivos e async/stress | 30/30, 52,71 s |
| BGFX — campanha dos caminhos afetados, Vulkan/OpenGL | 70/70, 199,27 s |
| BGFX — revisão final RTT, async/falha tardia, ownership, publicação e readback | 16/16, 45,19 s |
| BGFX — descriptors completos e readback após a correção de padding | 3/3, 3,31 s |
| RECORDING/CPU — grafo, publicação e RTT | 4/4, 0,49 s |
| Rust offline — unidades e validação de shaders | 16 + 2 aprovados |

Os testes de comparação usam referência Coin/GL obrigatória; RTT/publicação
incluem expectativas independentes de pixels, metadados, serial e ownership.
Não houve skip nessas campanhas. A primeira campanha wgpu teve 29 aprovações e
uma falha real na nova validação de pitch: o manager 16×16 usa staging alinhado
a 256 bytes. A validação foi corrigida no Core para aceitar padding da Infra e
testada com linhas compactas/alinhadas, tamanhos publicados e descriptors
incompletos; a campanha completa posterior passou 30/30. Esse caso não foi
atribuído às mensagens EGL do ambiente.

Registros locais: `/tmp/coin-p13-{wgpu,bgfx}-regression.log`,
`/tmp/coin-p13-bgfx-final-focused.log`, `/tmp/coin-p13-recording-tests.log` e
`/tmp/coin-p13-rust-tests.log`. A conferência final de descriptors BGFX está em
`/tmp/coin-p13-bgfx-final-descriptors.log`. A campanha 70/70 precede a extensão
da fixture async; os 16 casos afetados foram repetidos após essa extensão.
Esses resultados não encerram a matriz física de P20 nem a recuperação geral P14.
