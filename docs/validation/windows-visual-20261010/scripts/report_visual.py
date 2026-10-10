import pathlib,json
r=pathlib.Path(__file__).parent
repo=pathlib.Path(r'C:\Users\Diklu\.codex\worktrees\sampling-win-20261010\coin')
cohorts=['visual-initial','visual-million','visual-static-control','visual-static-matched']
rows={c:json.loads((r/c/'summary.json').read_text()) for c in cohorts}
analyses={c:json.loads((r/c/'pixel-analysis.json').read_text()) for c in cohorts}
n=sum(len(v) for v in rows.values());passed=sum(x['pass_gate'] for v in rows.values() for x in v)
lines=['# Cenas visuais de CoinRender no Windows', '',
f'Campanha pública em 1280×960: {n} processos, {passed} passes e {n-passed} falhas no gate estrito janela/offscreen. As cenas mostram edifícios, relevo, iluminação, textura e transparência. As falhas permanecem abertas; nenhuma tolerância foi ampliada.', '',
'Coin/CoinRender corresponde ao código publicado em `6caeb62d5be534dbc7b428c58c13c529a55e36d9`, na mesma branch `codex/coin-portable-sampling-study`. Foram usados os prefixes isolados BGFX e wgpu da campanha anterior, com o SDK BGFX corrigido ali e os consumidores públicos recompilados nesta campanha. Os caminhos e SHA256 das DLLs efetivamente carregadas constam dos logs e de `provenance.json`. Windows 10 Pro 19045, NVIDIA GTX 1060 6GB, driver 581.08 / 32.0.15.8108; D3D12, Vulkan e OpenGL físicos, sem fallback contado como passe. Intel não foi exercitada.', '',
'## Cenas e comparação', '',
'- Cidade de 40.000 edifícios: 200×200 cubos, 480.000 triângulos, oito cores, alturas variadas e duas luzes direcionais.',
'- Cidade de 1.000.000 de instâncias: 1000×1000 colocações de oito shapes compartilhados, 12.000.000 de triângulos geométricos antes de oclusão. São instâncias lógicas no grafo, não um milhão de shapes únicos nem um milhão de objetos FreeCAD. A captura da cidade inteira deixa muitos edifícios menores que um pixel; o controle de 40.000 mostra melhor o volume.',
'- Terreno iluminado: 501.501 vértices, 500.000 quads triangulados em 1.000.000 de triângulos, elevação sinusoidal e vinte faixas de materiais. Não equivale à cidade de um milhão de instâncias.',
'- Sólidos: 24 esferas/cones/cilindros/cubos, seis objetos com textura procedural 63×47 e seis com transparência 0,35. A transparência usa o comportamento padrão; não qualifica OIT ou oito sombras.', '',
'Cada processo usa uma política explícita `native` ou `portable`, um warmup e dois frames com submissões crescentes. A câmera ortográfica inclinada gira de 0,65 a 1 radiano; o controle separado mantém a câmera em 0,65. Nenhum readback ocorre dentro desse pequeno trecho. Os CSVs são diagnósticos de retorno CPU; a amostra curta e a pressão de memória das cidades não qualificam desempenho ou latência de display.', '',
'As capturas inicial/final ocorrem fora do trecho medido. Cada imagem da janela é comparada a um alvo offscreen novo, com nova action, a mesma cena, API e política: RGB máximo exigido 0 e cobertura maior que 10%. wgpu/OpenGL usa GDI externo; os demais usam readback público solicitado antes da submissão. A comparação verifica consistência entre superfícies; não é uma referência CoinGL independente nem uma prova completa da semântica de iluminação/sampling. Os PNGs preservam exatamente os RGB dos PPMs; as máscaras em branco mostram pixels divergentes, sem alterar os renders.', '',
'## Resultados', '', '| Campanha | Processos | Passes | Falhas |', '|---|---:|---:|---:|']
for c,v in rows.items():lines.append(f'| {c} | {len(v)} | {sum(x["pass_gate"] for x in v)} | {sum(not x["pass_gate"] for x in v)} |')
lines += ['', '| Cena e campanha | Backend/API | Passes de 2 políticas | Inicial max/pixels | Final max/pixels |', '|---|---|---:|---|---|']
for c,arr in analyses.items():
 for a in arr:
  if not a['label'].endswith('-0-native'):continue
  label=a['label'];other=label.replace('-0-native','-1-portable');p=sum(x['pass_gate'] for x in rows[c] if x['label'] in [label,other]);parts=label.rsplit('-',4);work='-'.join(parts[:-4]) if len(parts)>4 else parts[0]
  def fmt(stage):
   d=a['stages'].get(stage);return f'{d["rgb_max"]}/{d["different_pixels"]}' if d else 'não capturado'
  lines.append(f'| {label.removesuffix("-0-native")} ({c}) | {a["renderer_receipt"][0].split(" adapter=")[-1].split(" workload=")[0] if a["renderer_receipt"] else "sem receipt"} | {p}/2 | {fmt("initial")} | {fmt("final")} |')
lines += ['', 'Os valores max/pixels na tabela detalham `native`; `portable` está registrado integralmente nos JSONs e PNGs. Não capturado é ausência de evidência, nunca passe. Na primeira versão do fixture, uma falha da comparação inicial encerrava o processo antes da rotação; isso afetou BGFX/OpenGL na cidade de 40.000 e nos sólidos. A versão estendida mantém a falha inicial e prossegue para recolher o diagnóstico final quando a renderização permite.', '',
'O terreno no wgpu falhou nas duas políticas e três APIs: panic de validação `Coin Uncached Vertex Buffer` inválido, capturado pela ponte Rust como status 7. O diagnóstico do log não estabelece sozinho a causa ou o limite de buffer violado; a causa requer investigação. Nenhum desses processos recebeu passe ou imagem substituta.', '',
'As divergências após mover a câmera das cidades aparecem em ambos os backends; o controle com câmera fixa delimita o problema, mas não demonstra a causa. Não houve correção de produção nesta entrega. `native` continua sendo o padrão. Diferenças entre imagens native/portable nos sólidos também estão quantificadas em `policy-pixel-comparison.json`; essa comparação é diagnóstica, pois políticas distintas não são por si uma referência independente de correção.', '',
'## Imagens reais', '',
'![Cidade de 40 mil edifícios](validation/windows-visual-20261010/visual-initial/city-40000-bgfx-d3d12-0-native-initial-window.png)', '',
'![Cidade de um milhão de instâncias](validation/windows-visual-20261010/visual-million/city-million-bgfx-d3d12-0-native-initial-window.png)', '',
'![Terreno com um milhão de triângulos](validation/windows-visual-20261010/visual-initial/terrain-million-bgfx-d3d12-0-native-initial-window.png)', '',
'![Sólidos com textura e transparência](validation/windows-visual-20261010/visual-initial/solids-wgpu-d3d12-0-native-final-window.png)', '',
'## Reprodução e evidências', '',
'O diretório [de evidências](validation/windows-visual-20261010) guarda logs iniciais, XML de todos os resultados, comandos/ambiente, CSVs, clocks/P-state/driver, fontes das duas versões do fixture, logs de build, SHA256, PNGs e máscaras. `pixel-manifest.json` registra hashes dos PPMs mantidos no diretório de execução e dos RGB/PNGs publicados. `artifact-manifest.json` protege os bytes dos arquivos publicados. O SDK e suas revisões/patch estão registrados na [campanha anterior](coin-render-windows-remaining-gates-20261010.md).', '',
'Para recompilar, configurar `visual-src` separadamente com `CMAKE_PREFIX_PATH` apontando para cada prefixo Coin/CoinRender BGFX/wgpu, gerador Visual Studio 17 2022, x64, Release e parallel 2. Colocar o `bin` do respectivo prefixo primeiro em PATH, selecionar `COIN_BGFX_RENDERER` e `WGPU_BACKEND`, e executar:', '',
'```text', 'coin_visual_campaign.exe <d3d12|vulkan|opengl> <native|portable> <city-40000|city-million|terrain-million|solids> 2 1 <prefixo-saida>', '```', '',
'O controle estático usa `COIN_VISUAL_STATIC_CAMERA=1`. O fixture inicial preservado tem somente três cenas e encerra antes da captura final se a inicial divergir. Os runners publicados configuram as APIs, verificam a NVIDIA e os módulos carregados, preservam falhas e geram os XMLs.', '',
'Permanecem abertos: consistência exata das cidades sob movimento, BGFX/OpenGL nos casos divergentes, terreno iluminado wgpu e a qualificação independente de imagem/performance da cidade do milhão. Esta campanha C++ não fecha consumidores FreeCAD, monitores com DPI diferente, perda de device em outras APIs ou driver TDR.']
body='\n'.join(lines)+'\n'
body=body.replace('fontes das duas versões do fixture','fontes das três versões do fixture')
body=body.replace('o controle separado mantém a câmera em 0,65.','os controles separados mantêm a câmera em 0,65. O primeiro controle usou height 155; visual-static-matched repete height 140 e os mesmos parâmetros da cidade original, sem mudar a tolerância.')
(repo/'docs/coin-render-windows-visual-scenes-20261010.md').write_text(body,encoding='utf-8')
check=repo/'docs/coin-render-next-fronts-checklist.md';s=check.read_text(encoding='utf-8');marker='## 5. Hardware, superfícies e sombras'
addition=f'''- [x] Executar cenas visuais Win32 1280×960 em BGFX/wgpu e três APIs,
  native/portable: cidades de 40.000 e 1.000.000 de instâncias, terreno com
  um milhão de triângulos e sólidos com textura NPOT/transparência.
  {n} processos, {passed} passes e {n-passed} falhas preservadas, sem ampliar
  tolerância. Controle de câmera fixa, PNGs/máscaras, DLLs, logs/XML e escopo
  no [relatório visual Windows](coin-render-windows-visual-scenes-20261010.md).
- [ ] Resolver as divergências janela/offscreen das cidades sob movimento,
  os casos BGFX/OpenGL e o buffer inválido do terreno iluminado wgpu;
  depois repetir os gates exatos. O teste executado não encerra qualificação
  visual independente ou desempenho das cidades e não representa FreeCAD.

'''
assert 'coin-render-windows-visual-scenes-20261010.md' not in s
s=s.replace(marker,addition+marker,1);check.write_text(s,encoding='utf-8')
print('report processes',n,'passes',passed,'failures',n-passed)
