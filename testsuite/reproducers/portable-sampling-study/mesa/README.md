# Mesa Renoir: workaround externo e isolado

Este diretório é parte do **estudo**, não do backend CoinRender. O patch se
aplica à fonte oficial Mesa 25.2.8, SHA256
`097842f3e49d996868b38688db87b006f7d4541e93ce86d2f341d8b3e7be7c93`.
Ele não modifica Coin nem instala bibliotecas no sistema.

A transformação NIR fica limitada a Renoir, sampler 2D normalizado, não array,
sem comparação, float32, minificação nearest/mip-linear, magnificação linear,
sem anisotropia, sampler bias e minLOD zero. Offsets, gradientes explícitos e
fontes de instrução não suportadas conservam o caminho original. O sampler
continua ligado: magnificação usa a amostra nativa; minificação calcula LOD
pelas UV originais e centraliza separadamente em cada mip, inclusive NPOT.
Formatos continuam sendo decodificados pelo hardware.

O protótipo calcula a amostra nativa antes do ramo dependente de LOD, para
preservar derivadas implícitas. Portanto minificação pode executar **três
amostras**: uma nativa e duas reconstruídas. Não foi qualificado em desempenho,
outros chips ou aplicações gerais; não deve ser apresentado como driver pronto
para distribuição. Radeonsi usa ACO no fragmento; RADV também recebe o passo.

O guard `COIN_MESA_MIXED_FILTER_STUDY` é por **presença**. Para desativar, remova
a variável; o valor `0` também habilita. O guard não integra a chave do cache de
shaders: os launchers desabilitam o cache em ambos os controles A/B. Variáveis e
bibliotecas ficam restritas ao processo filho.

## Repetição

Extraia a fonte verificada em um diretório permanente e aplique:

```sh
patch -d "$mesa_source" -p1 -i "$study_repo/testsuite/reproducers/portable-sampling-study/mesa/mesa-25.2.8-renoir-mixed-filter.patch"
```

Use um prefixo privado, Meson 1.4.2 ou compatível, Mako, PyYAML e as dependências
de desenvolvimento registradas na evidência. Os comandos exatos, dependências,
bibliotecas e hashes deste PC ficam em
`docs/validation/failure-closure-20261008/mesa-build`. A variante ACO usa
`-Dllvm=disabled -Damd-use-llvm=false`. A variante com LLVM usa LLVM 18.1.3,
`-Dllvm=enabled -Damd-use-llvm=true`; o launcher pode manter o vertex shader em
LLVM e o fragmento em ACO com `--gl-compiler llvm-vs-aco-ps`.

Outras opções da compilação: Release, gallium radeonsi, Vulkan amd, platform x11,
GLX dri, EGL enabled, GBM/VA/VDPAU/build-tests disabled e video-codecs vazio.
Antes de instalar, confira que **todos** os destinos do install-plan do Meson
pertencem ao prefixo privado. Nenhum comando deste laboratório deve apontar o
prefixo para `/usr`, `/usr/local` ou a instalação do driver do sistema.

```sh
python3 "$study_repo/testsuite/reproducers/portable-sampling-study/mesa/with_mesa.py" --prefix "$mesa_prefix" -- "$program"
python3 "$study_repo/testsuite/reproducers/portable-sampling-study/mesa/with_mesa.py" --prefix "$mesa_prefix" --disabled -- "$program"
python3 "$study_repo/testsuite/reproducers/portable-sampling-study/mesa/run_cases.py" --artifacts "$study_artifacts" --mesa-prefix "$mesa_prefix" --name qualification --profiles all6 --cases projective,sampling,advanced,procedural,rtt,deep,direct,viewport,large,devices,stress,instancing,shadow-eight,shadow-viewport,shadow-alpha,curved,styles
```

`study_artifacts` deve conter os builds wgpu/BGFX da branch de estudo. Os testes
GPU são sequenciais, com referência CoinGL obrigatória e sem mudar os gates.
Exit77/SKIP não conta como aprovação. Os programas inaplicáveis a um backend
(LargeBindings/MultiDevice no BGFX, instancing específico no wgpu) não são
lançados. NVIDIA usa suas bibliotecas instaladas, como contracontrole.

A sonda `../sampler_state_probe.cpp` compila com `-lEGL -lOpenGL` e não liga Coin.
Ela é diagnóstica: aceita a divergência do modo nativo e exige os controles
independentes. Para fechar as falhas, use os testes integrados estritos e o
ledger de 28 resultados; exit0 da sonda isolada não prova paridade nativa.

Fontes primárias:
[Mesa 25.2.8](https://docs.mesa3d.org/relnotes/25.2.8.html) e
[explicação Mesa de TRUNC_COORD e filtros mistos](https://cgit.freedesktop.org/mesa/mesa/commit/?id=6fac2889317e80b601ac63f7e46047bf8893d597).

A variante LLVM foi um contracontrole descartado: a mesma borda CoinGL falhou
com ela, com ACO, com o driver instalado e com o runtime anterior congelado.
A campanha principal usa o prefixo ACO. `--cases portable-styles` executa o
contrato CPU/GPU sem comparação CoinGL opcional; é uma coorte separada. O caso
`styles` continua exigindo CoinGL, mantém os FAIL e não tem tolerância alterada.
