# Fechamento Linux dos quatro mapas GL do #758

Em 6 de outubro de 2026, foi concluída a revisão Linux de SoVBO, SoGLSLShaderProgram, SoShaderObject e SoUniformShaderParameter. A migração publicada no #758 foi reconciliada sobre master em uma branch pré-PR, com complemento separado de limpeza por contexto, travessias sem listas temporárias e correção de downcast encontrada pelo UBSan. O head publicado do #758 permanece intacto; Windows e transporte do complemento para publicação estão pendentes.

## Referências e composição

| Referência | SHA |
| --- | --- |
| Master de referência | `674e74267df863dbaf50416c477bc7f918a826d8` |
| #758 publicado, preservado | `900f11a1570bd03555578aae6f9df9d0271d6189` |
| SbSmallMap fechado, pré-requisito local | `ff1d09ddf3d458bb73023aa3fc001afce1e54fd6` |
| Snapshot de #758 reconciliado sobre esse pré-requisito | `9f024a80ea6963a4cd34bab2e158a7ec178656e1` |
| `codex/pre-pr/gl-contexts-small-complete` | `87cc9003e9adbacc702effe666d0e3b34b377983` |
| Lab fixo, preservado | `b27e37a6dd3ae941bcffcf480a377adc81e3fc27` |
| Lab temporário com o pré-requisito SbSmallMap | `b4b40d947d9f77630318fc52f41821e83f803233` |
| `lab/teste/gl-contexts-small-complete` | `547caebd3067a00ac3cfe90e8106bb6fa622aead` |

Worktrees: `/home/dikluwe/.codex/worktrees/gl-contexts-small-work/coin` e `/home/dikluwe/.codex/worktrees/gl-contexts-small-test/coin`.

A branch de trabalho parte de SbSmallMap fechado sobre master/fixture. O commit 9f024a80ea aplica somente os seis arquivos da migração #758 (52 inserções/31 remoções), extraídos da resolução do lab; não traz sensores, XML ou outros PRs do head antigo. O complemento é o diff `9f024a80ea..87cc9003e9`: seis arquivos, 262 inserções e 26 remoções. Na cópia do lab, SbSmallMap entrou primeiro como dependência identificada em dois commits; #758 já estava na base fixa. Depois entrou somente o complemento.

Os deltas adicionais da pré-PR e do lab são idênticos após remover apenas as linhas index dos hashes dos blobs de base. O lab fixo não foi modificado. Na publicação, transportar a migração/complemento para o destino do #758 depois de Windows e integração dos pré-requisitos; não usar o lab como branch de PR.

## Correções e revisão de propriedade

| Consumidor/assunto | Fechamento |
| --- | --- |
| SoVBO::vbohash | Guarda existente exclui o buffer GL novo quando put falha; contexto destruído e proprietário destruído eliminam os buffers pelas rotas próprias; retry e hits conferidos |
| SoGLSLShaderProgram::programHandles | Guarda existente exclui o programa novo quando put falha; nenhum handle publicado na falha; limpeza percorre e remove diretamente, sem uma lista temporária de chaves |
| SoShaderObjectP::glshaderobjects | unique_ptr existente protege o shader até publicação; falha após load exclui o shader recém-criado; limpeza percorre/remove diretamente; invalidateParameters também percorre diretamente |
| SoUniformShaderParameterP::glparams | unique_ptr existente protege criação/substituição; novo callback remove e destrói o parâmetro do contexto encerrado; destructor remove seu callback antes de liberar os restantes |
| Substituição de parâmetro | Se a fábrica da substituição lança, o parâmetro anterior continua no mapa; sucesso troca o ponteiro e destrói o anterior sem aumentar a contagem de objetos vivos |
| Downcast de matriz | updateStateMatrixParameters verifica o tipo pelo SoNode antes de converter para SoShaderStateMatrixParameter; UBSan reproduziu o downcast inválido com um SoShaderParameter1f comum |
| Propriedade | SbSmallMap continua não proprietário; responsabilidade permanece no consumidor. Nenhuma migração de registros complexos do bump ou de novos consumidores |
| Limpeza | A retirada das listas evita alocação auxiliar apenas para visitar as entradas. O agendador legado de delete callbacks ainda pode alocar; não se promete destructor recuperável sob qualquer OOM |
| Compatibilidade pública | Nenhum header instalado alterado; campos novos ficam no comportamento do pimpl existente, sem novos membros na representação pública |

O novo callback dos parâmetros cobre a pendência histórica indicada pelo FIXME do consumidor. Esses parâmetros não possuem um recurso GL a excluir nas implementações atuais; seus destructors liberam estruturas de CPU. Assim, podem ser destruídos diretamente no callback de contexto e na destruição do proprietário.

## Oráculo de integração e resultados

GLContextMapsTest compila os quatro fontes reais dos consumidores com o hook COIN_SMALLMAP_TESTING já existente no template, ligado à biblioteca normal. Não há implementação alternativa dos mapas ou dos consumidores. O driver intercepta somente as funções GL de criação/exclusão para contar recursos, delegando sempre às funções reais.

O teste cria cinco contextos GLX não compartilhados, sem sleeps. Exercita:

- quatro contextos inline, spill na quinta entrada e retorno aos quatro primeiros;
- falha determinística da quinta inserção nos quatro mapas, exclusão do recurso novo e retry;
- parâmetros vivos contados por uma subclasse de teste; criação e substituição que falham;
- shader real, uniform float e imagem: o pixel central deve corresponder à cor GLSL esperada;
- hits sem novos buffers, programas ou shaders;
- contexto antes do proprietário em dois contextos, proprietário antes do contexto nos outros três;
- contagem final zero de recursos, parâmetros vivos e exclusões de handles inválidos.

| Configuração | Resultado final |
| --- | --- |
| Pré-PR reconciliada sobre master, Release estática | 80/80 CTests, sem falhas ou testes ignorados |
| Lab com SbSmallMap como dependência, Debug compartilhado | 107/107 CTests, sem falhas ou testes ignorados |
| ASan/UBSan | Quatro fontes reais e driver instrumentados; teste completo aprovado |
| LeakSanitizer | Ativo, com exclusão restrita dos cinco registros de glue e suas alocações possuídas, pela pendência de lifetime separada abaixo |
| Implementação anterior dos parâmetros | Mesmo teste falha: após destructingContext, getGLShaderParameter ainda devolve o parâmetro e a contagem viva não diminui |

O restante da biblioteca usada pelo teste instrumentado não foi inteiramente instrumentado. O reprodutor histórico sai antecipadamente na asserção esperada, e apenas nessa execução detect_leaks=0 evita relatar recursos do fixture que não chegou ao epílogo. A execução corrigida mantém detect_leaks=1.

Os cinco contextos confirmam correção em cardinalidade acima do limite inline; não são um novo benchmark de desempenho. Não foi repetido o smoke FreeCAD nem reivindicado novo ganho medido. A justificativa histórica (um contexto observado em VBO e dois nos mapas de shaders) permanece como evidência de perfil, com os limites descritos no estudo anterior. Desempenho com muitos contextos continua linear; não houve troca para backend adaptativo.

## RAM, comandos e logs

Builds e temporários do compilador em `/dev/shm/coin-gl-contexts-small-20261006`:

```sh
export TMPDIR=/dev/shm/coin-gl-contexts-small-20261006/compiler-tmp
cmake -S "$WORKTREE" -B "/dev/shm/coin-gl-contexts-small-20261006/$BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE="$TYPE" -DCOIN_BUILD_SHARED_LIBS="$SHARED" \
  -DCOIN_BUILD_TESTS=ON -DCOIN_BUILD_DOCUMENTATION=OFF -DCOIN_THREADSAFE=OFF
cmake --build "/dev/shm/coin-gl-contexts-small-20261006/$BUILD" --parallel 8
xvfb-run -a -s '-screen 0 1280x1024x24 +extension GLX' \
  env LIBGL_ALWAYS_SOFTWARE=1 COIN_GLX_PIXMAP_DIRECT_RENDERING=1 \
  ctest --test-dir "/dev/shm/coin-gl-contexts-small-20261006/$BUILD" --output-on-failure --parallel 4
```

BUILD/TYPE/SHARED: master-release-static/Release/OFF e lab-debug-shared/Debug/ON. Rebuilds finais usaram --parallel 6. Logs `/tmp/gl-contexts-small-{master,lab}-{configure,build,rebuild,full}-20261006.log`, e execução focada em `/tmp/gl-contexts-small-master-focused-20261006.log`.

Instrumentação reproduzível em `/tmp/run-gl-contexts-sanitizers.py`, comandos/resultados em `/tmp/gl-contexts-small-sanitizers-final-20261006.log`. Extrai os comandos do Ninja, instrumenta os quatro consumidores e o driver com `-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined` e usa halt_on_error=1. `COIN_GL_MAP_LSAN_IGNORE_GLUE` é opt-in do teste instrumentado: chama __lsan_ignore_object somente para os cinco cc_glglue conhecidos, não para os mapas ou seus recursos. A execução normal não usa essa opção.

## Achados independentes preservados

1. **Vida útil do glue.** coin_glglue_destruct retira o registro do dicionário e fecha dl_handle, mas não libera o próprio cc_glglue e suas estruturas. O teste isolado de contexto reproduziu 68.410 bytes em 50 alocações no LeakSanitizer. Isso pertence à auditoria já registrada em codex/work/glglue-lifetime-audit (`5d28cf8e95`), que deve ser reconciliada e validada em ciclo próprio. A exclusão restrita no teste acima não declara esse defeito resolvido.
2. **Exceções de SoAction::apply.** A falha de inserção propagada através da travessia também revelou retenção da referência da raiz e falta de epílogo de desbloqueio/restauração na rota lançadora. O reprodutor mínimo independente, com SoCallbackAction e um callback que lança, confirmou `caught=1 root refs before=1 after=2`. Fonte `/tmp/gl-map-action-exception-repro.cpp`, executável em RAM e log `/tmp/gl-map-action-exception-repro-20261006.log`. A ação deve ter um ciclo próprio de correção/validação, incluindo caminhos de aplicação e build com threads. Neste teste dos mapas, a falha do shader é invocada diretamente no consumidor com um SoState inicializado; renderizações normais continuam passando pela ação real.

Esses achados ficam na checklist. O fechamento dos quatro mapas não implica segurança de exceções de toda a travessia, nem resolução do lifetime do glue.

## Windows e publicação

Validar Windows DLL/estático e LLP64 antes de transportar o complemento para #758. O teste GLX é Linux; a etapa Windows precisa de contexto nativo equivalente. Antes de publicar, repetir o teste sobre o master de destino atualizado e verificar contribuição exclusiva, com SbSmallMap/fixture tratados como pré-requisitos próprios. Nenhum push ou PR novo feito. Preservar o lab temporário até terminar Windows/publicação; depois arquivar o worktree gerenciado e eliminar lab/teste/gl-contexts-small-complete.

## Atualização posterior do lifetime

O ciclo seguinte fechou o lifetime em `codex/pre-pr/glglue-lifetime-complete` (`0d31242881`). A integração acima foi repetida com liberação real do glue e sem COIN_GL_MAP_LSAN_IGNORE_GLUE, mantendo LeakSanitizer ativo, incluindo uma ação reaproveitada com textura. [Registro](FECHAMENTO_GLGLUE_LIFETIME.md). A exclusão descrita neste documento continua sendo uma limitação apenas do snapshot original #758, não um requisito do conjunto corrigido. SoAction::apply sob exceção permanece pendente.
