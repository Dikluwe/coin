# Validação do agrupamento antecipado wgpu

Código `f95e99f8da`, referência `2574c776a3`.

- `benchmark/`: 24 processos, três pares por variante, 4/8 quadros; tempos, pico de RSS, checksums e SHA-256 dos PPMs.
- `fallback/`: seis processos wgpu/Vulkan, 10 mil prédios mais um Cube com escrita de profundidade desativada, 1/2 quadros.
- `trace/`: par diagnóstico Vulkan, 1/1 quadros.
- `tests/`: quatro casos CPU, onze GPU e dois controles dinâmicos.
- `comparison.json`, `metadata.json`, `baseline-binaries.json`: configuração, revisões e hashes.
- Scripts `measure*.py` e `verify*.py`: comandos completos; dependem dos builds/cópias/cenas `/tmp` documentados nos metadados.
- `discarded-vertex-transform/`: piloto sem ganho consistente, revertido integralmente.

PPMs e binários preservados nos respectivos diretórios `/tmp`, fora do Git. Para reproduzir após removê-los, compilar as revisões indicadas com a mesma configuração e regenerar as cenas.
