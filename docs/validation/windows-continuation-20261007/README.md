# Evidência da continuação Windows, 2026-10-07

Fonte de código/testes: `1a971511493b8e9fa30ec1f6dcd35766ceeaf416`, a partir
de `0d7ba61b7cc78318cb3ec7fdaa1efabe01cdeb23`. Ambiente: Windows 10 19045,
MSVC 19.44, GTX 1060 6 GB/NVIDIA 581.08. Escopos e limites estão no
[relatório](../../coin-render-windows-continuation-validation-20261007.md).

| Registro | Conteúdo |
| --- | --- |
| `baseline-results.json`, `baseline-*.xml/log`, `*-last-test.log` | Suítes integrais mistas e saída completa dos casos |
| `cross-results.json`, `cross-*.xml/log`, `*-selected.txt`, `*-inventory.json` | Repetições Vulkan/OpenGL, listas e overrides de API |
| `initial-*`, `*-build*.log`, `padding-regression-before.log`, XMLs focados | Falhas iniciais e verificações das correções |
| `smoke-results.json`, `*-win32-*.log` | Cinco smokes com pixels; wgpu/OpenGL apresenta sem captura e verifica rejeição/recuperação |
| `sdk-results.json`, `*-sdk-*.log/json/txt`, `sdk-consumer/` | Instalação isolada, hashes, export privado e consumidor dos headers públicos |
| `binary-manifest.json`, `evidence-sha256.json`, versões/ambiente | Revisões de dependências e hashes dos artifacts |
| `performance/verify/` | 78 processos, 546 hashes de imagens, 36 comparações/252 quadros idênticos e auditoria de pixels |
| `performance/representative-images/` | Quatro PPMs originais, sem conversão ou edição |
| `performance/measure/`, `performance/measure-geometry-10-recheck/` | 247 tentativas; 13 sobrepostas à auditoria CPU foram excluídas/substituídas |
| `performance/selected-measurement-results.json`, `performance/excluded-cpu-audit-overlap.json` | Seleção das 234 execuções qualificadas e registros excluídos |
| `performance/summary.json`, [tabela](performance/summary.md) | Mediana/p95/p99, primeiro quadro e comparações literal/reserva |

A suíte integral wgpu registra 204/205, com falha da fixture MultiDevice.
O XML focado posterior registra os seis testes relacionados aprovados; as
repetições Vulkan/OpenGL também incluem a fixture corrigida. Não interpretar
esse histórico como uma segunda execução integral dx12 de 205 casos.

O smoke wgpu/OpenGL com captura falhou por COPY_SRC indisponível; a tentativa
foi preservada em `initial-wgpu-win32-opengl-copy-src-unsupported.log`.
O modo posterior de apresentação sem captura passou e não conta como
equivalência visual da janela. O offscreen e o consumidor do SDK passaram.

JUnit trunca stdout de alguns casos aprovados; consultar também o `LastTest.log`
da rodada. Nenhum build, DLL, PDB ou conjunto completo de 546 imagens foi
adicionado ao Git. Esses arquivos permanecem em
`H:/Git/coin/build/windows-render-20261007` nesta máquina.

Os scripts são registros adaptados a esta máquina. Para reproduzir, ajustar
source/build/prefixes e usar um diretório novo de artifacts. Não sobrescrever
os registros arquivados; rever o inventário CTest antes de selecionar uma API.

O `.gitattributes` desta pasta desativa a conversão de fim de linha dos
artifacts, preservando seus bytes e checksums nas diferentes plataformas.
