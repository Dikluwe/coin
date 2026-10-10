# BGFX: mips NPOT de RTT direto por redução de área

Implementação local na branch `codex/coin-portable-sampling-study`, validada em
2026-10-09. O produtor 3×5 com última coluna branca exige RGB `(85,85,85)` no
mip 1×1; a autogeração BGFX divergira desse oráculo e era recusada antes do
produtor. A rota NPOT agora usa média exata por área, incluindo bordas ímpares,
sem readback nem geração CPU. A rota POT continua com autogeração nativa.

## Rota implementada

1. Para NPOT, o framebuffer da base usa `BGFX_ATTACHMENT_NONE`, evitando
   `AUTO_GEN_MIPS`. O preflight exige formato de framebuffer e textura destino
   de blit válidos antes de submeter o produtor.
2. A cada nível, `bgfx::blit` copia o mip anterior para uma textura temporária
   GPU, com filtro pontual. O shader `fs_mip_area.sc` lê até 3×3 texels, calcula
   pesos de interseção da área de origem e renderiza no mip de destino por
   `bgfx::Attachment`. Não há leitura e escrita simultâneas no mesmo objeto GL.
3. A redução usa duas views do bloco privado de 16 por alvo, em um frame BGFX
   por nível. Assim uma cadeia 2047×2047 não aumenta o número de views usadas
   pelo frame de sombras/transparência do produtor. O custo de frames extras
   ainda não foi medido como benchmark.
4. O orçamento comum de 64 MiB já cobra a cadeia e uma base temporária. O
   backend aloca um novo framebuffer para substituição NPOT e só troca o token
   em cache após concluir todos os níveis. Falhas preservam o framebuffer
   anterior, os pixels publicados e o serial.

## Validação neste PC

O [ledger com resumo e logs](validation/bgfx-npot-direct-mips-20261009/analysis.json)
registra 16/16 processos PASS no build separado: oito perfis de texturas
avançadas (AMD Renoir e NVIDIA RTX 3060 Laptop, Vulkan/OpenGL, native/portable),
seis gates RTT direto com CoinGL, um gate staged e um controle CPU. Os recibos
OpenGL do próprio BGFX identificam os drivers AMD e NVIDIA; os Vulkan informam
`1002:1638` e `10de:2560`.

- RGBA8 final: 3×5, 1×5, 5×1, 5×7, 129×127 e 2047×2047 tiveram diferença RGB
  máxima zero no centro do consumidor contra a média independente esperada.
- RGBA16F NPOT 3×5 manteve valor acima de 1 através dos mips (erro RGB máximo
  de um byte após a composição final RGBA8).
- Falha injetada após um nível gerado preservou pixels e serial. Uma segunda
  falha ao substituir um token já em cache preservou o token anterior; a
  repetição recuperou o mesmo token e os mesmos pixels.
- O RTT direto 64×48 passou sete transições por processo na AMD/Vulkan,
  AMD/OpenGL e NVIDIA/Vulkan, incluindo dimensão inválida sem publicação e
  recuperação; a referência CoinGL teve MAE máxima 0/0/0,667, respectivamente.
  Os controles POT, staged e CPU continuaram passando.

Build e logs completos estão em
`/mnt/Laranja/Git/externos/coin-portable-sampling-artifacts/20261009-npot-bgfx`.
GLSL 330 e SPIR-V foram compilados neste Linux. O compilador BGFX local não
inclui D3D4Linux para gerar DX11, portanto Windows não foi qualificado aqui.

## Continuação do gate

O [cenário combinado com oito sombras e transparência](coin-render-bgfx-npot-shadow-oit-20261009.md)
passou em oito perfis AMD/NVIDIA Vulkan/OpenGL; NVIDIA/OpenGL exigiu
`EGL_PLATFORM=surfaceless` neste PC. Os mips usam frames próprios e não
acrescentam views ao frame base. Houve comparação exploratória de captura
sincronizada NPOT sem/com mips, mas ela não isola o custo GPU dos mips.
Windows/DX11 permanece aberto. Não promover a mudança para a branch de
produção antes desse gate e da decisão de desempenho.
