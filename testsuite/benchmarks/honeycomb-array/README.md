# Coin honeycomb scale benchmark

This benchmark isolates Coin from FreeCAD and represents a regular honeycomb
with a small physical scene graph. Nested `SoArray` nodes instance one
hexagonal ring over a logical `N x N` lattice.

The requested sizes correspond to:

| Side | Logical holes |
| ---: | ---: |
| 1,000 | 1,000,000 |
| 10,000 | 100,000,000 |
| 100,000 | 10,000,000,000 |

Scene construction is compact, but Coin's current `SoArray` traversal is
linear in the logical instance count. Run bounding-box and rendering modes
under the supplied timeout so an impractical case cannot occupy the machine
indefinitely.

```sh
./run.sh /absolute/path/to/build/lib construct 1000
./run.sh /absolute/path/to/build/lib bbox 1000 60
./run.sh /absolute/path/to/build/lib render 1000 60
```

`construct` does not traverse the logical instances. `bbox` and `render` are
terminated by the runner after the requested number of seconds.

## Results

Measured on `lab/all-fixes-integration` at `56ece93a8e`, Release build, Linux,
an AMD Ryzen 7 5800H (8 cores/16 threads), 62 GiB RAM, an AMD Renoir GPU with
Mesa 25.2.8, and a 256 x 256 offscreen GLX render target. The runner enabled
`COIN_GLX_PIXMAP_DIRECT_RENDERING=1`; pbuffer selection emitted a warning and
Coin successfully fell back to the direct pixmap path.

| Grid | Logical holes | Physical `SoArray` nodes | Construct | Bounding box | Offscreen render | Peak RSS |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,000 x 1,000 | 1,000,000 | 2 | 0.000095 s | 0.774 s | 0.556 s | 88.5 MiB |
| 10,000 x 10,000 | 100,000,000 | 2 | 0.000089 s | >60 s | 49.342 s | 88.6 MiB |
| 100,000 x 100,000 | 10,000,000,000 | 4 | 0.000067 s | >10 s | >10 s | 87.7 MiB |

The largest grid needs nested arrays because `SoArray::numElements*` uses
`SoSFShort`. Construction remains constant-size, but the implementation in
`SoArray::doAction()` and `SoArray::getBoundingBox()` loops over every logical
instance. The 100-million-cell render is consistent with linear scaling from
the one-million-cell case. At that measured rate, a complete 10-billion-cell
render would take approximately 4,934 seconds (82 minutes), while its bounding
box would take roughly two hours.

This isolates the limiting behavior to traversal work rather than scene-graph
allocation. An explicit node or B-Rep per hole would add a separate and much
larger memory/construction cost that this compact Coin-only test intentionally
avoids.
