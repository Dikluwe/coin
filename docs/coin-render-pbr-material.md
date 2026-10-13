# SoPBRMaterial no CoinRender

`SoPBRMaterial` descreve uma superfície opaca pelo modelo metallic/roughness.
O nó pertence à biblioteca experimental CoinRender e usa campos, registro,
serialização e estado do Coin. Inicialize `SoDB` e `CoinRenderAction` antes de
instanciar o nó ou ler um arquivo `.iv` que o contenha.

## Campos e convenções

| Campo | Tipo Coin | Default | Domínio |
| --- | --- | --- | --- |
| `baseColor` | `SoSFColor` | `(0.8, 0.8, 0.8)` | RGB linear, cada canal de 0 a 1 |
| `metallic` | `SoSFFloat` | `0` | 0 a 1 |
| `roughness` | `SoSFFloat` | `0.4` | Rugosidade perceptual, 0 a 1 |
| `emissive` | `SoSFColor` | `(0, 0, 0)` | RGB linear HDR, cada canal de 0 a 65504 |

Valores não finitos ou fora do domínio invalidam a cena. A captura preserva os
fatores autorados; o piso de rugosidade pertence à avaliação da BRDF. O perfil
inicial exige `SoMaterialBinding::OVERALL`. Não há transparência, textura,
ambiente ou exposição no nó.

```inventor
SoPBRMaterial {
  baseColor 0.7 0.5 0.2
  metallic 1
  roughness 0.35
  emissive 0 0 0
}
```

## Estado e convivência com SoMaterial

O material afeta as formas seguintes no estado Coin. `SoSeparator` restaura o
estado anterior. Campos ignorados herdam os fatores PBR ativos; quando o estado
anterior é legado, herdam os defaults PBR. Um nó com todos os campos ignorados
não altera o modelo. Um `SoMaterial` com campos efetivos volta ao modelo legado;
campos ignorados ou bloqueados por override não causam essa transição.

O override PBR protege todos os fatores e o modelo. Overrides legados de diffuse
ou transparency impedem a ativação PBR. A aplicação do nó opaco estabelece
transparência zero na captura Coin, mesmo quando um material anterior era
totalmente transparente. Callbacks e PRUNE mantêm a ordem nativa da action.

O FramePlan tem modelo e fatores tipados. Igualdade, digest, Recording e
invalidação incluem esses dados. Revisões dos materiais e seus overrides são
acompanhadas com `SoNodeList` e `SbList`, pois `setOverride()` altera o ID do nó
sem necessariamente notificar a raiz. Overlays legados qualificados continuam
permitidos; a prova só avança após uma submissão bem-sucedida.

## Perfil M1

M1 entrega o contrato e sua captura. Recording descreve os fatores, enquanto
executores sem shading PBR recusam a submissão com `UNSUPPORTED` e conservam a
publicação anterior. A capacidade `COIN_RENDER_FEATURE_PBR_OPAQUE` permanece
desativada neste marco. CoinGL não oferece shading PBR por esse nó.

As imagens de M0 e da regressão M1 são legadas. A renderização GGX/HDR é M2;
ambiente pré-calculado é M3. Esses marcos têm critérios separados no plano e no
registro de execução.

## Base Coin

O snapshot usa `SbColor`, o nó usa `SbPimplPtr`, a compatibilidade de captura usa
`SoColorPacker` e o estado usa `SoElement`. Revisões usam as listas Coin. O viewer
e os fixtures existentes usam `SbVec*`, `SbMatrix`, `SbRotation`, `SbBox3f`,
`SbString`/`cc_string` e `cc_time`. Contêineres já pertencentes ao transporte
FramePlan e ao readback permanecem nos contratos existentes.

## Validação M1

Builds Release Recording, BGFX e Rust; 34 CTests selecionados passaram, cobrindo
o novo nó e regressões de action, cache, transformação, iluminação e publicação.
BGFX/Vulkan passou novamente nos 26 casos da matriz CAD M0. Os hashes de pixels
coincidem com a baseline armazenada. Logs, comandos e comparação estão em
`docs/validation/pbr/m1/`.

## Perfil M2

O executor BGFX/Vulkan agora admite faces PBR opacas com fatores, luzes diretas,
emissão HDR e clipping. A exposição pertence a CoinRenderOptions. Consultar
[shading](coin-render-pbr-shading.md) para saída e combinações admitidas.
Recording e Rust mantêm rejeição explícita. O contrato M1 e suas evidências
permanecem como baseline histórica.
