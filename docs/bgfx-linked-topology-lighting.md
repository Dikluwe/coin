# Links aninhados, montagens e iluminação Coin/GL

## Cobertura de topologia

O cenário `freecad-mouse-link-topology` usa duas instâncias separadas e três
estruturas: link de link de uma peça, link de uma montagem App::Part com
submontagem App::Part, e link de link dessa montagem. As estruturas aninhadas
ativam `LinkTransform` para compor o placement da origem com o da instância.
O subgrupo e a segunda instância também têm rotações próprias.

Para cada instância, faces, arestas e vértices são projetados a partir da
topologia transformada e confirmados por ray-picking. Hover e clique chegam
por XTest; a API de seleção somente lê resultados e limpa o cenário.
O teste exige o objeto raiz e o subcaminho completo, pixels visíveis de
destaque, isolamento da outra instância e limpeza. Ctrl adiciona as duas
instâncias. A última estrutura repete todos os elementos após resize e exige
idle estável no BGFX. Há 24 interações de topologia por célula.

`getSelectionEx(document, 0)` preserva a identidade da instância e o caminho
`SubAssembly.Box.FaceN/EdgeN/VertexN`. O modo padrão resolve a seleção até a
peça original; não é uma falha do renderer e não pode ser usado como oráculo
da identidade da montagem. Picks são normalizados usando `ParentObject` e
`SubName`, quando presentes, sem descartar o caminho aninhado.

O cenário usa pontos de 6 px e linhas de 2 px para medir quinas parcialmente
ocultas sem reduzir o mínimo de pixels. Capturas ficam no disco, mas imagens
em memória são liberadas a cada elemento para limitar o uso em DPR 2.

Esta é cobertura de hierarquias de renderização App::Part/App::Link. O build
mínimo não contém o Assembly workbench: joints, solver, ferramentas de edição,
links entre documentos e Link arrays não são aprovados por este teste.

## Especular compatível com o GL legado

O caminho anterior calculava o vetor do observador como `normalize(-positionView)`.
Isso produzia brilho diferente em peças deslocadas lateralmente, mesmo com
normais e luz direcional iguais. Coin/GL usa o observador no infinito do
fixed-function: vetor `(0, 0, 1)` em espaço da câmera, inclusive em perspectiva.

BGFX (opaque, depth peeling e weighted OIT), WGSL e CPU agora seguem esse
mesmo contrato. Na atualização Gouraud, posição, normais e termos de luzes
pontuais/spot passam a ser avaliados por vértice, antes da interpolação.
Não é um modo local-viewer configurável nem captura de estado GL externo.

`CoinRenderLightingTest` verifica um plano especular em posições fora do eixo da
câmera. A regressão falhou antes da correção e passou depois. Os 46 CTest
selecionados passaram com Xvfb; isso não é prova de GPU física nem de
disponibilidade da referência GL offscreen, que esteve indisponível ali.

A prova visual em FreeCAD usa a matriz física BGFX de links simples (8/8)
e a referência GL do mesmo runtime (2/2). Em 88 comparações, o MAE RGB dos
baselines passou de 1,932–4,069 para 0,303–0,554. O limiar de máscara continua
8, usando a união dos foregrounds, sem excluir a barra de abas. O IoU mínimo
das máscaras de destaque permanece 0,9944.

`compare_link_lighting.py` exige a matriz inteira e MAE dos baselines <= 1.
O conjunto anterior à correção é rejeitado; o novo passa. Esse gate é de
iluminação de links planos, não de equivalência pixel a pixel de toda cena.
A divergência de interpolação PHONG em normais curvas e luzes pontuais/spot
foi resolvida no caminho de compatibilidade. Veja os novos oráculos e o
escopo da validação em [bgfx-gouraud-parity.md](bgfx-gouraud-parity.md).
Não se declara paridade visual universal nem validação de hardware wgpu.

## Repetição

Use o runtime persistente descrito em [bgfx-recovery.md](bgfx-recovery.md) e
o runner `testsuite/qt-quarter/run_isolated.py`, com a seleção Mesa/AMD local
da máquina híbrida. Acrescente `--case freecad-mouse-link-topology --timeout 300`
para a matriz; use `--require-hardware` no BGFX e `--renderer opengl --reference-gl`
somente na referência. Nenhum SKIP ou UNSUPPORTED conta como aprovação.

```sh
python3 -m unittest discover -s testsuite/qt-quarter -p 'test_*.py'
python3 testsuite/qt-quarter/compare_link_lighting.py \
  --native build-bgfx-recovery/artifacts/links-lighting-aligned \
  --reference build-bgfx-recovery/artifacts/links-lighting-gl \
  --output build-bgfx-recovery/artifacts/links-lighting-parity.json
```

Falhas de preparação da primeira versão do teste foram preservadas em
`artifacts/link-topology-gl-probe*`; não foram transformadas em PASS. Resultados
sem composição de placements são provas distintas da matriz final com
`LinkTransform` ativo, em `link-topology-composed-bgfx` e `link-topology-composed-gl`.


## Resultados consolidados

`artifacts/link-topology-validated/results.json` contém as oito variantes
nativas completas e aprovadas em GPU física, e `validation.json` registra
a origem de cada execução. São 192 interações nativas de topologia, 24 por
célula, com Ctrl, isolamento, limpeza, resize e idle estável.

A execução inicial foi interrompida por SIGTERM após completar as quatro
células Vulkan. A causa da interrupção não foi estabelecida. Somente os
quatro resultados completos por célula foram preservados no consolidado;
o caso OpenGL interrompido não foi aprovado. As quatro células OpenGL foram
executadas novamente e passaram em `link-topology-composed-opengl/results.json`.

A referência final `link-topology-composed-gl/results.json` passou em 2/2
(DPR 1/2), com 48 interações de topologia. A matriz simples após a correção de
iluminação também passou em 8/8 BGFX e 2/2 GL. O novo gate visual está em
`links-lighting-parity.json`: 88 comparações e baselines com MAE <= 1.

Controles Python: **76/76 PASS**. CTest selecionados: **46/46 PASS**.
O runner agora salva resultados concluídos atomicamente após cada célula;
matrizes incompletas continuam sendo rejeitadas pelo gate de iluminação.
