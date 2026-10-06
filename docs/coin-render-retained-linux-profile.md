# Nós retidos no FreeCAD — perfil Linux de 2026-10-06

Continuação de P03/P15/P16 sobre a [entrega de recursos](coin-render-p03-p24-p28-profile.md).
O patch incremental [freecad_linux_retained_callbacks.patch](../examples/coinrender/freecad_linux_retained_callbacks.patch)
é aplicado depois dos patches anteriores e da integração experimental P16 do
checkout FreeCAD. A biblioteca CoinRender e o header experimental precisam ser
atualizados juntos. Verificar/aplicar o patch com `git apply --check
--ignore-space-change` e `git apply --ignore-space-change` conserva a
compatibilidade dos contextos LF/CRLF. MeshGui e PartGui encontram `CoinRender::CoinRender` apenas
sob `FREECAD_COIN_WGPU_EXPERIMENTAL`; a definição é restrita aos arquivos que
usam a nova rejeição. O protocolo privado permanece em 49, sem mudança na ABI C.

| Nó | Captura comum no host | Limite do perfil |
| --- | --- | --- |
| SoStringLabel | prepara textRoot, blend e imagem RGBA NPOT | glifos/layout gerados pelo host; limites de textura P07 |
| SoDatumLabel | prepara cena fria, blend/culling e m_Root | preserva imagem lógica para layout/picking; normaliza somente a textura retida |
| SoFCControlPoints | sincroniza e percorre renderRoot | produtos/soma avaliados em 64 bits, até 65.536 pontos; eixos zero devem ser pareados; dimensões vazias são inertes |
| SoFCBoundingBox | prepara caixa/rótulos, modelo projetado e depth explícito | cantos dentro do volume de profundidade; dependência de geometric depth clamp é recusada |
| SoShapeScale | atualiza escala antes de atravessar o kit | campo shape precisa conter geometria; shape nulo é vazio |
| SoTransformDragger | prepara cache e escala dos eixos | pixels, translation e caminhos lógicos de picking; arraste por eventos ainda separado |
| SoPolygon | contorno fechado retido com segmentos independentes, largura 3 e BASE_COLOR | faixa válida, até 65.536 pontos; menos de dois pontos é vazio |
| SoFCMeshGridNode | linhas verdes retidas | 1..128 por eixo, até 99.846 vértices; (0,0,0) é inerte |
| SoFCColorBar | prepara viewport antes de atravessar rótulos/gradiente | câmera foreground com height 10; três intervalos/precisões comparados com CoinGL |

O host prepara dados e estado; o Core continua responsável pela captura,
expansão portátil de linhas, validação e publicação. Não há interpretação dos
nós FreeCAD nos executores. `CoinRenderAction::rejectUnsupported(reason)` permite
que uma callback recuse a candidata quando sua semântica excede o perfil.
Fora de apply é inerte; durante apply termina a captura com UNSUPPORTED,
preserva a publicação anterior e permite recuperação na mesma action/target.

## Qualificação e reprodução

[Resultados e ambiente](validation/retained-linux-20261006/README.md).
A fixture `freecad_screen_content.FCMacro`, com `COIN_TEST_RETAINED_NODES=1`,
instancia os oito primeiros tipos reais dentro do viewport FreeCAD. Verifica
conteúdo visível, glifos quando aplicável, mutação, remoção, câmera/resize,
picking do documento e idle. O dragger pode consumir o picking do documento;
a prova usa caminhos de picking nas hastes visíveis, com câmera explícita no
grafo da action. Isto não certifica uma operação de arraste por mouse.
O ensaio separado `COIN_TEST_SPLINE_CONSUMER=1` cria um `Part::Spline` real,
ativa/desativa ControlPoints, regenera Shape e verifica nós reais e pixels.
`COIN_TEST_RETAINED_FILTER` serve somente para diagnóstico; execução filtrada
não conta como célula completa.

`--case retained-rejection` usa os tipos reais Part/Mesh para recusar cinco
entradas (incluindo produto 65.536 × 65.536 e eixo zero desemparelhado), comparar a publicação anterior e recuperar no mesmo target.
`--case colorbar` captura o estado frio antes do oráculo GL, mede três ranges
e exige glifos claros, impedindo que o gradiente sozinho conte como rótulo.
A captura de tela dos oito nós mede presença e lifecycle; não é comparação
pixel a pixel com CoinGL. Diferenças de strokes seguem o contrato portátil
CPU/BGFX/wgpu e os estudos de raster já registrados.

## Prova da GPU OpenGL

A janela Qt/oráculo usa GLX; BGFX usa EGL. O inventário GLX pode identificar
uma GPU diferente daquela que executa BGFX. A opção de diagnóstico
`COIN_BGFX_TRACE_GL_ADAPTER=1`, com tracing de fases ligado, lê vendor,
renderer e version nas callbacks de cache de programa do render worker, com
o contexto BGFX atual. Não cria screenshots nem modifica os frames.
O runner Linux ativa a opção para BGFX/OpenGL e exige essas strings na prova física;
string ausente, desconhecida ou de renderer software falha a prova.

Neste host, GLX NVIDIA e EGL Mesa/AMD permitem DPR 1/2. EGL NVIDIA falha ao
criar a surface; a combinação anterior GLX Mesa/EGL Mesa em DPR 2 teve timeout.
O consumidor Part::Spline em OpenGL DPR 1 apresentou dois timeouts durante
submit, antes da primeira captura. O controle com
`COIN_BGFX_DISABLE_PROGRAM_CACHE=1` concluiu; essa configuração explícita é
usada na qualificação desse consumidor em OpenGL. Os oito nós da matriz
principal passaram com o cache padrão ativo. A influência da configuração foi
observada, mas a causa do timeout e a relação com reuso/lifecycle ficam abertas
como estudo; desativar o cache não fecha a investigação.
Essas combinações permanecem diagnósticos de integração de drivers, separados
do perfil aprovado. Residência de processo em nvidia-smi não comprova qual GPU
executou o backend.

## Pendências preservadas

Windows deve repetir build, callbacks, pixels, DPI, input e rejeição/recuperação.
Os consumidores completos Sketcher/Measure, spline Part, Mesh/FEM e arraste do
dragger precisam de fixtures próprias; os tipos reais usados no viewport não
certificam todos os workbenches. A dependência de depth clamp do bbox e o registro tardio de subclasses em SoCallbackAction ficam como estudos.
Na primeira captura primitiva do Polygon, uma action criada depois do import
emitia quatro segmentos; a action do viewport já existia antes de MeshGui ser
carregado e não produzia pixels. `set_callback_data` enumera os tipos derivados
no registro, e `shouldGeneratePrimitives` consulta essa tabela por ID. Esse
mecanismo é compatível com a omissão observada; a melhoria futura deve tratar
novos tipos, callbacks do usuário e invalidação de caches sem duplicar callbacks.
O perfil publicado usa linhas retidas de tipos padrão já inicializados.
Formatos/mips RTT adicionais, RTT direto de janela, shaders, volume/cubo e
MSAA/multipass continuam nas frentes de recursos; suas definições de contrato
não equivalem a executores implementados.
