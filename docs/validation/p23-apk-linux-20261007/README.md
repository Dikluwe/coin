# P23: primeiro APK e validação Linux/AVD

Relatório: [link/APK](../../coin-render-p23-apk-validation-20261007.md).
Manifesto registra hashes de fontes/bibliotecas/APK e o escopo de qualificação.
Os SDKs/APKs/binários ficam no armazenamento permanente indicado; chaves de
assinatura de desenvolvimento não são versionadas. Os logs de falhas de
preparação, Vulkan e versões intermediárias ficam preservados, sem contar como
passes. `source-snapshot` guarda os fontes exatos da implementação testada.
`* -text` preserva bytes dos logs/XML/patches. Artifact hashes não cobrem o
próprio arquivo artifact-sha256.json. Reproduzir em diretórios novos.

`archive-selection.json` identifica por hash os logs completos mantidos localmente
e registra os filtros/excertos arquivados. O log final de lifecycle e os ELF
da versão final permanecem completos. Índices de comandos evitam repetir o
mesmo logcat completo a cada consulta; os hashes de stdout preservam a identidade.
