# B05 — Part::Spline/OpenGL e ciclo de vida EGL

**Estado:** timeout original corrigido e qualificado para o perfil Linux Xwayland/Weston com BGFX/OpenGL e GPU AMD Renoir. O cache de programas não é condição necessária: o consumidor FreeCAD `Part::Spline` travou com o cache ligado e desligado.

O diagnóstico arquivado preserva seis timeouts de 45 s antes da correção. A pilha mostra a thread principal em `bgfx::frame()` e a thread BGFX esperando em `loader_dri3_set_swap_interval()` → `loader_dri3_wait_for_sbc()` → `xcb_wait_for_special_event()`. O trace de instrumentação termina após `BGFX_EGL_INTERVAL begin=makeCurrent value=0` sem o evento correspondente de conclusão. O ponto é a aplicação de `eglSwapInterval(0)` à swapchain secundária depois das apresentações iniciais.

O patch [bgfx-egl-swap-interval-before-prime.patch](../cmake/patches/bgfx-egl-swap-interval-before-prime.patch) configura o intervalo logo após `makeCurrent()` e antes dessas apresentações. Ele acompanha o valor por superfície para reaplicá-lo quando `reset` realmente mudar. A campanha arquivada teve **20/20** passes com instrumentação, **8/8** no build limpo com cache ligado e **4/4** no build limpo com cache desligado. Cada processo passou com GPU física, 100 alterações visíveis e 100 regenerações da spline, além da remoção.

Nesta revisão, conferi que a árvore BGFX `81d81fba72c42d348c589514c774bbfe01e110fa` instalada em `build-bgfx-recovery/bgfx-prefix` contém exatamente o patch (`git apply --check --reverse` passou) e que o objeto e a biblioteca instalada são posteriores à fonte alterada. O build BGFX atual do CoinRender aponta para esse prefixo. Seis testes atuais BGFX/OpenGL de janela passaram em AMD, incluindo o teste que exige pixels X11 nas duas janelas, offscreen primeiro, weighted OIT e sorted layers.

O consumidor FreeCAD completo **não foi reexecutado nesta revisão**. Sua prova é a campanha arquivada com o mesmo SDK; os testes atuais verificam a integração de janela do CoinRender, sem substituir a prova Part::Spline. Outros drivers, Windows, Wayland nativo e mudança de DPI ainda precisam de qualificação própria. A correção pertence ao SDK BGFX distribuído com o produto, não apenas ao código CoinRender.

[Recibos, hashes e verificação](validation/coin-render-b05-20261017/verification.json). Auditoria reproduzível: `python3 scripts/coinrender/audit_b05.py docs/validation/coin-render-b05-20261017`.
