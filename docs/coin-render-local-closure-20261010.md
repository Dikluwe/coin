# Frente de fechamento neste Linux

Iniciada em 2026-10-10 na branch `codex/coin-render`. O
[checklist](coin-render-next-fronts-checklist.md) tinha 27 frentes abertas e
cinco critérios recorrentes de entrega no início desta rodada; o estudo da
linha 388 foi concluído abaixo. A classificação indica o que
este computador permite **executar**, não promete que uma investigação terá
resultado suficiente para marcar a caixa. Nenhuma falha visual ou gate será
reclassificado por mera ausência de reprodução.

| Linha do checklist | Alcance neste sistema | Próximo resultado verificável |
| --- | --- | --- |
| 58 — NPOT BGFX | Parcial | Repetir HDR/2047² e combinações Linux; D3D11 precisa de Windows. |
| 76 — rescaling, formatos | Parcial | Ampliar estado/formatos e FreeCAD Linux; Windows/Android em campanhas próprias. |
| 240 — borda fria FreeCAD | Local | Isolar a transição inicial sem alterar o critério de captura. |
| 260 — variação NVIDIA/wgpu | Local | A/B mais longo, clocks e replay em outras cargas. |
| 270 — sampling API | Parcial | AVD e consumidores Linux; Windows e outros dispositivos seguem separados. |
| 331 — junções e endpoints | Local | Concluir alternativas/custos e controles portáteis sem relaxar CoinGL. |
| 346 — bindings/shapes | Local | Acrescentar cruzamentos com casos de falha e recuperação. |
| 348 — depth clamp/formatos | Local | Separar clipping geométrico, range, precisão e formatos. |
| 351 — flat/sombras | Local | Fixture com iluminação por fragmento e QuadMesh preciso. |
| 353 — outros drivers/FreeCAD | Parcial | AMD/NVIDIA e FreeCAD Linux; Windows e outros drivers ficam externos. |
| 363 — workbenches | Parcial | Consumidores reais Linux; matriz Windows externa. |
| 368 — arraste/workbenches | Parcial | Input e workbenches Linux; repetir adaptadores no Windows depois. |
| 388 — bbox/depth clamp | Concluído nesta rodada | Clipping e clamp distinguidos em sonda CoinGL; contrato portátil continua no item 348. |
| 407 — mips/recursos | Local | Implementar células Linux com orçamento e rollback; outras APIs depois. |
| 452 — desempenho Windows | Externo | Exige host Windows e controle de clocks/latência. |
| 485 — igualdade janela/offscreen | Local | Isolar cidade em movimento e primeiro frame wgpu/OpenGL. |
| 550 — bordas AMD/Vulkan | Local | Snap comum e testemunhos P20/câmera com alpha/ownership isolados. |
| 560 — fontes GL no Coin base | Local | Inventariar símbolos/objetos restantes e remover em etapas com ABI verificada. |
| 562 — Vulkan no AVD | Condicionado | Verificar imagem/driver; Goldfish atual falha no submit. |
| 564 — Intel física | Externo | Este host não oferece essa GPU. |
| 565 — CoinGL, nove unidades | Externo | GPUs locais anunciam oito unidades de coordenadas. |
| 589 — DPI/TDR Windows | Externo | Exige monitores com DPI distinto e host Windows. |
| 592 — Metal/Wayland físico | Externo | Metal indisponível; compositor físico/escala fracionária requer outro perfil. |
| 603 — Android duas janelas | Local | Ampliar o AVD x86_64 e preservar zoom/RTT. |
| 611 — milhão no AVD | Local | Medir estados CPU, arraste e caudas de latência. |
| 614 — layout GPU compacto | Parcial | Diferença AMD/Vulkan/CoinGL aqui; Metal/D3D12 fora. |
| 616 — Android físico arm64 | Externo | Emulação x86_64 não substitui o dispositivo. |

## Primeiro controle executado: bounding box near/far

`CoinRenderBoundingBoxTest` agora inclui uma caixa girada com planos near/far
atravessando o centro, rejeição integral de cada lado e restauração A/B/A. O
controle compara CPU e GPU com CoinGL; as mudanças de imagem impedem que uma
cena constante aprove o ensaio. Em 96×80, a área visível foi 1728 pixels na
base, 1536 em cada corte e zero quando toda a caixa foi rejeitada. Máximo RGB
e MAE foram zero nas 12 comparações por backend. A regressão completa passou
com 143 comparações CoinGL em BGFX/OpenGL, BGFX/Vulkan e wgpu/Vulkan. Os
[recibos](validation/bbox-depth-20261010/) registram as saídas.

Este ensaio cobre clipping geométrico normal. A sonda separada
`--probe-gl-depth-clamp` habilitou `GL_DEPTH_CLAMP` apenas no CoinGL via
`SoCallback`, restaurou o estado após a caixa e comparou com a mesma cena sem
clamp. Near e far passaram de 1536 pixels sob clipping a 1728 sob clamp:
**192 pixels alterados em cada plano**
([log](validation/bbox-depth-20261010/coingl-depth-clamp.log)). Assim, o
clamp não pode ser inferido de um `SoDepthBuffer` com range limitado ou de
uma caixa que cruza o plano. O perfil atual de bounding box continua a usar
clipping geométrico; não há estado CoinRender portátil que solicite
`GL_DEPTH_CLAMP`. A implementação de tal estado, suas regras de rejeição e
a qualificação em formatos/drivers permanecem no item 348. Isso encerra o
**estudo específico da caixa**, não a funcionalidade geral de depth clamp.

Para reproduzir a sonda curta, compilar `CoinRenderBoundingBoxTest` em cada
build e executar `--capture --probe-depth-clip` e `--gpu --probe-depth-clip`.
As execuções GPU desta rodada usaram X11 `:0`, oráculo GLX NVIDIA com
`__NV_PRIME_RENDER_OFFLOAD=1`, `__GLX_VENDOR_LIBRARY_NAME=nvidia` e
`COIN_GLX_PIXMAP_DIRECT_RENDERING=1`; BGFX/OpenGL usou EGL Mesa AMD e as
rotas Vulkan usaram o ICD Radeon. A regressão completa foi executada sem a
opção de sonda; a sonda `--probe-gl-depth-clamp` usa somente CoinGL e não é um
gate de equivalência portátil.
