# C23 — câmera externa do SoRenderManager

O contrato C23 foi esclarecido e qualificado nas sete células Linux
disponíveis. `SoRenderManager::setCamera()` guarda a câmera para autoclip e
estéreo. A renderização CoinGL chama `action->apply(scene)`; ela não insere a
câmera externa no grafo nem a usa como câmera visual por si só. O adaptador
CoinRender já chama `prepareFrame()` antes de renderizar, que executa o
autoclip, e passa o mesmo grafo ao `CoinRenderSceneManager`. Encaminhar a
câmera diretamente como substituta visual mudaria a semântica do manager.

O teste de produto agora usa um cubo visível no grafo e uma câmera
`SoPerspectiveCamera` definida apenas por `setCamera()`. Confirma que o cubo
altera a imagem em relação ao fundo, que o autoclip atualiza near/far da
câmera externa e que a imagem do cubo permanece idêntica quando essa câmera
externa é definida. O teste passou em BGFX AMD/NVIDIA × Vulkan/OpenGL e wgpu
AMD/NVIDIA Vulkan + AMD OpenGL. [Logs, hashes, ambientes e runner](validation/coin-render-c23-external-camera-20261017/verification.json).

O probe BGFX/OpenGL desta fixture não expõe ID de fornecedor (`0x0000`);
a seleção de Mesa/NVIDIA é fixada pelo ambiente, e a qualificação física
dessas rotas é coberta pelos gates B01 em separado. Estéreo, superimpositions,
modos de render e overrides da action permanecem C19/C24 e R29/R30/R34.
Outras plataformas e consumidores ainda precisam de execução própria.
