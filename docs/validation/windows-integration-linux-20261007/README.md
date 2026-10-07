# Evidência de integração Windows e continuação Linux

Relatório: [integração Linux](../../coin-render-windows-integration-linux-20261007.md).
`manifest.json` registra source/base, escopo, resultados e hashes dos artifacts locais.
`artifact-sha256.json` cobre todos os arquivos deste pacote, exceto ele próprio.
Os scripts preservam paths/comandos deste host; reproduzir em diretório novo.
Falhas de preparação ficam preservadas e não são contadas como passes.
SDKs e binários completos continuam locais; headers/binários são identificados
por SHA-256. Os exports e fontes exatos testados estão arquivados.
Atributo `* -text` preserva os bytes dos logs/XML/patches.
