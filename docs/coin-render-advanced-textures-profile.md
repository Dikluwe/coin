# CoinRender — texturas avançadas, perfil de 2026-10-07

Continuação P07/P24 na branch `codex/coin-render`. Este documento amplia o
[primeiro perfil](coin-render-p07-sampling-rtt-contract.md). As campanhas anteriores
continuam descrevendo seus próprios commits; suas rejeições NPOT/anisotropia e
protocolos 49 não são os limites da implementação atual.

## Contrato e limites

| Recurso | Captura/Core/CPU | wgpu | BGFX |
|---|---|---|---|
| NPOT 2D | Extensões originais, sem rescaling implícito; cadeia completa até 1×1 | Upload nativo | Upload nativo |
| SRGB | Opção explícita do alvo; RGB decodificado antes de filtrar, alpha linear | RGBA8/BC3 SRGB nativos | Flags SRGB e admissão do formato nativo |
| Anisotropia | q>0,85; referência CPU por eixo principal da footprint | Limites 1/2/4/8/16, com capacidade do adaptador | 1 ou máximo nativo selecionado pela opção 16; fatores 2/4/8 recusados |
| Compressão | `SoTexture2.enableCompressedTexture` produz BC3 determinístico; referência CPU decodifica | Requer BC habilitado no dispositivo | Requer formato BC3 nativo |
| HDR | Snapshot RGBA16F privado; consumidor CPU preserva RGB antes da composição | Produtor `SoSceneTexture2::RGBA16F` direto | Mesmo produtor direto |
| Mipmaps RTT diretos | Solicitação recolhida de todos os consumidores, antes de submeter produtores | Redução por área no GPU; isolamento GPU de fontes em OpenGL | Geração automática nativa, somente POT |

Os limites existentes permanecem: oito unidades, imagem armazenada até 8192 por
eixo e 128 MiB por cadeia, RTT até 2048 por eixo, profundidade oito e orçamento
conservador de 64 MiB por apply. BC3 exige base múltipla de quatro nos dois eixos;
níveis menores são blocos preenchidos repetindo os texels da borda. Uma base
12×20 é permitida; 3×5 comprimida é recusada. Erro não publica frame parcial.

O produtor HDR só aceita RGBA16F nesta extensão. RGBA32F, depth/stencil como
imagem, publicação HDR do alvo final, tone mapping e HDR em staged ficam fora do
perfil. Staged HDR é recusado antes de qualquer produtor; não é convertido para
RGBA8. Valores half armazenados precisam ser finitos. A conversão binary16 usa
arredondamento para o par; clear HDR também rejeita NaN/Inf e valores acima da
faixa finita ±65504. O consumidor aplica o modelo/material antes da quantização
RGBA8 final. Não se certifica iluminação HDR arbitrária nem ausência de overflow
em todas as expressões de shader possíveis.

## Interpretação de cor e qualidade

`CoinRenderOptions.storedTextureColorSpace = COIN_RENDER_TEXTURE_SRGB` é uma
política imutável explícita do alvo para as imagens `SoTexture2` armazenadas.
O padrão continua linear. Não foi acrescentado um campo ao nó Coin nem feita
uma inferência a partir do nome/extensão do arquivo. Os produtores RTT são
lineares, RGBA8 ou RGBA16F, independentemente dessa opção.

Qualidade aceita [0,1], com zero desligando a textura. Mantêm-se os limiares
anteriores: nearest abaixo de 0,2; linear abaixo de 0,5;
nearest-mipmap-linear abaixo de 0,8; trilinear a partir de 0,8. Acima de 0,85,
`maxTextureAnisotropy` seleciona um limite potência de dois entre 1 e 16. No
RTT, preserva-se a fronteira Coin: mipmaps somente acima de 0,5. Configurações
legadas dos limiares fora dos valores qualificados continuam recusadas.

A referência anisotrópica CPU estima os eixos principal e secundário das
coordenadas por pixel, limita a razão e amostra ao longo do eixo principal.
Isso serve de referência portátil de comportamento; a posição e o número
exato de taps do hardware não têm promessa de igualdade bit a bit. O teste
usa faixas direcionais: isotropia deve borrar, anisotropia deve preservar
contraste. A [API wgpu exige filtros lineares com anisotropia maior que um](https://docs.rs/wgpu/24.0.5/wgpu/type.SamplerDescriptor.html).
BGFX usa [flags anisotrópicos e reset MAXANISOTROPY](https://bkaradzic.github.io/bgfx/bgfx.html);
a opção 16 significa máximo nativo do driver, não uma certificação de 16 taps.

## Mipmaps e RTT

POT RGBA8 linear conserva o box Coin anterior, incluindo arredondamento 2D e
truncamento no eixo degenerado. NPOT usa área exata para incluir bordas ímpares.
SRGB é convertido para linear antes da média e codificado de volta depois;
alpha nunca recebe gamma. RGBA16F conserva sua faixa. BC3 é codificado depois
de preparar cada nível, sobre bytes SRGB já codificados quando aplicável.

Controles independentes: preto/branco SRGB reduz para RGB 188 e alpha 128;
preto/preto/branco 3×1 linear reduz para 85; half 2/4 reduz para 3, sem clamp.
A compressão é uma primeira implementação de min/max de blocos, com perdas;
não é um encoder de qualidade comparável a codecs de produção. ETC2, ASTC,
BC1/BC5/BC7 e importação de contêineres comprimidos permanecem pendentes.

O grafo RTT recolhe a necessidade de mips em consumidores raiz e aninhados.
A admissão de formatos/capacidades de todo o grafo ocorre antes de submeter o
primeiro produtor. Formato e presença de cadeia fazem parte dos metadados do
token; owner, device, geração, tamanho e aposentadoria mantêm o contrato anterior.
SRGB/formato e anisotropia participam da deduplicação/cache/batching.
Samplers de base e de mips podem compartilhar a mesma imagem em Vulkan e wgpu.
No BGFX/OpenGL, essa combinação é recusada por frame: a API desta integração
muda o intervalo de mips do objeto, contaminando o outro sampler. O controle
espera azul 128 e encontrou erro máximo 64 antes da admissão explícita.
A implementação futura precisa de views/recursos independentes; não se muda
silenciosamente o filtro escolhido.

wgpu produz a cadeia com passes compute. Em OpenGL, usa uma textura de fonte
isolada por redução e cópias dentro do GPU para evitar alias de views/nível base.
Não há map, readback de imagem ou geração CPU nessa rota. O orçamento direto
reserva, além de cor/depth, duas cadeias inferiores e uma base adicional por
solicitação de consumidor, conservadoramente cobrindo as fontes temporárias.
APIs com views nativas podem gastar menos. HDR 2048² com mips, por exemplo,
ultrapassa 64 MiB com essa reserva e é recusado antes da execução.

BGFX usa `AUTO_GEN_MIPS` para POT. O diagnóstico de um produtor 3×5 cuja última
coluna é branca encontrou erro máximo RGB 85 no último mip nativo; o oráculo de área exata espera 85.
A diferença não foi mascarada aumentando tolerância: NPOT com mips diretos
BGFX é recusado antes dos produtores. NPOT armazenado, staged e RTT base são
suportados. A implementação de redução por área no GPU BGFX é melhoria futura.

## Compatibilidade e próximos passos

`SoGLImage::createGLDisplayList` aceita NPOT diretamente quando o driver oferece
suporte. O rescaling POT de `resizeImage` pertence às rotas legadas/sem essa
capacidade. A antiga explicação que atribuía rescaling a todo CoinGL NPOT fica
corrigida. Este perfil não reproduz políticas legadas de escala nem configurações
`SoTextureScalePolicy` alternativas.

O protocolo privado C++/Rust passa para **50**, com FrameView de **456 bytes**.
Layouts de textura/sampler ficam iguais; novos códigos e bits transportam
formatos, cadeia e anisotropia. A ABI pública de libCoin não é alterada.
Os novos feature bits indicam implementação compilada, não qualificação automática
do hardware. A preparação/admissão do adaptador pode recusar um recurso.

Permanecem como frentes: codec de melhor qualidade e formatos móveis;
redução NPOT direta BGFX; samplers de base/mips compartilhados BGFX/OpenGL; políticas legadas de escala; saída HDR/tone mapping;
qualificação Windows/D3D12/Metal/Android e cruzamentos adicionais FreeCAD.
Resultados desta máquina e falhas preservadas constam no
[relatório de validação](coin-render-advanced-textures-validation-20261007.md).
