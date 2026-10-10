import pathlib,json,hashlib,xml.etree.ElementTree as ET
r=pathlib.Path(__file__).resolve().parent;s=pathlib.Path(r'C:\Users\Diklu\.codex\worktrees\sampling-win-20261010\coin')
rel='validation/sampling-api-windows-20261010/'
def load(name):return json.loads((r/name).read_text(encoding='utf-8'))
gpu=load('npot/gpu-summary.json');timing=load('latency/timing-summary.json')
suite=[]
for backend in ['bgfx','wgpu']:
 p=r/'suite'/(backend+'-d3d12.xml')
 if backend=='wgpu' and (r/'suite-effective-wgpu.xml').exists():p=r/'suite-effective-wgpu.xml'
 cases=ET.parse(p).findall('.//testcase')
 suite.append((backend,len(cases),[x.attrib['name'] for x in cases if x.find('failure') is not None],[x.attrib['name'] for x in cases if x.find('skipped') is not None]))
text='''# Qualificação Windows da API de sampling

Coin/CoinRender foram recompilados e instalados em dois prefixes isolados a
partir de `codex/coin-portable-sampling-study`, revisão
`b86404cafc9ec13e91d0bac67f2e875f2b206a07`, com as correções desta entrega.
Native continua padrão. Esta campanha fecha o consumidor instalado e os gates
funcionais medidos na GTX 1060; não publica automaticamente um perfil geral
Windows, nem promove a API para a antiga branch Coin render ou master.

## Ambiente e instalação

Windows 10 Pro 19045 x64; i5-4670K; MSVC 19.44/VS 2022 e SDK Windows
10.0.26100.0; CMake 3.31.6; Rust/cargo 1.99.0. GPU exercitada: NVIDIA GeForce
GTX 1060 6GB, PCI 10de:1c03, driver 32.0.15.8108 / NVIDIA 581.08. A Intel
HD 4600, driver 10.18.15.4279, está habilitada, mas não foi selecionada por
estes processos; sua presença não é uma qualificação Intel. Vulkaninfo
enumerou somente a GTX 1060. Os dois monitores têm DPI efetivo 96. Os [30 recibos de API/GPU](validation/sampling-api-windows-20261010/physical-api-receipts.json) de SDK, sampling, janela e NPOT confirmam as APIs solicitadas na NVIDIA, sem fallback contado.

Checkout: `C:/Users/Diklu/.codex/worktrees/sampling-win-20261010/coin`.
Builds, prefixes e binários completos:
`H:/Git/coin/build/sampling-win-20261010/{build-bgfx,build-wgpu,install-bgfx,install-wgpu}`.
O checkout principal e suas alterações anteriores foram preservados. Git
`core.longpaths=true` resolveu a falha inicial de checkout por nome longo.

Builds Release, x64, `COIN_BUILD_RENDER`, renderer legado, testes, exemplos
Win32, benchmarks e instalação experimental ligados; MSBuild parallel 2,
C++ /MP2 e Cargo jobs 2. Compilação em disco, sem RAM drive disponível.
BGFX depende do SDK fixado em `H:/Git/coin/build/bgfx-windows-install`, com
bgfx 7346c3e731bd65f35c7e6a99819840e554f5b748, bx
1c986bd1e9a176a08ae885a6cdcefe76c3f700fc e bimg
6b08e87de28e7aa54782d5ce1b279dca373a10c6. A ponte wgpu 24 foi recompilada
com Cargo --locked. Os comandos e flags completos estão nos ledgers de build.

## SDK e sampling

O cliente CMake independente importa somente Coin, CoinRender e OpenGL do
sistema, sem incluir um prefix BGFX no cliente. Passaram 12/12 processos:
native e portable × D3D12/Vulkan/OpenGL × BGFX/wgpu, mais 2/2 sondas C11.
O cubo 32×32 compara RGB com CoinGL, limite máximo 3; nesta GPU, máximo 0.
Portable também exige o oracle de mip 40/160 nas colunas 30/31 do alvo 64×64,
limite máximo 1; máximo observado 0. Foram preservados PPMs e os caminhos
efetivamente carregados de Coin4.dll e CoinRender4.dll.

Capabilities v4 mediu 584 bytes, prefixo v3 536 e ordinal native zero.
Consulta estrita Windows retorna UNQUALIFIED_PROFILE; a disponibilidade
explícita sem exigir perfil publicado retorna SUPPORTED. Essa distinção é
testada e permanece no runtime. As consultas v1/v2/v3 não alteram bytes após
seu prefixo. O protocolo privado Rust permanece 51 e o prefixo uniform native
3040 bytes; portable usa 3168.

A matriz funcional reúne 102/102 processos aprovados: 78 passes iniciais e
24 recuperações dos fixtures de upload. Inclui políticas simultâneas, unidades
0/7, shader/cache native independente, resize 64→128, textura desligada/ligada,
async, formatos/mips/RTT/deep/viewport/procedural, recusa de sampler anisotrópico
incompatível sem alterar pixels/serial/ponteiro emprestado, e recuperação.
COIN_SAMPLING_STUDY=fetch conflita intencionalmente com a opção pública e não
seleciona o shader. Limites RGB existentes, incluindo MAE ≤1,5 e máximo ≤4 nos
gates de comparação, foram preservados. Rust/Naga: 47/47 testes.

## Falhas iniciais e correções

- HLSL BGFX recusou `ivec2(1)` no sampler portable. `ivec2(1, 1)` mantém a
  operação e compila os shaders DXBC, SPIR-V e GLSL.
- CoinRender não linkava quatro helpers privados de Coin: get de escala e
  qualidade, simage_wrapper e GLUWrapper. Foram exportados somente esses
  pontos de ligação, sem instalar os headers privados.
- O oracle GLU definia COIN_NOT_DLL junto da definição importada COIN_DLL;
  passou a herdar a ligação do target Coin. GL_SAMPLES ganhou o fallback
  padrão usado pelo fixture de raster Windows.
- O shader native wgpu permanecia com uniform 3168 enquanto o layout exigia
  3040: include_str preservava CRLF e a remoção do apêndice portable buscava
  LF. A especialização normaliza CRLF/LF. O teste Rust inicial reproduziu a
  falha; a regressão cobre ambas as terminações, standard, quatro/oito sombras
  e instancing, sem retirar perfis.
- Os testes advanced/procedural esperavam nova qualidade após mudar somente
  a dica. A revisão retém a qualidade do upload até notificação. Os fixtures
  agora verificam a retenção e notificam reupload de bytes idênticos antes
  de exigir BC3/mips e anisotropia 16. Recusa em 1,01 e recuperação continuam.
- O gate FFI ainda exigia protocolo 50. Foi ajustado ao contrato 51 já usado
  por C++/Rust, preservando stride 164 e estado 2292, e verificando frame 456
  e política native zero. O gate completo passou em D3D12/Vulkan/OpenGL.
- Um cliente Vulkan retornou 0 sem recibo impresso. O resultado não foi
  aprovado; saída imediata e exigência de recibo resolveram a observabilidade.
  Logs sem recibo, skips e falhas não são aprovação por código de saída.

## NPOT direto e oito sombras

BGFX passou 6/6 combinações: D3D12/Vulkan/OpenGL × peeling/weighted OIT,
oito sombras, produtor NPOT 63×47 com mips, imagens distintas ao remover
sombra ou transparência, nona sombra recusada sem nova publicação e
recuperação. O gate advanced, executado em cada API/política, confirma área
de mips para 3×5, 1×5, 5×1, 5×7, 129×127 e 2047×2047, HDR acima de um,
falha de alocação e recuperação do mesmo token anteriormente publicado.
Rollback do token é um gate direto separado; a combinação oito sombras/OIT
comprova pixels/serial e recuperação.

A API Windows exercitada é D3D12, renderer ordinal 4; não D3D11.
O arquivo de shader chamado dx11 é DXBC consumido pelo backend D3D12.
Compilação desse arquivo não foi contada como execução D3D11.

O benchmark usa produtor 63×63, cinco frames de blit/redução por cadeia,
44 registros por processo (4 funcionais + 40 de benchmark), exclui dez
aquecimentos e exige os cinco níveis completos nos 30 medidos. Tempos abaixo
são GPU; não incluem render da cena/sombras/OIT, readback ou custo CPU.

| API | OIT | Área mediana/p95 ms | Frames mediana/p95 ms | Gate custo |
| --- | --- | --- | --- | --- |
'''
for row in gpu:
 api=row['label'].split('-')[1];mechanism='peeling' if row['label'].endswith('-1') else 'weighted'
 area=f"{row['area_ms_median']:.6f}/{row['area_ms_p95']:.6f}" if row['gpu_cost_gate'] else 'indisponível'
 frame=f"{row['frame_ms_median']:.6f}/{row['frame_ms_p95']:.6f}" if row['gpu_cost_gate'] else 'indisponível'
 text+=f'| {api} | {mechanism} | {area} | {frame} | {"PASS" if row["gpu_cost_gate"] else "INCOMPLETO"} |\n'
text+='''
D3D12 perdeu timestamps em 5/30 cadeias peeling e 2/30 weighted na coleta
original. Não foram substituídos por zero nem excluídos para aprovar o gate.
As tentativas de drenagem por nível/espera também ficaram incompletas e
foram retiradas do código final; seus comandos, fontes e logs foram retidos.
A associação continua por número do frame efetivo, não pelo nome reutilizado
do view. Os clocks/P-state/temperatura foram registrados com nvidia-smi;
não estavam fixados e não se atribui uma diferença causal entre APIs.

## Win32 e condições retidas

Passaram 6/6 smokes de janela e 6/6 gates de sampling de janela: dois HWNDs
simultâneos com políticas independentes, resize, suspensão/remap, manager e
adapter, novas superfícies e ticket offscreen sobrevivente. wgpu/OpenGL
continua sem COPY_SRC; seu smoke de apresentação não é equivalência visual.
A equivalência visual do fixture sampling usa captura real GDI dos pixels
apresentados, com o mesmo oracle 40/160 e limite 1, em native/portable/resize/
remap/manager. PPMs preservam os resultados e não simulam readback GPU.

Latência de janela mede somente retorno CPU render/present, com possível
backpressure; nenhum readback ocorre no trecho medido. ABBA, 60 warmup +
600 frames por processo, 24 processos/14400 frames medidos, com clocks
NVIDIA. É um fixture 64×64 de uma textura; não encerra a campanha maior de
workloads/arrastes/milhão, nem mede latência de display ou GPU concluído.

| Backend/API | Ordem | Mediana ms | p95 ms | Gate |
| --- | --- | --- | --- | --- |
'''
for row in timing:text+=f"| {row['label']} | ABBA | {row.get('median_ms',0):.6f} | {row.get('p95_ms',0):.6f} | {'PASS' if row['pass_gate'] else 'FAIL'} |\n"
text+='''
DPI físico diferente não foi qualificado: ambos os monitores retornam 96;
nenhum evento sintético foi contado como WM_DPICHANGED real. Perda real de
device/driver permanece pendente; testes de fault injection são controles
lógicos e não certificam TDR/remoção física. Esta sessão não executou um reset
do adaptador do desktop. A documentação de
[ID3D12Device5::RemoveDevice](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device5-removedevice)
lista Windows build 20348 como mínimo para esse método; o host é 19045.
Nenhum passe de perda real é atribuído a essa API nesta campanha.

O [inventário FreeCAD](validation/sampling-api-windows-20261010/freecad-inventory.json) confirma a instalação 1.0, Coin4.dll legado e ausência de CoinRender4.dll integrado.
Part::Spline, Mesh/FEM, ColorBar, arraste por eventos e RTT do host permanecem
SKIP por falta do consumidor integrado; não houve substituição das DLLs do
FreeCAD instalado. Gates C++ do adapter não são passes FreeCAD.

## Regressão e evidência

'''
if (r/'suite-effective-wgpu.xml').exists():
 text+='A rodada wgpu inicial atingiu o orçamento global do runner (1800 segundos, exit 124) após 144 casos concluídos, ao iniciar o 145. A retomada executou os casos 145–212, com os limites CTest individuais originais. O orçamento global da retomada foi 7200 segundos; os logs e o XML do runner original foram preservados, junto do LastTest parcial. A contagem wgpu abaixo reúne os recibos originais e da retomada, sem contar a interrupção como passe.\n\n'
if (r/'ffi-recovery/summary.json').exists():
 text+='A retomada revelou uma falha FFI por expectativa obsoleta de protocolo 50. A correção do gate passou nas três APIs; a contagem efetiva inclui essa recuperação, enquanto a falha original permanece no XML da retomada e em ffi-recovery/initial-effective-wgpu.xml.\n\n'
camera=load('camera/summary.json')
text+=f'Complemento de câmera com referência obrigatória: {sum(x["pass_gate"] for x in camera)}/{len(camera)} processos nas três APIs de cada backend. O skip por variável ausente do CTest original permanece registrado abaixo.\n\n'
for backend,total,fail,skip in suite:text+=f'- {backend}: {total} casos CTest, {len(fail)} falhas, {len(skip)} skips. Falhas: {fail or "nenhuma"}; skips: {skip or "nenhum"}.\n'
text+=f'''
Hashes finais dos SDKs instalados, idênticos aos binários de build:

| Backend | DLL | SHA256 |
| --- | --- | --- |
'''
for backend in ['bgfx','wgpu']:
 for dll,values in load('sdk-final-drain/'+backend+'-dll-hashes.json').items():
  text+=f'| {backend} | {dll} | `{values["installed"]}` |\n'
text+=f'''
Os [ledgers e manifest SHA256]({rel}manifest.json) guardam comandos,
ambiente seletivo, hashes dos executáveis/DLLs/logs, XML, full LastTest.log,
PPMs, GPU/API, clocks e fontes de cada tentativa. DLLs completas e binários
continuam no root local; não foram adicionados ao repositório. A revisão base
e [diff do código testado]({rel}tested-source.diff) identificam as alterações.
Não misturar resultados de prefixes antigos ou campanhas de clock distintas.
'''
(s/'docs/coin-render-sampling-api-windows-20261010.md').write_text(text,encoding='utf-8',newline='\n')
print('report written')
