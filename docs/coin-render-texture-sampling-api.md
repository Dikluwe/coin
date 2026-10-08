# Política pública de sampling de texturas — primeira implementação

Implementação na branch de estudo `codex/coin-portable-sampling-study`, em 2026-10-08.
A política é uma opção imutável de cada alvo. `NATIVE` é o padrão; `PORTABLE`
reconstrói somente o filtro isotrópico `NEAREST_MIPMAP_LINEAR`. As demais
modalidades conservam a semântica nativa. O driver instalado não foi alterado.

## Uso

Com Coin e `CoinRenderAction` já inicializados, consulte o renderer que será
explicitamente usado pelo alvo:

```cpp
CoinRenderOptions options;
options.renderer = COIN_RENDER_RENDERER_VULKAN;
options.textureSamplingPolicy = COIN_RENDER_SAMPLING_PORTABLE;
CoinRenderCapabilities caps{};
if (coin_render_query_capabilities_for_renderer(
      COIN_RENDER_EXPERIMENTAL_OFFSCREEN, options.renderer,
      &caps, sizeof(caps)) != 0) { throw std::runtime_error("consulta inválida"); }
auto selection = coin_render_select_sampling_policy(
    &caps, options.textureSamplingPolicy, 1);
if (selection.reason != COIN_RENDER_SELECTION_SUPPORTED) {
  throw std::runtime_error("política indisponível ou não qualificada");
}
auto target = std::unique_ptr<CoinRenderTarget>(
    CoinRenderTarget::createOffscreen(SbVec2i32(640, 480), options));
CoinRenderAction action(SbViewportRegion(640, 480));
action.setRenderTarget(target.get());
action.apply(scene);
// Verificar status/diagnóstico e publicação pela API existente.
action.setRenderTarget(nullptr);
```

Os overloads sem opções continuam nativos. A seleção é pura: não inicializa
runtime, não lê ambiente e não aplica fallback. `require_qualified_profile=1`
exige evidência delimitada para a política; native permanece disponível com esse
argumento igual a zero. Não confundir os valores de política 0/1 com seus bits 1/2.
`COIN_SAMPLING_STUDY` deixou de selecionar shaders nesta revisão.

## Contrato e limites

O LOD usa derivadas das UV originais, antes de repeat/clamp ou centralização.
POT usa uma amostra no centro do texel do mip fino ativo, com mistura entre mips
pelo hardware; NPOT calcula os dois centros separadamente e mistura duas amostras.
Magnificação continua linear. Dimensões vêm do recurso GPU efetivamente ligado,
inclusive RTT direto. Produtores RTT herdam a política; misturas de políticas entre
alvo, plano e produtor são rejeitadas antes da submissão.

- O filtro reconstruído é o bit `1 << 2`; a anisotropia máxima nesse filtro é 1.
  Samplers ativos com anisotropia maior são recusados sem publicar imagem, trocar
  ponteiro de leitura ou avançar serial; uma próxima submissão válida recupera.
- O orçamento por cadeia de mipmaps é 128 MiB e há oito unidades. Outros limites
  de textura, formato, RTT e dispositivo continuam aplicáveis.
- Formatos compilados: RGBA8, SRGB8, RGBA16F, BC3 e BC3 SRGB. Essa máscara não
  certifica suporte do adaptador: a admissão de formato pelo dispositivo continua
  obrigatória. Alpha permanece linear em SRGB.
- CPU recording usa derivadas analíticas; wgpu Vulkan usa fine; wgpu OpenGL e
  BGFX Vulkan/OpenGL usam derivadas comuns. Fronteiras de LOD e rasterização podem
  diferir; não há promessa de identidade universal entre GPUs ou CoinGL.

Perfis de shader nativo/portátil são separados. wgpu compartilha dispositivo e
caches de recursos; o perfil portátil é criado sob demanda. Caminhos sem textura
ou de instancing conservam o perfil nativo. Não há novo resultado de desempenho
nesta rodada; medições anteriores de laboratório não certificam esta API.

## Capacidades e compatibilidade

`CoinRenderCapabilities` versão 4 separa `implemented_sampling_policies`,
`available_sampling_policies` e `qualified_sampling_policies`. Implementação não
implica adaptador disponível, e qualificação descreve evidência de um perfil de
código/teste Linux offscreen, não certificação automática do driver atual.
A consulta conserva os prefixos exatos v1/v2/v3 sem escrever além do buffer.
O seletor anterior de mecanismos continua aceitando v3. O novo seletor exige v4
completo e informa pedido inválido, implementação ausente, runtime não pronto,
hardware indisponível ou perfil não qualificado.

`CoinRenderOptions` ganhou um campo C++: recompilar consumidores experimentais.
Não se promete compatibilidade binária com objetos antigos dessa estrutura sem
versionamento. A ABI pública de libCoin não foi ampliada. O protocolo privado
C++/Rust passou de 50 para 51; tamanho do frame permanece 456 bytes, com a palavra
reservada final usada para a política. Prefixo nativo de uniformes permanece
3040 bytes e payload de instância permanece 96 bytes.

## Validação Linux

Perfis: wgpu/BGFX × AMD Vulkan, NVIDIA Vulkan e AMD OpenGL, offscreen.
Gates e referências não foram relaxados. Os testes públicos passaram mesmo com
`COIN_SAMPLING_STUDY=fetch`, provando que a opção explícita governa o resultado.

| Coorte | Processos PASS | Condição |
| --- | ---: | --- |
| Contrato CPU final | 2/2 | recording nos dois builds |
| API/capacidades e seleção finais | 12/12 | drivers instalados |
| Portátil profundo, RTT direto, viewport e procedural | 24/24 | drivers instalados |
| Native: seleção, publicação, ownership, avançadas e RTT | 30/30 | drivers instalados |
| Portátil: comparações projetivas, sampling, avançadas e RTT | 24/24 | Mesa externo para referência AMD |
| Native: oito famílias de texturas | 48/48 | Mesa externo para referência AMD |
| Recursos, instancing e sombras native | 30/30 | drivers instalados |

Total: 170/170 execuções de processo qualificadas; não são 170 casos únicos.
Adicionalmente: Rust/Naga 46/46 e probe C11 da API exportada PASS (v4=584 bytes,
prefixo v3=536). A API exercita alvos simultâneos native/portable, unidades 0/7,
desligar/reativar, wrap, oracle independente de texels, resize, leitura assíncrona,
rejeição sem publicação e recuperação. Os controles amplos cobrem perspectiva,
NPOT, formatos avançados, compressão e mipmaps RTT.

As coortes amplas foram executadas antes da última correção informativa das
capacidades BGFX (derivadas comuns); o código de sampling permaneceu igual.
API, seleção, recursos e sombras finais verificam o runtime com essa correção.
Hashes das duas revisões estão preservados. Trials anteriores — incluindo dois
SKIPs por caminho ICD incorreto, depois corrigido — permanecem no ledger e não
entram no total qualificado.

O workaround Mesa continua externo e opt-in. Native/CoinGL AMD ainda possui a
divergência de sampling nos drivers instalados; esta API não a corrige no driver.
As diferenças anteriores de recorte/iluminação CoinGL permanecem estudos abertos;
ver [fechamento das 28 falhas e condições](coin-render-failure-closure-20261008.md).
Windows, Android, janela, FreeCAD e desempenho desta API permanecem pendentes.

## Reprodução e evidências

Runner e receitas: [sampling-api](../testsuite/reproducers/sampling-api/README.md).
Ledger versionado: [manifest](validation/sampling-api-20261008/manifest.json).
Artefatos e baseline binário imutável estão em
`/mnt/Laranja/Git/externos/coin-portable-sampling-artifacts/20261008-sampling-api`.
Os diretórios build são reutilizados e mutáveis; use os hashes e cópias de baseline,
nunca trate um build atual como binário histórico. A API permanece na branch de
estudo, sem publicação ou integração na master/coin-render nesta rodada.
