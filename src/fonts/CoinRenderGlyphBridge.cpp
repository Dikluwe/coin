#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fonts/CoinRenderGlyphBridge.h"
#include "fonts/fontspec.h"
#include "fonts/glyph2d.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <new>

struct coin_render_glyph_font {
  cc_font_specification specification;
};

struct coin_render_glyph_handle {
  cc_glyph2d * glyph;
  const coin_render_glyph_font * font;
};

coin_render_glyph_font *
coin_render_glyph_font_open(const char * name, float size, float complexity)
{
  if (!name || !std::isfinite(size) || size <= 0.0f || size > 1024.0f ||
      !std::isfinite(complexity)) return nullptr;
  const size_t length = std::strlen(name);
  if (length > 1024) return nullptr;
  // cc_fontspec's family:style parser assumes a nonempty family/style. Avoid
  // sending malformed names into those legacy backwards trimming loops.
  const char * colon = std::strchr(name, ':');
  if (colon) {
    const char * familyEnd = colon;
    while (familyEnd > name && familyEnd[-1] == ' ') --familyEnd;
    const char * style = colon + 1;
    while (*style == ' ') ++style;
    if (familyEnd == name || !*style) return nullptr;
  }
  coin_render_glyph_font * font = new (std::nothrow) coin_render_glyph_font;
  if (!font) return nullptr;
  // This is precisely the service used by SoText2's SoGlyphCache. In
  // particular it preserves fontconfig style parsing and the Win32 bitmap
  // size path (cc_glyph2d uses complexity=-1 for its native font request).
  cc_fontspec_construct(&font->specification, name, size, complexity);
  return font;
}

void
coin_render_glyph_font_close(coin_render_glyph_font * font)
{
  if (!font) return;
  cc_fontspec_clean(&font->specification);
  delete font;
}

coin_render_glyph_handle *
coin_render_glyph_ref(const coin_render_glyph_font * font, uint32_t character)
{
  if (!font) return nullptr;
  coin_render_glyph_handle * handle = new (std::nothrow) coin_render_glyph_handle;
  if (!handle) return nullptr;
  handle->glyph = cc_glyph2d_ref(character, &font->specification, 0.0f);
  handle->font = font;
  if (!handle->glyph) { delete handle; return nullptr; }
  return handle;
}

void
coin_render_glyph_release(coin_render_glyph_handle * glyph)
{
  if (!glyph) return;
  cc_glyph2d_unref(glyph->glyph);
  delete glyph;
}

int
coin_render_glyph_bitmap(const coin_render_glyph_handle * glyph,
                         coin_render_glyph_bitmap_view * output)
{
  if (!glyph || !glyph->glyph || !output) return 0;
  int size[2], offset[2];
  coin_render_glyph_bitmap_view candidate = {};
  candidate.bytes = cc_glyph2d_getbitmap(glyph->glyph, size, offset);
  candidate.mono = cc_glyph2d_getmono(glyph->glyph) ? 1 : 0;
  if (size[0] < 0 || size[1] < 0 || (candidate.mono && size[0] % 8)) return 0;
  const size_t rowBytes = size_t(size[0]) / (candidate.mono ? 8 : 1);
  if (size[1] && rowBytes > std::numeric_limits<size_t>::max() / size_t(size[1])) return 0;
  candidate.width = size[0]; candidate.height = size[1];
  candidate.offset_x = offset[0]; candidate.offset_y = offset[1];
  candidate.byte_count = rowBytes * size_t(size[1]);
  *output = candidate;
  return 1;
}

int
coin_render_glyph_advance(const coin_render_glyph_handle * glyph, int * x, int * y)
{
  if (!glyph || !glyph->glyph || !x || !y) return 0;
  cc_glyph2d_getadvance(glyph->glyph, x, y);
  return 1;
}

int
coin_render_glyph_kerning(const coin_render_glyph_handle * left,
                          const coin_render_glyph_handle * right, int * x, int * y)
{
  if (!left || !right || !left->glyph || !right->glyph || !x || !y ||
      left->font != right->font) return 0;
  cc_glyph2d_getkerning(left->glyph, right->glyph, x, y);
  return 1;
}
