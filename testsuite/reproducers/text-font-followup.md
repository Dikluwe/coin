# Seis grupos de correções de fontes — 2026-10-06

## Organização local e base

As seis branches abaixo formam uma pilha local. O primeiro grupo parte de
`7f2e9335522a6ffdc9d66c4c18f80e4fcca5d0e5`, a #774 atualizada; cada grupo
seguinte parte do commit anterior. Para revisar somente um grupo, comparar
sua branch com a base indicada, não com master. Os PRs futuros devem respeitar
esta ordem ou ser rebaseados conforme as bases forem integradas.

A branch `codex/text-font-followup`, em `a85bbe2b82`, preserva a implementação
original completa. Nenhuma branch remota ou novo PR foi criado nesta divisão.
A #774 conserva as correções de ABI, viewAll e raster mono/RGBA.

| Grupo | Branch local | Commit de código | Base | CTest acumulado, legacy ON |
| --- | --- | --- | --- | --- |
| 1. Picking UTF-8 | `codex/font-pr1-utf8-picking` | `530331f40b` | `7f2e933552` | 86/86 |
| 2. Bitmaps | `codex/font-pr2-bitmap-limits` | `169d276eb8` | `530331f40b` | 88/88 |
| 3. Concorrência e rotação | `codex/font-pr3-cache-lifetime` | `d271bd8d05` | `169d276eb8` | 91/91 |
| 4. Fallback e ownership | `codex/font-pr4-fallback-ownership` | `1b656dba24` | `d271bd8d05` | 94/94 |
| 5. Parser e Fontconfig | `codex/font-pr5-font-discovery` | `14e25318f4` | `1b656dba24` | 97/97 |
| 6. Tesselação | `codex/font-pr6-tessellation` | `b447c646df` | `14e25318f4` | 99/99 |

## Escopo para os futuros PRs

### 1. Fix SoText2 picking for decoded UTF-8 characters

O picking usa a quantidade de caracteres decodificados, em vez dos bytes
UTF-8, e retorna o índice correto em SoTextDetail. Os limites das linhas e o
quad respeitam a justificação e a largura do layout. Arquivos de produção:
SoText2.cpp e a documentação do índice em SoTextDetail.cpp.
Regressão: SoText2Utf8PickingTest.cpp.

### 2. Validate font bitmap storage and preserve signed metrics

Valida dimensão, pitch, formato e produtos de armazenamento antes de copiar
bitmaps; preserva métricas assinadas sem truncamento de 16 bits. Centraliza os
helpers em bitmap.h e verifica a conversão do tamanho FreeType 26.6.
Arquivos: bitmap.h, glyph2d.cpp, fontlib_wrapper.cpp, freetype.cpp e o header
privado glue/freetype.h. Regressões: FontBitmapStorage e
FontBackendMetricsAndFallback.

### 3. Synchronize font cache lifetime and registry acquisition

Adquire e libera referências sob os mutexes dos caches 2D/3D. A obtenção e a
referência da fonte no wrapper passam a ser uma operação atômica; o identificador
emprestado antigo continua disponível. Inclui a rotação normalizada na chave do
cache 2D e do registro, inclusive para a fonte embutida. O helper privado
cc_flw_get_font_id_and_ref é consumido pelos caches e por SoGlyph.
Regressões: FontCacheConcurrentLifetime, FontRegistryConcurrentAcquisition e
FontAngleCacheIdentity.

### 4. Separate Unicode fallback and glyph representation ownership

Caractere Unicode ausente usa '?' explicitamente, sem truncar o codepoint para
um byte. Bitmap e vetor possuem decisões independentes de fallback e falha.
Referências dos consumidores protegem a representação compartilhada até a
última liberação. As operações FreeType selecionam o tamanho adequado para
bitmap e vetor, inclusive para avanço e kerning, evitando interferência entre
as representações. Este grupo usa a aquisição atômica do grupo 3 e a validação
de bitmap do grupo 2.
Regressões: FontUnicodeFallback, FontRepresentationOwnership e
FontRepresentationSizeIsolation.

### 5. Bound font specification parsing and clean up Fontconfig failures

Separa família e estilo sem ultrapassar a entrada; preserva padrões explícitos
Fontconfig e caminhos Windows. Escapa o estilo ao formar o padrão Fontconfig.
Libera patterns/fontsets em cada retorno de erro por ownership local.
Regressões: FontSpecificationParsing, FontSpecificationWithoutFontconfig e
FontconfigFailureCleanup, com falhas injetadas nas etapas de descoberta.

### 6. Bound 3D font tessellation and recover from errors

Normaliza complexidade para um valor finito no intervalo [0,1], limita a
subdivisão a 1–16 passos e verifica formato, entrypoints GLU, produtos de tamanho
e alocações dos buffers próprios. Erros de decomposição ou tesselação descartam
os dados parciais, liberam os recursos e permitem processar o próximo glifo.
Regressões: FontTessellationFailureRecovery e FontTessellationCoordinateLifetime.
O teste nativo GLU afastou a hipótese inicial sobre reutilização do array de
coordenadas: não há alteração de produção para essa hipótese. O lifetime dos
dados de callback existente foi mantido.

## Validação e limites

Cada prefixo foi configurado, compilado e testado em Linux shared/Release com
FreeType, Fontconfig e Xvfb/Mesa. Os totais da tabela não incluem testes pulados. A pilha final também passou
97/97 CTest com COIN_BUILD_LEGACY_GL_RENDERER=OFF, sem skips. Os logs dessa
configuração estão em /tmp/font-pr6-off-{config,build,tests}.log.
Os fixtures que usam interposição ELF são restritos a Linux shared com FreeType;
os que acessam helpers privados ficam desativados em Windows shared.
Logs desta execução: `/tmp/font-prN-config.log`, `/tmp/font-prN-build.log` e
`/tmp/font-prN-tests.log`, com N de 1 a 6.

A comparação ABI dos sete arquivos de produção alterados contra a #774 atual
produziu 0 funções removidas, 0 funções alteradas, 0 variáveis alteradas e
1 função privada adicionada: cc_flw_get_font_id_and_ref. O exit 4 de abidiff
indica essa adição; não houve incompatibilidade reportada. Nenhum header público
foi alterado. Relatório: `/tmp/coin-text-font-abi/abidiff.txt`.

A árvore final de fontes, glue/freetype.h e SoGlyph equivale à implementação
original, com apenas remoção de whitespace excedente. SoText2 mantém também a
correção mono/RGBA posterior da #774. Os resultados anteriores de sanitizers e
FreeCAD pertencem à validação da branch original; não foram executados novamente
nesta separação. FreeCAD permanece ferramenta de análise, sem alterações.

Continuam fora destes grupos: OOM geral nas listas e construção dos caches,
extractfont, validação dos backends nativos Windows/macOS, shaping e política
ampla de fallback. Remover workarounds no FreeCAD requer uma alteração separada,
após definir a versão mínima do Coin que incorpora o conserto correspondente.
