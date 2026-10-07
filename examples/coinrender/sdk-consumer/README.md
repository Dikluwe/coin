# Consumidor público CoinRender instalado

Projeto CMake independente do build Coin, sem headers privados. Requer os
pacotes Coin e CoinRender instalados com `COIN_INSTALL_RENDER_EXPERIMENTAL=ON`
e renderer legado ligado para a referência CoinGL. Usa somente targets
importados públicos e OpenGL do sistema. O SDK compartilhado BGFX não exige
o pacote/headers BGFX no cliente; no Windows preserva `NOMINMAX`.

```sh
cmake -S examples/coinrender/sdk-consumer -B build/sdk-consumer -DCMAKE_PREFIX_PATH=/path/to/sdk
cmake --build build/sdk-consumer --config Release
```

Executar `coin-render-sdk-consumer vulkan`, `opengl` ou `d3d12`, usando o
executável/configuração e o caminho de DLLs/bibliotecas da instalação. A opção
solicita a API explicitamente. Indisponibilidade retorna diagnóstico e código 3;
nenhuma API alternativa é contada como aprovação.

O consumidor consulta capacidades, desenha um cubo BASE_COLOR vermelho em
32×32 e exige o texel central correto. Compara todos os pixels RGB com CoinGL,
normalizando a orientação, com máximo de três níveis. Imprime API, adapter,
limites de coordenadas/samplers GL e resultados. Isso é um smoke de SDK/offscreen;
não qualifica outros perfis, superfícies nativas ou oito mapas CoinGL.

Selecionar e registrar a GPU física no runner. Um smoke funcional isolado não
prova hardware: rejeitar adapter de software na qualificação e registrar hashes
dos executáveis, bibliotecas carregadas, headers e prefix de instalação. Não
misturar DLLs/bibliotecas de builds ou revisões diferentes. SDK sem referência
GL ou API indisponível não pode ser contado como equivalência de pixels.
