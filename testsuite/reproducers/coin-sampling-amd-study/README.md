# Coin/OpenGL: isolamento de sampling projetivo AMD

Este reproducer compila fora do projeto principal. `coin-native-sampling` liga
somente Coin e OpenGL/EGL; `pure-egl-sampling` não inclui nem liga Coin.
`glx-visual-probe` consulta as ofertas GLX sem Coin. Nenhum alvo depende de
CoinRender, BGFX, wgpu ou seus formatos intermediários.

Build validado: Coin `master` em `01b360af32`, Linux, OpenGL 4.6 compatibility,
AMD Renoir/Mesa 25.2.8 e NVIDIA RTX 3060 Laptop/610.57.04. Paths reais e hashes
estão no relatório e manifest versionados. Exemplo portátil, a partir do checkout:

```sh
cmake -S . -B /path/to/build-coin -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCOIN_BUILD_TESTS=OFF -DCOIN_BUILD_DOCUMENTATION=OFF
cmake --build /path/to/build-coin --target Coin -j 6
cmake -S testsuite/reproducers/coin-sampling-amd-study \
  -B /path/to/build-probe -G Ninja \
  -DCOIN_SOURCE_DIR=/path/to/coin -DCOIN_BINARY_DIR=/path/to/build-coin
cmake --build /path/to/build-probe -j 6
python3 testsuite/reproducers/coin-sampling-amd-study/run.py \
  --build /path/to/build-probe --output /path/to/permanent-results
python3 testsuite/reproducers/coin-sampling-amd-study/analyze.py /path/to/permanent-results
```

O runner seleciona dois vendors desta máquina por GLVND e verifica a identidade
reportada em cada processo. Ele precisa do display/Xauthority do usuário. Não
considerar exit 77 uma execução física; o runner exige exit 0 em todos os casos.
Na NVIDIA, usa `SoGLRenderAction` em contexto EGL atual: a master falha antes do
draw no bootstrap GLX do `SoOffscreenRenderer` (visuais single-buffer ausentes).
O caminho GLX AMD e o EGL NVIDIA ficam identificados, sem chamar isso de PASS
para o bootstrap GLX NVIDIA.

São duas qualidades (0,5/0,8) e três offsets (zero/-0,001/+0,001). Cada processo
conserva RGB top-down 64×64, runtime, filtros, matriz e leitura dos oito mipmaps:

- `coin`: scene graph autoral; imagem, UV e matriz projetiva do Coin.
- `fixed`: upload/mips e coordenadas S/T/Q por OpenGL direto.
- `implicit`: shader OpenGL com `texture`, sem captura Coin.
- `explicit-lod`: LOD por maior norma das derivadas fine, sampler nativo.
- `fetch`: LOD calculado e seleção/bilinear por `texelFetch`.
- `fetch-round256`: diagnóstico de round 1/256 texel antes do fetch nearest.
- `query-lod`: consulta nativa; LOD codificado em dois canais a 1/4096.
- `boundary-scan`: mip 3 fixo e coordenadas binárias exatas por fragmento.
- `boundary-fetch`: mesma varredura, seleção por floor/fetch. As duas varreduras
  só caracterizam nearest na qualidade 0,5; qualidade 0,8 é controle linear.

`analyze.py` contém um oráculo de fórmula autoral independente, sem ler o IR ou
código do sampler CoinRender. Comparações centrais usam os mesmos 100 pixels e
MAE≤1,5/máximo≤4 do gate anterior. Varreduras verificam todos os 4.096 pixels,
sem tolerância. Os modelos de precisão servem para caracterizar esta máquina;
não são um contrato global de produção nem um teste de conformidade OpenGL.
