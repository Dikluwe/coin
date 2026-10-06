# Controle atual de branches

Aplicar o [método de branches](METODO_BRANCHES.md). Este controle contém apenas branches existentes e ações pendentes; remover as linhas conforme os trabalhos forem encerrados.

O chat responsável por dicionários CC e Sb mantém o [mapa de contratos, dependências e prioridades](DICT_CC_SB.md). A organização desse assunto inclui `cc_dict`, `cc_hash`, `SbDict`, `SbHash` e os mapas relacionados.

## Prioridades de organização

1. Validar os candidatos identificados abaixo e auditar os demais trabalhos por assunto. Eliminar redundâncias depois de conferir conteúdo e dependências.
2. Concluir a auditoria das cinco branches com prefixo `archive/` e dos quatro labs auxiliares. Os prefixos são legados em processo de organização; não criar novas branches desses grupos.
3. Atualizar o lab fixo sempre que master ou um PR mudar e repetir a validação do conjunto.

## Candidatos identificados na auditoria

Os candidatos deste grupo foram reconciliados nos fechamentos de [mapas](FECHAMENTO_MAPAS_CONSUMIDORES.md), [apoio](FECHAMENTO_APOIO_CONSUMIDORES.md), [SbHash](FECHAMENTO_SBHASH.md) e [glglue](FECHAMENTO_GLGLUE_LIFETIME.md). As contribuições ainda úteis estão nas branches pré-PR abaixo; as fontes experimentais permanecem identificadas no inventário até publicação.


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
| `codex/work/cc-hash-api` | Fonte preliminar substituída | Base `b27e37a6dd`; head `ebae1bd653` | Conteúdo reconciliado em cc_hash e SbDict finais; preservar até publicação. [Auditoria](FECHAMENTO_CANDIDATOS_CENTRAIS.md) |
| `lab/teste/cc-hash-api` | Lab preliminar | Base fixa `b27e37a6dd`; head testado `2dcf47b703` | 9 regressões e 106 testes aprovados; preservar o resultado até o ciclo de Windows/publicação, depois eliminar a cópia |
| `codex/pre-pr/cc-list-bounds` | Pré-PR; pronta para Windows | Master `674e74267d`; head `ea8e905436` | Índices cc_list; master 78/78, lab 105/105. [Fechamento](FECHAMENTO_APOIO_CONSUMIDORES.md) |
| `lab/teste/cc-list-bounds` | Lab de teste | Base fixa `b27e37a6dd`; head `820e8bb99f` | 105/105; conservar até Windows/publicação |
| `codex/pre-pr/sblist-value-bounds` | Pré-PR; pronta para Windows | Master `674e74267d`; head `af3ae60643` | Valor e índices SbList; master 78/78, lab 105/105. [Fechamento](FECHAMENTO_APOIO_CONSUMIDORES.md) |
| `lab/teste/sblist-value-bounds` | Lab de teste | Base fixa `b27e37a6dd`; head `d3f033ed24` | 105/105; conservar até Windows/publicação |
| `codex/pre-pr/sbplist-bounds` | Pré-PR; pronta para Windows | Master `674e74267d`; head `969a37276e` | Índices SbPList; master 78/78, lab 105/105. [Fechamento](FECHAMENTO_APOIO_CONSUMIDORES.md) |
| `lab/teste/sbplist-bounds` | Lab de teste | Base fixa `b27e37a6dd`; head `a63d490810` | 105/105; conservar até Windows/publicação |
| `codex/pre-pr/callback-list-copy-stress` | Pré-PR; pronta para Windows | Master `674e74267d`; head `23ccce4b84` | Regressão de cópia de `SoCallbackList`; 78/78 master, 105/105 lab. [Fechamento](FECHAMENTO_APOIO_CONSUMIDORES.md) |
| `lab/teste/callback-list-copy-stress` | Lab de teste | Base fixa `b27e37a6dd`; head `8fff68f221` | 105/105; conservar até Windows/publicação |
| `codex/pre-pr/bump-cache-ready-path` | Pré-PR; pronta para Windows | Master `674e74267d`; head `9d9c16f53a` | Cache pronto: medição e master 78/78, lab 105/105. [Fechamento](FECHAMENTO_APOIO_CONSUMIDORES.md) |
| `lab/teste/bump-cache-ready-path` | Lab de teste | Base fixa `b27e37a6dd`; head `7e6271af68` | 105/105; conservar até Windows/publicação |
| `codex/pre-pr/bump-cache-diagnostics-memory` | Pré-PR; pronta para Windows | Master `674e74267d`; head `1a373951dd` | Mensagens/rollback/OOM; master 78/78, lab 105/105. [Fechamento](FECHAMENTO_APOIO_CONSUMIDORES.md) |
| `lab/teste/bump-cache-diagnostics-memory` | Lab de teste | Base fixa `b27e37a6dd`; head `c6fc301116` | 105/105; conservar até Windows/publicação |
| `codex/pre-pr/bump-shared-programs` | Pré-PR; pronta para Windows | Master `674e74267d`; head `9b772453d4` | Contexto, propriedade e concorrência; master 78/78, lab 105/105. [Fechamento](FECHAMENTO_APOIO_CONSUMIDORES.md) |
| `lab/teste/bump-shared-programs` | Lab de teste | Base fixa `b27e37a6dd`; head `623d9daf2f` | 105/105; conservar até Windows/publicação |
| `codex/pre-pr/sbheap-cancel-contract` | Pré-PR; pronta para Windows | Master `674e74267d`; head `6436c4d193` | Contrato de cancelamento; master 78/78, lab 105/105. [Fechamento](FECHAMENTO_APOIO_CONSUMIDORES.md) |
| `lab/teste/sbheap-cancel-contract` | Lab de teste | Base fixa `b27e37a6dd`; head `0a599812df` | 105/105; conservar até Windows/publicação |
| `codex/pre-pr/scxml-attribute-alias` | Pré-PR; pronta para Windows | Master `674e74267d`; head `6ab2915cb7` | Correção isolada de alias de atributo, 78/78 em master; fonte adaptativa reprovada. Ver `CHECKLIST_XML_SCXML.md` |
| `lab/teste/scxml-attribute-alias` | Lab de teste | Base fixa `b27e37a6dd`; head `5b1b44b1a8` | 105/105; conservar até Windows/publicação |
| `codex/pre-pr/scxml-temporary-alias` | Pré-PR; pronta para Windows | Master `674e74267d`; head `b4204f9859` | Correção isolada de alias/ownership, 78/78 em master; [fechamento](FECHAMENTO_MAPAS_CONSUMIDORES.md) |
| `lab/teste/scxml-temporary-alias` | Lab de teste | Base fixa `b27e37a6dd`; head `97a1a57654` | 105/105; conservar até Windows/publicação |
| `codex/pre-pr/profiler-stats-lifetime` | Pré-PR; pronta para Windows | Master `674e74267d`; head `bb32da5fc2` | Liberação dos dados de ação no PImpl, 78/78 em master; [fechamento](FECHAMENTO_MAPAS_CONSUMIDORES.md) |
| `lab/teste/profiler-stats-lifetime` | Lab de teste | Base fixa `b27e37a6dd`; head `7821599d5a` | 105/105; conservar até Windows/publicação |

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
| Ownership de associações `ScXMLEvent` | `codex/work/scxml-event-association-ownership` em `f3609b069b` | `fe5a117065` | `codex/pr/scxml-event-association-ownership` em `a4fc778e7c` | Lab: build, 2 testes/8 checks e 105/105 do conjunto. Master: build, 2 testes/8 checks e 78/78 do conjunto. ASan/UBSan/LeakSanitizer: 2 testes/8 checks; com `ScXMLSendElt`, 3 testes/17 checks sem vazamento. |
| `cc_xml_path` | `codex/fix/cc-xml-path-loops-truncate` em `cd9761452df30613be7ac48291f071784f9a2478` | `efa664d94830decb3ad6a5a67ed748cea3e3b3c9` | `codex/pr/cc-xml-path-loops-truncate` em `bffa176d456a623078d1ded247d8b04bd3f7d898` | Lab: build, 2 testes específicos com 30 checks e 105/105 do conjunto. Master: build, 2 testes específicos com 30 checks e 78/78 do conjunto. |
| Escaping e formatos inteiros | `codex/fix/cc-xml-escaping-inttypes` em `478ca2ffdee2afff9ad080c5887496ff93dc4392` | `3847c79ace6df46d1524f240153445ac417de113` | `codex/pr/cc-xml-escaping-inttypes` em `0935125576bf039ce59f257a634549affc4137cc` | Lab: build, 2 testes específicos com 33 checks e 105/105 do conjunto. Master: build, 2 testes específicos com 33 checks e 78/78 do conjunto. |
| Parser transacional | `codex/work/cc-xml-parser-transactional` em `162dc998af31291fb2d861b0d6b1431acb1c18da` | `fc4abe5eb7ff525703721d52547f27d8a54509db` | `codex/pr/cc-xml-parser-transactional` em `162dc998af31291fb2d861b0d6b1431acb1c18da` | Lab: build, teste específico 1/1 e 106/106 do conjunto. Master: build, teste específico 1/1 e 79/79 do conjunto. |
| Guarda de comprimento XML | `codex/work/cc-xml-parse-length-guard` em `5c31577f7fb074d178bae7826019b2822bdfbf96` | `1f4b3c44c07707ac3e5c1f488a0f489416e176cd` (com parser `fc4abe5eb7`) | `codex/pr/cc-xml-parse-length-guard` em `f2142d0d1a32f8ac59e7b8d04a7be3d0d3dc1fc0`, **destino dependente** `codex/pr/cc-xml-parser-transactional` em `162dc998af31291fb2d861b0d6b1431acb1c18da` | Lab + parser: build, regressão transacional 1/1 e 106/106 do conjunto. Destino parser/master: build, regressão 1/1 e 79/79. `range-diff` exato nos dois commits. |
| Coalescência de character data | `codex/work/cc-xml-coalesce-cdata` em `5c4a34e04561eaef39abecf9acbca9609d39ddd5` | `3152c194784a8ce9531872e9ab12587ecc321321` | `codex/pr/cc-xml-coalesce-cdata` em `5c4a34e04561eaef39abecf9acbca9609d39ddd5` | Lab: build, 5 testes específicos com 82 checks e 105/105 do conjunto. Master: build, 5 específicos com 82 checks e 78/78 do conjunto. |
| Ownership do DOM | `codex/work/cc-xml-dom-ownership` em `28d74b9982bf0791d50abd60a1dcbbd7817fe1f0` | `1964dceeb7143c3d208e293ea19e966c9d4ba1d2` | `codex/pr/cc-xml-dom-ownership` em `28d74b9982bf0791d50abd60a1dcbbd7817fe1f0` | Lab: build, 3 testes/29 checks, teste C e 105/105 do conjunto. Master: build, mesmos testes específicos e 78/78 do conjunto. ASan/UBSan com LeakSanitizer: testes específicos e cliente C aprovados. |
| Regressões DOM e fuzzer | `codex/work/cc-xml-dom-regressions-fuzz` em `dcb54791bd3609458f7df5c5e22e83682b68358a` | `a46f8c1926bd339bda670e23e7b4b0c80d91b0c6` | `codex/pr/cc-xml-dom-regressions-fuzz` em `dcb54791bd3609458f7df5c5e22e83682b68358a`, **destino dependente** `codex/pr/cc-xml-dom-ownership` em `28d74b9982bf0791d50abd60a1dcbbd7817fe1f0` | Lab + ownership: build, 5 testes DOM/43 checks e 105/105 do conjunto. Destino ownership/master: build, mesmos testes e 78/78 do conjunto. Clang + ASan/UBSan/LeakSanitizer: 5 testes DOM/43 checks, cliente C e smoke libFuzzer 1/1 (100 execuções). |
| Erros de I/O XML | `codex/work/cc-xml-io-errors` em `ad116e6e61713ba2609fc583b7cbf642464ab764` | `a7b712e4720f39f8b918e63518457814225e8d04` | `codex/pr/cc-xml-io-errors` em `de27f9452e7c40ca852287a8fafd42bd0309a610` | Lab: build, regressões de escrita 1 teste/4 checks e leitura 1/1, conjunto 106/106. Master: mesmos testes e 79/79. ASan/UBSan/LeakSanitizer e reprodutor FIFO passaram no trabalho. Patch-id idêntico nas três variantes. |

Para os builds normais da tabela, os comandos de configuração e compilação foram:

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

### Ownership das associações de `ScXMLEvent` (2026-10-06)

- O LeakSanitizer do lab cumulativo acima apontou 30 bytes vazados em quatro valores criados por `ScXMLEvent::setAssociation()`. No master, o destrutor de `ScXMLEvent::PImpl` continha um `FIXME` para liberar os valores. A candidata libera somente os valores `new[]` antes da destruição do mapa; as chaves internadas por `SbName` não são suas. Testes cobrem substituição por alias/subtrecho e independência dos valores no clone. Não transporta a migração de mapas nem outras otimizações da branch legada SCXML.
- Base master `674e74267df863dbaf50416c477bc7f918a826d8`; trabalho `codex/work/scxml-event-association-ownership` em `f3609b069b39df2910dbf419dd727db6d05a5677`; PR local `codex/pr/scxml-event-association-ownership` em `a4fc778e7c07fd416d0799e4b002bdb090b92f8f`. `git merge-base master <PR>` retornou o master; `git diff --stat master...<PR>` apontou somente `src/scxml/ScXMLEvent.cpp` (38 inserções, 1 remoção), e `git diff --check` passou.
- Lab fixo conferido em `b27e37a6dd3ae941bcffcf480a377adc81e3fc27` (master + 17 PRs). A cópia `lab/teste/scxml-event-association-ownership`, no worktree `/tmp/coin-lab-teste-scxml-event-association-ownership`, recebeu **somente** o commit candidato por cherry-pick e chegou a `fe5a11706582caaebc1b373b2bebdc391516f427`. Os patch-ids estáveis de trabalho, PR e lab foram iguais: `a284562179e5c5247141ef1b2bef4942069b367e`.
- Em cada uma das bases master/PR e lab: `cmake -S <worktree> -B /dev/shm/coin-scxml-assoc-{pr,lab} -DCOIN_BUILD_TESTS=ON -DCOIN_BUILD_DOCUMENTATION=OFF -DCMAKE_BUILD_TYPE=Debug`; `cmake --build <build> --parallel 6`; `xvfb-run -a -s '-screen 0 1280x1024x24 +extension GLX' env LIBGL_ALWAYS_SOFTWARE=1 COIN_GLX_PIXMAP_DIRECT_RENDERING=1 ctest --test-dir <build> --output-on-failure --parallel 4`. Resultado: **78/78** no PR sobre master, **105/105** no lab; `bin/CoinTests --run_test='*scxml_event_association_*' --report_level=short` passou **2 testes/8 checks** em ambas. Uma primeira tentativa na branch de trabalho havia compilado só `CoinTests`, deixando executáveis de outros alvos ausentes; após `cmake --build <build> --parallel 8` completo, a suíte foi repetida e passou **78/78**.
- Variante instrumentada do trabalho em `/dev/shm/coin-scxml-assoc-work-asan`: Clang, `-DCMAKE_C_FLAGS=-fsanitize=address,undefined -DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined -DCMAKE_SHARED_LINKER_FLAGS=-fsanitize=address,undefined`, `-DCOIN_BUILD_TESTS=ON -DCOIN_THREADSAFE=OFF`; `cmake --build <build> --target CoinTests --parallel 6`; `env ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 <build>/bin/CoinTests --run_test='*scxml_event_association_*' --report_level=short`: **2 testes/8 checks**, sem diagnóstico. O master ainda não usa `COIN_ENABLE_SANITIZERS`; as flags explícitas foram necessárias.
- Para reproduzir o caso original, uma segunda cópia temporária `lab/teste/scxml-event-with-sendelt` partiu do PR local `codex/pr/scxml-sendelt-tokenize` em `5e8945f2086049850f8638bed144112fd85e1eb0`, recebeu somente a correção como `c388ac6369d886b7adae4e21352752183d1b31ec` e foi configurada/compilada com Clang e as mesmas flags em `/dev/shm/coin-scxml-event-with-sendelt-asan`. Com LeakSanitizer ativo, `bin/CoinTests --run_test='*scxml_event_association_*,*scxml_send_namelist_ignores_empty_tokens' --report_level=short` passou **3 testes/17 checks**, sem vazamento. É uma checagem adicional de interação, não uma alteração do lab fixo ou do PR `ScXMLSendElt`.
- Nenhuma publicação foi feita. As duas refs/worktrees `lab/teste` deste ciclo foram removidas após a verificação final; trabalho e PR locais permanecem para o gate Windows.

### Lab cumulativo de dez candidatas XML/SCXML (2026-10-06)

- O lab fixo permaneceu em `b27e37a6dd3ae941bcffcf480a377adc81e3fc27` (master `674e74267df863dbaf50416c477bc7f918a826d8` + 17 PRs listados acima). A cópia anterior de nove candidatas, `091c29fa32684f4914f59607f6850f8db7837a74`, ainda existia como commit e tinha esse lab fixo como merge-base; sua composição está registrada na seção anterior. Em worktree separado `/tmp/coin-lab-teste-cc-xml-scxml-cumulative-ten`, a nova ref temporária `lab/teste/cc-xml-scxml-cumulative-ten` partiu desse commit e recebeu **somente** a candidata `ScXMLEvent` de trabalho `f3609b069b39df2910dbf419dd727db6d05a5677` por cherry-pick, chegando a `252bc2d336731bdcefab84605fccac7e68bbe6ae`. O patch-id da décima mudança permaneceu `a284562179e5c5247141ef1b2bef4942069b367e`; `git diff --check` passou. Nenhuma branch de PR ou a base fixa foi alterada.
- Configuração e compilação em RAM: `cmake -S /tmp/coin-lab-teste-cc-xml-scxml-cumulative-ten -B /dev/shm/coin-xml-scxml-ten-internal -DCOIN_BUILD_TESTS=ON -DCOIN_BUILD_DOCUMENTATION=OFF -DCMAKE_BUILD_TYPE=Debug` e `cmake --build /dev/shm/coin-xml-scxml-ten-internal --parallel 4`. Repetido em `/dev/shm/coin-xml-scxml-ten-external` com `-DUSE_EXTERNAL_EXPAT=ON` (Expat do sistema 2.6.1). Em cada um: `xvfb-run -a -s '-screen 0 1280x1024x24 +extension GLX' env LIBGL_ALWAYS_SOFTWARE=1 COIN_GLX_PIXMAP_DIRECT_RENDERING=1 ctest --test-dir <build> --output-on-failure --parallel 4`. Resultado **108/108** com Expat embarcado 2.9.0 e **108/108** com Expat externo 2.6.1.
- Variante instrumentada em `/dev/shm/coin-xml-scxml-ten-asan`: mesmas opções de teste/Debug com `-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCOIN_BUILD_FUZZERS=ON -DCOIN_ENABLE_SANITIZERS=ON -DCOIN_THREADSAFE=OFF`; `cmake --build <build> --target CoinTests CoinXmlDomFuzzer XmlCApiTest --parallel 4`. O filtro `CoinTests --run_test='*buffer_round_trip_compares_real_dom,*write_escapes_character_data_and_attributes,*integer_format_round_trip,*character_data_*,*formatting_whitespace_*,*markup_remains_*,*empty_cdata_child_serializes_without_null_dereference,*dom_*_ownership,*cc_xml_path_*,*scxml_send_namelist_ignores_empty_tokens,*scxml_event_association_*' --report_level=short` passou **17 testes/193 checks** tanto no build normal quanto com `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1`, sem diagnóstico. Com essas variáveis, `ctest -R '^CoinXmlDomFuzzerSmoke$' --output-on-failure` passou **1/1**, e `XmlCApiTest` passou. É smoke do harness, não campanha de fuzzing.
- A cópia temporária foi removida após o registro final; permanecem pendentes o gate Windows, a política de `DOCTYPE`, estudo de limites e branches SCXML de mapas. Nenhuma publicação foi feita.

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

Próximos ciclos: estudar limites antes de defini-los e implementar DTD/validação em branch própria, após obter libxml2 compatível com resolução por contexto. As quatro fontes SCXML de mapas foram auditadas: correções funcionais de alias já foram extraídas; migrações adaptativas inseguras não viram PR. Ver `CHECKLIST_XML_SCXML.md` e `FECHAMENTO_MAPAS_CONSUMIDORES.md`.

Estudo de política DOCTYPE: o commit histórico `bd8287e3276887ef9064740923fc1ad4a86f9b72` foi extraído sobre `codex/pr/cc-xml-parser-transactional` em `codex/work/cc-xml-doctype-policy` (`8100371983c0fd19de9950373050fef188cdaf32`), **somente como candidata de trabalho**. Configuração/build normais em `/dev/shm/coin-xml-doctype-work/build`; `ctest -R '^CoinXmlTransactionalRead$'` 1/1 e conjunto 79/79 passaram. A pesquisa local não encontrou `DOCTYPE` nos XML/SCXML do FreeCAD clonado nem uso dele nos consumidores `cc_xml` do Coin. O usuário decidiu depois pelo contrato oposto: DTD completo, recursos externos exclusivamente por callback e validação `ELEMENT`/`ATTLIST`. Portanto esta ref de rejeição foi supersedida; não criar PR. O Expat não é validador DTD. O Coin não usa libxml2; o Linux local oferece 2.9.14, mas a API `xmlCtxtSetResourceLoader()` de resolução externa por contexto só existe desde 2.14.0. A implementação deverá ser opcional, isolada e testada em ambiente com essa versão ou superior, sem alterar automaticamente o comportamento do parser atual.

### Erros de I/O XML (2026-10-06)

- Base fixa: `lab/open-prs-integration` em `b27e37a6dd3ae941bcffcf480a377adc81e3fc27` (master + 17 PRs acima); destino de PR: `master` em `674e74267df863dbaf50416c477bc7f918a826d8`. Uma única mudança de trabalho `ad116e6e61713ba2609fc583b7cbf642464ab764` foi cherry-picked, sem conflitos, em `lab/teste/cc-xml-io-errors` (`a7b712e4720f39f8b918e63518457814225e8d04`) e `codex/pr/cc-xml-io-errors` (`de27f9452e7c40ca852287a8fafd42bd0309a610`). O patch-id estável é `85f0ed81637ee1a146ef202c999279e22bac70b0` nas três variantes.
- Build normal, todo em RAM: `cmake -S <source> -B <build> -DCOIN_BUILD_TESTS=ON -DCOIN_BUILD_DOCUMENTATION=OFF -DCMAKE_BUILD_TYPE=Debug`; `cmake --build <build> --parallel 6`, com `<source>` `/tmp/coin-pr-cc-xml-io-errors` ou `/tmp/coin-lab-teste-cc-xml-io-errors`, e `<build>` `/dev/shm/coin-xml-io-pr` ou `/dev/shm/coin-xml-io-lab`. `xvfb-run -a -s '-screen 0 1280x1024x24 +extension GLX' env LIBGL_ALWAYS_SOFTWARE=1 COIN_GLX_PIXMAP_DIRECT_RENDERING=1 ctest --test-dir <build> --output-on-failure --parallel 4`: **79/79** no PR/master e **106/106** no lab. `<build>/bin/CoinTests --run_test='*write_to_file_reports_io_errors' --report_level=short`: 1 teste/4 checks em ambos; `ctest --test-dir <build> -R '^XmlLoadFileTest$' --output-on-failure`: 1/1 em ambos.
- No trabalho, Clang com ASan/UBSan/LeakSanitizer passou as duas regressões; `env TMPDIR=/dev/shm sh testsuite/reproducers/xml-load-file-ftell-error/run.sh /dev/shm/coin-xml-io-work` passou no FIFO. O teste de leitura compila `src/xml/utils.cpp` diretamente, sem exigir exportar o helper pela DLL Windows. Comandos instrumentados: `cmake -S /tmp/coin-work-cc-xml-io-errors -B /dev/shm/coin-xml-io-work-asan -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_FLAGS=-fsanitize=address,undefined -DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined -DCMAKE_SHARED_LINKER_FLAGS=-fsanitize=address,undefined -DCOIN_BUILD_TESTS=ON -DCOIN_BUILD_DOCUMENTATION=OFF -DCMAKE_BUILD_TYPE=Debug -DCOIN_THREADSAFE=OFF`; `cmake --build /dev/shm/coin-xml-io-work-asan --target CoinTests XmlLoadFileTest --parallel 6`; com `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1`, `ctest --test-dir /dev/shm/coin-xml-io-work-asan -R '^XmlLoadFileTest$' --output-on-failure` (1/1) e `/dev/shm/coin-xml-io-work-asan/bin/CoinTests --run_test='*write_to_file_reports_io_errors' --report_level=short` (1 teste/4 checks).
- `git merge-base master codex/pr/cc-xml-io-errors` é o master acima; `git diff --check` passou e `git diff --name-status master...codex/pr/cc-xml-io-errors` lista somente `src/xml/document.cpp`, `src/xml/utils.cpp`, `src/xml/utils.h`, `testsuite/CMakeLists.txt` e `testsuite/XmlLoadFileTest.cpp`. Nenhuma branch de PR foi publicada; Windows pendente. A cópia/ref temporária do lab foi removida após este gate; a base fixa permaneceu inalterada.

### Guarda de comprimento antes de `XML_Parse()` (2026-10-06)

- A API pública recebe `size_t`, mas `XML_Parse()` recebe `int`. A candidata recusa valores acima de `INT_MAX` antes de criar/avançar o parser; se há transação parcial ativa, executa rollback e preserva raiz/current anteriores. O teste usa um ponteiro curto com comprimento impossível de representar e, em 64 bits, comprimento que antes fazia wrap para um XML curto válido. Não foi escolhida nenhuma cota de recursos nesta etapa.
- Base fixa `lab/open-prs-integration` `b27e37a6dd3ae941bcffcf480a377adc81e3fc27`; master `674e74267df863dbaf50416c477bc7f918a826d8`. O pré-requisito parser transacional no lab é `fc4abe5eb7ff525703721d52547f27d8a54509db`, único commit entre base fixa e essa base de teste. Os dois commits da guarda são `1839d358431c9b75ad9595dfa374055be4c2e3d8` e `5c31577f7fb074d178bae7826019b2822bdfbf96` no trabalho; `2a19c4305f9f335151a047fef052954f94bd6410` e `f2142d0d1a32f8ac59e7b8d04a7be3d0d3dc1fc0` no PR; `abe567bec11b0293edc48076eaf5e3e5a661daee` e `1f4b3c44c07707ac3e5c1f488a0f489416e176cd` no lab. `git range-diff` mostrou equivalência exata dos dois commits nas três variantes.
- Build em RAM: `cmake -S <source> -B <build> -DCOIN_BUILD_TESTS=ON -DCOIN_BUILD_DOCUMENTATION=OFF -DCMAKE_BUILD_TYPE=Debug`; `cmake --build <build> --parallel 6`, com `<source>` `/tmp/coin-work-cc-xml-parse-length-guard`, `/tmp/coin-pr-cc-xml-parse-length-guard` ou `/tmp/coin-lab-teste-cc-xml-parse-length-guard` e `<build>` `/dev/shm/coin-xml-parse-length-work`, `/dev/shm/coin-xml-parse-length-pr` ou `/dev/shm/coin-xml-parse-length-lab`. `ctest --test-dir <build> -R '^CoinXmlTransactionalRead$' --output-on-failure`: 1/1 nas três variantes. `xvfb-run -a -s '-screen 0 1280x1024x24 +extension GLX' env LIBGL_ALWAYS_SOFTWARE=1 COIN_GLX_PIXMAP_DIRECT_RENDERING=1 ctest --test-dir <build> --output-on-failure --parallel 4`: trabalho 79/79, PR 79/79, lab 106/106.
- `git merge-base codex/pr/cc-xml-parser-transactional codex/pr/cc-xml-parse-length-guard` é `162dc998af31291fb2d861b0d6b1431acb1c18da`; `git diff --check` passou e `git diff --name-status` entre essas refs lista somente `src/xml/document.cpp` e `testsuite/xml/XmlTransactionalReadTest.cpp`. O lab tem exatamente parser pré-requisito + dois commits da guarda e mantém o ancestral da base fixa.
- Gate instrumentado em RAM: `cmake -S <source> -B <build> -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_FLAGS=-fsanitize=address,undefined -DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined -DCMAKE_SHARED_LINKER_FLAGS=-fsanitize=address,undefined -DCOIN_BUILD_TESTS=ON -DCOIN_BUILD_DOCUMENTATION=OFF -DCMAKE_BUILD_TYPE=Debug -DCOIN_THREADSAFE=OFF`; `cmake --build <build> --target CoinXmlTransactionalReadTest --parallel 6`; `env ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir <build> -R '^CoinXmlTransactionalRead$' --output-on-failure`. Com `<source>` do trabalho e `<build>` `/dev/shm/coin-xml-parse-length-asan`, o Expat embarcado antigo produz UBSan em `xmlparse.c:1864` e o teste falha; **o mesmo diagnóstico foi reproduzido sem esta candidata** em `<source>` `/tmp/coin-pr-cc-xml-parser-transactional`, `<build>` `/dev/shm/coin-xml-parse-length-baseline-asan`. Com `UBSAN_OPTIONS=halt_on_error=0`, o teste da candidata termina 1/1 sem diagnóstico de ASan/LeakSanitizer, mas esse resultado não limpa o UBSan. Repetido com `-DUSE_EXTERNAL_EXPAT=ON` e `<build>` `/dev/shm/coin-xml-parse-length-external-asan`, o teste passou 1/1 com todos os sanitizadores em modo estrito (Expat externo 2.6.1).
- Estudo para a etapa seguinte de cotas: contabilizar bytes de entrada acumulados por arquivo e por chamadas parciais, texto de entidades/CDATA expandido, total de elementos e atributos, e profundidade da `parsestack`. As rotinas `cc_xml_elt_delete_x()`, `cc_xml_elt_calculate_size()` e `cc_xml_elt_write_to_buffer()` são recursivas; a soma de tamanhos e a alocação `bytes+1` também precisam de overflow checks antes de uma política de serialização. O filtro de streaming pode descartar nós, portanto é preciso decidir se cotas contam trabalho total processado ou apenas DOM retido. O Expat 2.9.0 atualizado possui proteções de amplificação (padrão 100× após 8 MiB) e alocação interna (100× após 64 MiB), mas elas não substituem limites de DOM; o Expat externo pode ter versão/defaults diferentes. Nenhum valor de cota foi definido.

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
| `codex/cc-hash-hardening-followup` | `/tmp/coin-cc-hash-hardening-followup` | Fonte reconciliada com #769–772 e fechamentos cc_hash/SbDict; preservar até publicação. [Auditoria](FECHAMENTO_CANDIDATOS_CENTRAIS.md) |
| `codex/cc-list-hardening` | Sem worktree associado | OOM já integrado no lab; limites extraídos para pré-PR. Preservar referência até publicação. [Auditoria](FECHAMENTO_APOIO_CONSUMIDORES.md) |
| `codex/cc-test-audit` | Sem worktree associado | Sem delta útil para dict/hash; mudanças worker/scheduler remetidas ao estudo de threads. [Auditoria](FECHAMENTO_APOIO_CONSUMIDORES.md) |
| `codex/coin-atexit-hardening` | `/mnt/Laranja/Git/externos/coin` | Auditar contribuição para PR e dependências |
| `codex/coin-prime-boundary` | `/home/dikluwe/.codex/worktrees/coin-prime-boundary/coin` | Primos e limites incorporados nos três fechamentos; helper legado preservado. Integrar `primep.h` uma vez na publicação. [Auditoria](FECHAMENTO_CANDIDATOS_CENTRAIS.md) |
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
| `codex/improve-sblist` | Sem worktree associado | Contratos SbList/SbPList e regressão de callback extraídos em três pré-PRs independentes. Preservar referência. [Auditoria](FECHAMENTO_APOIO_CONSUMIDORES.md) |
| `codex/issue-374-bbox-tests` | Sem worktree associado | Auditar contribuição para PR e dependências |
| `codex/maps/core/adaptive-map` | Sem worktree associado | Experimento reprovado por promoção insegura; preservar fonte. [Auditoria](FECHAMENTO_ADAPTIVE_MAP.md) |
| `codex/maps/core/sequential-map` | Sem worktree associado | Experimento reprovado por chave negativa invisível em Release; preservar fonte. [Auditoria](FECHAMENTO_MAPAS_CONSUMIDORES.md) |
| `codex/maps/integration/profiler-containers` | Sem worktree associado | Agregado substituído por correção isolada de lifetime; preservar fonte. [Auditoria](FECHAMENTO_MAPAS_CONSUMIDORES.md) |
| `codex/maps/use/coinresources-adaptive` | Sem worktree associado | Migração descartada; preservar fonte. [Auditoria](FECHAMENTO_MAPAS_CONSUMIDORES.md) |
| `codex/maps/use/fieldcontainer-mfield-sizes-hash` | Sem worktree associado | Otimização não justificada; preservar fonte. [Auditoria](FECHAMENTO_MAPAS_CONSUMIDORES.md) |
| `codex/maps/use/profiler-action-timings-sequential` | Sem worktree associado | Migração descartada; lifetime extraído. [Auditoria](FECHAMENTO_MAPAS_CONSUMIDORES.md) |
| `codex/maps/use/scxml-attributes-adaptive` | Sem worktree associado | Migração descartada; alias extraído. [Auditoria](FECHAMENTO_MAPAS_CONSUMIDORES.md) |
| `codex/maps/use/scxml-document-ids-adaptive` | Sem worktree associado | Promoção insegura; preservar fonte. [Auditoria](FECHAMENTO_MAPAS_CONSUMIDORES.md) |
| `codex/maps/use/scxml-evaluator-temporaries-adaptive` | Sem worktree associado | Migração descartada; alias extraído. [Auditoria](FECHAMENTO_MAPAS_CONSUMIDORES.md) |
| `codex/maps/use/scxml-type-registry-hash-name` | Sem worktree associado | Otimização não justificada; preservar fonte. [Auditoria](FECHAMENTO_MAPAS_CONSUMIDORES.md) |
| `codex/profiler-tokenize-sbstring` | Sem worktree associado | Comparar com PR relacionado; incorporar conteúdo exclusivo e eliminar redundância |
| `codex/qt-quarter-regressions` | Sem worktree associado | Auditar contribuição para PR e dependências |
| `codex/sbdict-ccdict-migration` | `/home/dikluwe/.codex/worktrees/sbdict-ccdict-migration/coin` | Migração substituída pelo fechamento SbDict; fixture idêntico à branch separada. Preservar até publicação. [Auditoria](FECHAMENTO_CANDIDATOS_CENTRAIS.md) |
| `codex/sbdict-hash-hardening` | Sem worktree associado | Correção inicial e testes substituídos pelos fechamentos cc_hash/SbDict. Preservar referência até publicação. [Auditoria](FECHAMENTO_CANDIDATOS_CENTRAIS.md) |
| `codex/sbname/experiment/compact-entry-pool` | Worktree de auditoria arquivado | Experimento compilado/testado; falhas e limites de alocação impedem PR. Preservar a branch. [Auditoria](FECHAMENTO_MAPAS_CONSUMIDORES.md) |
| `codex/scene-cache/compiled-prototype` | Sem worktree associado | Auditar contribuição para PR e dependências |
| `codex/scxml-sendelt-tokenize-sbstring` | `/tmp/coin-work-scxml-sendelt-tokenize` | Candidata com teste validada em `codex/pr/scxml-sendelt-tokenize`; manter até publicação localmente autorizada |
| `codex/soinput-read-errors` | `/home/dikluwe/.codex/worktrees/soinput-read-status/coin` | Auditar contribuição para PR e dependências |
| `codex/text-font-followup` | Sem worktree associado | Auditar contribuição para PR e dependências |
| `codex/wgpu-depth-offset` | Sem worktree associado | Auditar contribuição para PR e dependências |
| `codex/wgpu-frame-preparation` | Sem worktree associado | Auditar contribuição para PR e dependências |
| `codex/work/bump-cache-diagnostics-memory` | Sem worktree associado | Extraído em pré-PR; preservar fonte até publicação. [Auditoria](FECHAMENTO_APOIO_CONSUMIDORES.md) |
| `codex/work/bump-cache-ready-path` | Sem worktree associado | Extraído em pré-PR; preservar fonte até publicação. [Auditoria](FECHAMENTO_APOIO_CONSUMIDORES.md) |
| `codex/work/bump-shared-program-pool` | Sem worktree associado | Pool isolado em pré-PR; pré-requisito já no master. Preservar fonte. [Auditoria](FECHAMENTO_APOIO_CONSUMIDORES.md) |
| `codex/work/cc-xml-coalesce-cdata` | `/tmp/coin-work-cc-xml-coalesce-cdata` | Candidata validada em `codex/pr/cc-xml-coalesce-cdata`, inclusive com parser transacional no lab cumulativo; aguardar gate Windows |
| `codex/work/cc-xml-dom-ownership` | `/tmp/coin-work-cc-xml-dom-ownership` | Candidata validada em `codex/pr/cc-xml-dom-ownership`, com ASan/UBSan focado e lab cumulativo; aguardar gate Windows |
| `codex/work/cc-xml-parser-transactional` | `/tmp/coin-work-cc-xml-parser-transactional` | Candidata validada em `codex/pr/cc-xml-parser-transactional`; aguardar gate Windows e publicação autorizada |
| `codex/work/glglue-lifetime-audit` | Sem worktree associado | Validar candidato descrito acima |
| `codex/work/sbhash-insert-allocation-failure` | Sem worktree associado | Validar candidato descrito acima |
| `codex/work/sbheap-cancel-documentation` | Sem worktree associado | Contrato isolado em pré-PR; preservar fonte até publicação. [Auditoria](FECHAMENTO_APOIO_CONSUMIDORES.md) |
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

## Fechamento de SoAction::apply sob exceção

- Master de destino `674e74267df863dbaf50416c477bc7f918a826d8`; pré-PR `codex/pre-pr/action-apply-exceptions` em `b6afd3f1d71f16b46a68d11f9795c65269aa4b49`. Os dois commits são `10206c8cad196b2f50a9d546341ecd819cb3e2b6` e `b6afd3f1d71f16b46a68d11f9795c65269aa4b49`. Diff exclusivo de quatro arquivos, sem fixture estático e sem PRs do lab.
- Lab fixo preservado em `b27e37a6dd3ae941bcffcf480a377adc81e3fc27` (master mais 17 PRs); `lab/teste/action-apply-exceptions` em `c1aae576bf31376f552496ff17ccb150f9accf99` recebe somente os dois commits candidatos.
- Compilação na RAM `/dev/shm/coin-action-apply-20261006`: master puro Debug compartilhado 79/79, master com fixture separado Release estático 79/79, lab Debug compartilhado 106/106, todos com `COIN_THREADSAFE=ON`; regressão específica com ASan/UBSan e LeakSanitizer aprovada.

Windows/publicação pendentes. A restauração de estado próprio em `SoGLRenderActionP::render` fica no próximo ciclo. [Registro completo](FECHAMENTO_SOACTION_APPLY.md).

## Fechamento de SoGLRenderAction sob exceção

- Pré-requisito explícito `codex/pre-pr/action-apply-exceptions` `b6afd3f1d71f16b46a68d11f9795c65269aa4b49`; pré-PR GL `codex/pre-pr/gl-render-exceptions` `2f47f7fcf338190da9754ae9cd5564530d6985b6`. Diff próprio contra o pré-requisito: quatro arquivos, sem demais PRs do lab.
- Lab fixo `b27e37a6dd3ae941bcffcf480a377adc81e3fc27` preservado; `lab/teste/gl-render-exceptions` `06bcd67d50a0037db6f7c2fc3ec62d81561f3a40` incorpora SoAction separadamente e depois somente a candidata GL.
- Compilação em `/dev/shm/coin-gl-render-exceptions-20261006`: destino Debug compartilhado 80/80; lab Debug compartilhado 107/107, com `COIN_THREADSAFE=ON` e regressão GLX real. A biblioteca anterior falhou na segunda renderização após exceção de pré-callback (exit 134).

ASan/UBSan do build isolado passaram sem LeakSanitizer; com LeakSanitizer, só aparece a retenção anterior do cc_glglue (13.682 bytes). A integração temporária com as duas fontes da pré-PR do glue passou com ASan/UBSan/LeakSanitizer sem supressões. Windows/WGL e publicação pendentes. [Registro completo](FECHAMENTO_GL_RENDER_EXCEPTIONS.md).

## Extração do mapa privado ocioso de ScXMLObject (2026-10-06)

- Base fixa `lab/open-prs-integration` `b27e37a6dd3ae941bcffcf480a377adc81e3fc27` e destino master `674e74267df863dbaf50416c477bc7f918a826d8`, ambos preservados. Fonte histórica `archive/lab-sb-lists-evaluation-legacy-20261002` auditada sem transportar seu merge.
- Trabalho `codex/work/scxml-object-unused-map` `b33c07668281db6997d48aad73301806c9742ca5`; PR local `codex/pr/scxml-object-unused-map` `0b58696efdf4f60508ed3eea0ca41be8bee6e69d`; cópia temporária `lab/teste/scxml-object-unused-map` `377fce09fa1a7ada6870fa659a219045e2e9a22a`. Os três commits têm patch-id estável `388e9f64fcd4739cdfd80171dd7bca40fd8c773d`.
- Comandos em ambas as bases: `cmake -S <worktree> -B /dev/shm/coin-scxml-object-unused-map-{pr,lab} -DCOIN_BUILD_TESTS=ON -DCOIN_BUILD_DOCUMENTATION=OFF -DCMAKE_BUILD_TYPE=Debug`; `cmake --build <build> --parallel 4`; `xvfb-run -a -s '-screen 0 1280x1024x24 +extension GLX' env LIBGL_ALWAYS_SOFTWARE=1 COIN_GLX_PIXMAP_DIRECT_RENDERING=1 ctest --test-dir <build> --output-on-failure --parallel 4`. PR/master **78/78**; lab **105/105**.
- `git merge-base master codex/pr/scxml-object-unused-map` retornou o master; `git diff --check master...codex/pr/scxml-object-unused-map` passou; o diff só remove 22 linhas de `src/scxml/ScXMLObject.cpp`. Nenhuma publicação. A cópia temporária de lab foi removida após o gate; trabalho e PR locais ficam para Windows.

## DTD/libxml2: branch de trabalho experimental (2026-10-06)

- `codex/work/cc-xml-dtd-libxml2` `333d116c42585c82360658ed3c91c33596d83be8` parte de `codex/pr/cc-xml-parser-transactional` `162dc998af31291fb2d861b0d6b1431acb1c18da`; `git merge-base` confirmou esse pré-requisito. Diff próprio em oito arquivos, patch-id `bd16765f4aee5b321031e0919ded691e817ad869`; `git diff --check` passou. Não há branch de PR nem cópia lab desta mudança, pois ainda não foi declarada pronta.
- Fonte oficial libxml2 2.15.4 de `https://download.gnome.org/sources/libxml2/2.15/libxml2-2.15.4.tar.xz`, SHA-256 `98087fd181d9070724f3fbc65c7377db03038eb92bd882374daff44940138821` igual ao `.sha256sum` oficial. Extraída em `/tmp/coin-libxml2-2.15.4-KdqcZQQ7`, compilada em `/dev/shm/coin-libxml2-2.15.4-build` com validação DTD/threads, 8/8 testes oficiais. Nenhum `cmake --install`; Mint 22.3 e pacote de sistema 2.9.14 intactos. A configuração Coin com opção ON e a versão 2.9.14 foi recusada por CMake como incompatível.
- Build Coin em RAM da branch de trabalho com `COIN_XML_DTD_LIBXML2=ON` e um `FindLibXml2.cmake` temporário fora do repo apontando para a lib 2.15.4: `cmake -S <worktree> -B /dev/shm/coin-xml-dtd-work -DCOIN_BUILD_TESTS=ON -DCOIN_BUILD_DOCUMENTATION=OFF -DCMAKE_BUILD_TYPE=Debug -DCOIN_XML_DTD_LIBXML2=ON -DCMAKE_MODULE_PATH=<módulo temporário>`; `cmake --build <build> --parallel 4`; `xvfb-run -a -s '-screen 0 1280x1024x24 +extension GLX' env LIBGL_ALWAYS_SOFTWARE=1 COIN_GLX_PIXMAP_DIRECT_RENDERING=1 ctest --test-dir <build> --output-on-failure --parallel 4`: **80/80**. Com opção OFF em `/dev/shm/coin-xml-dtd-off`, mesmos comandos sem módulo: **80/80**; API C indisponível preserva o DOM. Os dois builds normais foram removidos depois do gate para liberar RAM.
- Clang com ASan/UBSan/LeakSanitizer, Expat externo 2.6.1 e libxml2 2.15.4 em `/dev/shm/coin-xml-dtd-asan`: alvo `CoinXmlDtdLibxml2Test`; `env ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir /dev/shm/coin-xml-dtd-asan -R '^CoinXmlDtdLibxml2$' --output-on-failure`: **1/1**, sem diagnóstico. A regressão dirigida inclui URL HTTPS suprida somente pelo callback, sem rede automática, além de DTD, entidades externas e rollback.
- **Bloqueios para PR:** definir limites após estudo, verificar equivalência das etapas libxml2/Expat e falhas de alocação, ampliar a matriz Expat, testar sobre cópia do lab fixo e sobre o destino de PR, Windows. A API atual é opt-in, apenas para buffer de uma vez; não promete leitura de arquivo/parcial com DTD e não se destina a XML não confiável nesta fase. O detalhe de contrato está em `docs/XML_DTD_LIBXML2.md` na própria branch.

## Limites XML: estudo sem valores (2026-10-06)

- `codex/work/cc-xml-limits` `c820b30b0f` parte de `codex/pr/cc-xml-parser-transactional` `162dc998af31291fb2d861b0d6b1431acb1c18da`. O único delta é `docs/XML_LIMITS_STUDY.md`; não há mudança de código, valores, branch de PR nem lab desta contribuição ainda.
- Foram examinados os caminhos de entrada por arquivo/buffer/chunks, callbacks Expat de elementos/atributos/CDATA e cálculo/escrita recursiva de tamanho, além do caminho experimental DTD libxml2 → buffer UTF-8 → Expat. Os quatro SCXML versionados têm 480, 4.123, 13.377 e 16.638 bytes; a amostra não fundamenta um teto. O próximo passo é medir documentos reais e definir o contrato antes de implementar limites e testes de fronteira.

## Leitura defensiva de ScXMLDocument (2026-10-06)

- Destino master `674e74267df863dbaf50416c477bc7f918a826d8`; trabalho e PR **somente locais** `codex/work/scxml-document-defensive-read` e `codex/pr/scxml-document-defensive-read`, ambos em `ef322f1f17`. Diff exclusivo: `src/scxml/ScXMLDocument.cpp` (guarda de documento/raiz/tipo nulos e regressão); `git diff --check` aprovado.
- Lab fixo `b27e37a6dd3ae941bcffcf480a377adc81e3fc27` preservado; cópia `lab/teste/scxml-document-defensive-read` recebeu somente `ef322f1f17` por cherry-pick e chegou a `6cafbf0b0c01be4709abdcdf0b00843c3fbbe326`. Diff da cópia: o mesmo arquivo, sem outros PRs na contribuição. Worktree temporário removido após validação.
- Em cada base: `cmake -S <worktree> -B /dev/shm/coin-scxml-defensive-{work,lab} -DCOIN_BUILD_TESTS=ON -DCOIN_BUILD_DOCUMENTATION=OFF -DCMAKE_BUILD_TYPE=Debug`; `cmake --build <build> --parallel 6`; `<build>/bin/CoinTests --run_test='*ScXMLDocumentRejectsMissingOrUnexpectedRoot' --report_level=short`; `xvfb-run -a -s '-screen 0 1280x1024x24 +extension GLX' env LIBGL_ALWAYS_SOFTWARE=1 COIN_GLX_PIXMAP_DIRECT_RENDERING=1 ctest --test-dir <build> --output-on-failure --parallel 4`. Build e teste dirigido passaram em ambas as bases (1 teste, 6 checks); conjunto **78/78 no master** e **105/105 no lab**. Tudo compilado na RAM, sem instalação/publicação. Gate Windows pendente.

## Fronteira header/biblioteca Expat (2026-10-06)

- Base de destino master `674e74267df863dbaf50416c477bc7f918a826d8`; trabalho `codex/work/cc-xml-expat-matrix` e PR **somente local** `codex/pr/cc-xml-expat-header-boundary` em `304f28db5764d9a194404e50adc0bcdbeacf0d88`. O diff do PR contém somente `CMakeLists.txt` e `src/xml/document.cpp`: o header `expat.h` passa a corresponder à biblioteca selecionada e o include path do pacote externo é propagado ao build. `git merge-base` confirma master, e `git diff --check` passa.
- Lab fixo `b27e37a6dd3ae941bcffcf480a377adc81e3fc27` preservado; cópia temporária `lab/teste/cc-xml-expat-header-boundary` recebeu apenas `304f28db57` por cherry-pick e chegou a `10af42ecb5818e2260420126984eccfde4b3ea88`. Diff da cópia: os mesmos dois arquivos, sete linhas adicionadas, sem transportar demais PRs para a branch de PR. Worktree e ref temporários removidos após o gate.
- Build/testes inteiramente em RAM: `cmake -S <worktree> -B /dev/shm/coin-expat-header-<modo> -DCOIN_BUILD_TESTS=ON -DCOIN_BUILD_DOCUMENTATION=OFF -DCMAKE_BUILD_TYPE=Debug`, com `-DUSE_EXTERNAL_EXPAT=ON` para `<modo>=external`, `OFF` para `embedded`, e sem essa opção para `<modo>=lab`; `cmake --build <build> --parallel 6`; `xvfb-run -a -s '-screen 0 1280x1024x24 +extension GLX' env LIBGL_ALWAYS_SOFTWARE=1 COIN_GLX_PIXMAP_DIRECT_RENDERING=1 ctest --test-dir <build> --output-on-failure --parallel 4`. Resultados: Expat externo do sistema **2.6.1, 78/78**; Expat embarcado do master **78/78**; lab **105/105**. A atualização vendorizada para 2.9.0 tem gate próprio separado. Windows e ampliação da matriz para outros sistemas/versões ainda pendentes; nenhuma publicação/instalação.
- `git merge-tree` entre esta branch e `codex/pre-pr/cc-xml-expat-2.9.0` encontrou conflito textual pequeno no bloco de `#include` de `src/xml/document.cpp`: ambas ajustam `XML_STATIC`. A resolução e o teste cumulativo estão registrados abaixo; as branches de PR permanecem independentes.

### Reconciliação cumulativa Expat (2026-10-06)

- Cópia `lab/teste/cc-xml-expat-combined` sobre o lab fixo `b27e37a6dd3ae941bcffcf480a377adc81e3fc27`: cherry-picks **somente para validação** do update `4d476427bf1b60070483675cabcfbeb8d83c846d`, requisito C99 `aef999cb3642638c61a82128f0db69ad9e532702` e correção de header `304f28db5764d9a194404e50adc0bcdbeacf0d88`. A última exigiu resolver o conflito textual em `src/xml/document.cpp`; head temporário `e43a9409fc02b61c1a129b5104973b32c4c22393`. As branches de PR originais não foram alteradas.
- Comandos, em uma sessão de RAM por variante: `cmake -S /tmp/coin-lab-teste-cc-xml-expat-combined -B /dev/shm/coin-expat-combined-<modo> -DCOIN_BUILD_TESTS=ON -DCOIN_BUILD_DOCUMENTATION=OFF -DCMAKE_BUILD_TYPE=Debug -DUSE_EXTERNAL_EXPAT=<OFF|ON>`; `cmake --build <build> --parallel 6`; `xvfb-run -a -s '-screen 0 1280x1024x24 +extension GLX' env LIBGL_ALWAYS_SOFTWARE=1 COIN_GLX_PIXMAP_DIRECT_RENDERING=1 ctest --test-dir <build> --output-on-failure --parallel 4`. Builds e suítes passaram nos modos embarcado 2.9.0 e externo 2.6.1 (105/105 em cada). O embarcado emite avisos de depreciação para `XML_GetCurrentLineNumber`/`XML_GetCurrentColumnNumber`, herdados do update 2.9.0; não houve falha. Sem Windows/publicação.

## Cotas configuráveis do parser XML (2026-10-06)

- Estudo inicial `codex/work/cc-xml-limits` `c820b30b0f4b202c3a2a8c4712774db0353fb400`. Candidata de implementação `codex/work/cc-xml-resource-limits` e PR **somente local** `codex/pr/cc-xml-resource-limits` em `41535dbcd9`, dependentes de `codex/pr/cc-xml-parse-length-guard` `f2142d0d1a32f8ac59e7b8d04a7be3d0d3dc1fc0` (que inclui o parser transacional). `git merge-base` confirmou esse head; o diff próprio tem cinco arquivos: documentação, header C, implementação, CMake e regressão. Nenhum outro PR do lab está no diff de PR.
- API opt-in por documento, zero = cota desabilitada: bytes XML cumulativos, bytes de strings emitidos por Expat (nomes, valores e character data), elementos, atributos e profundidade. A API legada mantém compatibilidade sem valores globais arbitrários. Excesso retorna `FALSE`, identifica a cota, para Expat e preserva raiz/filename anteriores. Testes cobrem limite/limite+1, chunks, arquivos acima de 8 KiB, expansão de entidade interna, profundidade, atributos, reutilização e recusa de alterar configuração durante parsing parcial. Ainda não cobre saída serializada nem alocações anteriores no estágio libxml2 DTD; não declarar perfil DTD seguro para XML não confiável.
- Build normal em RAM: `cmake -S /tmp/coin-work-cc-xml-resource-limits -B /dev/shm/coin-xml-resource-limits-work2 -DCOIN_BUILD_TESTS=ON -DCOIN_BUILD_DOCUMENTATION=OFF -DCMAKE_BUILD_TYPE=Debug`; `cmake --build <build> --parallel 6`; `ctest --test-dir <build> -R '^(CoinXmlLimits|CoinXmlTransactionalRead|XmlCHeaderDocumentTest)$' --output-on-failure` **3/3**; suíte sob `xvfb-run -a -s '-screen 0 1280x1024x24 +extension GLX' env LIBGL_ALWAYS_SOFTWARE=1 COIN_GLX_PIXMAP_DIRECT_RENDERING=1 ctest --test-dir <build> --output-on-failure --parallel 4` **80/80**.
- ASan/UBSan/LeakSanitizer em RAM com Clang, `-DUSE_EXTERNAL_EXPAT=ON`, `-DCOIN_THREADSAFE=OFF`, `-DCMAKE_C_FLAGS=-fsanitize=address,undefined`, `-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined`, flags equivalentes de link, alvo `CoinXmlLimitsTest CoinXmlTransactionalReadTest`: **2/2**, sem diagnóstico. Primeira tentativa parou durante a compilação por `/dev/shm` cheio, antes de testes; foram removidos apenas nove builds temporários já validados desta tarefa, liberando aproximadamente 2,4 GiB, e o build instrumentado foi concluído com `--parallel 3`.
- Lab fixo `b27e37a6dd3ae941bcffcf480a377adc81e3fc27` preservado. Cópia `lab/teste/cc-xml-resource-limits` aplicou, nesta ordem, parser transacional `162dc998af`, guarda `2a19c4305f`/`f2142d0d1a`, estudo `2b331e49f5` e implementação `41535dbcd9`, chegando a `c9cf764c114a2da3c09b798f155d28dc37e41844`. O conflito inicial em `testsuite/CMakeLists.txt` preservou tanto o teste NURBS do lab quanto o alvo transacional XML. Comandos de configuração/build iguais aos normais acima, fonte `/tmp/coin-lab-teste-cc-xml-resource-limits`, build `/dev/shm/coin-xml-resource-limits-lab`, `--parallel 4`: regressões **3/3**, conjunto **107/107**. `git diff --check` aprovado. Windows/publicação pendentes.

## Medição das associações de ScXMLEvent (2026-10-06)

- Estudo local `codex/work/scxml-event-associations` em `f38f8cb99db259e36b4b338c0cc9516df6bcf47e`, sobre a candidata de ownership `a4fc778e7c07fd416d0799e4b002bdb090b92f8f`; comparação histórica compacta `0f76dc8c09f0a990cc8011a8da0ac7a59843f246`. O delta contém somente o benchmark e `docs/SCXML_EVENT_ASSOCIATIONS_STUDY.md`, sem PR de código nem mudança no lab fixo.
- Compilação Release em RAM para as duas revisões; benchmark isolado de 1, 2, 4 e 8 associações, 2 milhões de operações, três rodadas alternadas sem builds concorrentes. A fonte versionada foi recompilada contra a candidata atual e executada com 4 associações (`get=22,18`, `set=34,02`, `keys=12,79` ns/op). Comandos e intervalos estão no estudo.
- Busca e substituição praticamente empatam; a enumeração melhora alguns nanossegundos. Sem medição de carga SCXML real e sem prova de invariantes sob falha de alocação/ordenação/alias, a variante compacta **não** vira PR neste ciclo. A candidata de correção de ownership permanece separada.

## Retorno das correções Windows ao Linux

As 12 versões corrigidas publicadas pelo Windows foram baixadas e testadas individualmente no Linux: Debug compartilhado e Release estático, ambas com threads habilitadas. Passaram as 24 configurações e 1.884 entradas de CTest, sem falhas ou skips. Inclui a correção de link de SbSmallMap/caches GL e o pré-requisito do fixture estático nas dez candidatas XML/SCXML.

O [relatório de retorno Windows](VALIDACAO_RETORNO_WINDOWS.md) identifica os heads `fork/codex/windows/*` e `fork/codex/windows-smallmap-test-link`, os comandos, logs e limites da rodada. O lab fixo e as branches principais anteriores permanecem preservados; integração cumulativa e consolidação das referências auxiliares não foram executadas nesta rodada.
