# Evidência: rasterização, 2026-10-07

[Relatório e limites](../../coin-render-raster-study-20261007.md).
Campanha executada a partir de `codex/coin-render`, base `8b7e7372bf44a2981d1b9955e2bc43ca1db9d0eb`,
com o novo executável diagnóstico ainda não commitado durante as medições.
As fontes executadas são identificadas por SHA-256 e preservadas em `source/`.
Isso não atribui ao commit base código que ainda estava no working tree.

## Resultado da coleta

- **50 execuções diagnósticas físicas**, dez por perfil: wgpu AMD Vulkan,
  wgpu AMD OpenGL, wgpu NVIDIA Vulkan, BGFX AMD Vulkan e BGFX NVIDIA Vulkan.
  API/backend/vendor são conferidos nos logs, além do retorno do processo.
- Dez execuções BGFX OpenGL adicionais: API/backend confirmados, vendor/device
  **0, desconhecidos**. Não contam como qualificação física AMD. O prefixo
  `bgfx-amd-gl` registra a seleção solicitada, não a identidade observada.
- As imagens RGB de BGFX e wgpu Vulkan são byte a byte iguais nas vinte
  comparações entre backends (dez AMD e dez NVIDIA), inclusive nos controles
  de projeção/quantização. Ver `summary.json`.
- Quatro gates existentes de estilos curvos: **84/84** em wgpu Vulkan/OpenGL
  e BGFX Vulkan/OpenGL, sem skip nem tolerância modificada. No BGFX OpenGL,
  o alcance continua sendo API/backend; o vendor desconhecido não vira AMD.
- Câmera original AMD/Vulkan: falha esperada preservada no quadro 5, MAE
  0,147278/0,149582 no reúso/travessia completa, 47/49 pixels >3. Câmera
  original AMD/OpenGL e NVIDIA/Vulkan aprovadas. A esfera texturizada contra
  CoinGL AMD continua falhando no máximo RGB 9. Falhas não foram dispensadas.
- `negative-nvidia-wgpu-gl.log`: wgpu OpenGL indisponível na seleção NVIDIA
  usada; nenhum resultado físico foi inventado para esse perfil.
- `negative-bgfx-gl-vendor.log`: primeira coleta bloqueada pela verificação
  estrita de vendor; a coleta posterior foi explicitamente classificada como
  OpenGL sem identificação física, pelo parâmetro `--allow-unidentified-gl`.

Concluir um `CoinRenderRasterStudy` significa coletar dados válidos, **não**
aprovar um gate de paridade. É por isso que os testes originais têm logs
separados e preservam seus próprios retornos/critérios.

## Arquivos e integridade

Cada manifesto contém comandos completos, ambiente, hashes das fontes,
bibliotecas e executável. `runs/` conserva `run.log`, matrizes e dados brutos:

- `*.ppm.gz`: imagem RGB original, sem editar pixels; orientação top-down.
- `*.depth-f32.gz`: `side × side` float32 little-endian deste host, top-down;
  depth em [0,1]. Side 80 para formas e 256 para a câmera.
- `*.csv.gz`: diferenças por pixel, coordenadas projetadas e testemunhos por
  polígono/edge. RGB MAE é sobre toda a imagem branca, sem máscara de interior.
- `polygons.csv.edges.gz`: comum versus GL_LINES canônico/autoral/invertido;
  `polygons.csv.gz`: comum versus polígono nativo e LINE_LOOP autoral.
- Os hashes nos manifestos são dos bytes **descomprimidos**, conferidos após
  copiar/comprimir a evidência. `evidence-hashes.json` identifica a versão
  comprimida e os demais arquivos publicados.

`analysis.json` contém grades subpixel, winners e parâmetros numéricos dos caps.
`analysis-hashes.json` identifica figuras e análise. `driver-source-manifest.json`
contém URLs e SHA-256 da tag upstream Mesa 25.2.8 usada para interpretar os
resultados; não é um trace do registrador da GPU nem uma prova do binário
Ubuntu ter fonte idêntica sem patches.

`source/run-raster-study-wgpu-frozen.py` é o runner das primeiras três campanhas;
`source/run-raster-study-bgfx-frozen.py`, o da AMD BGFX Vulkan. O runner atual
adiciona identificação de API/GPU e a coleta explicitamente não qualificada
quando o BGFX OpenGL informa vendor zero. Todas as campanhas físicas usam a
mesma fonte `CoinRenderRasterStudy.cpp`, SHA-256
`8f8a354a9730c7fa13a82678caebecebfdb4bceefe089906306415dbf612ccb7`.

Os artefatos/builds completos também permanecem em
`/mnt/Laranja/Git/externos/coin-render-artifacts/raster-study-20261007/`.
Os diretórios preliminares `final/`, `polygons-*` e a primeira matriz na raiz
foram diagnósticos intermediários. As campanhas citadas neste relatório são
`qualified/` e `unidentified-bgfx-gl/`, com as fontes e hashes indicados aqui.

## Reproduzir

Compilar `CoinRenderRasterStudy` em um build Release com renderer legado GL,
CoinRender e testes habilitados. O alvo é manual, sem registro no CTest.
Não usar o backend Recording como se tivesse GPU física.

```sh
cmake --build /caminho/do/build --target CoinRenderRasterStudy
python3 testsuite/coinrender/run-raster-study.py \
  --build /caminho/do/build --output /caminho/permanente/nova-campanha \
  --adapter amd --api vulkan --backend wgpu
```

A seleção de adapter/API é adaptada a este Linux híbrido. O runner confirma os
IDs observados e recusa sobrescrever dados. Repetir com AMD/OpenGL,
NVIDIA/Vulkan e build BGFX separado; o runner precisa de acesso ao display/GPU.
A análise de uma campanha com os três perfis wgpu usa numpy/matplotlib:

```sh
python3 testsuite/coinrender/analyze-raster-study.py /caminho/permanente/nova-campanha
```

Para reanalisar a evidência versionada, expandir os `.gz` em outro diretório
permanente preservando os nomes/estrutura `runs/`. Usar esse diretório como
campanha da análise. Ela não lê os comprimidos diretamente.
