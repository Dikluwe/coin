# Sampling AMD e instancing — continuação de 2026-10-07

A partir de `6c91ed26df655b5bdbf7fcbebce71a7b841c4539`, em
`codex/coin-render`. As duas falhas de instancing eram expectativas de testes
anteriores às otimizações P23. Foram corrigidas com oráculos exatos preservados.
O gate projetivo AMD continua falhando: esta rodada isola sua relação com a
precisão de seleção de texels e footprint, mas não entrega paridade universal.
Não houve alteração de executor, shader ou sampler CPU de produção, nem medição
ou promessa de ganho de FPS.

Fontes, patches experimentais, logs, comandos e hashes ficam em
[`validation/sampling-instancing-fixes-20261007`](validation/sampling-instancing-fixes-20261007/summary.json).
Os builds e experimentos completos permanecem em
`/mnt/Laranja/Git/externos/coin-render-artifacts/sampling-instancing-fixes-20261007`.

## Gates de instancing fechados

`CoinWgpuMultiDeviceTest` contava `sizeof(CoinWgpuInstance)` como upload GPU.
O transporte CPU continua com 144 bytes, mas o buffer GPU já tem 96 bytes por
instância: três linhas afins do modelo e três linhas da normal, com material
embutido. Duas instâncias e dois materiais de 80 bytes devem carregar **352
bytes**, não 448. O teste exige esse valor exato, um upload e retenção da
geometria. Reuso estático/câmera continua exigindo zero bytes. Os oráculos RGB e
depth continuam vindo de geometria baked independente, inclusive sob mutação na
mesma revisão, revisão nova, transformações, materiais e múltiplos dispositivos.

`CoinWgpuLargeBindingsTest` tinha geometria repetida que agora pode ser agrupada.
Esse gate precisa exercitar a arena de 25.600 uniformes independentes, além de
64 MiB. Um plano de clipping que aceita todos os vértices, `z >= -1`, mantém o
cenário na rota de draws individuais suportada. O teste exige **25.600 draws e
zero instâncias**; verifica todas as células RGBA em quatro fases: inicial,
estática, material e câmera. Não foram removidos limites, verificações de pixels
ou otimizações do executor.

LargeBindings, MultiDevice e stress passaram nas três células wgpu:
AMD Vulkan, NVIDIA Vulkan e AMD OpenGL: **9/9 execuções**. Cada processo agora
registra adaptador, renderer e vendor/device; OpenGL AMD reporta vendor 1002 e
não fornece o device ID. A ausência de adaptador retorna 77 em MultiDevice/stress,
com `SKIP_RETURN_CODE` no CTest. O controle com ICD inexistente comprovou dois
SKIPs, sem convertê-los em evidência de execução GPU.

## Sampling: a comparação CoinGL que estava escondida

O teste saía imediatamente quando CPU/GPU divergiam. Agora calcula os três pares
CPU/GPU, CPU/CoinGL e GPU/CoinGL, imprime coordenadas divergentes e executa as duas
qualidades projetivas e os controles posteriores de desligamento/recuperação.
A falha inicial continua determinando exit 1. A fixture original, sua região de
100 pixels e os limites **MAE <= 1,5 e máximo <= 4** permanecem.

Resultados originais, qualidade 0,5:

| Célula | CPU/GPU MAE / máximo | GPU/CoinGL MAE / máximo |
|---|---:|---:|
| wgpu AMD Vulkan | 8,08 / 78 | 0 / 0 |
| wgpu AMD OpenGL | 8,08 / 78 | 0 / 0 |
| BGFX AMD Vulkan | 8,08 / 78 | 0 / 0 |
| BGFX OpenGL | 7,99 / 77 | 0,09 / 1 |
| wgpu NVIDIA Vulkan | 0 / 0 | 0 / 0 |
| BGFX NVIDIA Vulkan | 0 / 0 | 0 / 0 |

BGFX OpenGL reporta vendor/device zero. O resultado qualifica o runtime OpenGL
selecionado com EGL Mesa; não certifica sua identidade física. Na qualidade 0,8,
CPU/GPU permanece dentro do gate: AMD wgpu/BGFX Vulkan máximo 3, BGFX OpenGL
máximo 3 e NVIDIA Vulkan máximo 2. Portanto, a grande divergência se concentra
na seleção nearest dentro dos mips, sem divergência grande no controle linear.

O modo `--projective-study` adiciona offsets de -0,001 e +0,001 nas coordenadas
S/T da mesma matriz, mantendo Q, imagem, geometria, filtros e tolerâncias. O
original zero permanece incluído e o modo também retorna FAIL se qualquer cena
falhar. O offset negativo dá **MAE 0 / máximo 0** na qualidade 0,5 em todas as
seis células. O positivo ainda cruza uma borda: AMD máximo 77, BGFX OpenGL 76.
Nenhum desses offsets substitui a fixture que falha no gate normal.

## Experimentos controlados, sem incorporação em produção

A [análise independente](validation/sampling-instancing-fixes-20261007/analyze_footprint.py)
usa a fórmula do quad autoral de 16 pixels, UV 0,03..0,97 e Q = 1 + 0,3*S.
Na coluna x=33, a coordenada S do mip 3 é **7,998724964 texels**, a somente
0,001275036 da borda inteira 8. Isso fica dentro de meio passo de uma
quantização de oito bits fracionários: 1/512 = 0,001953125 texel. O sampler
ideal da CPU escolhe o texel à esquerda; quantizar pode selecionar o vizinho de
cor oposta. Os logs localizam as diferenças nessa coluna e em (35,31).

Foram compiladas variantes temporárias da referência CPU com o mesmo gate:

| Variante diagnóstica | AMD Vulkan qualidade 0,5 MAE / máximo | Resultado |
|---|---:|---|
| Somente round de coordenada por mip a 1/256 texel | 0,69 / 10 | FAIL |
| Round 1/256 + derivadas coarse | 0,47 / 8 | FAIL |
| Round 1/256 + maior valor singular da footprint | 0,26 / 3 | PASS AMD |
| Última variante, NVIDIA Vulkan | 7,82 / 75 | FAIL NVIDIA |

A última variante AMD também passa na qualidade 0,8: MAE 0,42/máximo 1.
O contracontrole NVIDIA impede tratá-la como correção comum. A CPU original foi
restaurada e todos os binários finais reconstruídos. Os patches em `experiment-*`
são somente reproduções dos experimentos, não alterações aprovadas do contrato.

Esses dados sustentam a **inferência** de seleção subtexel/footprint diferente
entre as implementações nativas; não identificam exatamente o circuito ou
algoritmo do driver. Os [limites Vulkan](https://docs.vulkan.org/spec/latest/chapters/limits.html)
expõem precisões subtexel e de mip; o experimento não é uma certificação de
conformidade Vulkan. Tampouco demonstra corrupção de UV, erro de transporte
BGFX/wgpu ou falha AMD em reproduzir o CoinGL dessa máquina.

Fechar a paridade exige um contrato explícito: por exemplo, um perfil de sampling
calculado nos shaders, com seleção de texels e LOD portáteis, validado contra CPU
e medido quanto a custo, delimitando sua diferença para o sampler CoinGL nativo.
A alternativa de referências CPU específicas por precisão nativa também precisa
de contrato e qualificação. Não aplicar o modelo AMD globalmente nem aumentar a
tolerância para ocultar a troca de texel. O gate atual continua ativo e vermelho.

## Integração e outra expectativa antiga

A integração ampliada encontrou uma recusa de qualidade 0,95 no teste procedural,
embora o perfil avançado já a aceite como anisotropia. Agora há controle positivo
com sampler linear/mips/anisotropia 16; a recusa sem publicação usa qualidade
1,01, fora do intervalo autorizado. Pixels, recuperação e demais limites foram
preservados. CPU e Vulkan AMD/NVIDIA passam.

A mesma coleta expõe uma diferença OpenGL na esfera com linhas/UV DEFAULT:
MAE 0,319495/máximo 4, contra seu limite original MAE <= 1/máximo <= 3. Ela ocorre
antes do novo controle anisotrópico. Recompilar o teste **original de 6c91ed26**
reproduziu os mesmos números em wgpu/BGFX OpenGL. Logs e hashes dessa comparação
ficam no [baseline procedural](validation/sampling-instancing-fixes-20261007/baseline-procedural-summary.json).
Esse gate continua falhando, separado do sampling projetivo e ligado ao estudo
aberto de junções curvas; não houve aumento de tolerância ou exclusão OpenGL.

Resultados finais do conjunto selecionado:

- wgpu/AMD Vulkan: **33/34 passes**, falha somente de sampling projetivo.
- BGFX/AMD Vulkan + runtime OpenGL: **7/10 passes**, duas falhas de sampling
  projetivo e uma do procedural OpenGL original, descritas acima.
- Recording: **3/3 passes** de captura sampling/config/procedural.
- Matriz de seis perfis: instancing/stress **9/9**; sampling e estudo NVIDIA
  **4/4**; procedural GPU **4/6**. Os demais oito resultados de sampling/estudo
  AMD/OpenGL e os dois procedurais OpenGL ficam registrados como FAIL.

Essa seleção não equivale a toda a suíte do projeto. Não qualifica Windows,
Android, FreeCAD ou outros drivers. Nenhum skip conta como execução física.

## Reprodução

`testsuite/coinrender/run-sampling-instancing.py` recebe `--wgpu-build`,
`--bgfx-build` e `--output`, e executa os seis perfis sequencialmente. Cada
resultado conserva exit code, comando, ambiente, métricas, hash do binário e log.
O runner coleta todos os resultados; consultar `summary.json` para FAIL/SKIP.
Os comandos CTest selecionados e seus ambientes estão em `integrated-summary.json`.
O manifest inclui os hashes finais das fontes, scripts, binários e evidências.
