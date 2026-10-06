# Evidências de animação — Linux, 2026-10-04

Relatório: [coin-render-animation-linux.md](../../coin-render-animation-linux.md).
Protocolo: [coin-render-animation-benchmark.md](../../coin-render-animation-benchmark.md).
Referência: CoinGL, `SoGLRenderAction`, no mesmo adaptador NVIDIA dos caminhos
BGFX/Vulkan, BGFX/OpenGL e wgpu/Vulkan.

## Identidade e conclusão

[manifest.json](manifest.json) reúne fonte, máquina, cena, parâmetros, testes,
contagens e hashes dos arquivos arquivados.
[campaign-complete.json](campaign-complete.json) registra a conclusão às
21:30:41 UTC de 2026-10-04: **72 processos principais, 43.200 quadros medidos,
4.320 warmup e oito diagnósticos**, sem falhas. Pilotos e capturas RGB são etapas
separadas dessas contagens. Os manifests, resultados e `medians.json` originais
foram preservados sem reescrever seus números ou revisões.

## Arquivos

- `offscreen/` e `window/`: campanhas principais completas, 36 processos por
  escopo, três rodadas, 60 warmup e 600 quadros medidos por processo. Cada pasta
  contém CSVs, logs, comandos/ambiente/hashes no manifest, resultados por processo,
  `medians.json` bruto e `campaign-integrity.json`.
- `offscreen-report-summary.json` e `window-report-summary.json`: resumos
  independentes dos CSVs. As contagens somam os três processos e os máximos são
  globais; esses campos diferem de `medians.json`, que usa a mediana de cada
  estatística por processo.
- `animation-timings.png`: figura das medianas e p99 dos dois escopos.
- `pilot-offscreen/`: sete casos, uma rodada, cinco warmup e 20 quadros medidos.
  Explora a cobertura; não qualifica estabilidade de desempenho.
- `controls/`: testes CPU do helper nos dois builds, diagnóstico CoinGL da GPU,
  tamanho real da janela e snapshot pontual de governor/frequência/temperatura
  CPU. Seus tempos não compõem as tabelas.
- `phase-diagnostics/`: oito processos curtos após a campanha, com
  `COIN_RENDER_TRACE_PHASES=1`. `commands.json` registra comandos/ambiente,
  `frozen-inputs.json` preserva fonte/cena/binários e `summary.json` contém todos
  os traces e registros parseados. Há logs e CSVs de quatro casos offscreen
  dinâmicos e quatro controles de janela estática, em 1024 e 512 pixels.
- `window/display-state-observation.txt`: três snapshots de display somente de
  leitura, com monitor ligado e timeouts screensaver/DPMS em zero. Não há registro
  contínuo do histórico do display.
- `verification/`: verificação separada **offscreen** de RGB e estados, sete
  casos, sete quadros lógicos (0, 100, 200, 300, 400, 500, 600), quatro variantes.
- `verification/images/`: 12 PNGs RGB sem perdas, frame lógico 300, nos casos
  static, camera e transforms-10. `rgb-comparison.png` é uma montagem para inspeção.

## Agregação reproduzível

Para cada processo, o runner calcula p95/p99 por nearest rank: índice
`ceil(n * p) - 1` na série ordenada. Usa limites estritos `t > 1000/60` e
`t > 1000/30`; os rótulos arredondados não alteram os limites.

As medianas, p95 e p99 do relatório são medianas das três estatísticas por
processo. Não são percentis de uma distribuição única com 1.800 amostras.
O resumo independente valida 660 linhas e a sequência lógica -60..599 de cada
processo, conserva 600 linhas medidas, soma violações dos limites nos 1.800
quadros e calcula o máximo sobre todos eles. `medians.json` bruto continua
registrando a mediana dos máximos e das contagens por processo.

Os scripts deste arquivo de evidências usam somente os dados locais:

```sh
python3 docs/validation/animation-linux/summarize.py
python3 docs/validation/animation-linux/plot.py
```

`summarize.py` é a versão portátil do helper usado para o resumo independente;
`plot.py` reproduz a figura com eixo até 1600 ms. São operações sobre arquivos,
sem renderização GPU. A figura usa mediana e p99 agregados; os máximos globais
ficam nos JSONs e nas tabelas do relatório.

## Verificação de imagens

`rgb.json` contém **196 comparações RGB** em resolução integral 1024 × 1024,
inclusive **49 autocontroles CoinGL**. `motion.json` registra sete estados e
imagens distintos nos seis modos dinâmicos e um no estático. Os estados
correspondem exatamente entre variantes. `image-manifest.json` preserva os hashes
dos 196 PPMs originais. MAE máxima: 0,028090476989746094 por canal na escala 0–255;
maior erro de canal: 150; maior quantidade de pixels com erro >3: 13 de 1.048.576.
O analisador registra os erros RGB e verifica estados/movimento; não impõe um
limiar automático de aceitação RGB.

Os PPMs completos são arquivos temporários da campanha em
`/tmp/coin-render-animation-verify/images`; esta pasta versiona somente os 12 PNGs
representativos. Para repetir todos os cálculos com o analisador, regenere os PPMs
com o modo verify do runner, conforme o protocolo. O analisador não usa a montagem
ou imagens redimensionadas para decidir as métricas.

Os manifests de piloto/verificação registram o HEAD Git presente antes do commit
da implementação (`f710a3be333ad0f836ab5afc5cf7e57fa1919751`). O conteúdo compilado
corresponde à implementação consolidada em `0cc3caffa7dad661557ed2f946b18c8f62a473a8`;
os hashes dos binários e bibliotecas são iguais aos da campanha principal. As
revisões e hashes originais foram preservados.

## Escopos e intermitência da janela

Offscreen mede atualização + render com readback síncrono + cópia RGBA. Janela
mede atualização + chamada de render/apresentação; eventos são registrados à parte.
O campo publication da janela é não aplicável: o zero no `medians.json` não é uma
medição de cópia. Os tempos de janela não são timestamps GPU nem latência até a
tela. CoinGL usa `glFinish` final; os backends nativos podem deixar fila pendente.
Seus throughput scopes estão preservados nos resultados.

Todas as janelas principais tiveram drawable solicitado e real 1024 × 1024.
Na workarea 1920 × 1040, a origem (50, 32) deixa 16 pixels de altura fora dessa
área. Os controles 512 × 512 ficaram integralmente dentro dela.

As esperas próximas de um segundo das duas primeiras rodadas estáticas Vulkan
não reapareceram na terceira rodada nem nos quatro traces estáticos posteriores.
Os traces confirmaram `plan_cache_hit=1`, e BGFX também `resource_cache_hit=1`,
em 1024 e 512. Como nem o controle 1024 reproduziu a anomalia, o controle 512 não
identifica causalmente a influência da área fora da workarea. A origem dos picos
continua não identificada. Todas as rodadas e suas caudas foram mantidas.
