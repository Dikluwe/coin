# Nós e recursos — qualificação Linux, 2026-10-06

Continuação da base `51920a422d` em `codex/coin-render`.
[Contrato e limites](../../coin-render-linux-nodes-resources-closure.md).
A campanha final e os diagnósticos estão separados em `summary.json` e
`diagnostics/`. As evidências anteriores não foram substituídas.

## Controles nativos

A campanha cobre quatro grupos: RTT direto de janela, Polygon pelo caminho
primitivo, Part::Spline BSpline curva 5×1 e BSpline superfície 5×4.
Cada grupo executa BGFX Vulkan/OpenGL e wgpu Vulkan, object/weighted OIT e DPR
1/2: **48 células PASS, zero FAIL/SKIP/UNSUPPORTED**. Três células focadas
repetem as cinco entradas inválidas Part/Mesh e sua recuperação: 15 controles
e mais três passes nativos. Os resultados por célula registram presença/mutação, regeneração, remoção,
resize quando aplicável e idle. São testes de lifecycle/conteúdo do consumidor;
não certificam todos os workbenches ou arraste por eventos.

RTT compara a captura da janela antes/depois da rejeição de RGBA16F e verifica
recuperação sem fallback. A rodada usa GPU física AMD; GLX Qt/oráculo é NVIDIA,
EGL BGFX é Mesa/AMD. A string do contexto atual BGFX está em `execution.log`.
O cache de programas BGFX está **desativado** em toda esta nova campanha. O
estudo anterior do consumidor OpenGL com cache ativo continua aberto.

`provenance.json` registra o ambiente do runner antes dos overrides por célula.
No caso `rtt-window`, o runner força `COIN_RENDER_RTT_GPU_DIRECT=1`; o harness
exige essa variável e registra `direct_rtt_window=true`. Os modos/DPR e a prova
física efetiva constam em cada `result.json`.

## Oráculo offscreen e regressão

RTT mipmaps staged usa FBO e pbuffer, nas três rotas, com sete transições de
qualidade: 0,3/0,5/0,51/0,7/0,85/0,5/0,7. Quatro controles minificados por
execução verificam a cor independente 128/0/128 e RGB MAE ≤ 4 frente ao CoinGL.
As seis execuções passaram: **42 transições**, 24 comparações RGB minificadas;
MAE máximo 0,666667 em FBO e 0 em pbuffer. Todos os controles recusam NPOT, preservam publicação e recuperam; qualidade
zero/reativação também executam. Os logs registram a MAE realmente observada.
O ambiente e a prova física desses processos estão junto dos logs.

O oráculo offscreen usa a configuração desktop NVIDIA já qualificada.
A tentativa no Xwayland isolado não criou o offscreen CoinGL: fica em
`diagnostics/isolated-coingl-mips.log`, sem contar como passe ou aumentar tolerância.

As regressões passaram em 693 cenas de texturas/qualidade P07 (mais 18
rejeições), 1.728 controles RTT staged/direto e 279 cenas texto/imagem GPU
nas três rotas. A configuração explícita do teste RTT foi corrigida para ler
a API do Shell; as primeiras execuções identificadas como OpenGL tinham usado
Vulkan. Foram preservadas como diagnóstico e substituídas na qualificação por
execuções com prova real OpenGL/NVIDIA. O teste nativo wgpu de surface verifica serial
por alvo, avanço, rejeição sem alteração e reset no resize. Os gates CPU cobrem
registro tardio, ownership, orçamento/mips, publicação e filtros em três builds;
17 gates CPU passaram (6 BGFX, 6 wgpu, 5 Recording), além de 89 testes Python
e dois gates Qt focados. Estes validam o runner e imagens geradas pelo host.

## Reprodução

Os scripts `coin-front4-*.py` preservam comandos e configurações deste PC.
`coin-front4-final-matrix.py` aceita `window`, `polygon`, `bspline-curve` e
`bspline-surface`; aponta para o harness FreeCAD compilado e cria uma sessão
privada Xwayland. `coin-front4-mips-desktop.py` aceita backend, renderer e
`fbo`/`pbuffer`. `coin-front4-regression.py` executa as regressões sequencialmente;
`coin-front4-native-serial-probe.py` isola o controle nativo da surface wgpu.
Executar a partir da raiz Coin com builds e dependências nos caminhos indicados.
Não executar builds ou testes GPU concorrentes com essas campanhas.

Atualizar Coin/CoinRender e a ponte Rust juntos. Aplicar o
[patch incremental FreeCAD](../../../examples/coinrender/freecad_linux_rtt_recovery.patch)
após o patch Linux retido anterior, então recompilar FreeCADGui/MeshGui.
`summary.json` registra hashes dos fontes e binários; `capture-hashes.json`
identifica todas as imagens da campanha, e alguns controles representativos
estão arquivados. Cookies Xauthority e credenciais da sessão não fazem parte da
evidência. O protocolo conserva os layouts/versão 49 e acrescenta uma consulta
privada de serial, exportada também para Windows.

Windows, outros dispositivos, workbenches/input completos, depth clamp bbox,
mips GPU diretos, formatos HDR/depth/sRGB, shaders, volume/cubo e MSAA/multipass
continuam com os itens explícitos da checklist. Esses recursos ainda precisam
de implementação; não ficam fechados por esta campanha ou por definição de contrato.
