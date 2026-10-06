# CoinRender: prédios animados no Windows

Branch `codex/coin-render-transform-performance`, baseline `dd8483b987`.
Windows 10 x64, GTX 1060 6GB, driver 581.08. A identidade Coin/WGL foi
confirmada separadamente das medições com `COIN_DEBUG_GLGLUE=1`.

## Mudança compartilhada

`CoinRenderActionP::cameraSensorCB` consultava o ambiente a cada atualização
de material ou dimensão de Cube. Alterar largura, altura e profundidade de
8.000 prédios podia realizar 24.000 consultas ao ambiente por quadro.
No CRT Windows, esse trabalho pesava dentro da atualização da cena.

A notificação agora registra a mudança incondicionalmente. A admissão e a
preparação do overlay continuam lendo as opções por operação. Não há cache
permanente de configuração: desligar o overlay entre notificação e `apply`
força captura completa; religá-lo antes de `apply` permite usar a prova válida.
As verificações de ownership, campos conectados/ignorados, valores atuais,
limites, invalidação e rollback permanecem.

O teste existente de optout foi ampliado para material e Cube nos três casos:
desligado durante todo o ciclo, desligado somente na notificação e desligado
somente no apply. Verifica a rota pela geração de captura e compara todo o
payload de objetos com uma action de captura completa independente. Inclui
as três dimensões alteradas entre submissões. O lowering e a Infra dos dois
backends não foram modificados, assim como a API/ABI pública do Coin.

## Prédios animados: três rodadas OpenGL

Cidade com 40.000 prédios, geometria de 20% deles alterada a cada quadro,
768 × 768, passo lógico 3, cinco warmups e 30 quadros medidos por processo.
Antes/depois e CoinGL executados em processos novos, ordem intercalada,
sem builds ou outros testes GPU concorrentes. Readback síncrono de cor e
cópia de publicação em todos os caminhos. Três PPMs por processo, cuja
gravação fica fora dos timers. Tracing foi medido separadamente.

| Variante | Total antes → depois ms | Redução | Atualização antes → depois ms |
| --- | ---: | ---: | ---: |
| BGFX/OpenGL | 239,95 → 169,71 | 29,3% | 100,35 → 22,77 |
| wgpu/OpenGL | 274,17 → 185,53 | 32,3% | 105,20 → 22,16 |
| Coin/OpenGL, controle | 206,73 | — | 25,61 |

São medianas das três medianas por processo. Na amostra, BGFX/OpenGL ficou
17,9% abaixo do CoinGL no total. O ganho da atualização BGFX é 77,3%.
O total ainda equivale a aproximadamente seis quadros por segundo no perfil
offscreen com readback; não demonstra 30/60 FPS ou fluidez de janela.

| Variante | Faixa das medianas antes ms | Faixa depois ms |
| --- | ---: | ---: |
| BGFX/OpenGL | 229,96..268,02 | 151,48..178,55 |
| wgpu/OpenGL | 270,39..293,95 | 163,31..289,03 |
| CoinGL | 188,66..223,23 | — |

A primeira rodada wgpu/OpenGL aumentou de 270,39 para 289,03 ms (+6,9%),
apesar da redução de atualização. Seu render teve picos acima de um segundo;
esses dados permanecem nos logs/CSV. A mediana agregada não estabelece
ausência universal de regressão ou significância estatística. Clocks,
temperatura e carga externa não foram acompanhados continuamente.

## Outras APIs: uma rodada de verificação

Mesmo caso e escopo, N=1 por revisão/API; esta tabela é complementar e não
tem a repetição da campanha OpenGL.

| Variante | Total antes → depois ms |
| --- | ---: |
| BGFX/Vulkan | 270,59 → 180,43 |
| BGFX/D3D12 | 255,63 → 176,06 |
| wgpu/Vulkan | 286,55 → 194,74 |
| wgpu/D3D12 | 281,35 → 175,11 |

## Controles BGFX/OpenGL

Três rodadas por revisão/modo, cinco warmups e dez medidos, 20% animados:

| Modo | Antes → depois ms |
| --- | ---: |
| Estático | 6,53 → 7,85 |
| Câmera | 16,75 → 20,05 |
| Transformações | 114,60 → 115,40 |
| Materiais | 172,02 → 146,51 |

Os aumentos das primeiras amostras são preservados. Estático e câmera não
executam os ramos de notificação alterados; ainda assim, foram repetidos com
30 warmups e 120 medidos, em três rodadas intercaladas por revisão:

| Controle longo | Antes → depois ms |
| --- | ---: |
| Estático | 5,99 → 5,88 |
| Câmera | 16,38 → 16,23 |

Esses controles ficaram próximos. Os dados de todas as rodadas, inclusive
pequenos aumentos individuais, permanecem no resumo. Não foram substituídas
nem descartadas as amostras curtas.

## Custo que permanece

A sonda com tracing confirmou reuso do plano (`resource_rebuild`) sem
recaptura completa, 192.216 vértices CPU e 40.001 draws de origem. No BGFX,
eles viram 24 vértices, 40.001 instâncias e um draw GPU.

No último quadro dessa sonda: preparação/plano na action ≈23 ms, validação
no target ≈38 ms, lowering BGFX ≈52 ms e espera/readback ≈19 ms. A validação
de colisões de posições consome ≈5 ms dentro das fases comuns. As fases são
aninhadas e uma sonda não é campanha estatística; não somar medianas nem
tratar espera/readback como timestamp GPU isolado. O próximo custo grande é
revalidar/compor/empacotar o plano após a atualização de geometria.

## Imagens, testes e instalação

- **84 pares RGB antes/depois são exatamente iguais**, em geometria nas seis
  combinações de backend/API e nos quatro controles BGFX/OpenGL.
- **30 pares de geometria contra CoinGL** têm estados animados correspondentes.
  Maior MAE RGB: **0,022985** na escala 0–255; maior erro de canal isolado: **122**;
  no máximo **3 pixels de 589.824** com erro de canal acima de 3 por quadro.
  Não há igualdade bit a bit entre renderers nem promessa de paridade geral.
- **105 execuções CTest passaram, zero falhas e skips**: 17 casos BGFX e 18
  wgpu, três APIs por build. Incluem a action, reuse, câmera com referência
  CoinGL obrigatória, geometria indexada, textura, luzes, Gouraud, fog,
  clipping, composição, fronteiras e inventário de nós. O caso adicional wgpu
  é annotation. O gate BGFX de clipping fixa Vulkan nas propriedades CTest;
  os gates de câmera usam a API selecionada. Não se afirma suíte integral.
- O inventário reproduz a ausência de texto/textura de imagem e as rejeições
  de efeitos. Testar que uma lacuna foi caracterizada não implementa o recurso.
- Instalações `build/coin-render-bgfx-install` e `build/coin-render-install`
  atualizadas; hashes instalados conferidos contra os builds qualificados.

A campanha tem **60 processos de medição/sonda, 2.374 quadros medidos e 600
warmups**. Diagnóstico GL, gates e o teste direto de action ficam separados.
Uma tentativa de controle usou `--animation none`, rejeitado pelo parser;
foi corrigido para `static`, preservando registro/log da tentativa, sem
repetir os processos válidos. Os scripts retomam labels já concluídos.

O GIF anterior tem reprodução fixa a 20 FPS e mantém o resultado visual;
essa velocidade não é a velocidade de renderização medida. O ganho numérico
Windows não deve ser extrapolado para o custo de `getenv` no Linux.

## Reprodução e pendências funcionais

[Matriz atual de lacunas de paridade](coin-render-parity-gaps-windows-20261005.md).
Prioridades: `SoText2`, `SoImage` e adaptações GL-only; em seguida UV/qualidade
de textura, shader portátil, volume/cube map e perfis ampliados.

[Scripts, comandos, CSVs, logs, hashes, gates e resumo](validation/animated-buildings-windows-20261005).
PPMs e cópias dos binários permanecem no diretório homônimo em `build`;
hashes dos RGB e dos executáveis/bibliotecas constam da evidência.
