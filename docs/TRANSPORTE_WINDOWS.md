# Transporte para validação Windows

Este snapshot reúne as candidatas fechadas no Linux e a documentação necessária para validá-las no clone Windows. As refs serão disponibilizadas no fork `Dikluwe/coin` por pushes normais; isso transporta código para testes, sem abrir PRs, atualizar PRs existentes ou integrar no upstream.

A branch de documentação é `codex/docs/windows-validation-20261006`, baseada no master de referência. Os documentos originais foram copiados sem modificar os arquivos não rastreados dos chats ativos. Suas menções a “publicação pendente” descrevem o gate de contribuição upstream; este envio ao fork é somente transporte para Windows.

## Referências para testar

Os heads abaixo preservam os snapshots validados no Linux. Não rebasear para transportá-los. Os pré-requisitos fazem parte do histórico das candidatas; as referências adicionais servem para comparação e montagem das cópias de teste.

| Papel | Branch | SHA completo |
| --- | --- | --- |
| Candidata para Windows | `codex/pre-pr/bump-fixture-static` | `55dbefa0330ca59a68ff2d47591f1256e7cba7b2` |
| Candidata para Windows | `codex/pre-pr/cc-dict-complete` | `9b9237f13da3ab73492579e0ac7f06e2e8490a32` |
| Candidata para Windows | `codex/pre-pr/cc-hash-complete` | `c5dc4c643a48a7f1bf38fe32ac9da7cb24be62a8` |
| Candidata para Windows | `codex/pre-pr/sbdict-complete` | `b5ae0d4ff39f8783648b69b0c2503b1bb5516a69` |
| Candidata para Windows | `codex/pre-pr/sbhash-complete` | `5112bbcaf06755fb505772f3a77a737f33e0d3a1` |
| Candidata para Windows | `codex/pre-pr/sbsmallmap-complete` | `ff1d09ddf3d458bb73023aa3fc001afce1e54fd6` |
| Candidata para Windows | `codex/pre-pr/gl-contexts-small-complete` | `87cc9003e9adbacc702effe666d0e3b34b377983` |
| Candidata para Windows | `codex/pre-pr/glglue-lifetime-complete` | `0d31242881b0a2bc996ae348c3768fa8730c7a98` |
| Candidata para Windows | `codex/pre-pr/cc-xml-expat-2.9.0` | `aef999cb3642638c61a82128f0db69ad9e532702` |
| Candidata para Windows | `codex/pr/cc-xml-entity-abi` | `21dc59664b2ece234822901e9498ab86c539c6d6` |
| Candidata para Windows | `codex/pr/cc-xml-path-loops-truncate` | `bffa176d456a623078d1ded247d8b04bd3f7d898` |
| Candidata para Windows | `codex/pr/cc-xml-escaping-inttypes` | `0935125576bf039ce59f257a634549affc4137cc` |
| Candidata para Windows | `codex/pr/cc-xml-parser-transactional` | `162dc998af31291fb2d861b0d6b1431acb1c18da` |
| Candidata para Windows | `codex/pr/cc-xml-coalesce-cdata` | `5c4a34e04561eaef39abecf9acbca9609d39ddd5` |
| Candidata para Windows | `codex/pr/cc-xml-dom-ownership` | `28d74b9982bf0791d50abd60a1dcbbd7817fe1f0` |
| Candidata para Windows | `codex/pr/cc-xml-dom-regressions-fuzz` | `dcb54791bd3609458f7df5c5e22e83682b68358a` |
| Candidata para Windows | `codex/pr/scxml-sendelt-tokenize` | `5e8945f2086049850f8638bed144112fd85e1eb0` |
| Candidata para Windows | `codex/pr/scxml-event-association-ownership` | `a4fc778e7c07fd416d0799e4b002bdb090b92f8f` |
| Pré-requisito local | `codex/pre-pr/bump-static-testfix` | `c9c9234768b47e5889bbc657c707c9da5a7f6bb2` |
| Pré-requisito local | `codex/pre-pr/sbhash-prerequisites` | `25829e5e6bb0189c5a3a6af4fd7b6aafed6fe4ec` |
| Lab fixo de integração | `lab/open-prs-integration` | `b27e37a6dd3ae941bcffcf480a377adc81e3fc27` |

A lista original de 17 candidatas foi complementada por `codex/pr/scxml-event-association-ownership`, já marcada pronta para Windows na checklist atual. Seu gate individual e a combinação instrumentada com `ScXMLSendElt` passaram no Linux; a matriz cumulativa completa com Expat e todas as candidatas ainda está pendente. Não pressupor que todas as candidatas já foram testadas juntas.

A branch de `SoAction::apply` continua em desenvolvimento ativo e não entra neste transporte. As cópias `lab/teste/*`, branches experimentais e fontes antigas não são publicadas neste lote.

## Dependências

- `cc-dict-complete` e `cc-hash-complete`: `bump-static-testfix`, cuja base incorpora os pré-requisitos de #769–772.
- `sbdict-complete`: `cc-dict-complete`.
- `sbhash-complete`: `sbhash-prerequisites`, sobre #772 e o fixture. O snapshot reconcilia lazy storage sem transportar o lab inteiro.
- `sbsmallmap-complete`: `bump-fixture-static` sobre master.
- `gl-contexts-small-complete`: `sbsmallmap-complete`; inclui a migração de #758 reconciliada e seu complemento.
- `glglue-lifetime-complete`: `bump-fixture-static` sobre master.
- `cc-xml-dom-regressions-fuzz`: `cc-xml-dom-ownership`.
- Demais candidatas XML/SCXML e Expat: master de referência; o lab cumulativo precisa integrar as contribuições explicitamente.

## Pré-requisitos de PR já publicados

Estas refs já estão no fork e seus heads foram conferidos; não são modificados por este transporte.

| Branch | SHA completo |
| --- | --- |
| `codex/cc-dict-hardening` | `5a311648e38c63f9b6597e0968d3d1dc3428ba31` |
| `codex/cc-dict-resize-followup` | `dacbbd984d17a66be39ea6e78d0c8c175a6a34db` |
| `codex/cc-memalloc-hardening` | `729a5b9cd5351c7799519917381ec3a39b669be2` |
| `codex/cc-oom-consumers` | `6a169aaac4725014cddd0bd7665224ccd8950a67` |
| `codex/maps/use/gl-contexts-small` | `900f11a1570bd03555578aae6f9df9d0271d6189` |
| `fix/sbhash-04-noexcept-contract` | `4810469369dff74c1e23142bc68584cc9aca7e31` |
| `fix/sbhash-05-relink-resize` | `25a0c03d629eb07f776375ce9c9630036341b43a` |
| `fix/sbhash-06-lazy-storage` | `7fa9bae533a051fa0fd450f23e32c0e39d8e1c6b` |

Master de referência: `674e74267df863dbaf50416c477bc7f918a826d8`.

## Validação no clone Windows

1. Preservar a alteração local existente em `testsuite/bumprender/TestAdapter.h`. O fetch somente atualiza refs; executar os testes em worktrees separados evita alterar esse checkout.
2. Fazer fetch do fork e comparar cada head com o manifesto. Não usar pull/reset/clean para substituir o checkout com trabalho local.
3. Ler as checklists e os registros da contribuição antes de montar cada teste. Exercitar x64 DLL e estático, incluindo LLP64, e os caminhos WGL equivalentes aos cenários GLX aplicáveis.
4. Para o conjunto, criar uma cópia `lab/teste/<assunto>` do lab fixo, incorporar somente as contribuições/dependências selecionadas e registrar o SHA final. O lab fixo permanece preservado.
5. Registrar comandos, configuração, head testado, resultados e limitações. A aprovação Linux não comprova a validação Windows.

A documentação pode ser consultada em `git show fork/codex/docs/windows-validation-20261006:docs/CHECKLIST_DICT_CC_SB.md` e no arquivo equivalente XML, sem trocar a branch do checkout principal. Também pode ser extraída com `git archive` para uma pasta separada.

## Documentos transportados

Checksums SHA256 do snapshot copiado. Os documentos referenciados dentro deles fornecem comandos e resultados; caminhos Linux absolutos e logs em `/tmp` ou RAM são contexto das execuções anteriores, não arquivos a recuperar no Windows.

| Arquivo | SHA256 |
| --- | --- |
| `docs/CHECKLIST_DICT_CC_SB.md` | `1bfa39343718807fee11cc821f00bcdaf50beda4d41e247f8e474f1690d508d9` |
| `docs/CHECKLIST_XML_SCXML.md` | `d7b6a337996f7116137465c83ab1b8057899016ea2db30fb7c18a2936b65cb21` |
| `docs/CONTROLE_BRANCHES.md` | `8e4397b7827138e54b38c1c42fae67a5559926b88e9bb9d0aacc9308cc7c5f00` |
| `docs/DICT_CC_SB.md` | `fb3b69f261b9fc97e461bd10723d01c556839302ae7d6902c4b7c049ce25ca4f` |
| `docs/FECHAMENTO_CC_HASH.md` | `91a807c918e3df205f2090cd023f37cffa0040209448c9f66e7a8de50187c952` |
| `docs/FECHAMENTO_GLGLUE_LIFETIME.md` | `5dc061a3381f76f510ad9d40e8e24d84581022c09791f81bf8167eb29f5cd0f5` |
| `docs/FECHAMENTO_GL_CONTEXT_MAPS.md` | `1a5ef06ea49112aca17e169012975d6739e76405225750d2f45e6bc6741c3a2b` |
| `docs/FECHAMENTO_SBDICT.md` | `8eef7710e48406d8da94723a7b951aa9f371114601b4f2ba575fab62ab6c54ff` |
| `docs/FECHAMENTO_SBHASH.md` | `a6fb57c1b8a8f9e18e13d21cc3e20cf3585ef7902240b1a793b49b45e2c03fcd` |
| `docs/FECHAMENTO_SBSMALLMAP.md` | `7c6ff71274e59b89bb3da08175a5e248e589c0569f4f0b82cc80a25cd576e7c5` |
| `docs/METODO_BRANCHES.md` | `6ec8022a0da1d777d4ac2744f6098928ed0724f10b0777058d954ee9c0985496` |
| `docs/VALIDACAO_CC_DICT.md` | `6e8dfa88e347bdac690328a13de32a102eb49afb45a5b58be4b95781902b95dc` |
| `docs/VALIDACAO_CC_HASH.md` | `390a423e16e4b82b9e5b8d03905b72d5a18059fe5cb680571d7b8802eafec734` |
| `docs/VALIDACAO_FIXTURE_BUMP.md` | `128a937e104723c7e9ef8a23010f23b72a2256827e83c5b3019d34db26a16905` |
