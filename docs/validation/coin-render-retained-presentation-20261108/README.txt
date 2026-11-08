CoinRender — correções de atualidade e validação estrita — 08/11/2026

Estado
Worktree: /home/dikluwe/.codex/worktrees/retained-contract-audit-20261108/coin
Branch: codex/coinrender-retained-integration
Base: 099bf06d74e82920b1af304b5c0db3231700869c, mais alterações locais identificadas
por hashes e changes.patch no pacote de evidências. O commit de fechamento
registra este conjunto; seu hash e o estado remoto constam do recibo externo.
PBR preservado: codex/pbr-clean em 5aa61250370cbeae9a96142362f3f69446fc24ae.
O checkout principal não foi editado. Destino autorizado da atualização Git:
Dikluwe/coin, branch codex/coinrender-retained-integration. Sem PR ou resposta
externa nas discussões.

Os dois contraexemplos da revisão crítica foram corrigidos e ganharam regressões
como consumidor da API pública. Os scripts agora recusam falhas e evidências
incompletas. Os oito casos foram repetidos em BGFX Vulkan e wgpu Vulkan; também
há controle RECORDING e uma execução BGFX com pipeline de readback de três slots.

Correções produtivas
1. Estado estéreo da câmera
Os setters setStereoMode, setStereoAdjustment e setBalanceAdjustment publicam
mudanças efetivas para observadores privados opt-in de CoinRender, na thread do
produtor. Não fazem touch() no root nem enfileiram sensores de redraw dos viewers
convencionais. Setters sem mudança efetiva preservam o recibo.
O observador é instalado na travessia/captura existente da câmera; não há uma
busca adicional do grafo. Cada câmera retém somente weak_ptrs e remove entradas
expiradas/aposentadas. A autoridade retida pelos consumidores contém atomics,
sem pointers para nós/campos/paths. O pimpl já existente de SoCamera passa a ser
inicializado e gerenciado; não foram adicionados campos ao layout público.

O evento avança imediatamente as revisões de cena e câmera, antes de outro
apply. O ticket anterior é recusado como atual, mas continua legível pela API
histórica. As três regressões verificam root NodeId inalterado, revisão nova,
recusa antes da próxima submissão, outputs preservados, leitura histórica e
comparação RGBA exata com novos nós/action/target frios. A aposentadoria de outro
consumidor da mesma câmera não interrompe o observador ainda ativo.
Os setters estéreo são publicação explícita de configuração para CoinRender;
isso não é detecção mágica de mutação silenciosa em campos autorais.

O perfil IR retido nativo é monoscópico. Ele observa a publicação estéreo,
invalida o recibo e recusa câmera estéreo ativa com UNSUPPORTED/diagnóstico,
preservando a imagem anterior. Voltar a MONOSCOPIC recupera com uma captura por
revisão solicitada. Não foi implementada matemática estéreo nesse perfil; usar
a action convencional para o estéreo que já suporta.

2. Registro de shader portátil
setPortableShaderModule publica invalidação somente após concluir um registro
válido. clearPortableShaderModule/clearPortableShaderModules invalidam quando
removem associações existentes. Registro inválido/programa nulo, remoção ausente
e clear vazio preservam a autoridade anterior. Um registro válido é uma nova
publicação, inclusive se repetir um conteúdo semanticamente equivalente.
As regressões BGFX/wgpu cobrem substituição vermelho->verde, remoção única e
clear-all com tickets pendentes, antes de qualquer novo render; recusa stale
transacional, leitura histórica, recuperação e controle frio de RGBA. Falhas de
validação preservam a publicação. O tratamento de bad_alloc conserva a ordem
transacional, mas não houve uma campanha de injeção de todas as alocações.

Contrato de atualidade preservado
O recibo opaco conserva os domínios de identidade/revisão da cena e conjunto de
câmeras, revisão do payload, owner/época do target, geração de recursos/dispositivo,
serial público, serial de fila e token. Nenhum serial/token isolado prova atualidade.
O escopo privado é vinculado ao frame: copiar um DTO, alterar revision ou pedir
reuse não fabrica uma prova. Borrow/reuse e a validação transacional mantêm suas
autoridades existentes. A admissão é O(1), sem nova captura, plano ou cópia de malha.

O target verifica a prova antes de executar/submeter e antes do commit CPU.
Uma revisão nova após submit recusa a publicação como atual e restaura a imagem
CPU anterior; comandos GPU já enfileirados não são desfeitos. getPresentationReceipt/
admitsPresentation e pollCurrentReadback fazem a admissão da última revisão;
pollReadback permanece histórico. Resize, configuração válida de saída,
substituição/limpeza de root no manager e aposentadoria do produtor revogam
recibos antigos. Configuração inválida preserva o estado anterior.
Submissões síncronas qualificadas leem seu próprio frame, mesmo com pipeline
BGFX de três slots, usando a espera limitada já existente de dezesseis frames.
Isso pode aumentar a espera frente à política que permitia pixels predecessores.

Scripts de validação
Os scripts corrigidos estão em testsuite/coinrender/retained-revision/ na branch.
- build-run.sh coleta status configure/build/audit/CTest/pipeline/run, preserva
  logs e retorna erro agregado. Falha ao preservar cache também atualiza run.exit.
- validate-run.py exige inventário exato, tamanhos de buffers, recibos semânticos
  finitos, resumo completo e todos os testes CTest esperados via JUnit; skips falham.
- compare.py exige os backends declarados (BGFX e wgpu por padrão), 21 imagens e
  profundidades por backend, pares completos e três controles negativos por backend.
  Ausência, arquivo extra/antigo, corrupção, mismatch ou controle não detectado
  retornam código diferente de zero. --pipeline3 exige e compara o conjunto BGFX extra.
- expected-files.json fixa a matriz. A faixa baricêntrica e o limite de quantização
  da profundidade são os mesmos da auditoria anterior; tolerâncias não foram ampliadas.
- test_validation.py passou 19 testes: três positivos e dezesseis negativos,
  incluindo o comparador e a propagação real do status do shell em cada fase e
  no armazenamento de cache. Usa cópias de buffers e recibos sintéticos/PATH stubs;
  esses testes dos scripts não contam como execução de GPU.

Matriz final dos oito casos (mesmas fixtures nos dois executores)
1. Mutação publicada: verificada em BGFX e wgpu. Matrizes, materiais, arrays,
   topologia, geometria compartilhada/estado por ocorrência e câmera chegam ao
   mesmo RGBA/profundidade da cena fria. Os três setters estéreo e registro de
   shader agora têm regressões adicionais de admissão imediata.
2. Notificações desativadas: comportamento esperado verificado em ambos para
   os campos da matriz: sem nova revisão/update/captura, imagem da última revisão
   anunciada. O serial pode avançar sem provar correspondência à memória silenciosa.
3. touch/publicação explícita: verificada em ambos. A invalidação chega aos caches,
   transporte e pixels. invalidate() é publicação explícita e faz uma captura/plano
   no próximo render, sem uma segunda captura para validar atualidade.
4. Recurso pendente: protocolo NÃO IMPLEMENTADO. O controle de payload incompleto
   recebe UNSUPPORTED/diagnóstico em tempo limitado e preserva a publicação. Isso
   não qualifica um recurso legítimo PENDING, resolução assíncrona ou scheduling.
5. Cópia/transporte: PARCIAL. Cópia privada do DTO sobrevive à edição/destruição;
   sensores confirmam root/mesh destruídos antes do hash no worker, e replay
   histórico é correto. Não existe API pública de snapshot autônomo nem uma
   qualificação de ownership de todas as famílias PBR/texturas/IBL.
6. Incremental/rebuild quente/cena fria: verificado nesta integração em ambos.
   A cena fria usa nós e renderer/target novos, sem copiar caches; inclui arrays,
   containers, geometria compartilhada e estado por ocorrência.
7. Reuso após revisão nova: verificado no escopo conectado e ampliado pelos dois
   contraexemplos corrigidos. Leitura histórica válida; apresentação como atual
   recusada após mudanças publicadas de cena/câmera/shader, durante preflight ou
   após submit, resize, target diferente e aposentadoria. Não é garantia de
   apresentação física nem protocolo geral de execução concorrente em worker.
8. Divergência: verificada pelo ensaio independente. Cor/profundidade e recibos
   semânticos coincidem; nove controles negativos detectados com diagnóstico de
   primeiro vértice/revisão no caso de geometria. Não há protocolo produtivo de
   validação cruzada entre executores.

Resultados da rodada final
RECORDING: 121 observações, zero falhas; controles CTest 5/5.
BGFX Vulkan: 124 observações, zero falhas; controles CTest 5/5.
wgpu Vulkan: 124 observações, zero falhas; controles CTest 6/6.
BGFX pipeline3: 124 observações, zero falhas; inventário completo e 166 arquivos
RGBA/profundidade/semântica idênticos aos da rodada BGFX normal.

Os controles CTest incluem CoinRenderFreshnessTest (novo, somente API pública),
CoinRenderRetainedIncrementalTest (fixture corrigida), CoinRenderFrameReuseCoreTest,
CoinRenderPublicationTest e CoinRenderMultiTargetTest; wgpu também executa
CoinRenderAsyncActionTest. O teste incremental preserva SoTextureUnit admitido,
usa SoTexture3 para rejeição legítima e verifica que falha de cena nova não torna
atual a imagem antiga. Nenhum suporte PBR foi removido.

Oito casos: 21 buffers frios de cor e profundidade por executor, 63 comparações
entre pares de cor e 63 de profundidade exatas. 229299 checagens analíticas de cor,
zero erros; 258048 amostras de profundidade. Maior erro vs 4/9:
3.3113691566555303e-08, dentro do limite original de quantização de 24 bits.
Nove controles negativos detectados; nenhuma divergência semântica entre executores.
A telemetria GPU está disponível em wgpu e indisponível em BGFX/RECORDING;
available=0 não significa zero uploads. Contadores completos estão em audit.log.

Reprodução e evidências
Resultados/binários/build caches: /tmp/coin-retained-freshness-20261108.
Pacote limpo: /tmp/coin-retained-freshness-evidence-20261108.
Arquivamento: /tmp/coin-retained-freshness-evidence-20261108.tar.gz.
O pacote contém scripts congelados, logs/status/JUnit, buffers, recibos,
comparison.json, controles dos scripts, fontes, patch e manifesto com hashes.
As evidências anteriores e os contraexemplos da revisão permanecem preservados em
/tmp/coin-retained-presentation-evidence-20261108 e
/tmp/coin-retained-critical-review-20261108.

Usar uma cópia congelada do diretório de scripts e executar, para cada backend:
  bash build-run.sh BACKEND /caminho/do/worktree /tmp/evidencias
  python3 compare.py /tmp/evidencias --backends recording bgfx wgpu --pipeline3
  python3 test_validation.py /caminho/de/um/pacote/completo
O script configura/compila/executa no mesmo processo, em RAM privada /dev/shm,
com cache Mesa/temporários em RAM e Rust offline. GPU serializada por flock.
Ver README.txt dos scripts para configuração de bgfx, caches e inventário.

Ambiente
Linux x86_64; GCC 13.3.0; CMake 3.28.3; Rust 1.92.0; Release/shared e threads ON.
Vulkan explícito com ICD /usr/share/vulkan/icd.d/lvp_icd.json, llvmpipe LLVM 20.1.2 /
Mesa 25.2.8. Dependências fixadas no manifesto. Não é qualificação de GPU dedicada,
Android, Windows/macOS ou integração FreeCAD/Qt.

Limites e trabalho restante
PENDING precisa de estados Ready/Pending/Failed e identidade/revisão de recurso,
payload pronto imutável, conclusão publicada pelo produtor, defer/reagendamento
limitado, quotas/timeout, descarte de conclusão tardia após supersession,
cancelamento/falha/teardown e ensaios desses estados nos dois executores. Readback
pendente, fila cheia e dispositivo indisponível não substituem esse protocolo.

Snapshot público precisa de handle imutável que possua todos os bytes/descritores,
exporte captura/plano únicos e ofereça consumo público validado, sem COIN_INTERNAL.
SoDrawList/SoGeometryDesc mantêm pointers do produtor: cópia rasa não basta.
É necessário qualificar geometria e atributos, texturas/mips, materiais/shaders,
PBR/IBL quando admitidos, câmeras e proveniência por valores/handles estáveis;
excluir/congelar caches auxiliares e qualquer alias mutável. Definir também
revisão fixa histórica versus última publicada, lifetime de resultados,
thread/queue/device, orçamento, cancelamento e shutdown. O hash privado atual
cobre apenas campos selecionados e o replay ocorre na thread principal.
applyAsync captura/prepara/submete durante a chamada; não executa tudo em worker.

A admissão atual cobre frames completos do renderer retido mono e apply(root)/
applyAsync(root) convencionais, mais manager mono. DTO externo, apply(path/list),
canvas parcial e composição stereo/snapshots multipass com callbacks não recebem
prova de um único estado atual; seus caminhos históricos permanecem disponíveis.
A prova deve ser revalidada no consumo. Ela não é fence do compositor: no wgpu,
present ocorre dentro de submit, e a checagem posterior não pode desfazê-lo.
Sem alegação de sincronização geral com produtor mutável em outra thread ou de
abort de GPU pelo cancelamento de readback. Não foram acrescentadas qualificações
WSI físicas ou de todas as falhas de dispositivo/cancelamento durante readback.
Biblioteca Coin/CoinRender e consumidores experimentais devem ser reconstruídos.

Os resultados são da nossa integração. O par FreeCAD/coin #72
812f14f94e11bf260fb3a4a0f487e8694daadd3a versus Coin
a90405ee795df2cc0eced9db39e99bd72cbe8d49 não foi reconstruído. O import local
continua 17a9e4b21b643cb44d42e103183d8384eef6a6a5 com adaptações. Não transferir
os resultados positivos ou negativos desta implementação à proposta do tritao.

Revisão final e regressões adicionais
Ver final-review.txt e final-review-summary.json neste diretório: novos controles
convencionais e PBR, comparação direta com a base 099bf06d74e, referências
históricas e a limitação de ambiente Coin/GL explicitamente preservada.
