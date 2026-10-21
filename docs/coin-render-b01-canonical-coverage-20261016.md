# B01: cobertura canônica explícita e qualificação de bordas

Continuação final: [B01 corrigido e qualificado no Linux disponível](coin-render-b01-completion-20261016.md), incluindo clipping/depth, instancing, janelas e sombras. Os limites e estados abaixo são os desta campanha anterior.

Entrega de 16/10/2026 em `codex/coin-render`, sobre `24ddf557af4c8956b008d51a53446f4671f33a3b`, com alterações locais identificadas no [manifesto](validation/coin-render-b01-20261016/manifest.json). **B01 permanece parcial:** o novo perfil portátil está corrigido e qualificado no domínio abaixo; o gate CoinGL nativo AMD/Vulkan conserva sua falha conhecida. [Matriz atual](coin-render-feature-matrix.md).

Continuação: a [P20 portátil completa disponível](coin-render-b01-p20-portable-20261016.md) foi qualificada em 24 células CoinRender AMD/NVIDIA, incluindo janelas X11 e transparência. Os limites abaixo descrevem esta primeira campanha B01.1.

## Contrato e correção

CoinRender oferece agora duas escolhas explícitas em `CoinRenderOptions.rasterizationPolicy`: `COIN_RENDER_RASTER_NATIVE` (padrão existente) e `COIN_RENDER_RASTER_CANONICAL_8BIT`. A segunda fixa os vértices projetados em coordenadas ímpares de 1/256 de pixel, em espaço de janela Coin bottom-left:

```text
Q(x) = (floor(128*x) + 0.5) / 128
```

O passo é **1/128 de pixel**, com fase de **1/256**; o deslocamento máximo por eixo é **1/256 de pixel**. O nome 8BIT indica a representação de coordenadas, não um passo de 1/256. Centros de pixel têm coordenadas pares nessa representação; uma aresta horizontal ou vertical entre vértices quantizados não passa exatamente por esses centros. O rasterizador continua sendo o da GPU. Isso não elimina todos os empates diagonais nem equivale a rasterização analítica.

A implementação preserva Z/W dos vértices e reconstrói apenas XY. BGFX e wgpu aplicam a mesma regra nos shaders de superfície, incluindo a rota de instâncias. A CPU explicita os estágios model-view e projeção no perfil canônico, sem alterar o cálculo nativo anterior. Passes fullscreen de composição/conversão BGFX ficam fora do snap. Produtores RTT herdam a política e usam suas próprias dimensões. A política integra a identidade do plano e é conferida contra a configuração imutável do alvo.

Exemplo C++ do perfil qualificado offscreen:

```cpp
CoinRenderOptions options;
options.renderer = COIN_RENDER_RENDERER_VULKAN; // ou OPENGL, conforme o executor
options.rasterizationPolicy = COIN_RENDER_RASTER_CANONICAL_8BIT;
std::unique_ptr<CoinRenderTarget> target(
    CoinRenderTarget::createOffscreen(SbVec2i32(256, 256), options));
```

Para solicitar uma API específica, preencha renderer; não deduza a API a partir de UNKNOWN ou de uma sonda anterior. Os controles finais conferem as submissões efetivas. O caminho nativo anterior continua disponível. A estrutura experimental C++ ganhou um campo; consumidores/SDK precisam ser reconstruídos. O protocolo privado C++/Rust passou de **51 para 52**, com frame de 464 bytes e verificação de versão, sem reutilizar campos reservados.

## Investigação reproduzida

O fixture original de câmera confirmou a falha AMD/Vulkan no frame 5: MAE **0.149582**, máximo RGB **172**, **49** pixels >3. Os controles AMD/OpenGL e NVIDIA Vulkan/OpenGL passaram nos limites antigos. O [registro histórico](</home/dikluwe/Área de trabalho/Estudo coin/consolidacao-frentes-20261016/renderer-history/docs/validation/perspective-coverage-20261012/summary.md>) também mostra que o próprio CoinGL AMD e NVIDIA escolhe lados opostos em 44 pixels da coluna 195. Uma única imagem portátil não pode igualar simultaneamente essas duas referências nesse conjunto.

O primeiro snap simples em 1/256 expôs outra diferença: no contexto OpenGL testado, as bordas horizontais exatamente nos centros selecionavam a linha inferior; no contrato CPU/Vulkan usado, selecionavam a superior. O teste inteiro independente detectou 32 pixels errados no primeiro retângulo OpenGL. A primeira sequência de câmera canônica também localizou um resíduo ortográfico no frame 7. A grade ímpar evita esses empates horizontais/verticais e os sete controles de câmera agora têm **coverage_xor=0 em todos os 18 frames**. Não foi alterado nenhum limite para aceitar os protótipos.

A especificação de [rasterização Vulkan](https://docs.vulkan.org/spec/latest/chapters/primsrast.html#primsrast-polygons-basic) e a extensão [OpenGL ARB_clip_control](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_clip_control.txt) documentam critérios de cobertura/origem. Os logs locais demonstram os casos desta entrega; não são uma declaração de defeito ou inconformidade dos drivers.

## Validação final

Linux x86_64, AMD Renoir `1002:1638`/Mesa `25.2.8-0ubuntu0.24.04.4` e NVIDIA RTX 3060 Laptop `10de:2560`/driver `615.71.09`. Builds Release em RAM, dois jobs totais e GPU serial. [Comandos](validation/coin-render-b01-20261016/commands.txt), [runner](validation/coin-render-b01-20261016/run_cell.py), [resultados estruturados](validation/coin-render-b01-20261016/summary.json).

| Executor / GPU / API | Imagem e recuperação | Câmera contra máscara CPU | Maior erro RGBA RTT staged/direct |
| --- | --- | --- | --- |
| BGFX / AMD / Vulkan | PASS, 25 casos | PASS, 18 frames, XOR 0 | 0 / 0 |
| BGFX / AMD / OpenGL | PASS, 25 casos | PASS, 18 frames, XOR 0 | 0 / 0 |
| BGFX / NVIDIA / Vulkan | PASS, 25 casos | PASS, 18 frames, XOR 0 | 0 / 1 |
| BGFX / NVIDIA / OpenGL | PASS, 25 casos | PASS, 18 frames, XOR 0 | 0 / 0 |
| wgpu / AMD / Vulkan | PASS, 25 casos | PASS, 18 frames, XOR 0 | 0 / 0 |
| wgpu / AMD / OpenGL | PASS, 25 casos | PASS, 18 frames, XOR 0 | 0 / 0 |
| wgpu / NVIDIA / Vulkan | PASS, 25 casos | PASS, 18 frames, XOR 0 | 0 / 1 |
| wgpu / NVIDIA / OpenGL | Indisponível, retorno 77 | Não qualificado | Não executado |

São **14/14 processos canônicos aprovados**, em sete células; a indisponibilidade não é contada como passe. Os logs `accepted-*` e seus JSONs identificam API efetivamente submetida, GPU, executável e bibliotecas por hash. OpenGL pode reportar device PCI zero; o nome do adaptador/contexto efetivo complementa o vendor. Não basta o nome da célula nem uma sonda anterior para provar a API utilizada.

O [teste novo](../testsuite/coinrender/CoinRenderCanonicalCoverageTest.cpp) calcula a cobertura esperada de retângulos em inteiros, a partir das coordenadas autorais, sem usar uma imagem GPU como esperado. Exercita centros de pixel e perturbações subpixel, a aresta `x=195.50177127`, ortográfica/perspectiva com W variável, alpha 0.5 sem dupla mistura na diagonal, depth variável, interpolação perspectiva de cor com cálculo analítico, resize e viewports parcialmente fora do alvo. RTT staged/direct é comparado a uma imagem autoral independente. Recusas por política incompatível, primitivas nativas e sombras preservam publicação e serial; a recuperação usa o mesmo alvo. O preflight de produtores RTT diretos também recusa os casos não qualificados.

O fixture de câmera mantém a comparação reuso/reconstrução e retorno à âncora. O novo modo canônico compara a máscara de cobertura à CPU, com **zero tolerância de pixels**. Os testes CoinGL originais permanecem separados, com seus limites anteriores: MAE ≤0.10 e até 131 pixels >3. Resultado nativo BGFX: **3 PASS e 1 FAIL conhecido AMD/Vulkan**, com os mesmos 49 pontos/MAE do controle inicial. Isso não certifica paridade nativa do perfil canônico.

Outros controles: **2/2 CPU/ABI** ([log](validation/coin-render-b01-20261016/accepted-cpu-abi.log), [JUnit](validation/coin-render-b01-20261016/accepted-cpu-abi.xml)), **4/4 Rust/Naga** de variantes/layout de shaders ([log](validation/coin-render-b01-20261016/rust-shader-profile-tests.log)) e **8/8 regressões B12/B02** BGFX AMD/NVIDIA × Vulkan/OpenGL, registradas em `b01-regression-*`. Os testes novos estão inscritos no CTest.

## Evidências intermediárias e limites

Os logs anteriores foram preservados: erros de compilação corrigidos, o snap simples que falhou, seleção incorreta de API nos primeiros fixtures com opções explícitas, e revisões repetidas no fixture raw após resize. UNKNOWN estava escolhendo Vulkan nesses fixtures; os controles finais usam a configuração solicitada e verificam todas as submissões. A revisão repetida confundia a identidade do payload no cache wgpu; o fixture agora fornece revisões únicas. Esses resultados intermediários não contam na qualificação final.

O perfil atual exige superfícies trianguladas **sem mapas de sombra**; sombras e linhas/pontos nativos têm recusa explícita, incluindo produtores RTT. Linhas/pontos já expandidos em triângulos não receberam qualificação específica. A correção atua nos vértices com W positivo acima de 1e-6 e NDC XY limitado a ±2; vértices fora desse domínio conservam o caminho anterior. Clipping geométrico extremo, empates diagonais adversariais, culling/degeneração, a campanha P20 original completa, mips/LOD, janelas, tickets, Windows/D3D12, Android e Metal permanecem abertos. Os casos alpha deste teste não encerram toda a campanha P20.

Não houve benchmark de mediana/p95/memória. O perfil não aumenta a resolução nem aplica 3× automaticamente; isso não comprova ganho de desempenho. A posição quantizada muda cobertura e interpolação, podendo afetar UV/LOD em outras cenas; B10 e C01/C09/C10 acompanham a ampliação. **B01.1 fecha o perfil delimitado; B01.2/B01.3 e B01 agregado continuam abertos.**
