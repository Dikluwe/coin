# Associações de `ScXMLEvent`: medição antes de otimizar

Base: `codex/pr/scxml-event-association-ownership` (`a4fc778e7c07fd416d0799e4b002bdb090b92f8f`), com `std::map`. Comparação: commit histórico `0f76dc8c09f0a990cc8011a8da0ac7a59843f246`, com dois `SbList` compactos; ele foi revertido posteriormente por `812a5723ef`. Isto é um estudo, não uma candidata a PR.

O programa reproduzível está em `testsuite/benchmarks/ScXMLEventAssociations.cpp`. Foram compiladas as duas revisões em Release, em diretórios separados na RAM, e executadas três rodadas alternadas, sem builds concorrentes, com 2 milhões de operações por rodada e 1, 2, 4 ou 8 associações. Cada rodada mediu busca, substituição de valor e enumeração de chaves. `sink` impede a remoção simples do resultado. O ensaio mede um objeto isolado, não um fluxo SCXML completo; as revisões também diferem em correções de ownership, portanto as diferenças não podem ser atribuídas exclusivamente ao container.

Comandos de referência, substituindo os caminhos dos dois checkouts e das duas builds correspondentes:

```
cmake -S <checkout> -B /dev/shm/coin-scxml-assoc-bench-<variante> -DCMAKE_BUILD_TYPE=Release -DCOIN_BUILD_TESTS=OFF
cmake --build /dev/shm/coin-scxml-assoc-bench-<variante> --target Coin -j 4
c++ -O2 -std=c++11 -I<checkout>/include <checkout>/testsuite/benchmarks/ScXMLEventAssociations.cpp -L/dev/shm/coin-scxml-assoc-bench-<variante>/lib -Wl,-rpath,/dev/shm/coin-scxml-assoc-bench-<variante>/lib -lCoin -o /dev/shm/coin-scxml-assoc-bench-<variante>/assoc-bench
/dev/shm/coin-scxml-assoc-bench-<variante>/assoc-bench 4 2000000
```

Os intervalos observados nas três rodadas sem concorrência, em ns/op, foram:

| Associações | Operação | `std::map` | `SbList` duplo |
| ---: | --- | ---: | ---: |
| 1 | busca / substituição / chaves | 21,09–22,90 / 32,65–33,85 / 5,85–6,06 | 20,42–22,34 / 32,48–36,28 / 3,60–4,74 |
| 2 | busca / substituição / chaves | 22,82–23,60 / 33,35–36,71 / 7,48–8,35 | 20,99–22,07 / 33,92–37,11 / 5,38–5,47 |
| 4 | busca / substituição / chaves | 22,07–23,64 / 35,00–37,95 / 12,83–13,09 | 23,09–24,47 / 34,90–38,54 / 8,23–8,82 |
| 8 | busca / substituição / chaves | 22,81–23,20 / 33,91–35,94 / 35,76–36,68 | 22,05–23,74 / 34,45–38,32 / 29,53–29,93 |

Uma primeira execução enquanto builds concorriam na máquina sugeriu aceleração maior; foi descartada. Nas rodadas controladas, a busca e a substituição são similares. A enumeração de chaves melhora alguns nanossegundos, mas não há evidência de ganho relevante em um consumidor real. O `SbList` duplo também exige provar invariantes em falha de alocação e preservar semântica de ordenação/alias de ponteiros em plataformas diferentes.

Decisão: manter a implementação de ownership com `std::map` como candidata a PR e **não** criar PR da variante compacta neste ciclo. Reabrir somente com perfil de carga SCXML real, benchmark no mesmo código-base alterando apenas o container, testes de ordenação/alias e comportamento sob falha de alocação.
