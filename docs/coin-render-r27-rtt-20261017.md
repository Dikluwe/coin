# R27/C18 — MSAA 2×/4× com SoSceneTexture2

O produtor `SoSceneTexture2` herda a contagem de amostras do alvo. No modo
direto wgpu, ele renderiza em cor/depth multiamostra, resolve a cor RGBA8 para
a textura GPU e só então publica o token consumido pelo quadro pai. O modo
staged também usa a contagem do alvo e transfere a imagem resolvida pela CPU.
O readback síncrono de depth do alvo consumidor funciona junto com os dois
modos e as duas contagens. Ticket assíncrono MSAA continua recusado.

`CoinRenderMsaaRttTest` usa um produtor com triângulo vermelho sobre clear
azul e um consumidor texturizado. Em AMD/Vulkan, NVIDIA/Vulkan e AMD/OpenGL:

- O consumidor 4× difere de 1× em 728 canais, dos quais 534 estão no interior
  do quad consumidor, afastados da borda do próprio quad.
- Direto 4× e staged 4× são idênticos byte a byte (erro máximo 0). O direto
  não fez upload intermediário; staged transferiu 4096 bytes.
- Uma falha injetada depois da codificação do produtor 4×, antes da submissão,
  manteve os pixels e os seriais publicados pelo quadro pai.

Recibos com adaptador físico: [AMD/Vulkan](validation/coin-render-r27-rtt-20261017/amd-vulkan.log),
[NVIDIA/Vulkan](validation/coin-render-r27-rtt-20261017/nvidia-vulkan.log) e
[AMD/OpenGL](validation/coin-render-r27-rtt-20261017/amd-opengl.log).

Na continuação C18, o teste independente ampliou a fixture para deixar fundo
visível ao redor do consumidor e habilitou depth readback no alvo pai. Em
AMD/OpenGL, AMD/Vulkan e NVIDIA/Vulkan, passou com DIRECT e STAGED em 2× e
4×: depth de plano nos pixels cobertos, fundo 1,0, borda parcial, igualdade
exata de depth entre as rotas e diferença RGBA máxima de quatro níveis.
DIRECT teve delta zero no contador de upload CPU; falha injetada após encoding
preservou cor, depth, serial e cache, e o frame seguinte recuperou nas quatro
combinações. Os três CTests físicos passaram. Uma mutação que desligou o
ponteiro de depth no ABI foi rejeitada pelo teste (1/1); após restaurá-la, o
teste passou novamente.

Este é o perfil opaco RGBA8 sem requisito de mip. RGBA16F, cadeia de mips,
blend/OIT, depth do produtor RTT, multipass geral, BGFX e plataformas externas seguem
abertos em R27/C18.

Na continuação R26.5, uma subcena com SoBumpMap opaco e uma luz produziu RTT RGBA8 para um quad consumidor em DIRECT e STAGED, 1×/2×/4×. AMD/OpenGL, AMD/Vulkan e NVIDIA/Vulkan passaram: upload CPU delta zero no DIRECT, diferença RGBA máxima de quatro níveis, depth do consumidor idêntico, efeito MSAA interior e rollback/retry de cor, depth e serial após falha 307. A subcena sem luz histórica continuou recusada. Cinco CTests por GPU passaram (15/15), e uma mutação que forçou o produtor DIRECT a 1× foi rejeitada (1/1). HDR, mips, saída assíncrona e composições seguem abertas.
