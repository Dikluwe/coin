# Follow-up to SoText2 issue #417

This branch builds on PR #774, including its ABI repair commit
5ee49a197b1e56ef93ff26a975898aec969c85c8. It addresses the UTF-8 picking and
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
No public headers or FreeCAD sources are changed. The original SoText2P member
layout and exported signatures are preserved; the decoded-character picking
count reads the extended layout storage.

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

The independent SoFonts findings have now been implemented on this branch;
the proposed split below keeps them outside PR #774. The Win32 bitmap producer, CoreText, and native
Windows/macOS execution were not changed or tested here. Allocation handling
covered here is bitmap allocation/copy, not every allocation in font discovery
and glyph-cache construction.

FreeType's signed pitch, empty bitmap and pixel-mode contracts:
https://freetype.org/freetype2/docs/reference/ft2-basic_types.html#ft_bitmap

## ABI repair propagated from PR #774

The branch includes the ABI-preserving SoText2P storage extension from commit
5ee49a197b. UTF-8 picking reads the extended positions list. After integration,
all 84 CTest tests pass. A RelWithDebInfo comparison of the four changed
production units against repaired #774 reports **0 removed, 0 changed, 0 added
functions/symbols; abidiff exit 0** with the installed public headers. This
comparison isolates the follow-up changes using the same remaining library
objects and compiler flags, rather than comparing unrelated configurations.


## Additional SoFonts corrections and proposed PR split

All work is local on `codex/text-font-followup`. PR #774 remains unchanged.
The baseline includes its ABI repair; the commits below do not replace it.

| Proposed PR | Content | Commits / preparation |
| --- | --- | --- |
| UTF-8 picking | Decoded character indices, alignment and multiline hit positions | Extract the SoText2 portion and picking test of 26501c33e7, preserving the repaired extended storage |
| Bitmap backend limits | Signed pitch, checked storage, 32-bit private metrics, backend failure cleanup | Extract the font/bitmap portion and fixtures of 26501c33e7 |
| Font cache lifetime and keys | Serialized 2D/3D ref/unref, atomic font lookup/create/ref, normalized rotation key | 886925714e, 1f0413bc84, 79df06cefd |
| Fallback and representation ownership | Explicit Unicode replacement, separate bitmap/vector failure state, shared glyph references, correct per-representation FreeType size | baf8d41c45, 1efb95cc01, 5d2d0949a9 |
| Font names and discovery cleanup | Bounded family/style parsing, explicit Fontconfig patterns, cleanup on every discovery exit | 18a810ecde, 7b8c928a29 |
| 3D tessellation limits and recovery | Finite complexity, 1..16 steps, required GLU entry points, decomposition/GLU failures, checked owned allocations and rollback | dcb64a0fd6 and the subsequent native coordinate-lifetime test |

This is a review plan, not six independent cherry-picks ready for publication.
Tests and CMake entries must follow their respective code. The registry commit
introduces the private owned-acquisition helper used by the representation
fixtures; that group should land before those fixtures. `fontutils.h` is shared
by angle, Unicode and complexity fixes and must retain only each group's helpers
when preparing individual PRs. Changes to the same wrapper/backend file will
need rebasing in the order above. The first two groups need splitting because
the original follow-up commit contains both. No new PRs were opened or pushed.

### Implementation and verification

- Cache references are incremented before releasing the cache lock. Final
  release, cache removal and backend release occur under the same lock.
  Initializers use the constructor lock rather than racing on a mutex pointer.
  The old cache library crashes under the new concurrent lifetime fixture.
- Font lookup, creation and reference acquisition share one registry lock.
  `cc_flw_get_font_id_and_ref` is a new private helper; the existing exported
  borrowed-ID function remains available. Callers must retain an owning font or
  glyph reference while reading returned internal bitmap/vector pointers.
- Rotation is finite and canonicalized modulo one turn, at ten-microradian
  precision. Distinct rotations no longer share a 2D glyph. Equivalent full-turn
  representations do share the cache.
- Unsupported builtin Unicode maps to `?`, rather than its low byte. Latin-1
  remains available in the 2D builtin font; the vector builtin repertoire ends
  at ASCII 126. This remains a limited fallback font, not Unicode shaping.
- Native character availability, bitmap fallback and vector failure have separate
  state. Failed vectors are memoized. Two consumers of one wrapper glyph now
  hold two references; the first release cannot free the second's data. The
  legacy SoGlyph path balances its wrapper acquisition on final release.
- Native FreeType bitmap reads select the requested pixel size. Vector advances
  and kerning select the normalized 40-pixel vector size independently of prior
  bitmap operations. A native reproduction previously changed a 7x9 bitmap to
  22x29 after requesting a vector; it now retains dimensions, pixels and advance.
- Name parsing is bounded for null/empty strings and whitespace around `:`.
  Explicit Fontconfig property patterns and Windows drive paths are retained.
  Legacy `family:Bold Italic` produces the same fields with Fontconfig on/off.
  Fontconfig patterns have cleanup guards on success and every failure exit.
- Nonfinite complexity becomes 0.5, finite values clamp to [0,1], and curves use
  at least one and at most sixteen steps. Failed outline decomposition or GLU
  tessellation discards partial geometry, deletes the tessellator and releases
  the temporary FreeType glyph. Unsupported glyph formats return failure.
  The next request starts clean. The adapter checks its point/index/array
  allocations and size products; generic cc_list allocation failure remains a
  separate library-wide limitation.

Integrated Linux Release validation: **95 CTest tests passed, none failed or
skipped**. Added focused evidence includes:

- 8 threads, 160000 cache lifetime iterations per dimension (2D and 3D);
- 32 simultaneous font-creation rounds and 40000 acquisition/release cycles;
- 5 rotation cases and 9 Unicode fallback cases;
- 1037 parser cases each with Fontconfig enabled and forced off;
- bitmap failure then vector success, vector failure then bitmap success, and
  two-consumer lifetime (the old wrapper reports five failures);
- 6 Fontconfig success/failure stages with all created patterns destroyed;
- 36 tessellation checks, including conic/cubic outlines, pathological complexity,
  null tessellator, missing entry point, partial decomposition failure, GLU
  failure and recovery;
- 2 native FreeType size-isolation checks;
- native GLU coordinate reuse with stable vertex data.

Seven changed production units (SoText2, glyph2d, glyph3d, fontspec,
fontlib_wrapper, freetype and SoGlyph) passed targeted ASan/UBSan and
float-cast-overflow runs through the relevant fixtures. Assertions were enabled;
other library units stayed Release and leak detection was disabled. Targeted
TSan cache and registry fixtures passed with ASLR disabled **only for their
processes**; the first ordinary-ASLR run failed before main with an unexpected
memory mapping. This is not whole-library sanitizer coverage.

FreeCAD analysis was repeated using LD_PRELOAD: **44 checks passed**, with 434
bright text pixels in the corrected original/distant/control captures. FreeCAD
sources and installed libraries were not changed. Local outputs:
`/tmp/coin-text-font-freecad`, `/tmp/coin-text-font-asan-tests.log`,
`/tmp/coin-text-font-tsan-noaslr.log`, `/tmp/coin-text-font-tsan-registry.log`.

### ABI result for the additional work

A RelWithDebInfo library was relinked from the seven changed production units
and unchanged repaired-#774 objects using identical compiler flags. Public
headers are unchanged. `abidiff` against repaired #774 reports **0 removed,
0 changed, 1 added function**, with no removed/changed variables. Exit 4 reflects
only the added private `cc_flw_get_font_id_and_ref`; it is not an incompatible
change. No ABI suppressions or checker changes were introduced. The opaque
wrapper glyph acquired private fields, while SoText2P keeps the repaired ABI
prefix and old exported signatures. Evidence: `/tmp/coin-text-font-abi/abidiff.txt`.

### Correction to the original GLU lifetime hypothesis

The original SoFonts report claimed the local coordinate array had to survive
until EndPolygon. That conclusion was not established and is withdrawn. The
native GLU fixture submits a triangle through a single reused local array,
overwrites it with NaNs after each submission, and still receives exactly three
valid vertex-data indices at EndPolygon. The callback data must remain alive;
the existing allocated counters already do so. No persistent-coordinate
allocation was added to production code. This result covers the installed GLU;
other native backends and Windows/macOS execution are not claimed.

GLU reference: https://registry.khronos.org/OpenGL-Refpages/gl2.1/xhtml/gluTessVertex.xml

Remaining independent study items include generic cc_list/cache-construction
allocation failures, extractfont maintenance tooling, cross-platform native
backend checks and broader shaping/fallback policy. They are not presented as
fixed or required to close issue #417.
