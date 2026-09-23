# Perfil WebGPU experimental do Coin 4

Esta implementação é opt-in (`COIN_BUILD_WGPU=ON`) e fica em
`CoinWgpuExperimental`, separada de `libCoin`. A ação e o alvo de renderização
estão em `experimental/include` para uso na árvore de build; não são API nem
ABI pública instalada do Coin 4. A ponte C++/Rust é privada e versionada
(`COIN_WGPU_BRIDGE_PROTOCOL_REVISION=8` nesta revisão). Não promova esses
headers a consumidores externos como se fossem estáveis.

## Perfil implementado

| Área | Suportado agora | Fora do perfil / rejeição esperada |
| --- | --- | --- |
| Geometria | Triângulos de callback e `SoIndexedFaceSet`; linhas e pontos sem textura, inclusive `SoIndexedLineSet` | Topologias sem caminho implementado não têm paridade prometida |
| Materiais | `BASE_COLOR` e iluminação do perfil opaco; índices e bindings testados | Mistura/transparência de material com textura |
| Luzes | Direcional, pontual e spot, até oito ativas por draw | Excesso de oito luzes deve retornar `UNSUPPORTED` |
| Textura | `SoTexture2` na unidade 0, UV explícita, 1–4 componentes, alpha opaco, `MODULATE`, `REPEAT`/`CLAMP`, qualidade 0 ou 0,5 | Unidades adicionais, UV procedural/default, alpha parcial, `REPLACE`/`DECAL`/`BLEND`, linhas/pontos texturizados |
| Ambiente | Fog `NONE`, `HAZE`, `FOG` e `SMOKE` em distância de view space; fog depois de luz/textura e sem alterar alpha | Fórmulas ou estados de fog fora desses quatro modos |
| Raster | Front face e backface culling de `SoShapeHints` em triângulos, inclusive reflexão | Culling de linhas/pontos (não aplicável ao pipeline dessas topologias) |
| Alvos | Offscreen com cor/profundidade e janela X11 no backend Rust | Outros sistemas de janela ainda não validados |

Recursos não suportados devem produzir `UNSUPPORTED` e diagnóstico, não uma
imagem aparentemente válida que ignore silenciosamente parte do estado. A
referência CPU e o Recording permitem testar o contrato sem GPU; a paridade de
pixels exige GPU real e, opcionalmente, Coin/GL.

## Build e testes

```sh
cmake -S . -B build-wgpu -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_STANDARD=11 \
  -DCOIN_BUILD_WGPU=ON -DCOIN_WGPU_BACKEND=RUST_BRIDGE \
  -DCOIN_BUILD_TESTS=ON
cmake --build build-wgpu -j
ctest --test-dir build-wgpu --output-on-failure
```

O backend Rust exige Cargo e usa `cargo build --locked`. Para a referência GL
estrita em Linux/X11, acrescente `-DCOIN_TEST_WGPU_GL_REFERENCE=ON` na
configuração; é necessário `xvfb-run`. Para exigir uma janela real visível,
use `-DCOIN_TEST_WGPU_STRICT_DISPLAY=ON` e disponibilize um display X11.
O exemplo na árvore de build é `examples/wgpu/wgpu_window_cone.cpp`, ativado
com `-DCOIN_BUILD_WGPU_WINDOW_EXAMPLE=ON`.

O gate de isolamento usa outra configuração com
`-DCOIN_BUILD_WGPU=OFF -DCOIN_BUILD_TESTS=ON -DCMAKE_CXX_STANDARD=11`.
Compare símbolos de `libCoin.so` somente entre builds equivalentes (inclusive
`CMAKE_BUILD_TYPE`); Debug e Release exportam conjuntos diferentes por causa
da otimização. A comparação ON/OFF Debug por `abi-compliance-checker` nesta
etapa deu 100% de compatibilidade binária e de fonte. Os 878 headers instalados
são idênticos em ON/OFF e não incluem WebGPU. Um consumer C++11 externo do
pacote Coin instalado compilou e executou.

## Evidência adicional da 3E

A comparação histórica do Coin estável também foi feita com
`abi-dumper`/`abi-compliance-checker`: builds limpos, Debug, C++11 e
`COIN_BUILD_WGPU=OFF` do baseline `a8b55d0dc5` e deste HEAD tiveram 100%
de compatibilidade binária e de fonte, sem problemas nem avisos. Isso não
promete compatibilidade com os três headers WebGPU experimentais que existiam
no baseline e foram removidos no isolamento 3B.

O teste estrito de janela X11 sob Xvfb passou com frames repetidos, resize,
suspensão zero-size, fault injection de superfície e recuperação de device
lost. `WgpuStabilizationTest` já cobre falha transacional de alocação do cache;
`WgpuOffscreenTest` e `WgpuSurfaceTest` cobrem OOM e estados do alvo.

## Antes de considerar a Onda 3 concluída

Ainda faltam medições quantitativas reproduzíveis de upload de pixels, hits e
misses de textura, compilação/reuso de pipeline e retenção de recursos após
muitas trocas de textura. Os testes de correção e cache existentes não substituem
essa evidência de desempenho/lifecycle.
