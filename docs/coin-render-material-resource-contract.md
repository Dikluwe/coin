# Recursos de materiais no Rust wgpu

O perfil opaco instanciado já validado conserva buffers GPU imutáveis por
dispositivo. Geometria continua exigindo a prova existente de igualdade exata
ou a identidade de um snapshot Rust owned. Uma revisão, o número de elementos
ou um ponteiro do chamador não substituem essa prova.

## Invalidação independente

Após provar a geometria, instâncias e materiais comparam separadamente todos
os bytes com o snapshot da última submissão bem sucedida. Uma alteração na
tabela pode conservar as instâncias; uma alteração nos modelos pode conservar
os materiais. Mudanças em índices de material das instâncias, normal matrices,
signed zero, comprimento ou qualquer campo de material invalidam o respectivo
buffer. Se a geometria não qualifica, o comportamento conservador permanece.

Os buffers conservados pertencem ao mesmo dispositivo e continuam imutáveis.
Miss cria um novo buffer; não modifica recursos que uma submissão anterior
pode estar usando. Novos snapshots e recursos instanciados são publicados pela
transação existente somente após sucesso. Validação, limites, falhas, retry,
destruição de dispositivos e payload nulo de câmera seguem seus contratos.

A tabela GPU só é montada se o buffer de materiais precisar ser criado.
Isso também elimina o pack descartado nos hits já existentes de frame/câmera.
O layout continua `CoinWgpuMaterial` de 72 bytes → `GpuMaterial` de 80 bytes,
com dois floats finais zero. Clear sem materiais continua usando o sentinel
GPU de 80 bytes. ABI privada e shaders permanecem iguais.

`COIN_WGPU_DISABLE_MATERIAL_RESOURCE_REUSE=1` restaura, no mesmo binário,
o pack eager no ponto original e a invalidação acoplada das duas cargas.
O runner de animação remove essa opção pelo prefixo `COIN_WGPU_DISABLE_`.

## Diagnósticos

Os eventos exigem trace de fases e descrevem tentativas; não certificam o
sucesso da submissão final por si sós:

- `rust_material_resources`: cardinalidade, bytes fonte/GPU/pack, tempos de
  comparação e pack, hits, criação de buffers e bytes enviados separadamente.
  `resource_ms` engloba a criação/reuso dos buffers de instâncias e materiais;
  não é uma duração GPU. O pack exclui a liberação posterior do vetor temporário.
- `rust_material_snapshot`: bytes copiados de materiais e instâncias; o
  `snapshot_ms` engloba todo o snapshot, incluindo geometria, states e ordem.
  Não é uma medida isolada da cópia da tabela. Snapshot de câmera compartilha
  a geometria owned e registra zero nessas cópias.
- `rust_instances`: preserva os campos anteriores e acrescenta hits/uploads
  individuais. `payload_hit` continua significando que ambos deram hit.

Estes escopos são aninhados nas fases já existentes e não devem ser somados.
O snapshot CPU ainda pode copiar materiais e instâncias em um rebuild, mesmo
quando um dos buffers GPU é reutilizado. A mudança não faz upload parcial da
tabela nem evita essas cópias obrigatórias do snapshot.

BGFX usa outro transporte: materiais são incorporados a cada instância C++ de
160 bytes, sem esta tabela Rust/GPU separada. A mudança wgpu não se aplica a
essa representação.
