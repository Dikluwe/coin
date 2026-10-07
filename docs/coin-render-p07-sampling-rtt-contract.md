# P07: fechamento do primeiro contrato comum de texturas

Rodada de 2026-10-06 em `codex/coin-render`, após `b8fba0e530`. Fecha os três
itens restantes do **primeiro perfil P07**, com os limites abaixo. Não declara
suporte geral a todos os formatos, filtros, produtores ou plataformas.

## Qualidade e filtros de imagens armazenadas

O Wiring lê a qualidade efetiva do Coin. O Core decide o filtro e gera os
níveis; os executores recebem snapshots completos, sem callbacks/estado Coin.
A imagem `SoTexture2` ativa exige dimensões potência de dois em 1..8192,
componentes **L, LA, RGB ou RGBA de 8 bits**, expandidos para RGBA8Unorm linear.

| Qualidade efetiva | Magnificação | Minificação | Níveis |
| --- | --- | --- | --- |
| 0 | Desligada | Desligada | Sem captura/upload da imagem |
| 0 < q < 0,2 | NEAREST | NEAREST | Base |
| 0,2 ≤ q < 0,5 | LINEAR | LINEAR | Base |
| 0,5 ≤ q < 0,8 | LINEAR | NEAREST_MIPMAP_LINEAR | Cadeia completa |
| 0,8 ≤ q ≤ 0,85 | LINEAR | LINEAR_MIPMAP_LINEAR | Cadeia completa |

Essas fronteiras seguem `SoGLImage::applyFilter`/`shouldCreateMipmap` e os
limites padrão do Coin. A antiga faixa aproximada 0,45..0,55 foi removida:
qualidade 0,5 agora captura mipmaps de imagens armazenadas. Valores não finitos,
qualidade acima de 0,85 e overrides dos quatro limites `COIN_TEX2_*_LIMIT`
fora dos valores padrão retornam UNSUPPORTED quando a textura está ativa.
A anisotropia automática do Coin começa acima de 0,85 e fica fora deste perfil.
As variáveis de configuração devem ser definidas antes de iniciar a aplicação;
não há promessa de reconfiguração dinâmica de caches nativos por ambiente.

O primeiro perfil rejeita imagens `SoTexture2` NPOT: o CoinGL as redimensiona
implicitamente, e copiar bytes sem esse contrato não garante equivalência.
Não há fallback silencioso para outro filtro/tamanho. Essa restrição não altera
SoImage, glifos, marcadores ou o transporte manual de RGBA8 base do FramePlan.
SRGB, float/HDR, compressão, anisotropia, políticas alternativas de escala,
3D e cube maps ficam para um perfil posterior.

## Cadeia mecânica no Core

`CoinRenderTextureSamplingCore` gera todos os níveis até 1×1, com box de quatro
amostras e arredondamento inteiro `(soma+2)/4`; dimensões reduzidas a uma linha
usam duas amostras e truncamento. O esperado é independente da API/driver e
conserva a regra do `fast_mipmap` Coin. A imagem mantém os bytes base separados
dos níveis inferiores; o limite é **128 MiB por imagem, incluindo toda a cadeia**.
Esse limite não mede o pico total de memória do frame/driver.

Validação, deduplicação e reuso incluem presença/bytes de todos os níveis.
Um digest base igual não autoriza reutilizar níveis inferiores diferentes.
BGFX sobe a cadeia completa nas rotas normal e RTT direto. Wgpu recebe os bytes
concatenados e sobe cada nível, sem gerar semântica de mipmaps no Rust.

O CPU calcula footprints nas quads de fragmentos 2×2, após interpolação e
projeção ST/Q, e interpola os níveis adjacentes. O gate cobre minificação,
textura checker, transformações projetivas e os dois filtros de mipmaps.
Não certifica igualdade em todas as bordas/junções de raster ou todos os
footprints extremos de todos os drivers.

O protocolo **privado Rust é 47**, mantendo tamanhos/offsets das structs. O
formato 2 transporta cadeia RGBA8 POT completa; formato 0 continua base e 1
continua token RTT. Filtros 2/3 representam os dois novos modos. Transporte
malformado, índices inválidos e pedido de mipmaps sem cadeia são rejeitados.
A capacidade compilada acrescenta `COIN_RENDER_FEATURE_TEXTURE_MIPMAPS`, bit 12;
V1/V2/V3 permanecem com os mesmos tamanhos/versões.

## Modelos, unidades, desligamento e publicação

MODULATE, REPLACE e BLEND cobrem L/LA/RGB/RGBA. DECAL cobre RGB/RGBA;
**DECAL com L/LA é rejeitado** porque a operação legada não define um esperado
portátil. REPLACE de L/RGB conserva o alpha anterior, também em unidades
adicionais; LA/RGBA fornecem seu alpha. Wraps REPEAT/CLAMP mantêm o contrato
existente de repeat/clamp-to-edge.

Até oito unidades conservam filtros, imagens, coordenadas, matrizes e operações
independentes. O novo gate usa unidades 0/3 e os quatro modelos; os gates de
multitextura, combines, UV procedural e alpha existentes cobrem unidades
esparsas, oito unidades e os demais modos do perfil. As limitações de estilos
curvos/coincidentes permanecem em P02, sem serem promovidas a equivalência GL.

Qualidade zero e imagem vazia desligam a unidade. Reativação recaptura o estado
correto. Qualidade inválida, DECAL L/LA, NPOT e configuração fora do perfil
preservam imagem/serial do último sucesso e não submetem o frame inválido.
Depois de corrigir a entrada, a mesma action/target volta a renderizar.

A captura identifica bytes autorais de `SoTexture2` para permitir uma imagem
branca L 2×2 real; ela não é confundida com o dummy estático de filename pendente.
Arquivos ausentes/pendentes continuam rejeitados. A leitura do campo não faz
travessia extra nem altera override/PRUNE do node.

## Matriz do produtor RTT: diagnóstico antes da escolha

A fixture testemunha o estado Coin, a matriz GL física, o framebuffer ativo e
os pixels. No código anterior, com consumidor transladado em S por 0,25:

| Rota nativa | Framebuffer do produtor | Matriz do produtor | Pixel do consumidor |
| --- | --- | --- | --- |
| FBO | 1 | Tx=0,25 herdado | (0,204,0), verde |
| pbuffer | 0 | Identidade | (204,0,0), vermelho |

FBO fazia push/reset parcial no mesmo estado; pbuffer criava outra action.
Escolhemos **matrizes inicialmente identidade em todos os units do produtor**,
como na action independente do pbuffer e na captura Core existente. Matrizes
escritas na subcena são aplicadas a partir dessa identidade. Ao sair, todas as
matrizes do consumidor são restauradas e aplicadas somente à amostragem do
resultado. A correção nativa do FBO usa o próprio elemento Coin e seu pop.
Não copia nem multiplica a matriz do consumidor no plano filho.

A fixture verifica unidades 0/3, matriz própria, dois níveis de RTT e restauração
em CPU/captura, execução staged/direta BGFX/wgpu e CoinGL FBO/pbuffer. A
referência exige a rota efetiva pelo framebuffer, não aceita trocar de mecanismo
silenciosamente. Isso fecha a matriz de textura deste perfil, não a herança de
todos os outros estados possíveis de uma subcena.

## Fronteira de filtro RTT

A auditoria também mediu, em qualidade 0,5, **FBO LINEAR (9729), sem nível 1**,
e **pbuffer NEAREST_MIPMAP_LINEAR (9986), nível 1 com largura 16**. A causa era
reutilizar no readback pbuffer o limiar `>=0,5` de imagens armazenadas, enquanto
`SoSceneTexture2::shouldCreateMipmap` usa `>0,5` no FBO.

O pbuffer agora força linear/base através de 0,5. O contrato comum RTT é:
qualidade 0 desliga; **0 < q ≤ 0,5 usa linear/base**; q > 0,5 é UNSUPPORTED.
A fixture mede filtro real e ausência do nível 1 em q=0,1/0,3/0,5, nas duas
rotas, além das matrizes e dos pixels. Os produtores podem usar imagens
armazenadas com seus próprios mipmaps; o **resultado RTT** continua sem mipmaps.

Os limites RTT anteriores permanecem: RGBA8, unidade 0, MODULATE e políticas
NONE/ALPHA_BLEND/ALPHA_TEST do perfil P13. Mipmaps do resultado RTT, formatos
ampliados, outras unidades/modelos e qualificação Windows/FreeCAD não são
fechados por este incremento.

## Gates e evidência

`CoinRenderTextureSamplingTest` executa **231 cenas e seis rejeições por
processo**, com filtros/formatos/modelos, unidades 0/3, minificação, UV projetivo,
desligamento/reativação e recuperação. Na execução GPU, cada sucesso exige
CPU/GPU/CoinGL; o limite é MAE ≤ 1,5 e máximo ≤ 4 RGB em regiões interiores
fixas, sem relaxamento após falha. Checker minificado e cadeia 2×2 têm esperados
numéricos independentes. A configuração de ambiente fora do perfil tem gate
separado. O gate FFI verifica bytes próprios e mutação dos níveis inferiores;
Rust verifica layouts, tamanhos e limites sem gerar a cadeia.

`CoinRenderRttMatrixTest` verifica nove combinações por rota: três qualidades ×
matriz default/própria/RTT aninhado. Os gates de clipping, geometria, Gouraud,
alpha, shadows, RTT/publicação, callbacks e demais regressões permanecem na
campanha integrada. Logs, comandos, diagnóstico anterior e hashes estão em
[validation/p07-closure-20261006/summary.json](validation/p07-closure-20261006/summary.json).

A campanha final Release passou em **261 testes BGFX** e **197 testes wgpu**,
com dois skips conhecidos em cada configuração e nenhuma falha. Também passaram
40 testes Rust/shaders e dez gates Recording. O primeiro full BGFX registrou
um timeout de estilos durante outro build; o gate passou isoladamente em 11,01 s
e a campanha completa final passou com o timeout original de 60 s. Os tempos
registrados são diagnósticos, não benchmarks.

## Continuação: mipmaps staged de RTT

A [entrega Linux de nós/recursos](coin-render-linux-nodes-resources-closure.md)
amplia o resultado RTT para trilinear em `0,5 < q ≤ 0,85`, com cadeia RGBA8 POT
preparada no Core e orçamento do grafo. O limiar estrito RTT continua diferente
do limiar de imagens armazenadas. Mips diretos GPU permanecem recusados antes
do submit; a ampliação não modifica o contrato de matriz herdada acima.
