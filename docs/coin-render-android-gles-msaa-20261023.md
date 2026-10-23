# Android GLES/MSAA — revalidação de 23/10/2026

Resultado funcional aprovado no emulador x86_64, com wgpu/OpenGL ES explícito.
Base: `a55cc3afdfde4a400a586bc28ea79c11e17f8c5f`. Candidato de código e testes:
`0a4e9d2dcf2a2649f12d5aa2b9a1295603f8d3d1`. Este recibo não qualifica o G7 ThinQ, ARM64 ou Vulkan.

## Correções

1. O patch local de Naga forçava `Restrict` em loads de profundidade MSAA e
   gerava `textureSamples`, indisponível no GLSL ES 3.10 do AVD. O lowering
   agora respeita a política de imagens escolhida pelo backend GLES; os
   perfis desktop com checagens conservam `Restrict`. A política global do
   wgpu e as tolerâncias de imagem não foram alteradas. A dependência do
   arquivo Naga foi adicionada ao build incremental CMake.
2. A cor MSAA offscreen pedia `TEXTURE_BINDING`, embora só seja desenhada e
   resolvida. A amostragem usa a saída de uma amostra. O uso agora coincide
   com o attachment de janela: `RENDER_ATTACHMENT`. O framebuffer offscreen
   que falhava com `FRAMEBUFFER_INCOMPLETE_MULTISAMPLE` passou mantendo 4×.
   A causa completa da divergência do tradutor entre texturas/renderbuffers
   não foi demonstrada; não se declara uma correção de driver.
3. O host aceita `coinrender_samples` explícito (1/2/4), registra o valor e
   preserva as verificações de conteúdo. Qualifica também um tamanho nativo
   observado depois do callback de layout. O loop drena eventos prontos
   antes do próximo frame contínuo e evita render imediato do milhão em
   callbacks sem mudança de tamanho.
4. A chamada de elemento cúbico usa a classe derivada que concede amizade ao
   builder, corrigindo a rejeição de acesso do Clang/NDK sem mudar ABI pública.

## Campanha final

Emulator 37.2.12/API 37, NDK r30, mínimo API 26, AMD/Mesa no host e 8 GiB
apenas nesta execução do AVD. Renderer solicitado e observado: **2 / Gl**.
Não foi habilitado adaptador Vulkan não conforme, não houve troca de API.
Cada linha iniciou em processo novo e passou por captura, HOME/pausa,
retomada no mesmo processo, 1080×2400 → 2400×1080 → 1080×2400 e BACK limpo.

| Cena | Amostras | PID | Checkpoints | FNV64 offscreen estável | Resultado |
|---|---:|---:|---:|---|---|
| Cubo | 1 | 15438 | 11 | `dc8224a1b44d3d8d` | PASS |
| Cubo | 2 | 15600 | 11 | `aed3966407d7c58d` | PASS |
| Cubo | 4 | 15732 | 11 | `98c385ff9905b00d` | PASS |
| 40.000 prédios | 4 | 15860 | 11 | `85f1b4b5e385c0dd` | PASS |
| 1.000.000 prédios | 1 | 16052 | 4 | `067c1efbc25642eb` | PASS |

O milhão foi contado pelo próprio SoDB/host: 1.000.000 de prédios, mais o
chão. Nesta rodada, HOME foi enviado após a primeira captura, sem esperar
frames extras (`--startup-frames 0`). Todos os processos finais emitiram
`P23 smoke finished: OK`; nenhum PID final apareceu no ANR do sistema.
Seriais/gerações pertencem ao target/Activity e podem reiniciar em uma
recriação. Captura offscreen 64×64 e screenshots de apresentação são provas
separadas; não se afirma equivalência integral entre elas.

A regressão de lowering falhou antes da correção e passou depois. A suíte
Rust final teve **60 aprovações, zero falhas e 4 testes ignorados**, incluindo
resolução exata de profundidade 2×/4× em OpenGL e Vulkan físicos da AMD.
O teste de integração FFI de resolve e borda parcial, ignorado por padrão,
também foi executado explicitamente em processos separados: **1/1 OpenGL e
1/1 Vulkan**, com 1×/2×/4× em cada execução, sem falhas ou skips.

## Por que o milhão funcionava antes

A campanha histórica de 07/10 qualificou seu APK/revisão e o perfil 1×.
Ela não exercitava este resolvedor MSAA adicionado posteriormente. Na revisão
atual, a construção antecipada do pipeline MSAA fazia até uma captura 1×
falhar; isso não era um limite da quantidade de paralelepípedos. O novo APK
inclui as duas cidades e passou novamente no milhão, no escopo acima.

Tentativas anteriores desta campanha ficam preservadas: shader inválido,
conteúdo preto 4×, ANR com eventos pendentes e observador antigo que esperava
uma segunda captura inicial do milhão. O controle GLES sem Coin/wgpu também
reproduziu uma rejeição de readback com DRAW incompleto e READ completo;
esse defeito independente não explica sozinho a falha de desenho 4×. Uma
experiência de trocar o vínculo de leitura não resolveu o caso e foi retirada.
Toda instrumentação temporária foi removida do binário final.

## Reprodução e limites

O [validador e controle independente](../testsuite/reproducers/android-msaa-read-framebuffer/README.md)
contém os comandos portáveis. Build do APK:

```sh
python3 .github/scripts/build-coin-render-android-apk.py \
  --sdk "$HOME/Android/Sdk" --ndk "$HOME/Android/Sdk/ndk/30.0.16248370" \
  --java-home "$HOME/android-studio/jbr" \
  --build-dir /absolute/android-build --output-dir /absolute/android-build/apk \
  --include-million-city
```

APK local: `/mnt/Laranja/Git/externos/coin/build-android-current-20261023/apk/coin-render-p23-x86_64.apk`.
SHA-256: `dd75a2bc7fa07ebaaa6875a82fb8585bb3c8188dac62cebf73c77d03b5fd0f51`.
Os [recibos versionados](validation/coin-render-android-gles-20261023/manifest.json)
incluem fontes/binários, comandos, PIDs, capturas e logs. Screenshots e APK
permanecem nos artefatos locais identificados por hash, sem chaves no Git.

A campanha é funcional. Inicialização e recapturas do milhão ainda levam
dezenas de segundos; a mediana histórica de FPS não foi requalificada.
Pausa durante carregamento anterior à primeira captura, longa duração,
pressão de memória, múltiplas Activities/threads e demais combinações
permanecem fora deste recibo. Vulkan Goldfish continua não qualificado.
O G7 ThinQ e a execução física ARM64 ficam para depois.
