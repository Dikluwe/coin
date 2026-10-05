# Follow-up to SoText2 issue #417

This branch builds on the exact implementation submitted in PR #774, commit
3d57d697419aa4138492ecc9021a41a14fa42b9c. It addresses the UTF-8 picking and
bitmap/backend boundaries left outside that PR. The #417 study branch and PR
are preserved independently.

## Corrections

SoText2 now uses the cached decoded-character count when selecting a character.
Previously it used the UTF-8 byte length to index a list containing one entry
per character. Picking could read past the logical list and return a byte-derived
index. `SoTextDetail::getCharacterIndex()` documents decoded Unicode characters,
consistent with SoText3 and SoAsciiText; combining marks count separately.

Picking also uses the same logical line offsets as drawing, and subtracts the
bounding box's minimum pixel coordinate before normalizing glyph positions.
The bounding quad aligns by the maximum line advance, matching drawing, rather
than by the padded bitmap width. Padded fallback glyphs exposed the previous
RIGHT/CENTER mismatch. Selection still filters text outside the current view.

The FreeType adapter copies rows with a signed stride, preserving top-to-bottom
order even when pitch is negative. It validates dimensions, minimum row stride,
consumer int ranges, the padded monochrome width, and total storage before
allocation/copy. Empty bitmaps remain valid. Bitmap allocation failure and
conversion errors release the temporary glyph and return failure to the wrapper.
The pixel mode determines mono versus gray; unsupported nonempty formats fall
back instead of being interpreted as byte coverage or bits.

The wrapper validates native storage and flips owned rows in place for OpenGL.
This removes its second unchecked allocation and unsigned row-offset products.
Malformed native bitmaps use the existing default-glyph path. The opaque 2D
cache retains 32-bit dimensions and bearings rather than reducing them to
unsigned short/short. The runtime FreeType bitmap declaration matches the
signedness of the official structure without changing its binary layout.

The FreeType 26.6 size conversion uses checked long arithmetic instead of a
signed int shift. Negative/unrepresentable sizes are rejected at this boundary.
No public headers or FreeCAD sources are changed.

## Tests and evidence

Build and run:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCOIN_BUILD_TESTS=ON
cmake --build build -j4
COIN_GLX_PIXMAP_DIRECT_RENDERING=1 LIBGL_ALWAYS_SOFTWARE=1 \
  xvfb-run -a ctest --test-dir build --output-on-failure
```

- Complete suite: **84 passed, zero failed, none skipped** in the Linux
  shared-library/runtime-FreeType configuration used for this validation.
- UTF-8 selection: **96 cases passed**, using two camera types, three alignments,
  one/three repeated characters of one to four UTF-8 bytes, and cache rebuilds.
  The ray positions use advances measured from the difference between one and
  two equal glyphs, so padded fallback bitmaps do not distort the oracle.
  The test also passes with FreeType forced off. Against the unchanged #774
  library it reports **30 failures**.
- Bitmap storage: **23 cases passed**, including positive/negative pitch,
  odd-height flipping, padding, empty/null buffers, invalid ranges, 32-bit-size
  boundaries, and injected allocation failure before any source access.
- Real backend/cache with synthetic FreeType-glue fixtures: **13 cases passed**.
  Width/height 70000 and bearings -40000/+40000 survive the cache; allocations
  are only 70 KB. Invalid pitch and conversion failure release the temporary
  glyph. Tests also verify native fallback and the exact 26.6 size argument.
  Against #774, the metrics-only mode fails all three cases; the full mode
  reports size-conversion failures and terminates with SIGSEGV in the negative
  pitch case. This control was run with core dumps disabled and a 512 MB address
  limit. Fixtures require Linux/shared-library ELF interposition and skip when
  FreeType is unavailable.
- SoText2, glyph2d, fontlib_wrapper and freetype were separately compiled with
  assertions, ASan, UBSan and float-cast-overflow instrumentation. The 96 UTF-8,
  13 backend, 38 original bounds/picking, 45 extended bounds and 21 rendering
  cases pass without diagnostics. The 23 storage cases also pass in a separately
  instrumented executable. Other Coin units remain Release; leak detection was
  disabled. This is targeted validation, not whole-library sanitizer coverage.
- The adapter additionally compiles using system FreeType headers in the
  direct-link configuration.
- FreeCAD 26.3.0 was run with the new Coin library loaded through LD_PRELOAD.
  Original/distant text, manual-camera controls, Screen annotation and a Part
  box pass **44 checks**. Text captures contain 434 bright pixels, matching the
  previous corrected captures; the master controls contain zero. FreeCAD's
  installed libraries and sources remain unchanged. Existing unrelated PySide6
  workbench import warnings are not covered by these checks.

Local validation outputs are under `/tmp/coin-text-font-*`. The committed tests
provide reproducible fixtures without depending on those local files.

## Boundaries and further work

This change does not widen public short-based APIs such as SbViewportRegion or
SoGlyph::getBitmap. It does not remove FreeCAD's workaround; removal still needs
a separate FreeCAD change tied to the minimum supported Coin version.

The wider SoFonts study contains independent cache lifetime/concurrency, font
name parsing, angle keys, Unicode fallback mapping, bitmap/vector fallback
ownership, Fontconfig cleanup and GLU tessellation findings. Those are separate
from these two #417 follow-ups. The Win32 bitmap producer, CoreText, and native
Windows/macOS execution were not changed or tested here. Allocation handling
covered here is bitmap allocation/copy, not every allocation in font discovery
and glyph-cache construction.

FreeType's signed pitch, empty bitmap and pixel-mode contracts:
https://freetype.org/freetype2/docs/reference/ft2-basic_types.html#ft_bitmap
