# R30 — escrita de cor por draw para o modo HIDDEN_LINE

O plano comum agora carrega `colorWrite` (padrão `true`) no estado de cada
draw. CPU, BGFX e wgpu respeitam `false` sem desligar teste/escrita de depth.
O lowering BGFX aplica a máscara de escrita RGBA, enquanto o pipeline wgpu
usa `ColorWrites::empty()` e inclui o bit na chave de cache. A ponte privada
C++/Rust passou da revisão 52 para 53, com `CoinWgpuRenderState` de 2296 bytes.
O agrupamento de draws com escrita desligada foi desabilitado para conservar
a ordem; mudanças desse bit invalidam igualdade/reuso do plano.

O teste constrói duas faces coincidentes: a vermelha primeiro, a verde depois.
Com escrita de cor desligada na primeira, o centro permanece preto porque a
face vermelha escreve depth e oculta a verde. Ligando a cor, fica vermelho;
desligando a escrita de depth da primeira, fica verde. O controle também exige
que a mutação invalide reuso e que o FFI transporte `0` e `1` separadamente.
CPU passou nos builds BGFX e wgpu; o teste FFI wgpu e os builds passaram.
As sete células GPU Linux disponíveis passaram com executor GPU confirmado:
BGFX Vulkan/OpenGL AMD/NVIDIA e wgpu Vulkan AMD/NVIDIA/OpenGL AMD.
[Logs e hashes](validation/coin-render-r30-color-write-20261017/sha256.txt).
Os consumidores de teste que constroem diretamente o estado FFI passaram a
declarar `color_write=1`. Cinco CTests wgpu (material, luz, fog, multi-device e
surface) passaram em Vulkan AMD. A suíte Rust passou 43/43 em execução serial;
uma primeira execução paralela teve `BadAccess` ao criar EGL no teste de
depth transfer, sem falha de asserção do novo protocolo.

Isso habilita a **primitiva de execução**, não o modo `HIDDEN_LINE` do manager.
A action ainda não monta a passagem de faces depth-only seguida dos contornos,
nem há oráculo CoinGL pixel a pixel para esse modo. O adaptador continua
retornando `UNSUPPORTED` até que essas partes e os cruzamentos com
transparência/alpha, textura, offset e publicação sejam validados.
