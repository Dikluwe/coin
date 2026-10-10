# Tratamento das 28 falhas — estudo de 2026-10-08

**A campanha original passou de 134 PASS/28 FAIL para 162/162 PASS neste PC,
com os mesmos gates.** Quatro falhas foram corrigidas no renderer; as outras
24 passam com um workaround externo do Mesa, opt-in e instalado em prefixo
privado. O Mesa instalado continua apresentando a divergência de sampling.
Este resultado é condicionado ao perfil corrigido; não certifica o driver do
sistema nem uma política pública pronta para integrar no CoinRender.

Tudo permanece em `codex/coin-portable-sampling-study`. Renderer:
`69ba9d0a90134b3190dbe26b7efd473d5f29c837`; laboratório externo:
`436fd35aaf487e1300ebe6d398b1976f7ce0238a`. Coin original continua em
`01b360af322ac873cd4d1c4262315673aa1ad3c8`; `codex/coin-render` continua em
`1f8c9a49b94fda10a19dbc0c49ef3614afd37269`. Nenhuma integração ou publicação.

## Correções e atribuição

| Falhas originais | Correção | Verificação |
|---|---|---|
| 4 procedural, AMD/OpenGL, wgpu/BGFX, native/fine_uniform | Publicar XY dos strokes na mesma grade de 1/256 pixel da cobertura CPU; transportar profundidade clip no shader BGFX | Procedural passa nos seis perfis e nos dois modos, com gate máximo3 preservado |
| 8 projective + 8 sampling | Corrigir filtros mistos no Mesa, inclusive a referência CoinGL | Comparações CPU/GPU/CoinGL passam; no projetivo nearest, erro máximo0 |
| 4 mip profundo + 4 RTT direto, native AMD | Centralizar a amostragem separadamente nos dois mips no passo NIR externo | Gates de formatos, unidades0/7, wrap, NPOT e RTT direto passam |

A quantização fica na expansão de strokes após o deslocamento de propriedade
da borda. Contornos com `preservePolygonEdgeDirection` conservam seu contrato.
Não se reduziu a precisão Z nem se alterou a cobertura CPU para obter igualdade.
A tentativa de interpolar na CPU sem a grade melhorou uma cena e piorou outras;
foi descartada. A correção de XY expôs uma diferença POINTS/UV0 BGFX/OpenGL
máximo8. Mudar D24 para D32 não resolveu e foi descartado.

BGFX agora interpola o par clip Z/W e recupera a profundidade pela razão,
seguida do contrato comum de intervalo/offset. GLSL transporta o numerador já
convertido para [0,1]. Todos os fragmentos que compartilham o vertex shader
padrão têm o mesmo hash de interface BGFX, inclusive composição/readback.
Instancing e shaders dedicados de sombra conservam suas interfaces. Não houve
mudança na ABI pública, nos uniformes nativos wgpu3040 bytes ou instâncias96 bytes.

O diagnóstico EGL independente não liga Coin. Com o sampler
nearest/mip-linear + magnificação linear, a primeira coluna alta deveria ser32:
o Mesa instalado e o privado sem workaround selecionam24, com máximo120.
Alterar magnificação para nearest acerta a minificação, mas erra magnificação
com máximo60; essa alternativa foi rejeitada. O workaround mantém magnificação
linear (máximo1) e corrige LOD3/3,5 (máximo0). O projetivo integrado, com o mesmo
Mesa privado e guard desligado, volta a falhar com máximo78.

O Mesa documenta limitações de TRUNC_COORD para filtros nearest/linear mistos
em gerações anteriores a gfx11. A reprodução isolada neste Renoir é compatível
com essa explicação; isso não é uma prova para todos os chips.
[Explicação no código Mesa](https://cgit.freedesktop.org/mesa/mesa/commit/?id=6fac2889317e80b601ac63f7e46047bf8893d597).

O patch fica em `testsuite/reproducers/portable-sampling-study/mesa`, fora dos
backends. Usa a fonte oficial
[Mesa25.2.8](https://docs.mesa3d.org/relnotes/25.2.8.html), SHA256 verificado,
limitado a Renoir, 2D normalizado, float32, filtro misto, sem anisotropia,
sampler bias/minLOD zero. Mantém o caminho original para casos não suportados.
Na minificação, o protótipo calcula a amostra nativa e duas reconstruídas:
**três amostras possíveis, desempenho ainda não qualificado**. Não substitui
`fine_uniform`, que tem outro algoritmo e custo. O guard não integra o cache:
cache foi desabilitado em todos os controles A/B.

## Campanhas qualificadas

AMD Renoir e RTX3060 Laptop, wgpu Vulkan AMD/NVIDIA e OpenGL AMD, BGFX Vulkan
AMD/NVIDIA e OpenGL. BGFX/OpenGL informa vendor/device0; certifica o resultado
funcional, não a identificação física da GPU. GPU sempre sequencial.

| Campanha original com renderer corrigido + Mesa privado ACO | Processos | PASS | FAIL |
|---|---:|---:|---:|
| Piloto avançado wgpu | 6 | 6 | 0 |
| Projective/sampling/advanced/procedural/RTT público | 60 | 60 | 0 |
| Mip profundo/RTT direto/viewport | 36 | 36 | 0 |
| LargeBindings/MultiDevice/stress/instancing | 24 | 24 | 0 |
| Sombras8/viewport/alpha-RTT | 36 | 36 | 0 |
| **Total original** | **162** | **162** | **0** |

Nenhum SKIP, alteração de tolerância ou substituição da referência. O
[ledger individual](validation/failure-closure-20261008/original-28-ledger.json)
liga cada FAIL anterior ao PASS correspondente, com hashes dos logs. O piloto
repete seis casos já presentes no gate avançado, como na campanha anterior.

Com o driver do sistema e somente as correções do renderer: 168 processos,
144 PASS/24 FAIL; esse piloto ampliado contém12 processos. Os24 FAIL são
sampling AMD. Geometria adicional:16/16 PASS, incluindo12 controles de84 casos
curvos e quatro NVIDIA de1290 casos completos. Estilos CPU/GPU:4/4 PASS no
sistema; dois programas exclusivamente CPU passaram. Builds finais wgpu/BGFX
passaram. Rust não mudou nesta rodada; os46 testes aprovados da rodada anterior
continuam como recibo anterior, não como nova execução.

Um teste de estilos que também usa GPU foi iniciado junto a uma coorte profunda
por engano. A coorte profunda foi repetida sequencialmente em `final/deep-serial`;
a primeira execução `final/deep` e o estilo nomeado `cpu-final` não são recibos
qualificados. A sonda EGL retorna0 para seus controles diagnósticos mesmo quando
o modo nativo diverge; esse exit não foi usado como gate de paridade.

## Falhas adicionais preservadas

A rodada ampliada exige CoinGL também no programa de estilos. Resultado com
Mesa privado:12 controles curvos PASS e12 estilos com4 PASS/8 FAIL; somada à
campanha original, **186 processos,178 PASS/8 FAIL**. Esses oito FAIL não são
uma regressão atribuível ao workaround: o mesmo recorte por plano de usuário
falha com o guard desligado, o Mesa instalado e as bibliotecas do renderer
congeladas antes das correções. No caso `clip=1,width=1,pattern=65535`, CoinGL
não publica a borda x16,y10..27, enquanto Core publica255. Os logs anteriores
4/4 de estilos não exigiam CoinGL; não certificavam essa comparação.

Compilar com LLVM18.1.3 no vertex shader e ACO no fragmento preservou sampling,
mas não resolveu o recorte; a variante foi descartada. Os patches oficiais da
[mesma versão Ubuntu instalada](https://packages.ubuntu.com/noble-updates/mesa-libgallium)
foram inspecionados, sem identificar uma correção de recorte. Não se concluiu a
causa dessa diferença. O contrato portátil CPU/GPU foi repetido separadamente
no Mesa privado: **8/8 PASS** AMD. Essa coorte sem comparação CoinGL opcional
não substitui nem apaga os oito FAIL estritos.

Há também um gate anterior fora dos28: geometria completa AMD, material
`bindings/0/7/generated-0/alpha-0/fast-0`, CPU/CoinGL máximo11; reproduzido antes
e depois da correção de XY. Portanto não se afirma que todos os testes do
projeto passam. Essas diferenças de raster/referência continuam como estudos,
coerentes com o critério portátil CPU/BGFX/wgpu autorizado anteriormente.

## Evidências e continuação

[Resultados](validation/failure-closure-20261008/qualified-results.json),
[manifest](validation/failure-closure-20261008/manifest.json),
[hashes dos runtimes](validation/failure-closure-20261008/runtime-hashes.json) e
[receita do Mesa](../testsuite/reproducers/portable-sampling-study/mesa/README.md).
Logs de tentativas descartadas, crash inicial do compilador NIR e seu reparo
foram preservados. O crash foi no compilador do primeiro protótipo; a versão
corrigida passou NIR validation e os gates integrados.

Os arquivos duráveis estão versionados neste estudo e em
`/mnt/Laranja/Git/externos/coin-portable-sampling-artifacts/20261008-failure-fixes`.
Os builds ali são locais e mutáveis; hashes fixam os recibos desta rodada.
Nenhuma lista ou evidência depende exclusivamente de `/tmp`.

Antes de propor integração: medir custo/aplicabilidade do workaround externo,
definir a política/API portátil e seu fallback, tratar os estudos CoinGL de
recorte/iluminação, e qualificar Android/Windows/FreeCAD. O resultado162/162 é
local e condicionado ao perfil Mesa de estudo; não justifica atualizar a
produção ou distribuir o driver agora.
