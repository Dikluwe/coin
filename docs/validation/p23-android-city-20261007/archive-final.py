from pathlib import Path
import hashlib,json,re,shutil,subprocess
repo=Path('/mnt/Laranja/Git/externos/coin-render')
out=Path('/mnt/Laranja/Git/externos/coin-render-artifacts/p23-city-20261007')
q=out/'final-qualification';apkdir=out/'apk-controls-x86_64'
a=repo/'docs/validation/p23-android-city-20261007';a.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
log=(q/'qualified-final.log').read_text()
assert 'Fatal signal' not in log and 'P23 smoke finished: FAILED' not in log
assert ' E AndroidRuntime' not in log
entries=re.findall(r'\s(\d+)\s+(\d+) I CoinRenderP23: scene=city-40000 buildings=40000 ground=1 triangles=480012 continuous=1',log)
assert len(entries)==4 and len({p for p,t in entries})==1
pid,first_thread=entries[0];final_thread=entries[-1][1]
rows=re.findall(r'\s(\d+)\s+(\d+) I CoinRenderP23: generation=(\d+) serial=(\d+) size=(\d+)x(\d+) rgba_fnv64=([0-9a-f]+) capture_scope=(\w+)',log)
first=[r for r in rows if r[:2]==(pid,first_thread)]
assert {int(r[2]) for r in first}=={1,2,3,4}
assert {r[4:6] for r in first}=={('1080','2400'),('2400','1080')}
assert all(int(b[3])>int(a[3]) for a,b in zip(rows,rows[1:]))
assert all(r[7]=='offscreen' for r in rows)
assert log.count('P23 smoke finished: OK')==4
assert re.findall(r'Released idle Android renderer; next_generation=(\d+)',log)==['1','2','3','4']
assert 'rgba_fnv64=dc8224a1b44d3d8d' in log
interaction=(q/'qualified-interaction.log').read_text()
assert 'camera_touch' in interaction
focals=[float(v) for v in re.findall(r'camera_touch.*focal=([\d.]+)',interaction)]
assert len(set(focals))>=2
for name in ['qualified-city-before.png','qualified-city-yaw.png','qualified-city-zoom.png','qualified-city-portrait.png','qualified-city-landscape.png','qualified-city-final.png']:
 shutil.copy2(q/name,a/name)
for name in ['qualified-final.log','qualified-interaction.log','qualified-lifecycle.log','qualified-commands.json','qualified-start.log','qualified-install.log','qualified-cube.log','qualified-relaunch-1.log','qualified-relaunch-2.log']:
 shutil.copy2(q/name,a/name)
for name in ['linux-rebuild.log','linux-regression-current.log','emulator-visible.log','qualify-final.py','relaunch-crash.log','ui-diagnostic-full.log','launcher-log.log']:
 shutil.copy2(out/name,a/name)
# The prior failed APK's hash and exact sources remain distinct from delivery.
shutil.copy2(out/'apk-city-delivery-x86_64/binary-sha256.json',a/'rejected-relaunch-apk-sha256.json')
for name in ['commands.json','binary-sha256.json','configure.log','build.log','generate-city.log','verify.log','verify-align.log']:
 shutil.copy2(apkdir/name,a/('apk-'+name))
sourcefiles=['examples/coinrender/coin_render_android_smoke.cpp','examples/coinrender/AndroidManifest-p23.xml','.github/scripts/build-coin-render-android-apk.py','examples/coinrender/generate_large_scene.py','src/rendering/coinrender/CoinRenderBackendRuntime.h','src/rendering/coinrender/CoinRenderTarget.cpp','src/rendering/coinwgpu/CoinWgpuFfi.h','src/rendering/coinwgpu/CoinWgpuRuntime.cpp','src/rendering/coinwgpu/rust_bridge/src/lib.rs']
manifest={'base_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=repo,text=True).strip(),'apk_path':str(apkdir/'coin-render-p23-x86_64.apk'),'apk_sha256':sha(apkdir/'coin-render-p23-x86_64.apk'),'scene_sha256':sha(apkdir/'city-40000.iv'),'buildings':40000,'triangles':480012,'api':'wgpu/OpenGL ES','device':'Medium_Phone_API_37.0 x86_64 emulation on AMD Renoir','qualified_pid':int(pid),'first_native_thread':int(first_thread),'final_native_thread':int(final_thread),'surface_generations_first_activity':[1,2,3,4],'checkpoints_first_activity':len(first),'checkpoints_all':len(rows),'idle_epochs':[1,2,3,4],'city_activity_threads':[int(t) for p,t in entries],'source_sha256':{n:sha(repo/n) for n in sourcefiles},'limits':['emulation only','64x64 offscreen capture; window presentation inspected separately','interactive functional samples, not a controlled benchmark','camera viewAll on surface recreation','about 1 FPS at 40000 buildings; performance study open','Vulkan/physical Android/async ticket retention across Activity threads unqualified'],'excluded':'launcher-log.log/relaunch-crash.log and ui-diagnostic-full.log are prior failures, not delivery passes'}
(a/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
(a/'implementation.patch').write_bytes(subprocess.check_output(['git','diff','--',*sourcefiles],cwd=repo))
(a/'.gitattributes').write_text('* -text\n')
(a/'source-sha256.json').write_text(json.dumps(manifest['source_sha256'],indent=2)+'\n')
report=f'''# P23 — cidade Android interativa de 40.000 prédios

Em 2026-10-07, o APK x86_64 passou no AVD API 37 deste Linux, por
**wgpu/OpenGL ES**. A janela gráfica do emulador foi aberta e permaneceu na
cidade ao concluir. O launcher abre a cidade por padrão, sem Intent extra;
o cubo do smoke anterior depende de seleção explícita.

## Cena e controles

O pacote inclui `assets/city-40000.iv`: grade 200 × 200, seed 136,
SHA-256 `{manifest['scene_sha256']}`. É o mesmo arquivo das campanhas
anteriores, com PHONG, duas luzes direcionais, materiais e transforms do Coin.
SoDB lê o asset; SoSearchAction confirma **40.001 ocorrências de Cube**:
40.000 prédios e um chão, equivalentes a **480.012 triângulos**. Compartilhar
nós de cena não implica que todo backend use instancing GPU.

Arrastar horizontalmente gira a câmera em torno do ponto focal; arrastar
para cima aproxima e para baixo afasta. No emulador, usar botão esquerdo do
mouse. O zoom limita a distância e atualiza os planos de corte. A posição
final de UP também é aplicada, para preservar arrastes curtos quando eventos
MOVE forem agrupados entre quadros lentos.
A câmera é reenquadrada por `viewAll` ao recriar a janela; a distância de zoom
não é preservada nesse evento.

## Resultado integrado do APK de entrega

- Build/link Android, assinatura e alinhamento 16 KB passaram.
- MAIN/LAUNCHER sem extras carregou 40.000 prédios e renderer 2.
- Giro e zoom chegaram à câmera, incluindo gestos de 600 ms. As capturas
  antes/giro/zoom foram inspecionadas. Houve interação manual durante a
  execução; ela não é uma campanha isolada de desempenho.
- Três HOME/retornos produziram TERM_WINDOW/INIT_WINDOW reais: gerações
  **1 → 2 → 3 → 4**, PID {pid}/thread nativa {first_thread}. O alvo offscreen
  independente permaneceu vivo no Host e voltou a capturar após cada retorno.
- {len(first)} checkpoints na primeira Activity e {len(rows)} no processo,
  com seriais crescentes, RGBA 64 × 64 completo e conteúdo presente; o quadro
  normal não publicou readback de janela.
- Dimensões reais de janela 1080 × 2400 e 2400 × 1080 foram observadas.
- Duas reaberturas após BACK criaram novas threads no **mesmo PID** e
  apresentaram a cidade. O encerramento liberou o runtime ocioso, com epochs
  1/2/3/4 registrados; IDs globais de tickets/tokens/submissão não são zerados.
- O cubo selecionado explicitamente manteve checksum `dc8224a1b44d3d8d`;
  BACK e launcher normal voltaram à cidade. Quatro encerramentos deram `OK`.
- Cinco controles CPU Linux passaram após recompilar os fontes atuais no
  backend RECORDING; isso não é um gate GPU Linux.

GLES não oferece COPY_SRC de janela neste AVD: captura offscreen e screenshot
são evidências separadas, não equivalência integral de pixels. Avisos EGL e
mensagens `Fake map` do wgpu estão preservados. A execução final não registrou
Fatal signal, falha do smoke ou exceção AndroidRuntime.

## Falha encontrada e fronteira de encerramento

Antes da correção, BACK encerrava a thread nativa mas deixava o device/EGL
compartilhado do processo vivo. Reabrir em outra thread produziu SIGFPE no
`GL2Encoder::s_glBindBufferRange` do GLES do emulador. A pilha está arquivada;
não há um reproducer mínimo independente nem diagnóstico completo do driver.

A correção introduz um hook **privado, somente Android**, chamado após o
último alvo sair do registro e liberar seu backend. O runtime recusa teardown
com superfícies, tickets, aposentadorias pendentes, devices explícitos ou RTT
publicado vivos. No caso ocioso espera a fila, destrói o runtime na thread
que termina e avança o epoch para a próxima Activity. A lib não troca API.
O objeto Infra continua com lifetime do processo; ABI de estruturas permanece
49. O offscreen sobrevivente mantém o registro não vazio durante HOME/retorno.
A retenção de tickets/RTT através da troca de thread ainda exige campanha
específica; não foi qualificada por este smoke síncrono.

A tentativa anterior com launcher padrão do cubo também está preservada.
`ui-diagnostic-full.log`, `launcher-log.log` e `relaunch-crash.log` são falhas
anteriores excluídas dos passes. Não foram apagadas nem agregadas à entrega.

## Desempenho e estudos abertos

As médias funcionais em grupos de dez quadros ficaram próximas de **1 FPS**,
com cerca de **0,9–1,0 s** por render/present. Inicialização, capturas, eventos
ou interação entram em `fps_cpu_wall`; os valores não são timestamps GPU ou
latência até a tela, nem qualificação de desempenho de Android físico.

**Estudo futuro:** perfilar captura/lowering, quantidade de draws, uploads,
instancing GLES e gfxstream antes de escolher a otimização. Também seguem
abertos preservação de câmera entre janelas recriadas, duas janelas, API AUTO,
tickets entre threads, outros formatos/fixtures, Vulkan e Android físico/ARM.

## Entrega e reprodução permanentes

APK de desenvolvimento: `{manifest['apk_path']}`.
SHA-256: `{manifest['apk_sha256']}`.
[Evidência e hashes](validation/p23-android-city-20261007/manifest.json).

```sh
python3 .github/scripts/build-coin-render-android-apk.py \\
  --sdk "$HOME/Android/Sdk" \\
  --ndk "$HOME/Android/Sdk/ndk/30.0.16248370" \\
  --build-dir /mnt/Laranja/Git/externos/coin-render-artifacts/p23-apk-20261007/build-x86_64 \\
  --output-dir /mnt/Laranja/Git/externos/coin-render-artifacts/p23-city-20261007/apk-new-x86_64 \\
  --java-home "$HOME/android-studio/jbr"
adb -s emulator-5554 install --no-incremental -r /path/to/coin-render-p23-x86_64.apk
adb -s emulator-5554 shell am force-stop org.coin3d.coinrender.p23
# Confirmar pidof vazio antes de iniciar.
adb -s emulator-5554 shell am start -W \\
  -n org.coin3d.coinrender.p23/android.app.NativeActivity
```

Para o cubo, usar cold-start com `--ez coinrender_city false
--ez coinrender_continuous false`. A Activity é singleTask; mudar fixture
exige encerrá-la antes. Vulkan permanece explícito por `--ei coinrender_renderer 1`.
O roteiro `qualify-final.py` e comandos usados estão arquivados. Ele usa caminhos
deste host e requer um novo output ao reproduzir.

A janela do emulador usa a seleção AMD da campanha anterior, sem `-no-window`.
O teste usa `wm user-rotation lock`; `adb shell wm user-rotation free` restaura
o estado anterior. Interação no toolbar do emulador pode alterar a orientação.
APK/evidência anteriores ficaram em seus próprios diretórios permanentes.
'''
(repo/'docs/coin-render-p23-android-city-20261007.md').write_text(report)
p=repo/'docs/coin-render-next-fronts-checklist.md';s=p.read_text();s=s.replace('''- [ ] Ampliar Android para recriações repetidas de superfície na mesma execução,
  múltiplos alvos, API automática e demais fixtures/formatos.''','''- [x] Ampliar o AVD x86_64/GLES para 40.000 prédios: asset no APK, launcher padrão,
  giro/zoom e loop contínuo; três recriações reais de janela com offscreen vivo,
  rotação, duas reaberturas após BACK no mesmo processo e controle do cubo.
- [x] Liberar runtime Android ocioso antes da thread da Activity terminar;
  epochs avançam e alvos/recursos externos vivos impedem esse teardown.
- [ ] Ampliar Android para duas janelas, API automática, tickets/RTT entre threads
  de Activities e demais fixtures/formatos; preservar o zoom entre recriações.
- [ ] Estudo: perfilar 40.000 prédios em Android/GLES/gfxstream; a amostra
  funcional observou cerca de 1 FPS, sem qualificar desempenho físico.''');s=s.replace('[Primeiro APK e validação emulada](coin-render-p23-apk-validation-20261007.md).','[Primeiro APK e validação emulada](coin-render-p23-apk-validation-20261007.md).\n[Cidade interativa e continuação Android](coin-render-p23-android-city-20261007.md).');p.write_text(s)
p=repo/'docs/coin-render-p23-android.md';s=p.read_text().replace('[Relatório e evidência](coin-render-p23-apk-validation-20261007.md).', '''[Primeiro relatório e evidência](coin-render-p23-apk-validation-20261007.md).
A [continuação interativa](coin-render-p23-android-city-20261007.md) inclui
40.000 prédios no APK atual, giro/zoom, launcher padrão, três recriações de
janela com offscreen sobrevivente e reabertura após BACK no mesmo processo.
O cubo do primeiro APK é selecionável explicitamente; esse relatório anterior
é histórico.''');p.write_text(s)
(a/'artifact-sha256.json').write_text(json.dumps({str(p.relative_to(a)):sha(p) for p in sorted(a.rglob('*')) if p.is_file() and p.name!='artifact-sha256.json'},indent=2)+'\n')
print(json.dumps(manifest,indent=2))
