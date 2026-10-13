# SoPBRMaterial — qualidade e ferramentas CAD (M4)

O perfil `pbr-cad-m4` combina compensação especular, um residual difuso recíproco,
filtragem de normais e composição técnica após a saída HDR. O material continua
um nó Coin; CoinRender mantém travessia, estado, geometria, clipping e recursos.
A base dos algoritmos novos inclui SbVec*, SbMatrix, SbRotation, SbBSPTree,
SbList, SbPlane, SbString, SbByteBuffer e cc_time.

## Energia

Com ambiente configurado, `pbrEnergyCompensation=true` habilita o lóbulo de
compensação Kulla/Conty, descrito em [Filament](https://github.com/google/filament/blob/main/docs/Filament.md.html).
`--single-scattering` conserva o oracle de interação única M3. Sem pacote, a
rota direta M2 permanece. A LUT do pacote é a mesma; uma textura 1×N RGBA16F
derivada no upload guarda as médias hemisféricas de A, B e A+B. Em N=256,
são mais 2048 bytes nominais por executor, sem novo asset incompatível.

Defina `Ev=A(Nv,r)+B(Nv,r)`, `El=A(Nl,r)+B(Nl,r)` e
`Eavg=2 integral_0^1 E(mu,r) mu dmu`. Com
`Favg=(1+20 F0)/21`, o peso adicional é
`W=Favg² Eavg / (1-Favg(1-Eavg))`, por canal.
O especular direto é o GGX de interação única mais
`W*(1-Ev)*(1-El)/(pi*(1-Eavg))`. Trata-se de uma aproximação de múltiplas
interações, sem rastreamento estocástico dos caminhos entre microfacetas.

A refletância direcional compensada é
`Sv=F0*A(Nv,r)+B(Nv,r)+(1-Ev)*W`. Sua média Savg vem das médias de A/B
na textura derivada. O difuso direto usa
`base*(1-metallic)*(1-Sv)*(1-Sl)/(pi*(1-Savg))`.
Assim ele é recíproco e, sob ambiente branco, integra o residual `1-Sv`.
O indireto adiciona `E_SH(N)/pi*(1-Ev)*W` ao especular split-sum e usa
`E_SH(N)/pi*base*(1-metallic)*(1-Sv)` como difuso. Para ambientes angulares
não uniformes, a separação e SH9 continuam aproximações. Valores brancos de
metal e dielétrico conservam o furnace; metais coloridos seguem a expectativa
dos lobos compensados e não recebem ganho arbitrário de exposição.

Os testes usam DFG em double por amostragem NDF, Smith Lambda explícito e
integração hemisférica independente. Não importam o shader, o integrador VNDF
nem a LUT GPU como oracle. Passaram 72 furnaces, nove pontos diretos e
reciprocidade com tolerâncias previamente codificadas; o baseline de 18
furnaces de interação única continua passando.

## Filtragem especular

A largura quadrática de GGX usa
`alpha²_filtered=min(r^4 + min(2*variance*(|dN/dx|²+|dN/dy|²),threshold),1)`.
A mesma rugosidade resultante alimenta o BRDF, DFG e LOD do ambiente.
Os defaults do target são variance=0,15 e threshold=0,10, finitos em [0,1].
Threshold zero, ou `--no-specular-filter`, desabilita o filtro.
A solução usa variação da normal, como as opções de filtragem descritas em
[Filament Materials](https://google.github.io/filament/Materials.md.html).

O controle de um pico subpixel passou de 13584 para 221,875 em radiância,
sem apagar o lóbulo. Uma face plana mantém pixels idênticos com o filtro.
O filtro não muda vértices ou chanfros. Isso não elimina todo aliasing de
silhueta: o perfil inicial continua single-sample. Órbita e zoom são parte da
campanha, e não uma substituição do controle numérico.

## Composição técnica

Faces PBR ocupam o layer de cena. O passe Neutral/sRGB copia também a
profundidade HDR para o framebuffer final. Overlays legados BASE_COLOR são
executados depois, preservando suas cores de display. Assim seleção e medidas
não variam com exposição. Teste/escrita de profundidade, barreiras por viewport
e ordem de traversal continuam os do compositor CoinRender. O readback público
de profundidade reflete essa saída; o diagnóstico privado HDR contém só a cena.

O perfil admite overlays opacos BASE_COLOR, sem fog/alpha test/texturas.
Linhas, pontos e texto triangulado podem usar essa rota. Uma camada técnica PBR,
legacy lit, mistura de opacos legados no layer base, transparência de material,
texturas PBR e shadow groups são recusados explicitamente. Não há conversão
silenciosa de cor legada em radiância. O orçamento reserva os IDs de diagnóstico:
11 views técnicas, cobrando duas para cada layer que limpa depth. A recusa ocorre
antes de enfileirar draws GPU, mantendo a publicação anterior.

`--cad-tools` usa SoSelection/SoRayPickAction para escolher uma superfície e
SoFullPath para identificar o componente. O destaque é uma caixa da seleção e
uma linha diagonal de medida, com label SoText3 em unidades do fixture.
Arestas são extraídas das primitivas Coin com SbBSPTree e SbList: fronteiras ou
ângulos maiores que 20 graus, sem diagonais internas de triângulos coplanares.
O teste conserva 12 arestas de um cubo e 512 nos oito perfis do anel chanfrado,
removendo a malha de subdivisão suave. A quantização é
`max(1e-6,diagonal*1e-7)` na escala do fixture.

Os overlays usam SoAnnotation, SoLightModel, SoMaterial override,
SoDepthBuffer e SoPolygonOffset. Arestas testam profundidade com offset;
destaque/label não a escrevem. Picking de overlays é desabilitado.
Cortes de fixtures usam a mesma SoClipPlane na cena e nas arestas. O fixture
não cria tampa de corte artificial. O primeiro viewer usa seleção determinística;
não implementa edição CAD nem uma interface completa de anotação.

## Views, importação e oracle geométrico

```sh
coin_render_viewer --fixture housing --environment studio.cibl --cad-tools \
  --offscreen --validate-depth --capture-depth --frames 2 --capture-prefix housing
coin_render_viewer --fixture assembly --environment asymmetric.cibl \
  --offscreen --orbit-frames 36 --validate-depth --capture-prefix orbit
coin_render_viewer --fixture flange --view detail --zoom-factor .5 \
  --environment studio.cibl --offscreen --frames 2 --capture-prefix detail
```

`--capture-depth` grava f32 nativo, com layout top-to-bottom, além de RGB/JSON.
`--validate-depth` lança 192 raios definidos por SbViewVolume sobre triângulos
Coin capturados por SoCallbackAction, com SbVec e SbPlane para interseção e
clipping. A comparação exclui a faixa de dois pixels junto à silhueta, verifica
cobertura sem cor e compara depth com tolerância absoluta 0,002. Triangulação é
essencial: SoSphere::rayPick usa esfera analítica, diferente da malha exibida.
Picking da seleção continua o picking nativo. Depth/oracle são offscreen;
janela compara os pixels finais por seu readback existente.

O controle S4 usa a montagem FreeCAD identificada em M0, sem alterar o original.
`--scene Assembly.iv --convert-import --environment studio.cibl` troca os nós
SoMaterial por SoPBRMaterial por uma política explícita: diffuse assumido sRGB
é decodificado para linear; metallic=0,9 e roughness=0,35, ajustáveis por flags;
emissão linear e override/nome são preservados. Arrays não OVERALL e
transparência são recusados. É uma política de aparência, não inferência da
liga real. O manifest registra a conversão e os hashes do original/exportado.
Corte importado não está qualificado e é recusado. A profundidade S4 coincide
exatamente com seu controle legado.

A campanha passou 106/106 casos: as 72 vistas mínimas em 1280×960, 18 controles
legados de geometria, grade, ferramentas/cortes, janela/offscreen, duas órbitas
de 36 frames e S4. São 280 imagens e 52170 probes geométricos; os 18 masks
PBR/legacy coincidem em depth f32 completo. Zoom próximo/distante e ferramentas
na janela passaram como três controles adicionais. Primeiro frame/replay são
idênticos nas cenas estáticas, e cada órbita tem 36 imagens distintas.

CTest: 23 BGFX, 12 Recording, 13 Rust. Controles M0: 26/26 e hashes de pixels
preservados. [Resumo](validation/pbr/m4/summary.json),
[matriz](validation/pbr/m4/results.json), [galeria](validation/pbr/m4/gallery.png)
e [órbita](validation/pbr/m4/orbit-studio.gif). O GIF é só preview redimensionado;
PNGs de todos os frames e os hashes originais preservam a evidência.

M5 permanece necessário para SDK instalado/consumidor, qualificação de lifecycle,
budgets/performance e atualização de materiais sem recaptura da geometria.
