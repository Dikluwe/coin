# Evidências: prédios animados e lacunas de paridade

Resultados e limites em [prédios animados](../../coin-render-animated-buildings-windows.md)
e [matriz de lacunas](../../coin-render-parity-gaps-windows-20261005.md).

`summary.json` contém as medianas por processo, agregados, faixas, p95,
comparações RGB, hashes e a tentativa com argumento CLI inválido. `runs.json`
preserva comandos e hashes de executável/biblioteca. `test-summary.json` e
JUnit registram 105 execuções em seis suítes. `hardware-and-binaries.json`
e `coingl-gpu-diagnostic.log` identificam GPU/driver/revisões binárias;
`installed-hashes.json` confirma as instalações locais.

Reprodução nesta máquina, a partir de `H:/Git/coin`:

1. Compilar a baseline `dd8483b987` separadamente e preservar seus binários nos
   diretórios `build/animated-buildings-windows-20261005/baseline-bin` e
   `wgpu-baseline-bin`. A campanha original usou cópias antes da edição.
   Os hashes esperados estão no manifesto; as DLLs não são versionadas aqui.
2. Compilar os builds atuais `coin-render-bgfx-msvc` e
   `coin-render-wgpu-msvc`, Release, targets `CoinRenderActionTest` e
   `coin_render_gl_benchmark`. Bibliotecas BGFX/Rust e cena
   `build/large-scenes/city-40000.iv` são pré-requisitos. A cena é identificada
   por SHA-256 no resumo.
3. Copiar os scripts deste diretório para
   `build/animated-buildings-windows-20261005`. `measure.py` executa processos
   GPU serialmente e retoma labels válidos existentes; para uma nova campanha
   com outros binários usar um diretório novo e ajustar `out`, preservando
   os dados desta campanha.
4. Executar `measure.py`, `summarize.py`, `qualify.ps1` e `diagnostics.py`.
   Usar Python com Pillow e NumPy. Não sobrepor builds, benchmarks ou gates
   GPU. O tracing e diagnóstico GL ficam fora dos processos de medição normais.
5. Instalar os builds nos prefixes locais qualificados e executar `archive.py`.

`profile_geometry.py` preserva a baseline BGFX e sua primeira sonda. A sonda
inicial tem 2 warmups/4 medidos; a sonda depois, identificada pelo sufixo
`-trace`, tem 5 warmups/4 medidos. São diagnósticos separados, não números
agregados da campanha principal.

PPMs, binários anteriores e executáveis atuais permanecem em `build`, fora
deste diretório. Os 84 pares antes/depois foram comparados nos PPMs originais,
sem recorte/paleta GIF. `evidence-hashes.json` identifica os arquivos desta
evidência; não inclui este README nem o próprio manifesto. O log diagnóstico
GL versionado remove espaços ao fim das linhas; o stdout original permanece
em `build` e tem seu hash em `diagnostic-raw-sha256.json`.

Não se afirma cobertura integral de CoinGL, suíte completa, qualificação de
janela/FreeCAD no Windows, execução em outras GPUs ou FPS de apresentação.
