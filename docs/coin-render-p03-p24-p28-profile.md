# P03/P15/P16/P24/P28 — entrega do perfil de nós e recursos

A frente amplia o perfil Linux de nós e RTT e define a arquitetura dos recursos
seguintes. Os [contratos de shaders, texturas espaciais e MSAA/multipass](coin-render-portable-resources-contract.md)
estão definidos; sua execução continua aberta. Isso não fecha Windows nem todas
as workbenches FreeCAD.

## RTT RGBA8 ampliado

Wiring captura a unidade 0..7 e os modelos MODULATE, REPLACE, DECAL e BLEND;
Core reutiliza o contrato de multitextura P07 para a composição. `blendColor`,
REPEAT/CLAMP, qualidade linear (0,0.5] e NONE/ALPHA_BLEND/ALPHA_TEST mantêm seus
significados. Mais de uma unidade ativa pode misturar imagem armazenada e
produtor de cena. Dimensões 1..2048, oito níveis, ciclos e orçamento de 64 MiB
continuam com os donos e limites anteriores. O novo controle usa RTT 16×24;
não transforma isso em uma certificação de toda dimensão/driver.

A subcena recebe a política de transparência vigente em SoState. Um
`sceneTransparencyType` contendo SoTransparencyType seleciona outra política;
um nó de outro tipo é rejeitado neste perfil. Nós da subcena podem mudar a
política quando não existe override herdado. Herdar uma política originada de
um nó SORTED_LAYERS não equivale a instalar um override global na action filha.
Wiring copia o override real, sem inventar um novo. A Infra continua validando
os mecanismos requeridos pelos draws capturados, antes de publicar.

O framebuffer dos produtores convencionais aplica a equação CoinGL:
`alpha = sourceAlpha*sourceAlpha + destinationAlpha*(1-sourceAlpha)`.
A raiz conserva a acumulação anterior com fator ONE para alpha. Core marca
`legacyBlendAlpha` no plano do produtor; CPU, BGFX e wgpu executam a mesma
regra. Additive e as resoluções de peeling/OIT mantêm seus contratos próprios;
o controle novo não certifica outras equações dessas extensões.

Clear opaco sozinho não prova o resultado opaco: um draw sem blending ou com
blend legado pode reduzir o alpha. As referências capturadas e tokens RTT
ficam conservadoramente sem essa prova. A classificação Coin NONE/ALPHA_BLEND
continua separada dessa propriedade física. wgpu aceita uma descrição
conservadora de token e rejeita afirmações de opacidade sem prova, mantendo as
checagens de identidade, dimensão, payload, dispositivo e geração.

O protocolo privado passa de 48 para 49 para o bit de alpha legado; layouts C
FFI permanecem iguais. O campo adicional do FramePlan é privado. A API pública
Coin não muda. Falhas de formato/preflight/runtime continuam preservando os
pixels, depth, serial e tickets segundo P13/P14.

### Bugs nativos diagnosticados

1. FBO finaliza a imagem usando bind/unbind GL fora do image element. Se o
   produtor não altera a identidade da imagem herdada, o pop não restaura seu
   binding. Um RTT na unidade 1 fazia a unidade 0 perder a imagem anterior.
   `SoSceneTexture2P::updateFrameBuffer` agora restaura as unidades habilitadas
   depois do pop. O controle exige imagem ordinária na unidade 0 e RTT em outra
   unidade, sem repetir o nó ordinário para esconder o erro.
2. `updatePBuffer` instalava getTransparencyType na criação e depois substituía
   a política explícita pela política do consumidor no apply. Agora usa a mesma
   decisão de FBO e da instalação inicial. O controle mantém consumidor NONE,
   produtor BLEND e material parcialmente transparente.

A referência nativa acompanha os comportamentos válidos corrigidos conforme a
[política de compatibilidade](coin-render-compatibility-policy.md). Não se aumenta
uma tolerância para ocultar os dois bugs. Cada execução nova tem 288 controles:
unidades 0..7 × quatro modelos × três funções × três políticas de produtor,
mais rejeição RGBA16F e recuperação em cada controle. O centro RGB é comparado;
orientação/crop/matrizes continuam nos testes RTT P07, não são inferidos do centro.
Neste NVIDIA, quatro unidades fixed-function permitem 144 referências CoinGL;
as outras 144 têm equações escalares independentes. A limitação é consultada no
contexto GL efetivo e não vira uma certificação nativa das unidades superiores.

Fora do perfil: formatos float/depth/sRGB, outros wraps, mipmaps RTT e direto de
janela. Estes itens permanecem explicitamente abertos; não são habilitados por
uma capacidade de hardware.

## Delegação raster do host

`CoinRenderAction::captureScreenContent(node)` é uma extensão experimental para
callbacks de subclasses SoText2/SoImage. O host prepara seus campos com o estado
atual e chama a delegação em lugar do callback herdado. O Core captura o mesmo
raster/layout dos tipos nativos. O callback do host não é reentrado; pre/post e
observadores de primitivas continuam presentes, sem duplicar a geometria do
SoImage. O estado dos observadores é local. Fora de apply é inerte; nó/contexto
inválido ou recurso fora do perfil rejeita o candidato e permite recuperação.

A delegação conserva os limites de P03/Image: fontes/payload/dimensões, rejeição
de imagem com texturas herdadas ativas e dos casos não qualificados com sombras
ou ordenação por triângulo. Não é uma promessa de equivalência para qualquer
subclasse, callback GL-only ou estilo de Complexity. A integração escolhe
explicitamente essa semântica; não há downcast especulativo de todas as subclasses.

O [patch do host](../examples/coinrender/freecad_p03_raster_delegation.patch)
adapta SoColorBarLabel e SoFrameLabel. FrameLabel chama prepareImage a frio e
atualiza a imagem por material/string, sem exigir GL anterior. ColorBarLabel
entrega os campos SoText2 ao Core. O guard experimental FreeCAD continua
obrigatório; o caminho GL mantém sua preparação anterior.

O [patch de imagens geradas](../examples/coinrender/freecad_p16_label_textures.patch)
normaliza RGBA NPOT de rótulos NaviCube, letras dos eixos e overlays do viewer
para dimensões POT. O host reamostra em alpha premultiplicado e entrega RGBA
reto, com cópia própria, aos dois caminhos; o quad lógico mantém seu tamanho.
Imagens POT conservam os bytes originais. A conversão tem limites de 8192 por
eixo e 16 MiB; entradas fora desses limites seguem o diagnóstico do perfil.
Isso adapta produtores específicos do GUI, sem habilitar NPOT arbitrário no
Core nem mudar o layout binário das classes do host.

A auditoria de marcadores encontrou uma falha da medição: o bitmap fino tinha
29 pixels visíveis e zero amostras na grade de passo 2. A fixture agora compara
todos os pixels e registra também a contagem antiga para tornar o diagnóstico
reproduzível. A medição de idle começa depois de assentar a remoção dos nós e
exige contador estável no intervalo seguinte.

A fixture `freecad-screen-content` usa o viewport real com SoText2, SoImage,
marcadores, SoColorBarLabel, SoFrameLabel, alpha test, matriz UV projetiva e
Complexity BOUNDING_BOX. Verifica pixels/glyphs, mutação, remoção, câmera/resize,
picking do documento e idle. FrameLabel sem borda/background não passa apenas
pela presença de um retângulo; alpha test precisa remover os fragmentos
rejeitados; UV altera q sem mover a geometria. Os overlays são declarados
UNPICKABLE, preservando o picking do Box. Isso não qualifica seleção de texto
ou de um ícone de workbench.

Windows e consumidores completos de Draft/Sketcher/Fem/Measure/Mesh, StringLabel,
DatumLabel, kits/draggers e geometria GL-only restante conservam células próprias
no inventário. Um teste desta fixture não fecha uma workbench inteira.

## Validação

Regressões focadas: 19 CTests wgpu e 27 BGFX, incluindo os sete novos gates
RTT (2.016 controles, além de rejeição/recuperação), matriz RTT anterior,
políticas SceneTexture, orçamento, sombras e transporte privado. Recording:
três gates comuns. Rust: 40 testes em execução serial. Runner Qt: 79 testes;
inventário: três testes; normalizador do host: identidade POT, alpha/cor NPOT,
propriedade, formato e vazio.

Viewport FreeCAD real: dez células em GPU física — BGFX Vulkan e wgpu
object/weighted OIT × DPR 1/2; BGFX OpenGL × DPR 1. Dois controles CoinGL
ficam separados como REFERENCE_PASS. NaviCube NPOT: 12 células em hardware,
sete orientações por célula, com controle sem rótulos, máscaras e picking.
BGFX OpenGL DPR 2 permanece aberto: timeout AMD antes da primeira captura;
offload concluiu conteúdo e idle, mas sem prova suficiente de GPU física.

Os comandos, hashes, logs e resultados finais acompanham a entrega em
`docs/validation/nodes-resources-20261006`. Execuções preliminares com parser,
fixture ou headers antigos são diagnósticos, não passes. Resultados remotos ou
históricos Windows não qualificam os callbacks novos.
