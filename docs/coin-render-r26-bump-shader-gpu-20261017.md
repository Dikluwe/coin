# R26 — passes difuso, base e especular em GPU

Atualização de sampler: fatores anisotrópicos 2/4/8/16 da base bump wgpu
foram admitidos no perfil mip-linear nas três células Linux; a action pública
capturou fator 8 com sRGB. [Prova e limite visual](coin-render-r26-anisotropy-20261017.md).

Atualização de combinações: a base RGBA8 sRGB, inclusive cadeia mipmapped,
passou no executor opaco wgpu offscreen e janela nas três células GPU Linux;
[prova e limites](coin-render-r26-srgb-base-20261017.md). As restrições a
RGBA8 linear descritas nas etapas históricas abaixo foram ampliadas por essa
qualificação.

Atualização de recursos: duas chamadas de draw que referenciam a mesma base
agora compartilham um upload de imagem/sampler dentro do frame, com matriz UV
individual por draw. O teste GPU exige `draws=2, image_uploads=1` nas três
células; [prova e limites](coin-render-r26-shared-base-20261017.md).

Atualização de custo: o dispositivo wgpu agora guarda os três pipelines
R26 (difuso, base e especular) por formato de saída e comparação de depth.
O teste GPU do encoder reutilizou exatamente uma entrada do cache entre
draws sem base, com base e com mipmaps. Após a mudança, os testes públicos
offscreen/B12 e de janela/resize/recuperação de alfa conservaram os pixels e
seriais nas três células AMD/Vulkan, NVIDIA/Vulkan e AMD/OpenGL. O A/B de
encoding no build de teste mediu medianas 14,48–15,03 ms sem cache e
0,487–0,491 ms com cache nas três células. Incluindo submit e espera GPU,
as medianas foram 15,16–16,24 contra 0,69–0,84 ms;
[método e logs B10](coin-render-b10-bump-pipeline-cache-20261017.md).
O fixture não inclui action, readback público ou apresentação.

Atualização de janela de 17/10: o mesmo encoder opaco agora atende
`coin_wgpu_surface_submit` e a action pública em alvo X11. O teste
`CoinRenderBumpWindowTest` observou, após **uma submissão por frame**, o
centro `[204,51,25]` sem base e `[102,26,13]` com `SoTexture2` MODULATE,
com serial de superfície 1→2, em AMD/Vulkan, NVIDIA/Vulkan e AMD/OpenGL.
O observador usa `XGetImage` do drawable apresentado, inclusive na célula
AMD/OpenGL que não oferece `COPY_SRC`. O controle B03 de primeiro present
AMD/Vulkan continuou com três frames exatos após essa ligação. A mesma
action e alvo X11 passaram pelo resize 256×256→320×192 com RGB
`[102,26,13]` exato e serial 3 nas três células. Alterar o alfa da base
para 128 retornou `Unsupported` sem novo serial nem alteração do pixel
apresentado; restaurar 255 reapresentou o pixel exato com serial 4. Isso
qualifica resize e rejeição/recuperação de alfa neste perfil opaco.
Composição real de alfa, sombras, multitextura e demais modos seguem abertos.

Atualização pública de 17/10: `CoinRenderAction` agora admite o perfil
offscreen wgpu opaco com mapa normal ativo, uma luz PHONG, culling desativado,
depth LESS/LEQUAL e transparência `NONE`. A cena real `SoBumpMap` + `SoCube`
passou pela action, captura do plano, backend e readback com centro
`[204,51,25,255]` e serial publicado nas três células AMD/Vulkan,
NVIDIA/Vulkan e AMD/OpenGL. Os 11 casos B12 de rejeição e recuperação
continuaram aprovados nessas células; CPU continua recusando bump ativo.
O modo `SCREEN_DOOR` padrão precisa de composição e segue recusado pelo
perfil restrito. Plan-only/RTT, outras combinações de janela, transparência e demais combinações
continuam pendentes.

`SoTexture2` público com MODULATE e o filtro mipmapped padrão agora também
passa junto do bump: o segundo apply da mesma cena publicou serial novo e
centro `[102,26,13,255]` nas três células. O encoder aceita RGBA8 linear
com um nível ou cadeia completa de mipmaps e filtros 0–3, verificando alfa
255 em todos os texels. Um teste ABI separado enviou explicitamente níveis
2×2 e 1×1 e produziu `[229,90,45,255]` em AMD/Vulkan, NVIDIA/Vulkan e
AMD/OpenGL. A suíte Rust completa terminou com 48 testes aprovados.
Na mesma action pública, mudar o alfa da textura para 128 recusou o frame
sem alterar cor, profundidade ou serial; restaurar 255 voltou a produzir a
imagem opaca inteira e um serial novo nas três células. Essa sequência
verifica a transição de estado, não apenas uma chamada isolada ao backend.

Atualização de 17/10: o encoder offscreen opaco aceita agora uma textura
base RGBA8 com alfa integralmente 255, MODULATE, matriz UV homogênea e
sampling nearest/linear com repeat/clamp. A imagem base é carregada na GPU
e combinada com o fator DOT3 no passe difuso antes da soma especular. O
teste `bump_abi_payload_renders_with_dedicated_shader` verificou o encoder
completo (não só o shader isolado): `[229,90,45,255]` no pixel texturizado,
além dos controles sem textura `[255,115,57,255]` e com segundo mapa normal
`[0,0,0,255]`, em AMD/Vulkan, NVIDIA/Vulkan e AMD/OpenGL. A entrada ABI
offscreen agora encaminha os vetores de texturas e samplers a esse encoder.
O teste `CoinWgpuFfiFrameTest --gpu-bump-base` também percorreu plano C++,
`prepareBumpFrame`, submissão ABI Rust e readback do backend: o mesmo pixel
`[229,90,45,255]` e serial 1 em AMD/Vulkan, NVIDIA/Vulkan e AMD/OpenGL.
O teste C++ ordinário e o controle GPU sem textura passaram após a integração.
No mesmo teste, alterar o alfa do texel base para 128 após o frame aprovado
retorna `Unsupported` nas três células sem modificar a cor publicada ou emitir
novo serial. A prova usa `backend.submit` diretamente; nesse nível o serial
fica no resultado da submissão, enquanto o alvo mantém seu contador anterior.
Ainda faltam liberação pela action pública, janela, alfa/texturas complexas e
demais combinações R26.

O shader dedicado `coin_bump.wgsl` foi executado como pipeline wgpu
isolado com um triângulo de tela, mapa normal RGB 1×1 e textura de saída
RGBA8. A luz direcional frontal produziu o pixel central
`[204, 51, 25, 255]`; a luz invertida produziu `[0, 0, 0, 255]` nas três
células físicas disponíveis: AMD/Vulkan, NVIDIA/Vulkan e AMD/OpenGL. O teste
confere a saída contra o cálculo difuso esperado com tolerância de um nível
de canal. Com luz pontual, o centro deu `[201, 50, 25, 255]` e o pixel mais
afastado `[191, 48, 24, 255]` nas três células, comprovando a variação por
posição. O passe specular dedicado produziu `[127, 64, 32, 254]` com luz
frontal, `[0, 0, 0, 0]` com luz invertida e `[9, 4, 2, 17]` com luz angulada
nas mesmas células. O último caso discrimina o expoente legado
`shininess × 64` do expoente Phong comum `× 128`. O shader passou no
parser/validador Naga e na tradução GLSL de vertex e dos dois fragments.
O núcleo comum também calcula o fator especular legado em CPU a partir do
meio vetor tangente e de `shininess × 64`. Os controles frontal, oposto e
angulado passaram no `CoinRenderFrameCoreTest` dos builds BGFX e wgpu.

[Recibos por GPU, identificações e hashes](validation/coin-render-r26-bump-shader-gpu-20261017/results.json).

O teste inicial executou o shader diretamente pelo wgpu. A ABI Rust
offscreen síncrona passou a codificar o perfil opaco descrito abaixo;
o backend C++ já encaminha o plano nesse perfil, mas a action ainda recusa
`SoBumpMap` ativo. A composição
com passe base e cruzamentos de recursos continua aberta.

O contrato privado C++/Rust avançou à revisão 54: `CoinWgpuFrameView` tem
campos opcionais para o fluxo de bump e suas imagens (496 bytes; vértice 44,
imagem 32). `CoinWgpuFfiFrameTest` confirmou os campos vazios em quadros
comuns, e o teste Rust confirmou que qualquer payload presente é detectado.
Na introdução da ABI, as entradas Rust retornavam `Unsupported` para o
payload. Agora offscreen síncrono aceita o perfil restrito documentado ao
fim deste relatório; janela continua recusada.
O empacotador C++ já consegue preparar explicitamente o pacote FFI com
vértices remapeados e cópia própria dos bytes da imagem. O teste
`CoinWgpuFfiFrameTest` cobriu vértice duplicado, alteração posterior da fonte,
contagem incompatível, imagem truncada e índice fora do plano; as falhas
preservaram o pacote anterior. A chamada normal de `prepare()` ainda recusa
bump ativo; a ABI Rust direta offscreen agora desenha o perfil restrito.
O ingresso Rust agora valida contagem/alinhamento, slots, coordenadas e
tangentes finitas, Q não nulo, dimensões, flags e orçamento das imagens, e
faz cópia própria do RGB. Dois testes Rust do contrato passaram nessa etapa;
payload fora do perfil offscreen ainda recebe `Unsupported`. Depois dessa
mudança, `CoinWgpuFfiFrameTest` e os 11 casos B12 em AMD/Vulkan GPU passaram.

O estágio Rust de upload já cria um buffer de vértices e texturas RGBA8 a
partir do RGB validado, respeitando clamp/repeat e recusando dimensões acima
do limite da GPU antes de alocar. O teste físico copiou de volta o vértice e
os dois texels `[128,128,255,255]`/`[240,128,128,255]` sem diferença em
[AMD/Vulkan](validation/coin-render-r26-bump-upload-20261017/amd-vulkan.log),
[NVIDIA/Vulkan](validation/coin-render-r26-bump-upload-20261017/nvidia-vulkan.log)
e [AMD/OpenGL](validation/coin-render-r26-bump-upload-20261017/amd-opengl.log).
Esse upload ainda é exercitado pelo teste, não pela submissão CoinRender.

Um segundo teste liga o pacote ABI validado ao pipeline dedicado definido na
biblioteca Rust usando duas
fontes de vértices: posições/normais do layout padrão e UV/tangentes do
layout de bump. O pixel central dos passes difuso/especular foi,
respectivamente, `[204,51,25,255]`/`[127,64,32,254]` nas
três células: [AMD/Vulkan](validation/coin-render-r26-bump-abi-draw-20261017/amd-vulkan.log),
[NVIDIA/Vulkan](validation/coin-render-r26-bump-abi-draw-20261017/nvidia-vulkan.log)
e [AMD/OpenGL](validation/coin-render-r26-bump-abi-draw-20261017/amd-opengl.log).
O teste prova o percurso ABI→upload→pipeline→pixel para os dois entry points;
ele ainda constrói o draw
diretamente, fora da submissão normal e de suas combinações.

Os uniformes desse teste agora são derivados de `CoinWgpuRenderState` e
`CoinWgpuMaterial`, em vez de valores finais escritos diretamente no teste.
A conversão leva luz pontual/direcional e observador do espaço de vista para
o espaço do objeto usando a matriz normal, aplica cor e intensidade da luz e
mantém `shininess` para o expoente legado do shader. Um teste CPU verificou
matriz com escala e translação, ambos os tipos de luz e rejeição de tipo
fracionário/intensidade negativa. O desenho repetido preservou os pixels
difuso/especular `[204,51,25,255]`/`[127,64,32,254]` em
[AMD/Vulkan](validation/coin-render-r26-bump-state-20261017/coin-bump-state-amd-vulkan.log),
[NVIDIA/Vulkan](validation/coin-render-r26-bump-state-20261017/coin-bump-state-nvidia-vulkan.log)
e [AMD/OpenGL](validation/coin-render-r26-bump-state-20261017/coin-bump-state-amd-opengl.log).
Esse teste de shader continua submetendo o desenho diretamente; a prova
posterior pelo backend C++ está descrita abaixo. A action ainda recusa bump.

O mesmo teste agora compõe os passes em um único alvo com mistura aditiva
`ONE,ONE` para o especular, como o trecho de bump do `SoShape` nativo. O
pixel central resultou em `[255,115,57,255]` nas três células, mantendo
os controles isolados de cada passe. [AMD/Vulkan](validation/coin-render-r26-bump-composed-20261017/coin-bump-composed-amd-vulkan.log),
[NVIDIA/Vulkan](validation/coin-render-r26-bump-composed-20261017/coin-bump-composed-nvidia-vulkan.log),
[AMD/OpenGL](validation/coin-render-r26-bump-composed-20261017/coin-bump-composed-amd-opengl.log).
Isso valida a operação GPU difuso+especular do perfil opaco de uma luz;
o passe normal/base e as demais combinações CoinRender ainda não entram
nessa composição de teste.

O contrato Rust agora resolve o slot de mapa normal por draw indexado antes
da codificação: três draws com slots 1, 2 e 0 foram identificados corretamente;
índices fora do plano e mistura de slots em um draw são recusados. O teste
GPU deixou de usar `draw` sequencial e passou a enviar índice U32 e
`draw_indexed` com o intervalo de `CoinWgpuDraw`. Os pixels isolados e
compostos permaneceram iguais nas três células:
[AMD/Vulkan](validation/coin-render-r26-bump-indexed-20261017/coin-bump-indexed-amd-vulkan.log),
[NVIDIA/Vulkan](validation/coin-render-r26-bump-indexed-20261017/coin-bump-indexed-nvidia-vulkan.log),
[AMD/OpenGL](validation/coin-render-r26-bump-indexed-20261017/coin-bump-indexed-amd-opengl.log).
Ainda falta chamar esse roteamento e o pipeline a partir do encoder de
quadros offscreen/janela.

O teste físico agora transporta duas imagens normais no mesmo payload ABI,
seis vértices e dois `CoinWgpuDraw` indexados. A escolha do segundo slot
pela ligação de textura deu `[0,0,0,255]` com normal invertida; o primeiro
continuou em `[204,51,25,255]`, e a composição do primeiro manteve
`[255,115,57,255]`. Os draws foram codificados em passes sucessivos do mesmo
teste, ainda fora da submissão pública:
[AMD/Vulkan](validation/coin-render-r26-bump-two-images-20261017/coin-bump-two-images-amd-vulkan.log),
[NVIDIA/Vulkan](validation/coin-render-r26-bump-two-images-20261017/coin-bump-two-images-nvidia-vulkan.log),
[AMD/OpenGL](validation/coin-render-r26-bump-two-images-20261017/coin-bump-two-images-amd-opengl.log).

Há agora uma função Rust de codificação de quadro opaco com bump:
`encode_bump_opaque_frame` consome vértices/índices/draws/estados/materiais,
resolve imagens por draw, cria dois pipelines com depth e soma o especular.
Ela exige triângulos indexados, todos com bump ativo, uma luz válida,
material opaco, alvo inteiro e profundidade `LESS`/`LEQUAL`, recusando
textura base, fog e composição ainda não implementadas nesse caminho.
No teste GPU, uma chamada da função deu `[255,115,57,255]`; dois draws no
mesmo quadro com mapas diferentes deram `[0,0,0,255]` após o segundo. O
controle de textura base ativa recebeu `Unsupported` antes de codificar.
[AMD/Vulkan](validation/coin-render-r26-bump-encoder-20261017/coin-bump-encoder-amd-vulkan.log),
[NVIDIA/Vulkan](validation/coin-render-r26-bump-encoder-20261017/coin-bump-encoder-nvidia-vulkan.log),
[AMD/OpenGL](validation/coin-render-r26-bump-encoder-20261017/coin-bump-encoder-amd-opengl.log).
A função está ligada ao backend C++ offscreen no perfil restrito; ainda não
está ligada à action nem à entrada Rust de janela. Passe normal/base, sombras
e múltiplas luzes não foram qualificados.

A entrada Rust `coin_wgpu_submit` agora despacha esse encoder para payloads
bump no perfil offscreen síncrono. Um teste chama a ABI completa, incluindo
validação do frame, codificação, submissão, readback RGBA e publicação do
serial. O pixel central foi `[255,115,57,255]` com `LEQUAL` e `LESS` em
[AMD/Vulkan](validation/coin-render-r26-bump-offscreen-20261017/coin-bump-offscreen-amd-vulkan.log),
[NVIDIA/Vulkan](validation/coin-render-r26-bump-offscreen-20261017/coin-bump-offscreen-nvidia-vulkan.log)
e [AMD/OpenGL](validation/coin-render-r26-bump-offscreen-20261017/coin-bump-offscreen-amd-opengl.log).
O perfil recusa antes de codificar textura base,
sombras, instancing, transparência composta, RTT direto e readback assíncrono.
O teste físico conjunto AMD/Vulkan passou 3/3 (upload, encoder com múltiplas
imagens e ABI offscreen). `CoinWgpuFfiFrameTest` passou e B12 público continuou
11/11 na AMD/Vulkan: a action ainda não encaminha esse perfil, e a entrada de
janela Rust ainda retorna `Unsupported` para bump.
Assim, a ABI direta offscreen funciona nesse perfil, mas o recurso visual
CoinRender permanece fechado ao público até completar o passe base e os
cruzamentos necessários.
Após a ligação, a suíte Rust completa passou 48/48 com AMD/Vulkan selecionada
e execução serial ([log](validation/coin-render-r26-bump-offscreen-20261017/coin-wgpu-lib-r26-amd-vulkan.log)).
A primeira execução sem selecionar backend registrou `EGL BadAccess` no teste
de transferência de depth; o rerun Vulkan passou, sem mudança nesse teste.

O backend C++ agora seleciona `prepareBumpFrame` quando há imagem bump ativa
e envia o pacote pela ABI Rust offscreen. Um teste GPU opcional constrói um
`CoinRenderFramePlan` com UV/tangentes/imagem, luz direcional capturada e
material opaco, chama `CoinWgpuBackend::submit` e confere pixel
`[255,115,57,255]` e serial publicado em
[AMD/Vulkan](validation/coin-render-r26-bump-cxx-backend-20261017/coin-bump-cxx-backend-amd-vulkan.log),
[NVIDIA/Vulkan](validation/coin-render-r26-bump-cxx-backend-20261017/coin-bump-cxx-backend-nvidia-vulkan.log)
e [AMD/OpenGL](validation/coin-render-r26-bump-cxx-backend-20261017/coin-bump-cxx-backend-amd-opengl.log).
A primeira ligação revelou que luz direcional real chega em
`direction_cutoff.xyz`, não em `position_type.xyz`; o uniforme agora usa o
negativo da direção capturada, como o shader padrão. Também foi necessário
tratar o padrão `layers=4` sem peeling como configuração inativa, e o teste
marca `transparencyType=NONE` para não solicitar screen door. Os três testes
físicos Rust (upload, encoder, ABI) passaram em cada célula após a correção
([logs](validation/coin-render-r26-bump-cxx-backend-20261017/)). A action
continua recusando o recurso até haver textura base, janela e
cruzamentos qualificados.

O passe `renderNormal` do CoinGL ocorre com `GL_LIGHTING` desligado e multiplica
o DOT3 anterior pela cor/material/textura do shape. Um oráculo visual mudou
ambiente RGB de `(0.2, 0.2, 0.2)` para `(0.9, 0.1, 0.1)` e emissão de zero
para `(0.3, 0.4, 0.5)` sem alterar qualquer byte do quadro em
[AMD/GLX](validation/coin-render-r26-bump-base-20261017/coin-bump-base-amd-glx.log)
ou [NVIDIA/GLX](validation/coin-render-r26-bump-base-20261017/coin-bump-base-nvidia-glx.log).
O encoder deixou de recusar esses dois campos e o plano C++ com ambiente
padrão `0,2` preservou `[255,115,57,255]` em
[AMD/Vulkan](validation/coin-render-r26-bump-base-20261017/coin-bump-base-backend-amd-vulkan.log),
[NVIDIA/Vulkan](validation/coin-render-r26-bump-base-20261017/coin-bump-base-backend-nvidia-vulkan.log)
e [AMD/OpenGL](validation/coin-render-r26-bump-base-20261017/coin-bump-base-backend-amd-opengl.log).
Ainda falta reproduzir a multiplicação pela textura base e os materiais por
vértice, além de qualificar o passe normal em cenas mais amplas.

O empacotador C++ ganhou uma entrada interna `prepareBumpFrame`: ela desliga
reuso, instancing e batching de geometria nesse perfil, mantém todos os
vértices e índices na ordem capturada e anexa o pacote FFI. O teste
`CoinWgpuFfiFrameTest` conferiu a ordem integral e os slots ativos/inativos;
os 11 casos B12 em AMD/Vulkan GPU continuaram passando porque a entrada
pública `prepare()` ainda recusa bump ativo. A entrada interna agora é usada
pelo backend C++ offscreen; a action pública permanece fechada.

O `CoinBumpGLXTest` agora registra também a composição especular nativa com
normal constante, textura base preta e cor especular 0,1. No pixel central,
AMD/GLX deu frontal 147, luz tangencial com `shininess=0,5` 94, com
`shininess=0,05` 99, luz invertida 0 e controle sem especular tangencial 74.
NVIDIA/GLX deu respectivamente 147, 147, 152, 0 e 127. Ambos passaram o gate
de resposta à rugosidade (+5) e de presença de especular sobre o baseline.
O baseline e a cor total distintos entre GPUs mostram que o render normal
CoinGL também contribui para a cena; esses valores não são o esperado exato
do fragmento especular isolado nem um contrato universal de pixels nativos.
[Recibo AMD](validation/coin-render-r26-bump-specular-coingl-20261017/amd-glx.log),
[recibo NVIDIA](validation/coin-render-r26-bump-specular-coingl-20261017/nvidia-glx.log).
