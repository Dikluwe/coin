# Reserva de updates: piloto A/B Windows offscreen

Cena city-2500, 256 × 256, publicação RGBA síncrona. Seis casos, seis APIs/backend,
controles literal/reserva e CoinGL. Três processos por variante/caso; cada um usa
10 quadros de aquecimento e 60 amostras. Mediana/p95 abaixo combinam 180 amostras.
Primeiro quadro é de processo novo; o cache do driver não foi limpo.

Verificação visual separada: 78 processos, 546 imagens, 252 pares de quadros
literal/reserva idênticos. Medição: 234 processos, sem captura de imagens ou tracing.
Foram excluídos os 13 processos de geometry-10 da primeira rodada, sobrepostos
à auditoria CPU de pixels. O bloco inteiro foi repetido isoladamente ao final,
na mesma ordem intercalada, com manifests idênticos. Os 247 processos tentados
e os 13 registros excluídos foram preservados; a análise usa 234 processos.
Ordem intercalada/rotacionada. Estado/energia GPU e plano de energia amostrados
por rodada. A campanha não alterou display/driver; não houve auditoria contínua
do estado físico dos monitores. Este piloto não qualifica janela sem readback.

Razão reserva/literal: abaixo de 1 indica menor tempo; acima de 1 indica regressão.

| Caso | Backend/API | Literal mediana/p95 (ms) | Reserva mediana/p95 (ms) | Razão mediana/p95 |
| --- | --- | ---: | ---: | ---: |
| static | bgfx/d3d12 | 2.010/3.303 | 1.804/3.392 | 0.898/1.027 |
| static | bgfx/vulkan | 0.818/1.126 | 0.816/1.095 | 0.997/0.973 |
| static | bgfx/opengl | 1.103/1.866 | 1.139/1.781 | 1.032/0.955 |
| static | wgpu/dx12 | 1.634/2.932 | 1.600/2.685 | 0.979/0.916 |
| static | wgpu/vulkan | 1.341/2.359 | 1.384/2.873 | 1.032/1.218 |
| static | wgpu/gl | 4.329/10.761 | 4.225/6.615 | 0.976/0.615 |
| camera | bgfx/d3d12 | 2.177/3.123 | 2.209/3.531 | 1.015/1.131 |
| camera | bgfx/vulkan | 1.179/1.623 | 1.087/1.685 | 0.921/1.039 |
| camera | bgfx/opengl | 1.231/2.338 | 1.227/2.299 | 0.997/0.983 |
| camera | wgpu/dx12 | 1.932/3.287 | 2.045/3.598 | 1.059/1.094 |
| camera | wgpu/vulkan | 1.769/2.643 | 1.887/3.037 | 1.067/1.149 |
| camera | wgpu/gl | 4.635/6.892 | 4.623/5.938 | 0.998/0.862 |
| transforms-10 | bgfx/d3d12 | 7.404/9.007 | 7.284/8.876 | 0.984/0.985 |
| transforms-10 | bgfx/vulkan | 4.909/6.139 | 5.209/6.171 | 1.061/1.005 |
| transforms-10 | bgfx/opengl | 5.387/6.597 | 5.378/6.673 | 0.998/1.012 |
| transforms-10 | wgpu/dx12 | 7.408/8.769 | 7.458/9.129 | 1.007/1.041 |
| transforms-10 | wgpu/vulkan | 7.460/9.111 | 7.196/8.136 | 0.965/0.893 |
| transforms-10 | wgpu/gl | 10.109/12.190 | 10.469/12.325 | 1.036/1.011 |
| materials-10 | bgfx/d3d12 | 8.227/9.584 | 8.050/9.714 | 0.978/1.014 |
| materials-10 | bgfx/vulkan | 6.078/7.381 | 6.303/7.623 | 1.037/1.033 |
| materials-10 | bgfx/opengl | 6.575/7.689 | 6.510/7.946 | 0.990/1.033 |
| materials-10 | wgpu/dx12 | 7.874/9.000 | 7.976/10.343 | 1.013/1.149 |
| materials-10 | wgpu/vulkan | 7.667/8.670 | 7.549/9.401 | 0.985/1.084 |
| materials-10 | wgpu/gl | 10.937/13.223 | 10.937/13.078 | 1.000/0.989 |
| geometry-10 | bgfx/d3d12 | 9.044/11.076 | 9.058/10.471 | 1.001/0.945 |
| geometry-10 | bgfx/vulkan | 6.473/7.962 | 6.774/8.480 | 1.046/1.065 |
| geometry-10 | bgfx/opengl | 7.024/9.944 | 6.824/7.956 | 0.971/0.800 |
| geometry-10 | wgpu/dx12 | 8.882/10.677 | 8.511/9.768 | 0.958/0.915 |
| geometry-10 | wgpu/vulkan | 8.267/10.383 | 8.525/10.293 | 1.031/0.991 |
| geometry-10 | wgpu/gl | 11.857/13.792 | 11.682/13.396 | 0.985/0.971 |
| geometry-100 | bgfx/d3d12 | 29.009/31.699 | 28.344/30.603 | 0.977/0.965 |
| geometry-100 | bgfx/vulkan | 27.590/29.540 | 27.133/29.922 | 0.983/1.013 |
| geometry-100 | bgfx/opengl | 27.636/29.469 | 26.656/28.808 | 0.965/0.978 |
| geometry-100 | wgpu/dx12 | 28.273/30.287 | 26.397/29.445 | 0.934/0.972 |
| geometry-100 | wgpu/vulkan | 28.672/30.958 | 26.227/27.959 | 0.915/0.903 |
| geometry-100 | wgpu/gl | 31.473/34.625 | 30.719/34.565 | 0.976/0.998 |

O JSON contém update/render/publicação, p99, primeiro quadro e as medianas
dos três processos. Logs/CSV, manifests de hashes, estado GPU e controles
CoinGL acompanham a evidência. Diferenças nesta amostra não são uma estimativa
universal de ganho nem uma campanha completa de latência.
