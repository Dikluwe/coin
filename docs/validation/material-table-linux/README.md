# Evidências: tabela de materiais e diagnóstico BGFX

O relatório `../../coin-render-material-table-linux.md` separa a comparação de desempenho, a ablação de recursos de materiais e o diagnóstico BGFX. Os resultados são derivados dos CSVs e logs arquivados. Medianas por processo e faixas entre rodadas preservam aumentos de tempo e outliers.

## Mudança

No perfil instanced já validado do wgpu, recursos de instâncias e materiais usam provas independentes de igualdade byte a byte contra o último payload Rust de uma submissão bem sucedida. Mudanças de material podem conservar o buffer de instâncias; mudanças de transformação podem conservar a tabela de materiais. A prova de geometria e as admissões existentes continuam necessárias.

A tabela fonte tem 72 bytes por slot; o buffer GPU tem 80 bytes por slot, com os mesmos campos e padding zero. O packing da tabela GPU ocorre quando falta um recurso reutilizável. As cenas dimensionais têm 257/4.097 slots na base estática; materials-10 mediu 4.257/8.097 slots por causa dos clones preparados. O relatório usa as contagens efetivamente observadas. O snapshot Rust mantém o payload fonte atual, inclusive materiais sem referência. Buffers GPU são imutáveis e pertencem ao device; os novos recursos só são publicados no commit após sucesso. A opção privada `COIN_WGPU_DISABLE_MATERIAL_RESOURCE_REUSE=1` restaura a decisão acoplada e o packing literal antecipado.

Essa etapa não modifica BGFX. Seu material continua no payload de 160 bytes por instância. Os diagnósticos BGFX são evidências separadas para investigar a variação registrada na etapa anterior.

## Campanhas

As contagens finais, parâmetros, fontes compiladas e hashes constam em `stage-metadata.json` e nos manifests dos runners. A coleta exige campanhas completas antes de escrever a árvore final.

- Comparação quiet: seis casos, três rodadas e cinco papéis por caso/rodada — CoinGL, BGFX/Vulkan, BGFX/OpenGL, wgpu antes e wgpu depois. CoinGL/BGFX são controles executados uma vez por caso/rodada. São 90 processos únicos, 2.925 quadros medidos e 825 warmups; os cinco casos dinâmicos usam 5 warmups/15 quadros e large/static usa 30/120. Warmups permanecem registrados e são excluídos pelas flags do CSV.
- Ablação trace principal: 42 processos, 294 quadros medidos e 126 warmups. Os oito processos dimensionais adicionais (56 medidos e 24 warmups) ficam separados, com seus próprios parâmetros e provas. Timers de packing, comparação, recursos e snapshot são fases CPU; `snapshot_ms` inclui vários componentes, não apenas cópia de materiais. Contadores `attempt=1` descrevem a tentativa, não comprovam o commit.
- RGB antes/depois: sete casos wgpu e sete quadros lógicos por caso; 49 pares antes/depois (14 processos/98 PPMs). A comparação de imagem com CoinGL/BGFX no cenário de muitos materiais fica separada: 12 processos de controles/84 PPMs novos, pareados aos 28 quadros wgpu depois já existentes. PPMs e binários ficam fora da árvore, preservando referências, hashes, métricas, checksums e logs.
- Gates: resultados reais, nomes, comandos, categorias e marcadores do manifesto; a execução Cargo completa e seus dois novos oráculos CPU é registrada separadamente dos 12 gates. A contagem de testes é derivada da saída real Cargo. Os seis testes CPU do gerador de cenas têm log, comandos e hashes próprios.

Sem campanha nova de primeiro quadro ou fluidez em janela. O total offscreen inclui setters/notificações, renderização, espera/readback e publicação; o relatório não interpreta fases sobrepostas como parcelas aditivas de uma mediana.

## Variação histórica e diagnóstico BGFX

A campanha histórica observou +49,03% em BGFX/Vulkan geometry-10. O dado permanece no relatório e na referência histórica. Um diagnóstico posterior, na mesma fonte compilada `96e5ed80fa3ab3c9016cf10e6bbfc9b638335a33`, usou 18 processos, 270 medidos e 90 warmups nos três backends, com traces CPU e sem GPU timestamps. Ele não substitui a campanha histórica nem explica sua causa.

Esse diagnóstico conserva CSVs, stdout/stderr/logs, todas as fases e observações `/proc/stat` e `/proc/pressure/cpu` antes/depois e a cada segundo. O evento Target/BGFX/Rust pertence ao próximo marcador Action concluído, antes de selecionar warmups do CSV. `update_ms` inclui setters e notificações síncronas; não há timer separado de notify. O marcador Action antecede commit/qualificação final, portanto o residual de render fora das fases Action também contém trabalho posterior e tracing.

Snapshots de carga/hardware são observações limitadas; não identificam a causa de quadros lentos. Nenhuma rodada é descartada. O relatório mantém o diagnóstico, seu protocolo, seus ranges e as diferenças positivas de pares separados das campanhas de materiais.

## Arquivo e reprodução

O arquivo preserva inputs de cenas e geradores, scripts de execução/análise, CSVs/logs/comandos, cards de fontes e binários, hardware, resultados RGB, gates e ferramentas de relatório/validação. `stage-files.json` inventaria hashes e exclui a si próprio. Caminhos originais descrevem o ambiente medido; a recomputação usa os arquivos relativos arquivados.

Copie o relatório e esta árvore mantendo `docs/validation/material-table-linux/`. Na raiz da cópia:

```sh
python3 docs/validation/material-table-linux/validate-archive.py
```

O validador é somente leitura: recomputa CSVs, diagnósticos, provas, hashes e Markdown. A recomputação usa CPU e preserva o arquivo. Resultados ausentes ou incomparáveis impedem a coleta/geração em vez de virar resultados presumidos.
