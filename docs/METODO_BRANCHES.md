# Método de criação e controle de branches

Manter três tipos de branch: PR, lab e desenvolvimento/trabalho. O controle registra apenas o trabalho atual e suas próximas ações. O histórico das mudanças fica no Git e nos PRs; não criar branches de arquivo nem manter inventários históricos neste documento.

## Tipos de branch

| Tipo | Função | Duração |
| --- | --- | --- |
| PR | Publicar e acompanhar uma mudança proposta ao master | Até o PR ser integrado ou encerrado |
| Lab | Manter a base fixa de integração ou testar um candidato a PR em uma cópia temporária | Base fixa permanente; cópia de teste descartável |
| Desenvolvimento/trabalho | Desenvolver, estudar e validar uma mudança que pode virar PR | Enquanto houver trabalho útil e próximo passo definido |

Estudo, benchmark e revisão são atividades do trabalho, não tipos de branch. A revisão ocorre na própria branch de trabalho ou de PR. Coin Render é uma linha de desenvolvimento/trabalho, mantida em `codex/coin-render`.

## Nomes e bases

| Uso | Nome | Base |
| --- | --- | --- |
| Lab fixo | `lab/open-prs-integration` | Master atualizado e heads atuais dos PRs abertos integrados |
| Lab de teste | `lab/teste/<assunto>` | Cópia do lab fixo em um SHA conhecido |
| Desenvolvimento/trabalho | `codex/work/<assunto>` | Lab fixo por padrão; master quando a mudança precisar ser independente |
| PR | `codex/pr/<assunto>` | Master de destino, com somente a mudança proposta e suas dependências explícitas |

Os nomes existentes podem continuar durante a organização. Uma branch de trabalho pode se tornar a própria branch de PR, sem cópia ou renomeação obrigatória. Não renomear branches publicadas apenas para adequar seu prefixo.

`origin` é `coin3d/coin`; `fork` é `Dikluwe/coin`. A referência de master para contribuições upstream é `origin/master`. Publicar os PRs próprios no remoto `fork`.

## Lab fixo como master futuro

`lab/open-prs-integration` representa o master futuro: o master atual com todas as mudanças dos PRs abertos integradas. Ele é a base comum para experimentar a próxima contribuição. A integração no lab não significa que o PR foi aceito no master upstream.

Manter o lab sincronizado quando o master ou um PR mudar:

1. Atualizar o master e consultar os PRs abertos do escopo, incluindo contribuições externas.
2. Buscar os heads atuais dos PRs e integrar suas versões exatas, respeitando dependências.
3. Resolver conflitos e testar o conjunto. Registrar a base, os heads incorporados e o SHA final testado no controle atual.
4. Quando um PR for integrado no master, atualizar a base e eliminar sua branch de PR após verificar eventuais commits locais adicionais.
5. Quando um PR mudar ou for fechado sem integração, reconciliar o lab para representar o conjunto atual.

O lab fixo recebe a integração dos PRs publicados. Experimentos e candidatos ainda não publicados são testados em cópias temporárias. Uma reconstrução do lab substitui sua composição anterior; não criar branches históricas para cada versão.

## Fluxo de trabalho até PR

1. Procurar trabalho existente sobre o mesmo assunto antes de criar uma branch.
2. Abrir uma branch de desenvolvimento/trabalho a partir do lab fixo, identificando seu SHA de base, objetivo e dependências.
3. Implementar, testar e revisar na própria branch. Usar worktree separado quando houver outro trabalho no checkout atual.
4. Para a validação final antes do PR, criar `lab/teste/<assunto>` como cópia do lab fixo atualizado e incorporar a mudança candidata.
5. Validar o candidato no conjunto. Corrigir os problemas na branch de trabalho e repetir a integração na cópia de teste quando necessário.
6. Preparar a branch de PR com somente a mudança candidata. Se o trabalho partiu do lab, transportar os commits necessários para uma base limpa do master, sem levar os outros PRs pendentes no diff.
7. Testar a branch de PR na sua base de publicação, publicar no fork e abrir o PR para o master de destino.
8. Integrar o head publicado no lab fixo e testar o conjunto. Eliminar o lab de teste e a branch de trabalho substituída, após confirmar que não têm alterações úteis exclusivas.
9. Continuar os ajustes e revisões na própria branch do PR até sua integração ou encerramento.

Um PR dependente de outro deve declarar essa dependência e a base usada. A validação no lab futuro e a validação da branch publicada são necessárias para conferir tanto a convivência entre mudanças quanto a contribuição isolada.

Exemplo de cópia temporária para a validação antes do PR:

```bash
git rev-parse lab/open-prs-integration
git worktree add -b lab/teste/assunto /tmp/coin-lab-teste-assunto lab/open-prs-integration
```

Em chats do Codex, usar o gerenciador do app para o ciclo dos worktrees gerenciados. O comando exemplifica o fluxo manual; ajustar nome e caminho ao trabalho.

## Auditoria de estudos e trabalhos existentes

Auditar as branches atuais de estudo e trabalho para identificar conteúdo útil que possa virar PR. Aplicar a mesma auditoria a benchmarks, revisões separadas, labs antigos e branches de arquivo existentes.

Para cada assunto:

1. Comparar commits e conteúdo com master, lab fixo, PRs e trabalhos relacionados. Conferir mudanças não commitadas e arquivos não rastreados nos worktrees.
2. Identificar a contribuição: qual problema resolve, qual evidência sustenta a solução, quais testes faltam e quais dependências existem.
3. Se houver conteúdo útil, escolher uma única branch de trabalho e reunir nela o que ainda precisa ser desenvolvido ou preparado para PR.
4. Se a mudança já estiver no master ou em uma branch de PR atual, eliminar a branch redundante após conferir o conteúdo.
5. Se o trabalho não tiver utilidade para continuar, registrar a decisão no encerramento e eliminar a branch. Não convertê-la em branch de arquivo.

A auditoria deve produzir uma ação concreta por branch: continuar como trabalho, preparar PR, incorporar conteúdo útil em outro trabalho ou eliminar. Não manter uma branch apenas por conter uma versão antiga ou uma revisão já concluída.

## Eliminação de branches

Eliminar branches de PR depois da integração no master. Eliminar labs de teste logo após o ciclo de validação e publicação. Eliminar trabalhos substituídos, revisões redundantes e estudos descartados depois da auditoria.

PRs abertos mantêm sua branch de origem, mesmo que já estejam integrados no lab fixo. PR fechado sem merge passa pela auditoria de conteúdo antes da eliminação.

Antes da eliminação, conferir dependências, commits úteis exclusivos e o estado do worktree. Incorporar conteúdo que deve continuar na branch de destino adequada. O Git conserva os commits alcançáveis pelo master e pelas branches mantidas; reflog não é garantia permanente de conservação de commits exclusivos descartados.

Remover o worktree encerrado e a branch local; remover também a branch publicada no fork quando ela tiver cumprido sua função. Não excluir branches de outros autores. Usar `git branch -d` quando a ancestralidade comprovar integração. Para squash, rebase ou commits transportados, verificar equivalência do conteúdo antes de eventual exclusão forçada.

## Controle atual

Consultar [Controle atual de branches](CONTROLE_BRANCHES.md) para as branches existentes, candidatos identificados e ações pendentes.

Manter somente o necessário para conduzir as branches ativas:

| Campo | Conteúdo |
| --- | --- |
| Branch e tipo | PR, lab fixo, lab de teste ou desenvolvimento/trabalho |
| Objetivo e responsável | Resultado esperado e pessoa ou chat responsável |
| Base e dependências | Ref, SHA da base e mudanças necessárias |
| PR e publicação | URL e branch no fork, quando houver |
| Validação | SHA testado, comandos e resultados |
| Próxima ação | Continuar, publicar, atualizar lab ou eliminar |

No lab fixo, manter a composição atual dos PRs e a validação do conjunto. No lab de teste, indicar qual trabalho está sendo validado e quando será eliminado. Remover do controle as linhas encerradas; seu histórico permanece no Git e nos PRs.

## Aplicação à organização atual

- [x] Reconciliar o lab fixo com master e com os heads atuais dos PRs abertos; manter a composição e os testes no controle atual.
- [ ] Identificar e eliminar branches de PR já integradas no master, conferindo diferenças locais.
- [ ] Auditar estudos e trabalhos para selecionar contribuições úteis que possam virar PR.
- [ ] Consolidar revisões na própria branch do trabalho ou PR e eliminar branches redundantes.
- [ ] Substituir os labs antigos pelo fluxo de base fixa e cópias de teste temporárias.
- [ ] Eliminar branches usadas apenas como arquivo após conferir o conteúdo que ainda precisa continuar.
- [ ] Manter Coin Render como desenvolvimento/trabalho e definir o destino de suas contribuições relacionadas.
