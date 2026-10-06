# Nós retidos Linux — qualificação de 2026-10-06

Continuação da base Coin `41fcfcaeea` na branch `codex/coin-render`.
[Perfil, limites e patch FreeCAD](../../coin-render-retained-linux-profile.md).
O protocolo permanece em 49; não há mudança de ABI C. As fontes da entrega,
o inventário FreeCAD e os binários finais têm hashes em `summary.json`.

## Resultados

- 12 células completas dos oito tipos reais no viewport FreeCAD:
  BGFX OpenGL/Vulkan e wgpu Vulkan × object/weighted OIT × DPR 1/2.
  Glifos, mutação, remoção, câmera/resize, picking e idle passaram.
- Três ColorBar frios, um por executor: três ranges/precisões comparados com
  CoinGL, com glifos presentes. MAE RGB e contagens estão nos resultados.
- Três gates finais de rejeição/recuperação, com cinco entradas cada:
  MeshGrid fora de orçamento, Polygon fora da faixa, polos sem coordenadas,
  produto 65.536 × 65.536 e eixo zero desemparelhado. Mesma target, RGBA
  anterior idêntica após cada rejeição e recuperação após corrigir os campos.
- Três células focadas, DPR 2/object, revalidam control points e Polygon após
  a inclusão dos limites finais. Não contam como células completas de oito nós.
- Seis células do consumidor real Bezier `Part::Spline`: DPR 1/2 nas três
  rotas, ControlPoints liga/desliga, Shape regenerada, nós reais presentes,
  pixels alterados e zero resíduo após desligar. OpenGL deste consumidor usa
  **COIN_BGFX_DISABLE_PROGRAM_CACHE=1**; as outras rotas usam o padrão.
- Duas células do perfil anterior de raster encerram BGFX OpenGL DPR 2,
  object/weighted OIT, com prova da GPU no contexto EGL atual.
- Referências CoinGL são registradas separadamente com REFERENCE_PASS.
  O perfil de viewport mede conteúdo/lifecycle, sem comparação pixel a pixel
  de todos os oito tipos. ColorBar tem comparação RGB própria.
- Gates CPU: NodeInventory nos três builds, 80 testes Python, dois CTests
  Qt específicos (ResultGate e GeneratedImage) e inventário atualizado.

A matriz completa antecedeu os guardas finais de orçamento; estes não alteram
geometria válida e foram revalidados pelos três controles nativos focados,
15 rejeições/recuperações e pelo consumidor Part::Spline. Os três gates de
rejeição anteriores, com três casos, são preservados como baseline e excluídos
do total oficial. Os grupos e totais finais constam em `summary.json`.

## Ambiente e comandos

GPU jobs serializados, Weston headless GL/Xwayland privado, FreeCAD experimental,
Qt/PySide6, PartGui/MeshGui e CoinRender correspondentes via LD_LIBRARY_PATH.
A janela/oráculo GLX usa NVIDIA; BGFX EGL usa AMD Radeon, comprovado por
`bgfx_gl_adapter` no render worker. Vulkan usa RADV/AMD. `environment.json` e
os arquivos de provenance registram o binding e as variáveis pertinentes.

Comando de viewport:

```sh
COIN_TEST_RETAINED_NODES=1 python3 testsuite/qt-quarter/run_isolated.py \
  --server xwayland --weston-prefix /tmp/coin-p16-weston \
  --harness /tmp/coin-front3-qt/QtQuarterRegression \
  --freecad /tmp/freecad-p16-build/bin/FreeCAD --artifacts /tmp/resultado \
  --backend bgfx --renderer opengl --case freecad-screen-content \
  --require-hardware --timeout 180
```

Usar o ambiente registrado: GLX offload NVIDIA, EGL vendor Mesa e ICD Vulkan
Radeon. Trocar backend/renderer e a biblioteca Coin correspondente para Vulkan
BGFX/wgpu. `--case retained-rejection` e `--case colorbar` executam os gates
C++; o harness depende dos módulos PartGui/MeshGui. Para o consumidor,
`COIN_TEST_SPLINE_CONSUMER=1 --mode object`, com cache desativado em OpenGL.
Referência: `--reference-gl --renderer opengl --mode object`, sem require-hardware.

O runner Linux agora ativa e exige a sonda do contexto BGFX em OpenGL.
Renderer ausente/desconhecido/software não fornece prova física; a GPU GLX da
janela e residência em nvidia-smi não bastam. Nenhum fallback CoinGL conta como
passe de backend. Hashes de capturas ficam em `capture-hashes.json`; as imagens
originais permanecem nos caminhos `/tmp/coin-linux-final-matrix` registrados.
Logs de execução guardam resultados e as linhas essenciais de prova GPU.

## Diagnósticos e estudos

- O teste inicial exigia que o raio central cruzasse o dragger mesmo após
  câmera/âncora modificadas. A fixture final usa pixels visíveis das hastes e
  exige um caminho lógico contendo o dragger; não certifica arraste por input.
- Part::Spline/OpenGL DPR 1 com cache padrão teve **dois timeouts** durante
  submit, antes da primeira captura. Os logs não instrumentavam o setup;
  não determinam se a criação do consumidor já havia ocorrido. O controle
  sem cache passou, seguido da matriz do consumidor com essa configuração
  explícita. A causa e a relação cache/lifecycle permanecem estudo.
- O CTest geral QtQuarterMatrix acionado fora da sessão privada não conseguiu
  abrir DISPLAY=:0: agregado falhou, com filhos FAIL/SKIP/UNSUPPORTED. Esse
  comando não qualifica GPU. Os CTests CPU específicos passaram em seguida.
- As combinações GLX Mesa/EGL Mesa com timeout anterior e EGL NVIDIA com falha
  de surface pertencem à campanha anterior. O binding aprovado é explícito;
  não certifica todos os drivers/provedores.

A auditoria do primeiro caminho primitivo Polygon identificou um mecanismo
compatível com a omissão: SoCallbackAction enumera subclasses no registro de
callbacks, e MeshGui é importado depois da primeira submissão da action do
viewport. Uma nova action CPU emitia quatro segmentos. O estudo passa a focar
registro tardio, callbacks do usuário e invalidação de caches; a captura retida
publicada usa tipos padrão existentes desde SoDB::init.

Windows, arraste por eventos, consumidores completos de workbenches, bbox com
geometric depth clamp e registro tardio de subclasses ficam nas
listas de estudo/qualificação. Formatos/mips RTT adicionais, produtores de
janela e executores shader/volume/cubo/MSAA seguem suas frentes próprias.
