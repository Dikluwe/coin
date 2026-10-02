# P27–P30 — desdobramento do plano atual

P25 (efeitos) e P26 (execução avançada) continuam como frentes gerais. P27–P30
são fechamentos funcionais específicos dentro delas, com um dono para cada
decisão semântica. Nenhuma caixa abaixo é inferida de um bit de capacidade da
GPU. Cada fechamento exige teste de publicação/falha, comparação com o contrato
Coin quando houver, e evidência por backend/API/driver/alvo.

| Fase | Resultado que fecha | Dependências | Estado |
|---|---|---|---|
| P27 — sombras Coin | `SoShadowGroup` ativo, estilos, spot/directional, caster/receiver, mapa, bias, transparência e resize produzirem a mesma semântica Coin/GL em BGFX e wgpu | Captura Wiring de Coin/GL; plano Core de passes e ownership P12–P14; executor Infra; oráculo GL | [P27.1–P27.3 e perfis de P27.4](coin-render-p27-shadows.md) executam em BGFX/wgpu. Composição com grupos aninhados/anotações, transparência de material/textura e alfa RTT NONE/ALPHA_BLEND/ALPHA_TEST staged/direct qualificados até oito mapas. ALPHA_TEST acompanha o comportamento real Coin/GL, sem descarte automático. Peeling/OIT e qualidade ampliada com especular, normais suaves e atenuação qualificados. Cinco a sete mapas têm referência GL nativa; oito usa oráculo equivalente de sete. Overrides e caminhos parciais qualificados; [seis células físicas Linux](coin-render-p27-linux-validation.md) fechadas. NVIDIA/OpenGL PRIME e seu oráculo resolvidos. [Windows/NVIDIA D3D12 e Vulkan](coin-render-p21-windows-validation.md) qualificados, 39/39 sombras por API. Referência GL nativa com oito mapas, GPU Intel e macOS/Metal pendentes |
| P28 — efeitos e texturas espaciais | `SoTexture3` e `SoTextureCubeMap` capturados e amostrados com formatos/UV/wrap/orientação corretos; SSAO como opção tipada separada do Coin | P07 textura/UV; P24 cor/depth/multipass; P25 contrato; execução Infra e referência GL para nós Coin | Aberto: volume/cube map rejeitados quando ativos; SSAO não especificado na API |
| P29 — compute e culling | Preparação/culling opcional com decisão comum e executor por backend, resultados idênticos para cor, depth, seleção e ordem | Perfil/medições P17–P19; capacidades P11; ownership P12–P14 | Aberto: probe BGFX conhece hardware compute, sem mecanismo CoinRender |
| P30 — instancing e indirect | Lotes/instâncias/indirect opcionais, com fallback convencional equivalente e ganho medido | Plano de lotes Core P26; P29 quando preparação for GPU; transparência/camadas P09–P10 | Aberto: draws convencionais e agrupamento existente não são instancing/indirect |

## Critérios compartilhados

1. Wiring captura `SoState` e nós sem interpretar o GL no backend. Core resolve
   modalidade, ordem, passes e dependências; Infra aloca e submete recursos.
2. Uma opção não suportada retorna diagnóstico antes do submit e preserva
   imagem, depth, serial e tickets. Não há fallback visual implícito.
3. Cada executor demonstra cenas opacas/transparentes, clipping, RTT, resize,
   perda de dispositivo e múltiplos alvos conforme a fase exigir.
4. O benchmark usa a mesma cena e saída nas rotas convencional e otimizada;
   relata mediana/p95, uploads, submits, memória nominal e readback.
5. Hardware/driver sem acesso local fica no registro externo; código e testes
   executáveis aqui continuam nas fases funcionais, sem marcar qualificação remota.
