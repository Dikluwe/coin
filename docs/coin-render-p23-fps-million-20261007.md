# P23 — FPS Android e um milhão de prédios, 2026-10-07

A cidade de **1.000.000 prédios e um chão** foi apresentada no AVD x86_64
API 37 por wgpu/OpenGL ES, com giro e zoom. São **12.000.012 triângulos**, sem
LOD, descarte de ocorrências CPU ou substituição da cena por uma imagem.
O APK inclui também a cidade anterior. O trabalho sucede a
[cidade de 40.000](coin-render-p23-android-city-20261007.md).

## Implementação e limites

- O host deixa de chamar `setViewportRegion` quando a região permanece igual;
  o setter anterior invalidava o plano em todos os quadros.
- SCREEN_DOOR com máscara zero só entra no perfil opaco quando os materiais
  são realmente opacos. Máscaras positivas e alfa fracionário, inclusive o
  caso 1/128 que quantiza a máscara para zero, preservam o fallback anterior.
- A ABI privada continua **49**, com transporte CPU de **144 bytes** por
  instância. O buffer GPU usa **96 bytes**, mantendo os floats de três linhas
  afins do modelo e da normal e o material como `u32`, sem quantização.
- Limites: 1.048.576 instâncias, 160 MiB de transporte CPU, 32 MiB de materiais,
  8 MiB de geometria canônica, 128 grupos e metadados até 64 MiB/65.536 spans.
  O cache de matrizes mantém 16 MiB; o milhão usa seu bypass delimitado.
  A capacidade real de storage do device continua sendo verificada.
- O milhão ocupa **96.000.096 bytes** de instâncias GPU, abaixo do binding de
  128 MiB deste device. O trace confirmou **um draw instanciado**, 24 vértices,
  36 índices, 1.000.001 instâncias e zero novo upload nos quadros reutilizados.
  Não se deve confundir os 1.000.001 draws do plano CPU com os draws GPU.
- SoCallbackAction conta ocorrências sem guardar um milhão de SoPaths. Isso
  evita a remoção quadrática dos auditores de caminhos da lista de filhos.
- A qualificação do milhão mantém apenas um plano CPU grande por vez entre
  janela e offscreen; o alvo offscreen permanece vivo como guarda do runtime.
  Retomada/resize liberam o plano anterior antes da recaptura, e notificações
  de layout com dimensões iguais usam o quadro normal.

A cena é lida por SoDB. A contagem observada foi 1.000.001 Cubes, não uma
contagem estimada a partir do nome do asset. O generator aceita grid até 1000;
`--include-million-city` inclui a cena grande no APK de forma optativa.
A última cidade apresentada com sucesso é salva. O launcher sem extras a
reabre; `coinrender_million_city false` seleciona novamente os 40.000 em um
início frio. O cubo continua sendo uma seleção explícita.

## Medições e evidência visual

O AVD foi iniciado com **8.192 MiB temporários** (`-memory 8192`), GPU host AMD
Renoir/Mesa 25.2.8, KVM e display virtual de 60 Hz. A configuração salva do AVD
continua com 2 GiB. Não há qualificação de Android físico ou ARM nesta rodada.

Duas rodadas intercaladas original/otimizado usaram o mesmo asset, câmera,
renderer e janela **2400 × 1080**, sem trace e sem input. Em cada processo,
o primeiro grupo de dez quadros foi excluído e três grupos seguintes foram
medidos: 60 quadros medidos por variante, em médias de dez quadros.

| 40.000 prédios | Original | Otimizado |
|---|---:|---:|
| Mediana das médias render/present CPU | 910,50 ms | 16,64 ms |
| Intervalo dessas médias | 905,17–932,92 ms | 16,54–16,76 ms |
| FPS CPU/wall dos grupos | 1,07–1,10 | 59,61–60,43 |

Isso representa cerca de **54,7×** nessa medida. O loop chegou ao limite do
display virtual. Não são timestamps GPU nem latência medida até a tela.
O APK de comparação tem SHA-256 `7df24560524767474549dec94b82e16ffb5f867dc2e06350cc7bea77b522d23f`;
os ajustes posteriores tratam a memória da qualificação do milhão. A entrega
final voltou a apresentar 40.000 a cerca de 60 FPS, incluindo interação.

O checksum offscreen 64×64 da comparação foi idêntico:
`7e6fa0ca92e6a9eb`. Nos screenshots apresentados, recortando apenas as barras
Android (RGB, y=[64,1016), x completo), **um pixel de 2.284.800** mudou nas duas
rodadas: MAE 0,0000318, máximo 91. Essa diferença está preservada, não removida
por tratamento de imagem ou por alteração do gate.

No piloto sem input de um milhão, em 2400×1080, os 19 grupos após os primeiros
dez quadros variaram de **16,25 a 30,87 FPS**, mediana de render/present
**36,81 ms**; os últimos grupos ficaram aproximadamente em 27–31 FPS.
Execuções mais longas também mostraram cerca de 30 FPS quando a câmera ficou
parada. A interação e o enquadramento alteram o custo: arrastes chegaram a
produzir grupos de cerca de 5 FPS. O ganho estático não encerra desempenho
com câmera nem uma campanha longa de caudas de latência.

A memória ao fim da qualificação sequencial ficou perto de **5 GB PSS**.
A leitura da cena levou aproximadamente 13 s; a qualificação inicial, mais
24 s nessa amostra. O milhão não está qualificado no AVD padrão de 2 GiB.
Há margem pequena de RAM; estes números não constituem um pico máximo medido.

GLES não oferece COPY_SRC de janela neste AVD. O checksum é de outro alvo
64×64; screenshots Android comprovam apresentação separadamente.
Os checksums iniciais do milhão foram `930b7c22b9501787` com janela landscape
e `067c1efbc25642eb` com janela portrait. O host qualifica conteúdo, serial
crescente e ausência de readback no quadro normal.

## Validação e exclusões

36 testes Rust passaram, incluindo layout WGSL/GLSL, empacotamento inteiro,
limite de quantidade e rejeições. CoinWgpuFfiFrameTest passou no build Linux
real e no controle RECORDING: zero-mask opaco, equivalência com NONE,
alfa/máscara fracionária, estado inválido não usado e recuperação na mesma
revisão. Cinco controles CPU de cena, reuso e inventário passaram.

A referência de câmera **AMD/OpenGL** passou contra reconstrução integral e
CoinGL para perspectiva/ortográfica, nove posições e mutação da cena. Foi
necessário GLX pixmap direto (`COIN_GLX_PIXMAP_DIRECT_RENDERING=1`,
`COIN_GLXGLUE_NO_PBUFFERS=1`): a tentativa pbuffer reporta max-pixels zero.
A referência **AMD/Vulkan** falhou no gate CoinGL no frame 5 de perspectiva,
MAE 0,147278/0,149582. Um controle com o Rust/shader anterior de 144 bytes e o
mesmo C++ produziu exatamente as mesmas 18 métricas até a falha. O gate não
foi relaxado; a diferença permanece estudo e não é um pass Vulkan.

Excluídos dos passes/medição: Intent entregue à Activity já aberta que
continuou com 40.000; trace de candidato antigo; rodada A/B com arrastes;
medições finais com input manual ou orientação alterada; tentativa instrumentada
PID 7313 encerrada pelo lowmemorykiller antes de completar a captura (RSS
aproximadamente 6,56 GB, além de swap). A captura sequencial corrige a duplicação
que contribuiu para esse pico. Os logs locais completos foram preservados;
o arquivo no Git contém apenas linhas relevantes do diagnóstico de memória.

Uma retomada curta não recriou a superfície e não fechou o gate TERM_WINDOW;
mais tarde TERM_WINDOW foi observado, mas a sessão do emulador encerrou antes
de concluir aquele harness. Após reabrir o AVD, a seleção publicada continuou
1000 e a cidade foi apresentada novamente pelo launcher sem extras. Um
harness que limpou o log depois do início perdeu a linha da contagem e deu
timeout; ele não é contabilizado como qualificação integrada.

Uma validação dedicada após essa reabertura confirmou **TERM_WINDOW real,
HOME/retorno no mesmo PID 3208/thread 3226 e geração 1 → 2**, com novo checkpoint
RGBA completo e serial crescente. O alvo offscreen permaneceu vivo. Houve
interação manual e orientação portrait/landscape nessa execução: é evidência
funcional, não medição isolada. BACK com nova thread no mesmo processo e uma
campanha longa de retomadas do milhão ainda não estão fechados.

## Estudos seguintes

- Compactar/compartilhar estados CPU por ocorrência: hoje só os estados do
  plano do milhão ocupam aproximadamente 1,65 GB. Preservar identidade,
  invalidação, erros sem publicação e recuperação antes de escolher o desenho.
- Perfilar os arrastes de câmera, validação de payload, present/gfxstream,
  primeiro quadro e latência. Separar cena parada de interação.
- Ampliar a campanha de memória/retomada, Android físico/ARM e outros drivers;
  qualificar o layout novo em Metal/D3D12. Não extrapolar o AVD para esses alvos.
- Diagnosticar a diferença de raster/iluminação Vulkan/CoinGL, mantendo o
  contrato portátil e os gates existentes.

## Entrega permanente

APK de desenvolvimento (debuggable, assinado e alinhado a 16 KB):
`/mnt/Laranja/Git/externos/coin-render-artifacts/p23-fps-20261007/apk-final/coin-render-p23-x86_64.apk`.
SHA-256 `416ee7877668803736389659cda67898524df8e5a60780cb278b0a34b91aef26`.
Asset milhão: 133.200.021 bytes, seed 136,
SHA-256 `b52a2ce714bf94129d3ddf7a02f910ffacadf915a72b61c8c86acb40ed72ab19`.
O asset de 40.000 mantém o hash `bb9ccc612cd38ffb749ee599d9350a80974649eab5bf477d2f344538a42b2576`.

[Evidência e hashes](validation/p23-android-fps-million-20261007/manifest.json).
Logs textuais no Git usam `.log.gz`, com hashes dos bytes descomprimidos no
manifesto. Os originais legíveis permanecem no diretório de artefatos acima.
[Checklist](coin-render-next-fronts-checklist.md).

```sh
python3 .github/scripts/build-coin-render-android-apk.py \
  --sdk "$HOME/Android/Sdk" --ndk "$HOME/Android/Sdk/ndk/30.0.16248370" \
  --java-home "$HOME/android-studio/jbr" \
  --build-dir /mnt/Laranja/Git/externos/coin-render-artifacts/p23-apk-20261007/build-x86_64 \
  --output-dir /mnt/Laranja/Git/externos/coin-render-artifacts/p23-fps-20261007/apk-reproduction \
  --include-million-city
adb install --no-incremental -r /path/to/coin-render-p23-x86_64.apk
# -S termina a Activity anterior; extras de seleção exigem um início frio.
adb shell am start -S -W -n org.coin3d.coinrender.p23/android.app.NativeActivity \
  --ez coinrender_million_city true
# Para retornar aos 40.000, usar o mesmo início frio com o extra false.
# Trace optativo, fora de medições: --ez coinrender_trace true
# Ler com run-as ... cat files/coinrender-phases.log.
```
