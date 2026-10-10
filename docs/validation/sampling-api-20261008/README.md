# Sampling API — evidência 2026-10-08

Implementação: `a5e5a1c7782f128adaf2d4aaac8c302c202b0146`; baseline anterior: `43f00b0e18830ba79fe8fd19020d1f8ececa4983`.
170/170 execuções qualificadas, Rust46/46 e C11 PASS. Consulte `ledger.json`
para as condições e revisões; `qualified` conserva cada log e resultado.
`trials` conserva execuções substituídas e SKIPs de launcher, excluídos do total.
Logs first/second de build/Rust incluem erros de integração reparados; logs finais
registram sucesso. `legacy-guard.log` registra recusa esperada do runner antigo.

`manifest.json` contém hashes relativos de todos os arquivos deste diretório,
exceto o próprio manifest. `implementation.patch` fixa fonte; runtimes têm hashes
antes/depois da última mudança informativa das capacidades. Baseline binário e
builds persistem no root de artefatos, fora do Git; builds continuam mutáveis.
O ledger não certifica Windows/Android/janela/FreeCAD nem desempenho. Mesa é
externo e opt-in apenas nas coortes identificadas; driver instalado permanece igual.
