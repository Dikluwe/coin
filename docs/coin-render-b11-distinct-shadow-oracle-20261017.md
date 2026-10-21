# B11 — oráculo independente para oito mapas de sombra distintos

O CoinGL nativo continua incapaz de renderizar oito mapas neste host: os
contextos disponíveis anunciam oito coordenadas de textura e o caminho legado
reserva uma unidade adicional para a cena. Esse limite do **oráculo CoinGL**
não é limite da API portátil. Para qualificar o comportamento funcional, o
teste `CoinRenderShadowEightMapTest` agora usa oito spot lights em posições
distintas ao redor de um cubo sobre o chão.

O teste mantém oito mapas ativos em todas as capturas. Usa intensidade zero
como imagem base, renderiza cada luz isolada e calcula a soma das oito
contribuições depois de subtrair a base comum. Compara essa previsão com o
quadro de oito luzes. Para cada luz, compara a imagem com e sem recepção de
sombra, mede a máscara afetada e exige que ela difira de todas as outras.
Também projeta analiticamente o topo do cubo no plano do chão a partir da
posição da luz; o centro da sombra deve ficar a até quatro pixels desse
ponto. Assim, oito mapas duplicados ou sombras no lado errado não satisfazem
o gate.

| Medida nas sete células Linux | Resultado |
| --- | ---: |
| Soma de oito luzes: maior MAE RGB | 1,08122 |
| Soma de oito luzes: maior erro por canal | 5 |
| Menor área de sombra por mapa | 1.812 pixels |
| Menor diferença entre pares de máscaras | 1.977 pixels |
| Maior erro de centro geométrico | 1,321 pixels |

Passaram BGFX AMD/NVIDIA × Vulkan/OpenGL e wgpu AMD/NVIDIA Vulkan + AMD
OpenGL. O controle negativo com as oito luzes na mesma posição falhou como
esperado: diferença mínima entre máscaras zero e erro de centro até 58,96
pixels. O controle anterior de intensidade coincidente, rejeição da nona luz,
serial e imagem preservados, resize e recuperação exata também continua no
mesmo teste. [Resultados, logs, comandos e hashes](validation/coin-render-b11-distinct-20261017/verification.json).

Este oráculo comprova oito contribuições distintas e a posição aproximada
das oclusões neste perfil. Não declara paridade pixel a pixel com CoinGL de
oito mapas, que ainda exige contexto/implementação legado capaz de usar nove
unidades. Outras geometrias, qualidade, transparência/RTT e plataformas
mantêm seus IDs de qualificação. O B11 fica resolvido para o contrato
portátil independente descrito aqui, com a referência nativa externa
registrada separadamente.
