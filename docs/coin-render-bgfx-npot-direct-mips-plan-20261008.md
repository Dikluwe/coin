# Próxima implementação: mips NPOT de RTT direto no BGFX

## Contrato a preservar

O controle existente cria um produtor 3×5 não uniforme, com a última coluna
branca, e espera RGB `(85, 85, 85)` no mip final. `bgfx::AUTO_GEN_MIPS`
divergiu desse resultado; a admissão recusa NPOT antes de executar produtores.
Essa recusa continua ativa. A redução requerida é a média por área do
`CoinRenderTextureSamplingCore::generate`: para cada pixel do nível seguinte,
pesos de sobreposição cobrem todo o nível anterior, inclusive bordas ímpares.
RGBA16F deve preservar valores acima de 1; RGBA8 deve arredondar conforme o
oráculo. Não há readback/geração CPU na rota direta.

## Rota BGFX proposta

1. Preparar programa de redução para RGBA8 linear e RGBA16F. O shader recebe
   dimensões exatas de origem/destino e amostra texels do nível anterior com
   pesos de área; usar filtro pontual e coordenadas explícitas.
2. Renderizar cada nível em uma textura temporária, depois copiar para o mip
   correspondente da textura de saída. Não amostrar e escrever no mesmo objeto
   GL. A API `bgfx::Attachment` seleciona o mip do framebuffer e `bgfx::blit`
   faz a cópia GPU. Evitar `BGFX_ATTACHMENT_AUTO_GEN_MIPS` nesses passes.
3. Reservar até duas cadeias inferiores e uma base temporária dentro do
   orçamento de 64 MiB já cobrado pelo grafo. Validar `TEXTURE_FRAMEBUFFER` e
   destino de blit do formato antes da primeira submissão.
4. Planejar os views antes de alocar: o backend dispõe de 16 por alvo, usados
   também por sombras/transparência/readback. A cadeia 2048² pode ter 11
   reduções; se faltar espaço, dividir em frames internos com ordem explícita
   ou recusar atomicamente no preflight. Não ultrapassar o bloco de views.
5. Só publicar o token depois de todos os níveis e da checagem de falha BGFX;
   injetar falha de alocação/encoding e provar preservação de pixels, serial e
   possibilidade de recuperação.

## Gates de aceitação

- 3×5 não uniforme → mip final RGB 85 no AMD/Vulkan e AMD/OpenGL;
- outras dimensões ímpares, 1×N e N×1, com erro numérico delimitado pelo
  formato, comparadas ao oráculo CPU independente;
- RGBA16F acima de 1 sem clamp intermediário;
- cenário com sombras/transparência que exaure views recusa antes do produtor;
- falha sem publicação e frame seguinte recuperado;
- POT existente e replay de token sem regressão.

A `nvidia-smi` deste PC ainda retorna desencontro entre módulo e NVML 615.71;
a matriz NVIDIA fica para um ambiente com driver íntegro.
