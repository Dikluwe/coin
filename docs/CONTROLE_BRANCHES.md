# Controle atual de branches

Aplicar o [método de branches](METODO_BRANCHES.md). Este controle contém apenas branches existentes e ações pendentes; remover as linhas conforme os trabalhos forem encerrados.

O chat responsável por dicionários CC e Sb mantém o [mapa de contratos, dependências e prioridades](DICT_CC_SB.md). A organização desse assunto inclui `cc_dict`, `cc_hash`, `SbDict`, `SbHash` e os mapas relacionados.

## Prioridades de organização

1. Validar os candidatos identificados abaixo e auditar os demais trabalhos por assunto. Eliminar redundâncias depois de conferir conteúdo e dependências.
2. Concluir a auditoria das cinco branches com prefixo `archive/` e dos quatro labs auxiliares. Os prefixos são legados em processo de organização; não criar novas branches desses grupos.
3. Atualizar o lab fixo sempre que master ou um PR mudar e repetir a validação do conjunto.

## Candidatos identificados na auditoria

A comparação abaixo é uma auditoria de conteúdo. Estes candidatos ainda não foram compilados ou testados nesta organização e não estão prontos para publicação.

| Branch de trabalho | Conteúdo útil identificado | Próxima ação |
| --- | --- | --- |
| `codex/work/bump-cache-ready-path` | Evita inicializar o diagnóstico quando o cache já está pronto | Medir o ganho, validar o retorno e preparar PR isolado |
| `codex/work/bump-cache-diagnostics-memory` | Reduz diagnóstico inline de 512 para 64 bytes e guarda mensagens longas sob demanda; inclui mudanças nos testes de falha | Validar memória, diagnósticos e comportamento sob OOM |
| `codex/work/bump-shared-program-pool` | Compartilha programas bump validados; há um commit de pré-requisito equivalente ao master | Isolar a contribuição nova e validar contexto, propriedade e concorrência |
| `codex/work/glglue-lifetime-audit` | Reprodutor de lifetime e chave experimental que libera a instância glglue | Executar o reprodutor com ASan e transformar a conclusão em solução de produção; não publicar a chave experimental como correção |
| `codex/work/sbhash-insert-allocation-failure` | Move crescimento antes da inserção para evitar inserir e depois falhar; contém teste de falha que difere do head atual do PR 755 | Conferir a correção contra os PRs atuais e consolidar no PR adequado ou preparar contribuição separada |
| `codex/work/sbheap-cancel-documentation` | Documenta amostragem do callback e heap parcialmente desordenado após cancelamento; este texto não está no master | Conferir a semântica e preparar PR de documentação |


## Branches pré-PR e validações temporárias

| Branch | Tipo | Base e mudança | Validação e próxima ação |
| --- | --- | --- | --- |
| `codex/pre-pr/cc-xml-expat-2.9.0` | Pré-PR; pronta para Windows | Master `674e74267d`; update `4d476427bf`; C99/head `aef999cb36` | Expat embarcado 78/78, externo 2.6.1 78/78, biblioteca única compilada; lab temporário 105/105. Validar no Windows antes de publicar |
| `codex/pre-pr/cc-dict-complete` | Pré-PR; pronta para Windows | Pré-requisito `c9c9234768`; candidato `9b9237f13d` | Escopo conhecido fechado no Linux; 93 testes em Release estático. Ver [fechamento](VALIDACAO_CC_DICT.md) |
| `codex/pre-pr/cc-hash-complete` | Pré-PR; pronta para Windows | Pré-requisito `c9c9234768`; candidato `c5dc4c643a` | Escopo conhecido fechado no Linux; cliente C/header antigo, sanitizadores e 91 testes estáticos. Ver [fechamento](FECHAMENTO_CC_HASH.md) |
| `codex/pre-pr/sbdict-complete` | Pré-PR; pronta para Windows | Pré-requisito cc_dict `9b9237f13d`; candidato `b5ae0d4ff3` | Migração e contratos fechados no Linux; 97 testes estáticos, cliente antigo e sanitizadores. Ver [fechamento](FECHAMENTO_SBDICT.md) |
| `lab/teste/sbdict-complete` | Lab de teste com pré-requisito explícito | Base fixa `b27e37a6dd`; cc_dict `92e648c4da`; head `e464ab0cb4` | 112 testes aprovados; conservar até concluir Windows/publicação |
| `lab/teste/cc-hash-complete` | Lab de teste | Base fixa `b27e37a6dd`; head `0505c1caed` | 106 testes aprovados, build em RAM; conservar até concluir Windows/publicação |
| `codex/pre-pr/bump-static-testfix` | Pré-requisito local de cc_dict | Base #772 `6a169aaac4`; head `c9c9234768` | Preservar por ser a base local de cc_dict; mesmo patch da branch limpa de fixture abaixo |
| `codex/pre-pr/bump-fixture-static` | Pré-PR; pronta para Windows | Base master `674e74267d`; candidato `55dbefa033` | Uma correção de fixture, sem dependência dos PRs de cc_dict; 78 testes em master estático e 105 no lab compartilhado. Ver [revisão](VALIDACAO_FIXTURE_BUMP.md) |
| `lab/teste/bump-fixture-static` | Lab de teste | Base fixa `b27e37a6dd`; head `f780ab9a44` | 105 testes aprovados; conservar até concluir Windows/publicação |
| `lab/teste/cc-dict-complete` | Lab de teste | Base fixa `b27e37a6dd`; head `44eedede9c` | 108 testes em Debug compartilhado, build em RAM; conservar até encerrar o ciclo Windows/publicação |
| `codex/work/cc-hash-api` | Desenvolvimento/trabalho | Base `b27e37a6dd`; candidato `ebae1bd653` | Complemento de API externa isolado dos pré-requisitos. Preparar branch de PR com diff próprio; ver [registro](VALIDACAO_CC_HASH.md) |
| `lab/teste/cc-hash-api` | Lab de teste | Base fixa `b27e37a6dd`; head testado `2dcf47b703` | Build em RAM; 9 regressões e 106 testes do conjunto aprovados. Concluir gate na base de publicação e encerrar a cópia após o ciclo |

### Expat 2.9.0: validação local

- Fonte oficial: `expat-2.9.0.tar.xz` (SHA-256 `1e6371862cc31999b368c3b89b49994f0677e1bab5f1b2b85ae3741f5d803051`). Arquivos upstream em `src/xml/expat` idênticos aos do tarball; só `expat_config.h`, `CMakeLists.txt` e `README` são integração Coin.
- Base master `674e74267df863dbaf50416c477bc7f918a826d8`; candidata pré-PR `aef999cb3642638c61a82128f0db69ad9e532702` (commits `4d476427bf1b60070483675cabcfbeb8d83c846d` e `aef999cb3642638c61a82128f0db69ad9e532702`). `git merge-base master codex/pre-pr/cc-xml-expat-2.9.0` retornou a base master; `git diff --check master...codex/pre-pr/cc-xml-expat-2.9.0` não apontou erros.
- Lab fixo preservado em `b27e37a6dd3ae941bcffcf480a377adc81e3fc27`; cópia `lab/teste/cc-xml-expat-2.9.0` partiu desse SHA e recebeu somente os dois commits candidatos por `git cherry-pick`, chegando a `4aa5825c45a964f3997eba58948d455459483df6`. O worktree/cópia temporária foi eliminado após a validação.
- Builds na RAM: `cmake -S <worktree> -B /dev/shm/coin-expat-2.9.0-build -DCOIN_BUILD_TESTS=ON -DCOIN_BUILD_DOCUMENTATION=OFF -DCMAKE_BUILD_TYPE=Debug`, seguido de `cmake --build <build> --parallel 8`; 78/78 testes. Repetição com `-DUSE_EXTERNAL_EXPAT=ON` em `/dev/shm/coin-expat-2.9.0-external-build` (Expat de sistema 2.6.1): 78/78. `-DCOIN_BUILD_SINGLE_LIB=ON -DCOIN_BUILD_TESTS=OFF` em `/dev/shm/coin-expat-2.9.0-single-build`: compilação aprovada. Cópia lab em `/dev/shm/coin-lab-expat-2.9.0-build`: 105/105.
- Testes: `xvfb-run -a -s '-screen 0 1280x1024x24 +extension GLX' env LIBGL_ALWAYS_SOFTWARE=1 COIN_GLX_PIXMAP_DIRECT_RENDERING=1 ctest --test-dir <build> --output-on-failure --parallel 4`. Um smoke test C local compilado com `cc -std=c99` contra o `libCoin.so` embarcado confirmou `XML_ExpatVersionInfo()` = 2.9.0, `XML_FEATURE_GE`, configuração da proteção contra amplificação e parse válido. A política de `DOCTYPE` não mudou. Pendente: compilação/testes Windows (upstream suporta oficialmente MSVC 2022+) antes de qualquer publicação.

## Branches de PR

As 15 branches próprias estão reconciliadas com os heads publicados e acompanham suas branches no remoto `fork`. Manter as branches enquanto os PRs estiverem abertos; os ajustes e revisões ocorrem na própria branch de PR.

| PR | Branch principal | Head publicado e local | Próxima ação |
| --- | --- | --- | --- |
| [#774](https://github.com/coin3d/coin/pull/774) | `codex/fix/issue417-sotext2-pr` | `7f2e9335522a6ffdc9d66c4c18f80e4fcca5d0e5` | Acompanhar revisão no próprio PR |
| [#772](https://github.com/coin3d/coin/pull/772) | `codex/cc-oom-consumers` | `6a169aaac4725014cddd0bd7665224ccd8950a67` | Acompanhar revisão no próprio PR |
| [#771](https://github.com/coin3d/coin/pull/771) | `codex/cc-dict-hardening` | `5a311648e38c63f9b6597e0968d3d1dc3428ba31` | Acompanhar revisão no próprio PR |
| [#770](https://github.com/coin3d/coin/pull/770) | `codex/cc-memalloc-hardening` | `729a5b9cd5351c7799519917381ec3a39b669be2` | Acompanhar revisão no próprio PR |
| [#769](https://github.com/coin3d/coin/pull/769) | `codex/cc-dict-resize-followup` | `dacbbd984d17a66be39ea6e78d0c8c175a6a34db` | Acompanhar revisão no próprio PR |
| [#768](https://github.com/coin3d/coin/pull/768) | `codex/issue-136-remove-win9x` | `b3ae400d0da5044739d359b20a96d83f62ec9225` | Acompanhar revisão no próprio PR |
| [#767](https://github.com/coin3d/coin/pull/767) | `codex/issue-413-nurbs-control-colors` | `fd9f7db48838e6df3b9f747c385e5d6a2837ca4e` | Acompanhar revisão no próprio PR |
| [#765](https://github.com/coin3d/coin/pull/765) | `codex/fix/issue-374-lod-unreachable` | `c79366f12d98585fd90bca1e6bc67eaaf33c3aeb` | Acompanhar revisão no próprio PR |
| [#761](https://github.com/coin3d/coin/pull/761) | `fix/sbplane-three-plane-intersection-upstream` | `79984c883569834583543b8699b6a254d5afc904` | Acompanhar revisão no próprio PR |
| [#758](https://github.com/coin3d/coin/pull/758) | `codex/maps/use/gl-contexts-small` | `900f11a1570bd03555578aae6f9df9d0271d6189` | Acompanhar revisão no próprio PR |
| [#755](https://github.com/coin3d/coin/pull/755) | `fix/sbhash-06-lazy-storage` | `7fa9bae533a051fa0fd450f23e32c0e39d8e1c6b` | Acompanhar revisão no próprio PR |
| [#754](https://github.com/coin3d/coin/pull/754) | `fix/sbhash-05-relink-resize` | `25a0c03d629eb07f776375ce9c9630036341b43a` | Acompanhar revisão no próprio PR |
| [#753](https://github.com/coin3d/coin/pull/753) | `fix/sbhash-04-noexcept-contract` | `4810469369dff74c1e23142bc68584cc9aca7e31` | Acompanhar revisão no próprio PR |
| [#747](https://github.com/coin3d/coin/pull/747) | `fix/sonodekitpath-object-model-safety` | `d73b8c8b47f6c2e915008e65fed270b03a9a5409` | Acompanhar revisão no próprio PR |
| [#746](https://github.com/coin3d/coin/pull/746) | `fix/sotemppath-policy-preservation` | `e98c4da7973433fea7b2416c3161ccb0a3418186` | Acompanhar revisão no próprio PR |

## Lab fixo

`lab/open-prs-integration` contém os heads atuais dos 17 PRs abertos sobre o master de referência. Todos os heads estão contidos por ancestralidade Git.

- Base master: `674e74267df863dbaf50416c477bc7f918a826d8`.
- Lab e SHA testado: `b27e37a6dd3ae941bcffcf480a377adc81e3fc27`.
- Worktree: `/tmp/coin-lab-open-prs-integration`.
- Build: `/tmp/coin-lab-open-prs-build`, configuração Debug, testes habilitados, renderer OpenGL legado habilitado e `COIN_THREADSAFE=OFF`.
- Resultado: compilação concluída e **105 testes aprovados, zero falhas e nenhum teste ignorado**.
- Limite desta validação: exemplos opcionais, Tracy, Windows, macOS e configuração threadsafe não foram exercitados.

As resoluções de integração combinam armazenamento lazy com alocação alinhada e tratamento de OOM de `SbHash`, preservam as entradas de NEWS e mantêm os caminhos de linking estático/compartilhado e os exemplos Qt/Hello World. Essas resoluções estão nos commits de merge do lab; as branches publicadas dos PRs mantêm seus heads originais.

A execução dos testes gráficos requer um display X acessível e `COIN_GLX_PIXMAP_DIRECT_RENDERING=1` neste ambiente com llvmpipe. Sem essa configuração, o renderer offscreen não consegue criar o contexto GLX indireto.

```bash
cmake -S /tmp/coin-lab-open-prs-integration -B /tmp/coin-lab-open-prs-build \
  -DCOIN_BUILD_TESTS=ON -DCOIN_BUILD_DOCUMENTATION=OFF
cmake --build /tmp/coin-lab-open-prs-build --parallel 8
xvfb-run -a -s '-screen 0 1280x1024x24 +extension GLX' \
  env LIBGL_ALWAYS_SOFTWARE=1 COIN_GLX_PIXMAP_DIRECT_RENDERING=1 \
  ctest --test-dir /tmp/coin-lab-open-prs-build --output-on-failure --parallel 4
```

## Composição atual do lab

| PR | Autor | Head integrado | Commit de integração |
| --- | --- | --- | --- |
| [#614](https://github.com/coin3d/coin/pull/614) | oursland | `98117a589374321696ed6e809a7ae351bcd5df4f` | `8874ca284aa6b2ca6dd59d7a06aa750434dc2cd8` |
| [#632](https://github.com/coin3d/coin/pull/632) | veelo | `a2d766f9fea95f43d35217d21c21b3c383e83725` | `b27e37a6dd3ae941bcffcf480a377adc81e3fc27` |
| [#746](https://github.com/coin3d/coin/pull/746) | Dikluwe | `e98c4da7973433fea7b2416c3161ccb0a3418186` | `3f21903b721340cb4a3b101b4f3f2019853ca5de` |
| [#747](https://github.com/coin3d/coin/pull/747) | Dikluwe | `d73b8c8b47f6c2e915008e65fed270b03a9a5409` | `2f4255119cc6ae9c4a5eaae4bdfe49e5fc7d5b5b` |
| [#753](https://github.com/coin3d/coin/pull/753) | Dikluwe | `4810469369dff74c1e23142bc68584cc9aca7e31` | `2dc6ad3706d1522988ec49efcbf045f851ff0063` |
| [#754](https://github.com/coin3d/coin/pull/754) | Dikluwe | `25a0c03d629eb07f776375ce9c9630036341b43a` | `a5120a56dba216e7b8b84820bbaf540b4c7cdae7` |
| [#755](https://github.com/coin3d/coin/pull/755) | Dikluwe | `7fa9bae533a051fa0fd450f23e32c0e39d8e1c6b` | `19f7cdb9929ec3d7a9a1c898dda2a472a0578839` |
| [#758](https://github.com/coin3d/coin/pull/758) | Dikluwe | `900f11a1570bd03555578aae6f9df9d0271d6189` | `13c3a5853ad471f0a0156141d63360c70ff8f8c3` |
| [#761](https://github.com/coin3d/coin/pull/761) | Dikluwe | `79984c883569834583543b8699b6a254d5afc904` | `a8505e1fe3340c15d6c2c5b223578b9ac14ceabe` |
| [#765](https://github.com/coin3d/coin/pull/765) | Dikluwe | `c79366f12d98585fd90bca1e6bc67eaaf33c3aeb` | `4bdd9d7c919ec96b171e939509d5ff573d8991cb` |
| [#767](https://github.com/coin3d/coin/pull/767) | Dikluwe | `fd9f7db48838e6df3b9f747c385e5d6a2837ca4e` | `cd29a6dcb0186f124aa72173d555b533199afeca` |
| [#768](https://github.com/coin3d/coin/pull/768) | Dikluwe | `b3ae400d0da5044739d359b20a96d83f62ec9225` | `7d1a7ba2b8aa217203ecfb69a75792f9b753a3dc` |
| [#769](https://github.com/coin3d/coin/pull/769) | Dikluwe | `dacbbd984d17a66be39ea6e78d0c8c175a6a34db` | `01cae5a3607af5877be8148b2f021eea52fa8cad` |
| [#770](https://github.com/coin3d/coin/pull/770) | Dikluwe | `729a5b9cd5351c7799519917381ec3a39b669be2` | `e986afc87b9d79c40f255be85f1406857bea04ff` |
| [#771](https://github.com/coin3d/coin/pull/771) | Dikluwe | `5a311648e38c63f9b6597e0968d3d1dc3428ba31` | `3775d6709f759720f1c4f1556d76aa401495e8f7` |
| [#772](https://github.com/coin3d/coin/pull/772) | Dikluwe | `6a169aaac4725014cddd0bd7665224ccd8950a67` | `11f8f86fcfd9e0d0f33eb9e8931b3f29d7c53f02` |
| [#774](https://github.com/coin3d/coin/pull/774) | Dikluwe | `7f2e9335522a6ffdc9d66c4c18f80e4fcca5d0e5` | `e5e9dd5d892759befaeaf38ca4642da779b2e6fa` |

## Frente XML e SCXML

Checklist de todas as branches existentes desta frente, candidatas ainda planejadas e gates restantes: [CHECKLIST_XML_SCXML.md](CHECKLIST_XML_SCXML.md).

Esta frente usa o lab fixo `b27e37a6dd3ae941bcffcf480a377adc81e3fc27` e o master de destino `674e74267df863dbaf50416c477bc7f918a826d8`. A composição dos 17 PRs listados acima foi conferida por ancestralidade Git. Os testes abaixo foram feitos somente localmente, com builds em `/dev/shm`; nenhum candidato foi publicado ou incorporado ao lab fixo.

| Contribuição | Head de trabalho | Head testado em `lab/teste` | Head local de PR sobre master | Resultado |
| --- | --- | --- | --- | --- |
| Entidades XML | `codex/fix/cc-xml-entity-abi` em `6763526cd9a1c8910441f7e7c1ba82cdb4d458ac` | `70c7a25e018cbb12aa230e142fb344e20e301da0` | `codex/pr/cc-xml-entity-abi` em `21dc59664b2ece234822901e9498ab86c539c6d6` | Lab: build, 2/2 testes específicos e 107/107 do conjunto. Master: build, 2/2 específicos e 80/80 do conjunto. |
| `ScXMLSendElt` namelist | `codex/scxml-sendelt-tokenize-sbstring` em `c858099dfb213e9bc24c74d0da49f9e44884cc3d` | `2453add43679dc26b4bb690e4fd7f6bcd3c8a1c4` | `codex/pr/scxml-sendelt-tokenize` em `5e8945f2086049850f8638bed144112fd85e1eb0` | Lab: build, 1 teste específico com 9 checks e 105/105 do conjunto. Master: build, 1 teste específico com 9 checks e 78/78 do conjunto. |
| `cc_xml_path` | `codex/fix/cc-xml-path-loops-truncate` em `cd9761452df30613be7ac48291f071784f9a2478` | `efa664d94830decb3ad6a5a67ed748cea3e3b3c9` | `codex/pr/cc-xml-path-loops-truncate` em `bffa176d456a623078d1ded247d8b04bd3f7d898` | Lab: build, 2 testes específicos com 30 checks e 105/105 do conjunto. Master: build, 2 testes específicos com 30 checks e 78/78 do conjunto. |
| Escaping e formatos inteiros | `codex/fix/cc-xml-escaping-inttypes` em `478ca2ffdee2afff9ad080c5887496ff93dc4392` | `3847c79ace6df46d1524f240153445ac417de113` | `codex/pr/cc-xml-escaping-inttypes` em `0935125576bf039ce59f257a634549affc4137cc` | Lab: build, 2 testes específicos com 33 checks e 105/105 do conjunto. Master: build, 2 testes específicos com 33 checks e 78/78 do conjunto. |
| Parser transacional | `codex/work/cc-xml-parser-transactional` em `162dc998af31291fb2d861b0d6b1431acb1c18da` | `fc4abe5eb7ff525703721d52547f27d8a54509db` | `codex/pr/cc-xml-parser-transactional` em `162dc998af31291fb2d861b0d6b1431acb1c18da` | Lab: build, teste específico 1/1 e 106/106 do conjunto. Master: build, teste específico 1/1 e 79/79 do conjunto. |
| Coalescência de character data | `codex/work/cc-xml-coalesce-cdata` em `5c4a34e04561eaef39abecf9acbca9609d39ddd5` | `3152c194784a8ce9531872e9ab12587ecc321321` | `codex/pr/cc-xml-coalesce-cdata` em `5c4a34e04561eaef39abecf9acbca9609d39ddd5` | Lab: build, 5 testes específicos com 82 checks e 105/105 do conjunto. Master: build, 5 específicos com 82 checks e 78/78 do conjunto. |
| Ownership do DOM | `codex/work/cc-xml-dom-ownership` em `28d74b9982bf0791d50abd60a1dcbbd7817fe1f0` | `1964dceeb7143c3d208e293ea19e966c9d4ba1d2` | `codex/pr/cc-xml-dom-ownership` em `28d74b9982bf0791d50abd60a1dcbbd7817fe1f0` | Lab: build, 3 testes/29 checks, teste C e 105/105 do conjunto. Master: build, mesmos testes específicos e 78/78 do conjunto. ASan/UBSan com LeakSanitizer: testes específicos e cliente C aprovados. |
| Regressões DOM e fuzzer | `codex/work/cc-xml-dom-regressions-fuzz` em `dcb54791bd3609458f7df5c5e22e83682b68358a` | `a46f8c1926bd339bda670e23e7b4b0c80d91b0c6` | `codex/pr/cc-xml-dom-regressions-fuzz` em `dcb54791bd3609458f7df5c5e22e83682b68358a`, **destino dependente** `codex/pr/cc-xml-dom-ownership` em `28d74b9982bf0791d50abd60a1dcbbd7817fe1f0` | Lab + ownership: build, 5 testes DOM/43 checks e 105/105 do conjunto. Destino ownership/master: build, mesmos testes e 78/78 do conjunto. Clang + ASan/UBSan/LeakSanitizer: 5 testes DOM/43 checks, cliente C e smoke libFuzzer 1/1 (100 execuções). |

Para os dezesseis builds normais acima, os comandos de configuração e compilação foram:

```bash
cmake -S <source> -B <build> -DCMAKE_BUILD_TYPE=Debug \
  -DCOIN_BUILD_TESTS=ON -DCOIN_BUILD_DOCUMENTATION=OFF -DCOIN_THREADSAFE=OFF
cmake --build <build> --parallel 8
xvfb-run -a -s '-screen 0 1280x1024x24 +extension GLX' \
  env LIBGL_ALWAYS_SOFTWARE=1 COIN_GLX_PIXMAP_DIRECT_RENDERING=1 \
  ctest --test-dir <build> --output-on-failure --parallel 4
```

| Contribuição | `<source>` do lab e do PR | `<build>` do lab e do PR | Teste específico |
| --- | --- | --- | --- |
| Entidades XML | `/tmp/coin-lab-teste-cc-xml-entity-abi`, `/tmp/coin-pr-cc-xml-entity-abi` | `/dev/shm/coin-xml-scxml-entity-abi/build`, `/dev/shm/coin-xml-scxml-entity-abi-pr/build` | `ctest --test-dir <build> -R '^XmlEntity' --output-on-failure` |
| `ScXMLSendElt` | `/tmp/coin-lab-teste-scxml-sendelt-tokenize`, `/tmp/coin-pr-scxml-sendelt-tokenize` | `/dev/shm/coin-xml-scxml-sendelt-lab/build`, `/dev/shm/coin-xml-scxml-sendelt-pr/build` | `<build>/bin/CoinTests --run_test='*scxml_send_namelist_ignores_empty_tokens'` |
| `cc_xml_path` | `/tmp/coin-lab-teste-cc-xml-path-loops-truncate`, `/tmp/coin-pr-cc-xml-path-loops-truncate` | `/dev/shm/coin-xml-scxml-path-lab/build`, `/dev/shm/coin-xml-scxml-path-pr/build` | `<build>/bin/CoinTests --run_test='*cc_xml_path_*'` |
| Escaping e formatos inteiros | `/tmp/coin-lab-teste-cc-xml-escaping-inttypes`, `/tmp/coin-pr-cc-xml-escaping-inttypes` | `/dev/shm/coin-xml-scxml-escaping-lab/build`, `/dev/shm/coin-xml-scxml-escaping-pr/build` | `<build>/bin/CoinTests --run_test='*write_escapes_character_data_and_attributes,*integer_format_round_trip'` |
| Parser transacional | `/tmp/coin-lab-teste-cc-xml-parser-transactional`, `/tmp/coin-pr-cc-xml-parser-transactional` | `/dev/shm/coin-xml-parser-transactional-lab/build`, `/dev/shm/coin-xml-parser-transactional-pr/build` | `ctest --test-dir <build> -R '^CoinXmlTransactionalRead$' --output-on-failure` |
| Coalescência | `/tmp/coin-lab-teste-cc-xml-coalesce-cdata`, `/tmp/coin-pr-cc-xml-coalesce-cdata` | `/dev/shm/coin-xml-coalesce-lab/build`, `/dev/shm/coin-xml-coalesce-pr/build` | `<build>/bin/CoinTests --run_test='*character_data_*,*formatting_whitespace_*,*markup_remains_*'` |
| Ownership do DOM | `/tmp/coin-lab-teste-cc-xml-dom-ownership`, `/tmp/coin-pr-cc-xml-dom-ownership` | `/dev/shm/coin-xml-dom-ownership-lab/build`, `/dev/shm/coin-xml-dom-ownership-pr/build` | `<build>/bin/CoinTests --run_test='*dom_*_ownership'` e `ctest --test-dir <build> -R '^XmlCApiTest$' --output-on-failure` |
| Regressões DOM e fuzzer | `/tmp/coin-lab-teste-cc-xml-dom-regressions-fuzz`, `/tmp/coin-pr-cc-xml-dom-regressions-fuzz` | `/dev/shm/coin-xml-dom-regressions-lab/build`, `/dev/shm/coin-xml-dom-regressions-pr/build` | `<build>/bin/CoinTests --run_test='*buffer_round_trip_compares_real_dom,*empty_cdata_child_serializes_without_null_dereference,*dom_*_ownership'` |

Os `git range-diff` dos quatro primeiros candidatos, da coalescência e do ownership confirmaram equivalência exata dos commits. Para o parser transacional e as regressões DOM, o `range-diff` aponta somente o contexto diferente da inserção do mesmo bloco de teste no CMake; os demais arquivos da mudança são idênticos entre lab e PR. Os diffs de PR contêm apenas a respectiva contribuição. Os oito worktrees e refs `lab/teste` individuais foram removidos após a validação, sem eliminar os commits de trabalho ou as branches locais de PR.

Teste cumulativo adicional: `lab/teste/cc-xml-parser-coalesce`, criado a partir do mesmo lab fixo `b27e37a6dd3ae941bcffcf480a377adc81e3fc27`, recebeu **somente** parser transacional (`fe8dc97629`) e coalescência (`3df2a4a99c5009548bf5ec87988c9a4d5f1c248d`). Configurado e compilado com os comandos acima em `/dev/shm/coin-xml-parser-coalesce-lab/build`; teste transacional 1/1, coalescência 5 testes/82 checks e conjunto 106/106 aprovados. Worktree e ref temporários removidos após o teste.

Teste cumulativo seguinte: `lab/teste/cc-xml-parser-coalesce-ownership`, a partir do commit anterior, incorporou **somente** ownership (`3acee1f4e0ca5da2049f9fcc9bbd82dac88634ab` e `ea2ddc8c4c7718a24c3855fe870a31aacba1ab0d`). O conflito em `document.cpp` foi resolvido preservando `parserroot` provisória, os testes de coalescência e os testes de ownership. Configurado e compilado com os comandos acima em `/dev/shm/coin-xml-dom-ownership-cumulative/build`; parser 1/1, API C 1/1, 8 testes específicos/111 checks e conjunto 106/106 aprovados. Worktree e ref temporários removidos após o teste.

Teste cumulativo final deste ciclo: `lab/teste/cc-xml-cumulative-regressions`, iniciado no histórico cumulativo `ea2ddc8c4c7718a24c3855fe870a31aacba1ab0d` (descendente direto do lab fixo, contendo apenas parser, coalescência e ownership), recebeu **somente** a candidata de regressões/fuzzer como `ab6db992cbb993951227637a0824635ad7190f62`. Os conflitos textuais em `src/xml/document.cpp` e `testsuite/CMakeLists.txt` foram resolvidos preservando ambos os conjuntos de testes, o teste de parser e o alvo opcional do fuzzer. Configuração e build normais com os comandos acima em `/dev/shm/coin-xml-cumulative-regressions/build`; `ctest -R '^CoinXmlTransactionalRead$'` 1/1, `CoinTests --run_test='*buffer_round_trip_compares_real_dom,*empty_cdata_child_serializes_without_null_dereference,*dom_*_ownership,*character_data_*,*formatting_whitespace_*,*markup_remains_*'` 10 testes/125 checks, conjunto 106/106. Worktree e ref temporários removidos; lab fixo permaneceu em `b27e37a6dd3ae941bcffcf480a377adc81e3fc27`.

### Lab cumulativo XML/SCXML com Expat 2.9.0 (2026-10-06)

- Base fixa conferida: `lab/open-prs-integration` = `b27e37a6dd3ae941bcffcf480a377adc81e3fc27`, master de destino = `674e74267df863dbaf50416c477bc7f918a826d8`. A composição da base é master + 17 PRs abertos, discriminados acima. A cópia `lab/teste/cc-xml-expat-cumulative` foi criada em worktree separado `/tmp/coin-lab-teste-cc-xml-expat-cumulative` e recebeu **somente** as nove candidatas prontas abaixo, em 14 commits cherry-picked. A decisão `DOCTYPE`, limites e as branches SCXML de mapas não foram incorporadas.
- Heads de origem: Expat `aef999cb3642638c61a82128f0db69ad9e532702`; entidades `21dc59664b2ece234822901e9498ab86c539c6d6`; path `bffa176d456a623078d1ded247d8b04bd3f7d898`; escaping/inteiros `0935125576bf039ce59f257a634549affc4137cc`; parser transacional `162dc998af31291fb2d861b0d6b1431acb1c18da`; coalescência `5c4a34e04561eaef39abecf9acbca9609d39ddd5`; ownership `28d74b9982bf0791d50abd60a1dcbbd7817fe1f0`; regressões/fuzzer dependentes `dcb54791bd3609458f7df5c5e22e83682b68358a`; SCXML send `5e8945f2086049850f8638bed144112fd85e1eb0`.
- O cherry-pick chegou a `519932f779b2c9c95a548ec3c6887f0ef6226e47`. Conflitos em `testsuite/CMakeLists.txt` e `src/xml/document.cpp` foram resolvidos preservando os testes do lab, do parser, de coalescência, ownership e DOM. O primeiro teste instrumentado encontrou uma liberação de documento omitida nessa resolução, embora ela já estivesse correta na branch de PR de coalescência. Restaurada **só na cópia cumulativa** pelo commit `091c29fa32684f4914f59607f6850f8db7837a74`, head final validado. As fontes de teste geradas pelo CMake foram regeneradas antes da repetição. `git diff --check` não apontou erros; branches de PR e base fixa não foram alteradas.
- Comandos de build em RAM: `cmake -S /tmp/coin-lab-teste-cc-xml-expat-cumulative -B /dev/shm/coin-xml-expat-cumulative-internal -DCOIN_BUILD_TESTS=ON -DCOIN_BUILD_DOCUMENTATION=OFF -DCMAKE_BUILD_TYPE=Debug` e `cmake --build /dev/shm/coin-xml-expat-cumulative-internal --parallel 8`; repetidos em `/dev/shm/coin-xml-expat-cumulative-external` com `-DUSE_EXTERNAL_EXPAT=ON` (Expat do sistema 2.6.1). Após o ajuste de integração, `cmake -S <worktree> -B <build>` e `cmake --build <build> --target CoinTests --parallel 4` regeneraram/recompilaram os testes em ambos.
- Comando do conjunto: `xvfb-run -a -s '-screen 0 1280x1024x24 +extension GLX' env LIBGL_ALWAYS_SOFTWARE=1 COIN_GLX_PIXMAP_DIRECT_RENDERING=1 ctest --test-dir <build> --output-on-failure --parallel 4`. Resultado final: **108/108** com Expat embarcado 2.9.0 e **108/108** com Expat externo 2.6.1. As regressões específicas, via `bin/CoinTests --run_test='*buffer_round_trip_compares_real_dom,*write_escapes_character_data_and_attributes,*integer_format_round_trip,*character_data_*,*formatting_whitespace_*,*markup_remains_*,*empty_cdata_child_serializes_without_null_dereference,*dom_*_ownership,*cc_xml_path_*,*scxml_send_namelist_ignores_empty_tokens' --report_level=short`, deram **15 testes/185 checks**.
- Variante instrumentada em `/dev/shm/coin-xml-expat-cumulative-asan`: `cmake -S <worktree> -B <build> -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug -DCOIN_BUILD_TESTS=ON -DCOIN_BUILD_FUZZERS=ON -DCOIN_ENABLE_SANITIZERS=ON -DCOIN_BUILD_DOCUMENTATION=OFF -DCOIN_THREADSAFE=OFF`; `cmake --build <build> --target CoinXmlDomFuzzer CoinTests XmlCApiTest --parallel 8`. Com `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1`, o fuzzer smoke `ctest -R '^CoinXmlDomFuzzerSmoke$'` passou **1/1**, `XmlCApiTest` passou e as regressões XML sem o caso SCXML passaram **14 testes/176 checks**. O caso SCXML passou **1 teste/9 checks** com `detect_leaks=0`, mas, com LeakSanitizer ativo, revelou **30 bytes em quatro alocações** de `ScXMLEvent::setAssociation()`: o destrutor de `ScXMLEventP` no master já contém `FIXME: delete strings in associations map (values only)`. É um defeito anterior às candidatas, a tratar em contribuição SCXML separada; não se declara o conjunto XML+SCXML livre de leaks.
- Após a validação, o worktree e a ref temporários foram removidos; a base fixa, as branches candidatas e a documentação local foram preservadas. A validação no Windows continua pendente antes de publicação.

Sanitizers no PR local de ownership: `cmake -S /tmp/coin-pr-cc-xml-dom-ownership -B /dev/shm/coin-xml-dom-ownership-asan/build -DCMAKE_BUILD_TYPE=Debug -DCOIN_BUILD_TESTS=ON -DCOIN_BUILD_DOCUMENTATION=OFF -DCOIN_THREADSAFE=OFF '-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined' '-DCMAKE_SHARED_LINKER_FLAGS=-fsanitize=address,undefined'`; build `cmake --build /dev/shm/coin-xml-dom-ownership-asan/build --target CoinTests XmlCApiTest --parallel 8`; `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1` nos dois executáveis. Ambos aprovados, sem diagnóstico dos sanitizers.

```bash
env ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  /dev/shm/coin-xml-dom-ownership-asan/build/bin/CoinTests --run_test='*dom_*_ownership'
env ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  /dev/shm/coin-xml-dom-ownership-asan/build/bin/XmlCApiTest
```

Gate de regressões DOM/fuzzer: o lab temporário começou em `b27e37a6dd3ae941bcffcf480a377adc81e3fc27`, recebeu **somente** os commits de ownership `1ff3e4e83f` e `01d431c1dd` e a candidata `a46f8c1926bd339bda670e23e7b4b0c80d91b0c6`. A sobreposição em `testsuite/CMakeLists.txt` preservou os testes GLX do lab e o novo alvo opcional; `git range-diff` mostrou só esse contexto. No PR, `git merge-base codex/pr/cc-xml-dom-ownership codex/pr/cc-xml-dom-regressions-fuzz` retornou `28d74b9982bf0791d50abd60a1dcbbd7817fe1f0`; o diff próprio toca nove arquivos, sem trazer os PRs do lab. Builds normais usaram os comandos acima, com Xvfb na suíte do lab e do PR. Para sanitizers/fuzzer:

```bash
cmake -S /tmp/coin-work-cc-xml-dom-regressions-fuzz \
  -B /dev/shm/coin-xml-dom-regressions-fuzz/build \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_BUILD_TYPE=Debug -DCOIN_BUILD_TESTS=ON \
  -DCOIN_BUILD_FUZZERS=ON -DCOIN_ENABLE_SANITIZERS=ON \
  -DCOIN_BUILD_DOCUMENTATION=OFF -DCOIN_THREADSAFE=OFF
cmake --build /dev/shm/coin-xml-dom-regressions-fuzz/build \
  --target CoinXmlDomFuzzer CoinTests XmlCApiTest --parallel 8
env ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir /dev/shm/coin-xml-dom-regressions-fuzz/build \
  -R '^CoinXmlDomFuzzerSmoke$' --output-on-failure
env ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  /dev/shm/coin-xml-dom-regressions-fuzz/build/bin/CoinTests \
  --run_test='*buffer_round_trip_compares_real_dom,*empty_cdata_child_serializes_without_null_dereference,*dom_*_ownership'
env ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  /dev/shm/coin-xml-dom-regressions-fuzz/build/bin/XmlCApiTest
```

O smoke limita cada entrada a 4096 bytes, 10 segundos e 2048 MiB de RSS; o harness também rejeita entradas acima de 4096 bytes. É um teste breve de infraestrutura, não uma campanha de fuzzing nem uma política geral de limites do parser. Tudo passou sem diagnóstico dos sanitizers. O worktree/ref temporário `lab/teste/cc-xml-dom-regressions-fuzz` foi removido após a validação; o lab fixo permaneceu inalterado.

Próximos ciclos: confirmar a política de DOCTYPE e validar sua candidata no lab e no destino de PR; estudar limites antes de defini-los. As quatro branches SCXML de mapas ainda incluem infraestrutura antiga de `SbHash` ou `SbAdaptiveMap` e conflitam com os PRs do lab: extrair primeiro o delta SCXML em branches de trabalho separadas, com pré-requisitos explícitos, antes de criar suas cópias `lab/teste`.

Estudo de política DOCTYPE: o commit histórico `bd8287e3276887ef9064740923fc1ad4a86f9b72` foi extraído sobre `codex/pr/cc-xml-parser-transactional` em `codex/work/cc-xml-doctype-policy` (`8100371983c0fd19de9950373050fef188cdaf32`), **somente como candidata de trabalho**. Configuração/build normais em `/dev/shm/coin-xml-doctype-work/build`; `ctest -R '^CoinXmlTransactionalRead$'` 1/1 e conjunto 79/79 passaram. A pesquisa local não encontrou `DOCTYPE` nos XML/SCXML do FreeCAD clonado nem uso dele nos consumidores `cc_xml` do Coin; as ocorrências no repositório Coin são plist, SVG e HTML fora deste parser. O Expat informa que o callback de início de `DOCTYPE` ocorre antes do subset e que entidades externas são ignoradas sem handler; a opção segura recomendada é rejeição por padrão. O contrato aguarda confirmação do usuário: não há branch de PR nem cópia `lab/teste` para este item.

## Trabalhos e auditorias pendentes

As branches abaixo são desenvolvimento/trabalho ou conteúdo legado aguardando auditoria. Um nome que mencione PR, review, bench ou archive não define um tipo adicional. Os labs auxiliares precisam ter seu conteúdo útil extraído ou ser eliminados; não são novas bases fixas. `master` é a referência base e fica fora dos três tipos de trabalho.

| Branch existente | Worktree | Próxima ação |
| --- | --- | --- |
| `archive/lab-gl-bump-smallmap-evaluation-legacy-20261002` | Sem worktree associado | Auditar conteúdo exclusivo; aproveitar ou eliminar |
| `archive/lab-map-instrumentation-legacy-20261002` | Sem worktree associado | Auditar conteúdo exclusivo; aproveitar ou eliminar |
| `archive/lab-sb-lists-evaluation-legacy-20261002` | Sem worktree associado | Auditar conteúdo exclusivo; aproveitar ou eliminar |
| `archive/local-pre-fork-sync-20261003/codex/cc-oom-consumers` | Sem worktree associado | Auditar conteúdo exclusivo; aproveitar ou eliminar |
| `archive/superseded/gl-bump-contexts-small-20260919` | Sem worktree associado | Auditar conteúdo exclusivo; aproveitar ou eliminar |
| `bench/honeycomb-scale-benchmark` | Sem worktree associado | Auditar contribuição para PR e dependências |
| `bench/sbhash-relink-local` | Sem worktree associado | Auditar contribuição para PR e dependências |
| `codex/cc-hash-hardening-followup` | `/tmp/coin-cc-hash-hardening-followup` | Chat de dicionários: reconciliar API externa com #769–772 e preparar PR próprio; ver [plano](DICT_CC_SB.md) |
| `codex/cc-list-hardening` | Sem worktree associado | Auditar contribuição para PR e dependências |
| `codex/cc-test-audit` | Sem worktree associado | Auditar contribuição para PR e dependências |
| `codex/coin-atexit-hardening` | `/mnt/Laranja/Git/externos/coin` | Auditar contribuição para PR e dependências |
| `codex/coin-prime-boundary` | `/home/dikluwe/.codex/worktrees/coin-prime-boundary/coin` | Auditar contribuição para PR e dependências |
| `codex/coin-render` | `/tmp/coin-render-first-frame` | Continuar desenvolvimento do renderizador |
| `codex/fix/cc-xml-coalesce-cdata` | Sem worktree associado | Contribuição transportada para `codex/work/cc-xml-coalesce-cdata`; conferir conteúdo exclusivo antes de eliminar a branch original |
| `codex/fix/cc-xml-dom-ownership-hardening` | Sem worktree associado | Ownership e o conteúdo exclusivo de testes/sanitizers/fuzzer já foram transportados e validados em dois PRs locais dependentes; conferir conteúdo residual antes de encerrar a origem |
| `codex/fix/cc-xml-entity-abi` | Sem worktree associado | Candidata validada em `codex/pr/cc-xml-entity-abi`; manter até publicação localmente autorizada |
| `codex/fix/cc-xml-escaping-inttypes` | Sem worktree associado | Candidata validada em `codex/pr/cc-xml-escaping-inttypes`; aguardar autorização para publicação |
| `codex/fix/cc-xml-path-loops-truncate` | Sem worktree associado | Candidata validada em `codex/pr/cc-xml-path-loops-truncate`; aguardar autorização para publicação |
| `codex/fix/cc-xml-reject-doctype` | Sem worktree associado | Parser transacional já extraído e validado; política DOCTYPE extraída para `codex/work/cc-xml-doctype-policy`, aguardando confirmação do contrato e gate lab/PR |
| `codex/fix/issue417-sotext2` | `/home/dikluwe/.codex/worktrees/issue417-sotext2/coin` | Comparar com PR relacionado; incorporar conteúdo exclusivo e eliminar redundância |
| `codex/fltk-connector` | `/tmp/coin-fltk-connector` | Auditar contribuição para PR e dependências |
| `codex/font-pr1-utf8-picking` | Sem worktree associado | Auditar contribuição para PR e dependências |
| `codex/font-pr2-bitmap-limits` | Sem worktree associado | Auditar contribuição para PR e dependências |
| `codex/font-pr3-cache-lifetime` | Sem worktree associado | Auditar contribuição para PR e dependências |
| `codex/font-pr4-fallback-ownership` | Sem worktree associado | Auditar contribuição para PR e dependências |
| `codex/font-pr5-font-discovery` | Sem worktree associado | Auditar contribuição para PR e dependências |
| `codex/font-pr6-tessellation` | `/home/dikluwe/.codex/worktrees/text-font-followup/coin` | Auditar contribuição para PR e dependências |
| `codex/improve-sblist` | Sem worktree associado | Auditar contribuição para PR e dependências |
| `codex/issue-374-bbox-tests` | Sem worktree associado | Auditar contribuição para PR e dependências |
| `codex/maps/core/adaptive-map` | Sem worktree associado | Auditar contribuição para PR e dependências |
| `codex/maps/core/sequential-map` | Sem worktree associado | Auditar contribuição para PR e dependências |
| `codex/maps/integration/profiler-containers` | Sem worktree associado | Auditar contribuição para PR e dependências |
| `codex/maps/use/coinresources-adaptive` | Sem worktree associado | Auditar contribuição para PR e dependências |
| `codex/maps/use/fieldcontainer-mfield-sizes-hash` | Sem worktree associado | Auditar contribuição para PR e dependências |
| `codex/maps/use/profiler-action-timings-sequential` | Sem worktree associado | Auditar contribuição para PR e dependências |
| `codex/maps/use/scxml-attributes-adaptive` | Sem worktree associado | Extrair a mudança SCXML da infraestrutura antiga de mapas; declarar pré-requisitos |
| `codex/maps/use/scxml-document-ids-adaptive` | Sem worktree associado | Extrair a mudança SCXML da infraestrutura antiga de mapas; declarar pré-requisitos |
| `codex/maps/use/scxml-evaluator-temporaries-adaptive` | Sem worktree associado | Extrair a mudança SCXML da infraestrutura antiga de mapas; declarar pré-requisitos |
| `codex/maps/use/scxml-type-registry-hash-name` | Sem worktree associado | Extrair a mudança SCXML da infraestrutura antiga de mapas; declarar pré-requisitos |
| `codex/profiler-tokenize-sbstring` | Sem worktree associado | Comparar com PR relacionado; incorporar conteúdo exclusivo e eliminar redundância |
| `codex/qt-quarter-regressions` | Sem worktree associado | Auditar contribuição para PR e dependências |
| `codex/sbdict-ccdict-migration` | `/home/dikluwe/.codex/worktrees/sbdict-ccdict-migration/coin` | Chat de dicionários: reconciliar com cc_hash e pré-requisitos atuais; validar migração, Windows e ABI |
| `codex/sbdict-hash-hardening` | Sem worktree associado | Chat de dicionários: incorporar testes úteis nos candidatos posteriores e encerrar a branch substituída após conferir o conteúdo |
| `codex/sbname/experiment/compact-entry-pool` | Sem worktree associado | Auditar contribuição para PR e dependências |
| `codex/scene-cache/compiled-prototype` | Sem worktree associado | Auditar contribuição para PR e dependências |
| `codex/scxml-sendelt-tokenize-sbstring` | `/tmp/coin-work-scxml-sendelt-tokenize` | Candidata com teste validada em `codex/pr/scxml-sendelt-tokenize`; manter até publicação localmente autorizada |
| `codex/soinput-read-errors` | `/home/dikluwe/.codex/worktrees/soinput-read-status/coin` | Auditar contribuição para PR e dependências |
| `codex/text-font-followup` | Sem worktree associado | Auditar contribuição para PR e dependências |
| `codex/wgpu-depth-offset` | Sem worktree associado | Auditar contribuição para PR e dependências |
| `codex/wgpu-frame-preparation` | Sem worktree associado | Auditar contribuição para PR e dependências |
| `codex/work/bump-cache-diagnostics-memory` | Sem worktree associado | Validar candidato descrito acima |
| `codex/work/bump-cache-ready-path` | Sem worktree associado | Validar candidato descrito acima |
| `codex/work/bump-shared-program-pool` | Sem worktree associado | Validar candidato descrito acima |
| `codex/work/cc-xml-coalesce-cdata` | `/tmp/coin-work-cc-xml-coalesce-cdata` | Candidata validada em `codex/pr/cc-xml-coalesce-cdata`, inclusive com parser transacional no lab cumulativo; aguardar gate Windows |
| `codex/work/cc-xml-dom-ownership` | `/tmp/coin-work-cc-xml-dom-ownership` | Candidata validada em `codex/pr/cc-xml-dom-ownership`, com ASan/UBSan focado e lab cumulativo; aguardar gate Windows |
| `codex/work/cc-xml-parser-transactional` | `/tmp/coin-work-cc-xml-parser-transactional` | Candidata validada em `codex/pr/cc-xml-parser-transactional`; aguardar gate Windows e publicação autorizada |
| `codex/work/glglue-lifetime-audit` | Sem worktree associado | Validar candidato descrito acima |
| `codex/work/sbhash-insert-allocation-failure` | Sem worktree associado | Validar candidato descrito acima |
| `codex/work/sbheap-cancel-documentation` | Sem worktree associado | Validar candidato descrito acima |
| `fix/cc-storage-thread-exit-cleanup` | Sem worktree associado | Auditar contribuição para PR e dependências |
| `fix/socallbacklist-add-exception-safety` | Sem worktree associado | Auditar contribuição para PR e dependências |
| `fix/socallbacklist-assignment-exception-safety` | Sem worktree associado | Auditar contribuição para PR e dependências |
| `fix/socallbacklist-copy-assignment-exception-safety` | Sem worktree associado | Auditar contribuição para PR e dependências |
| `lab/cc_round2_tsan` | `/home/dikluwe/.codex/worktrees/cc-round2-tsan/coin` | Auditar conteúdo útil; consolidar em trabalho e encerrar lab legado |
| `lab/gl-bump-smallmap-evaluation` | Sem worktree associado | Auditar conteúdo útil; consolidar em trabalho e encerrar lab legado |
| `lab/map-instrumentation` | Sem worktree associado | Auditar conteúdo útil; consolidar em trabalho e encerrar lab legado |
| `lab/sb-lists-evaluation` | Sem worktree associado | Auditar conteúdo útil; consolidar em trabalho e encerrar lab legado |

## Fechamento SbHash

- `codex/pre-pr/sbhash-prerequisites`: `25829e5e6bb0189c5a3a6af4fd7b6aafed6fe4ec`, resolução local de lazy storage sobre #772/fixture, sem PR próprio.
- `codex/pre-pr/sbhash-complete`: `5112bbcaf06755fb505772f3a77a737f33e0d3a1`, contribuição própria em seis arquivos, pronta Linux.
- `lab/teste/sbhash-complete`: `10cfd24634fcbc6220432b40bc9e82657e01682c`, 106/106 testes, delta idêntico ao da contribuição; base fixa preservada.

Pré-PR estática 91/91 e sanitizadores aprovados. Windows e teste final sobre master de publicação pendentes. [Registro completo](FECHAMENTO_SBHASH.md).

## Fechamento do tipo SbSmallMap

- `codex/pre-pr/sbsmallmap-complete`: `ff1d09ddf3d458bb73023aa3fc001afce1e54fd6`, sobre master + fixture independente `55dbefa033`; contribuição própria de quatro arquivos.
- `lab/teste/sbsmallmap-complete`: `52ed877e66304d22e204a143782eaa26774e89ef`; base fixa preservada.

Lab 106/106, master/fixture estático 79/79, ASan/UBSan e contrato de igualdade aprovados. Tipo pronto para Windows; revisão dos consumidores #758 permanece separada. [Registro completo](FECHAMENTO_SBSMALLMAP.md).

## Fechamento dos quatro caches GL do #758

- Head publicado preservado `900f11a157`; migração reconciliada sobre master/SbSmallMap em `9f024a80ea6963a4cd34bab2e158a7ec178656e1`.
- Pré-PR `codex/pre-pr/gl-contexts-small-complete`: `87cc9003e9adbacc702effe666d0e3b34b377983`, 80/80 na base de master/fixture/SbSmallMap.
- Dependência SbSmallMap no lab temporário: `b4b40d947d9f77630318fc52f41821e83f803233`.
- `lab/teste/gl-contexts-small-complete`: `547caebd3067a00ac3cfe90e8106bb6fa622aead`, 107/107, base fixa preservada.

Cinco contextos GLX, falhas e limpeza aprovados; sanitizadores com limites explícitos. Windows e transporte para #758 pendentes. Lifetime do glue e exceções de SoAction são ciclos próprios. [Registro completo](FECHAMENTO_GL_CONTEXT_MAPS.md).

## Fechamento da vida útil do GL glue

- `codex/pre-pr/glglue-lifetime-complete`: `0d31242881b0a2bc996ae348c3768fa8730c7a98`, sobre master/fixture `55dbefa033`, seis arquivos de contribuição.
- `lab/teste/glglue-lifetime-complete`: `c71f71965c7bc1f00b0b33ff5e3edfa78dbc0083`; 107/107, base fixa preservada.

Master 80/80, threads ON, ASan/UBSan e LeakSanitizer sem exclusões de glue. Borrowers, IDs extremos e reutilização validados; Windows/publicação pendentes. [Registro completo](FECHAMENTO_GLGLUE_LIFETIME.md).
