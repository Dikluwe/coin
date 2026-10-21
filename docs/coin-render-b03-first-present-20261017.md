# B03 — observação de submissão única e diagnóstico X11

Continuação: [terreno histórico, correção do resize BGFX/OpenGL e regressões](coin-render-b03-terrain-resize-20261017.md). Os números deste relatório pertencem à campanha plana anterior; B03.2 histórico permanece aberto.

**B03 continua aberto.** Foi comprovado que o retorno da submissão pode
anteceder a imagem nova observável pelo X11. A ocorrência histórica específica
em wgpu/OpenGL, cuja captura já esperava 50 ms, ainda não foi reproduzida.
Não houve alteração do renderer para declarar corrigida essa ocorrência.

## Campanha física

| Evidência final | Resultado |
| --- | --- |
| Processos positivos | 51/51 |
| Quadros com oráculo independente exato | 153/153, RGB máximo 0 |
| Células GPU/API | Sete: BGFX Vulkan/OpenGL AMD/NVIDIA; wgpu Vulkan AMD/NVIDIA e OpenGL AMD |
| Quadro antigo logo após retorno | 152/153 observações; preservadas como observações, sem substituir o resultado inicial |
| Quadro antigo na observação aos 50 ms | 38 observações, todas BGFX; zero wgpu/OpenGL |
| Controle negativo de submissão omitida | Rejeitado: retorno 1, serial sem avanço e marca nova ausente após o limite |
| wgpu/OpenGL NVIDIA | Retorno 77, indisponível; não conta como render aprovado |

O teste envia somente um quadro por revisão. Um retângulo subdividido em um
milhão de triângulos recebe cor vermelha/verde alternada e um clear distinto
por revisão. A cor do pixel de borda identifica a chegada do quadro; depois,
todos os pixels são comparados com o retângulo e as cores calculados diretamente
das coordenadas autoradas. Nenhum render offscreen, segundo redraw ou espera
por uma imagem de referência antecede a primeira observação X11.

Cada processo cobre a criação da janela, troca de cor e resize 1280×960 para
640×480. Há controles de dois triângulos, política nativa/canônica, janela
com/sem gerenciamento do WM, além de 20 processos adicionais gerenciados em
wgpu/OpenGL AMD. Os três perfis wgpu também executam 16 milhões de triângulos
por quadro com overdraw. A identidade de API/GPU vem dos recibos dos backends;
os comandos, ambientes e hashes estão no JSON de resultados.

A leitura imediata e a leitura aos 50 ms permanecem registradas separadamente.
Se a marca ainda não chegou, o teste observa novamente a mesma janela, sem
renderizar, por até cinco segundos. A chegada tardia exige serial inalterado
e oráculo completo exato. `elapsed_ms` inclui as leituras e esperas do teste;
não mede latência de scanout nem duração GPU. Alpha não é observado pelo X11.

## Correções do diagnóstico e limites

A primeira versão da fixture selecionava alvo canônico e deixava o plano
nativo. A validação recusou corretamente as sete tentativas com
`Frame rasterization policy differs from the immutable target policy`.
A fixture foi corrigida para transmitir a política do alvo e a campanha
inteira foi repetida; os sete controles canônicos agora passam.

O estresse BGFX de 16 milhões de triângulos foi recusado antes da apresentação
com `BGFX geometry exceeds buffer size limits`. O lowering expandido excedeu
o limite uint32 de bytes dos buffers, protegido em `CoinBgfxBackend.cpp`.
Esses quatro resultados são limitações de capacidade/expansão, preservadas em
`initial-diagnostics`, e não são contados como passes B03. A campanha final
BGFX cobre um milhão de triângulos e o controle pequeno. Essa limitação fica
registrada em B10 para a análise de memória, reuso e particionamento.

O contrato de `getLastSubmissionSerial()` agora explica explicitamente que
seu avanço não é uma cerca de apresentação da janela. Captura RGBA solicitada,
quando suportada, representa a imagem renderizada, sem prometer scanout.
A superfície wgpu/OpenGL AMD continua sem COPY_SRC; o teste observa RGB X11.

## O que falta para encerrar B03

É necessário capturar a recorrência do cenário histórico de terreno com
uma imagem antiga após a espera de 50 ms e discriminar apresentação omitida,
apresentação tardia e eventos/lifecycle da janela. A geometria plana deste
novo teste não substitui o terreno iluminado histórico. As novas leituras de
submissão única e o controle negativo dão instrumentos para essa distinção;
mais repetições aprovadas não provam que a causa histórica foi corrigida.

[Resultado estruturado](validation/coin-render-b03-first-present-20261017/results.json),
[verificação](validation/coin-render-b03-first-present-20261017/verification.json),
[manifesto e fontes congelados](validation/coin-render-b03-first-present-20261017/manifest.json),
[teste](../testsuite/coinrender/CoinRenderFirstPresentTest.cpp),
[runner](../scripts/coinrender/run_first_present_matrix.py),
[histórico](coin-render-wgpu-opengl-first-present-20261010.md) e
[matriz](coin-render-feature-matrix.md).

Reprodução, com builds em RAM e GPUs executadas em série:

```sh
python3 scripts/coinrender/run_first_present_matrix.py \
  --wgpu-build /dev/shm/coinrender-b01-wgpu \
  --bgfx-build /dev/shm/coinrender-stage2-bgfx \
  --output-dir /tmp/b03-repeat
python3 scripts/coinrender/audit_first_present_matrix.py \
  docs/validation/coin-render-b03-first-present-20261017
```

O runner gera os resultados de uma nova campanha. O auditor usa o manifesto
e o snapshot dos fontes da campanha arquivada; a revisão posterior da branch
não é apresentada como se tivesse produzido recibos anteriores.
