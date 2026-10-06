# CoinRender: retomada integrada em 2026-10-06

Base de desenvolvimento: `codex/coin-render`, consolidada em `468b33fa71`.
Coin upstream incorporado: `674e74267d`, também a revisão da master local.
A integração preserva o histórico de BGFX, wgpu e do contrato comum.

## Correção comum de textura desligada

`SoComplexity.textureQuality = 0` desliga a textura mesmo quando a imagem
existe e as coordenadas são DEFAULT/FUNCTION. A captura genérica verificava
UV antes da qualidade; o caminho indexado repetia essa rejeição na action e
considerava somente os bytes da imagem ao definir `hasTexture`.

A qualidade agora é resolvida antes do contrato de UV. O builder entrega ao
Core a presença efetiva de textura, incluindo habilitação da unidade. A action
apenas fornece a geometria; a regra duplicada foi removida. Texturas ativas
com DEFAULT/FUNCTION continuam explicitamente fora do perfil atual.

`CoinRenderTextureTest` verifica os dois caminhos de captura, coordenadas default
e função, rejeição ao reativar a textura, preservação dos pixels e recuperação.
A regressão falhou antes do fechamento completo das guardas e passou após a
correção. A ausência de textura habilitada permite o caminho indexado sem
exigência de UV; nenhuma aceleração de tempo de quadro é alegada nesta rodada.

O teste offscreen também destruía targets emprestados antes de desvincular a
action. A troca subsequente acessava o target liberado em `detachedFromAction`.
O teste agora cumpre o contrato documentado de `setRenderTarget(NULL)` antes da
destruição, preservando as verificações de NOT_READY/OOM/DEVICE_LOST/recovery.

## Capacidades atuais e escopo

A action anterior `SoWgpuRenderAction` evoluiu para `CoinRenderAction`. Captura,
validação, composição e publicação são comuns; CoinBgfx/CoinWgpu implementam
recursos e submissão. A seleção de conector permanece no build. As quatro fases
de [isolamento](coin-render-isolation-roadmap.md) já foram implementadas.

| Capacidade | Estado atual | Qualificação/limites |
| --- | --- | --- |
| Malhas, bindings, materiais, luzes e fog | Captura comum, CPU/BGFX/wgpu | Matrizes ampliadas P05/P06 continuam abertas. |
| Clipping, draw styles, depth e viewport | Contratos e execução existentes | Até oito planos; P02/P04 ainda têm perfis limitados. Viewport externo wgpu permanece aberto. |
| Multitextura, combinadores e strokes texturizados | Contrato comum com executores BGFX/wgpu | Perfil P08, oito unidades; UV procedural ativo e filtros gerais seguem P07. |
| Transparência Coin, anotações, peeling e weighted OIT | Planos comuns e execução BGFX/wgpu | Modos Coin P09; peeling 1..8, OIT extensão explícita; limites por alvo/driver. |
| Texto, imagens e marcadores | Captura comum implementada | Tipos nativos exatos, perfis Linux; qualificação Windows recente e subclasses continuam abertas. |
| UV projetivo e caixas de Complexity | Captura comum implementada | Contratos Linux próprios; texgen/multitextura de caixas ainda limitados. |
| Sombras | Executores BGFX/wgpu existentes | Perfil P27; qualificação GPU não implica oráculo CoinGL de oito mapas. |
| RTT, tickets, múltiplos alvos e recuperação | Contratos comuns e Infra por conector | Perfil RGBA8, staged/direto offscreen e orçamentos registrados. |
| FreeCAD | Integração e inventário existentes | Part/BRep qualificado; GL-only, subclasses e workbenches seguem P15/P16. |
| Shaders próprios, texturas 3D/cubo, MSAA/multipass | Contrato portátil ainda aberto | Rejeições explícitas dos efeitos ativos onde implementadas; não declarar suporte pelo hardware. |

A consulta pública V3 separa implementação, disponibilidade após probe e
qualificação delimitada. Sua máscara legada `features` não inventaria todos os
nós, nem certifica seu conteúdo visual. Consulte o [contrato de seleção](coin-render-selection-contract.md),
o [inventário](coin-render-node-inventory.md) e a [auditoria de lacunas](coin-render-parity-gaps-windows-20261005.md).
A ponte Rust privada está na revisão **45**; o perfil experimental foi corrigido.

## Próximas frentes das quatro prioridades

- Contrato comum/P07: UV procedural/default quando a textura está ativa,
  qualidade, filtros e formatos, com semântica definida antes dos shaders.
- BGFX/P02/P04: ampliar draw styles, viewport/depth e suas referências CoinGL;
  novas APIs/plataformas precisam de campanhas próprias.
- wgpu/P04/P07: viewport parcialmente externo e o mesmo contrato de textura;
  usar o builder/Core como dono da semântica.
- Desempenho: preservar os gates de reutilização/cache/instancing e medir o
  custo dominante em campanha isolada. Esta rodada não executa benchmark A/B
  de tempos enquanto builds e testes GPU concorrentes estão ativos.

Bugs confirmados do CoinGL seguem a [política de compatibilidade](coin-render-compatibility-policy.md);
nenhuma tolerância ou esperado visual foi relaxado para aprovar a integração.

## Validação

Os logs desta rodada ficam em [validation/upstream-20261006](validation/upstream-20261006).
Os resultados e limites finais são registrados após o término das campanhas.
