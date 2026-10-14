#ifndef COIN_RENDER_GLYPH_BRIDGE_H
#define COIN_RENDER_GLYPH_BRIDGE_H

#ifndef COIN_INTERNAL
#error this is a private header file
#endif

#include <Inventor/C/basic.h>
#include <stddef.h>
#include <stdint.h>

/* Private, additive Coin service for CoinRender's Wiring. Build the provider
 * inside Coin only with COIN_BUILD_RENDER. No public class/layout changes and
 * no font implementation is linked into a renderer. Handles own references;
 * bitmap bytes are borrowed until the glyph is released. */
#ifdef __cplusplus
extern "C" {
#endif

typedef struct coin_render_glyph_font coin_render_glyph_font;
typedef struct coin_render_glyph_handle coin_render_glyph_handle;

typedef struct coin_render_glyph_bitmap_view {
  const unsigned char * bytes;
  size_t byte_count;
  int width;
  int height;
  int offset_x;
  int offset_y;
  int mono; /* MSB-first, bottom-up packed bits; otherwise bottom-up gray8. */
} coin_render_glyph_bitmap_view;

COIN_DLL_API coin_render_glyph_font * coin_render_glyph_font_open(
  const char * name, float size, float complexity);
COIN_DLL_API void coin_render_glyph_font_close(coin_render_glyph_font * font);
COIN_DLL_API coin_render_glyph_handle * coin_render_glyph_ref(
  const coin_render_glyph_font * font, uint32_t character);
COIN_DLL_API void coin_render_glyph_release(coin_render_glyph_handle * glyph);
COIN_DLL_API int coin_render_glyph_bitmap(
  const coin_render_glyph_handle * glyph, coin_render_glyph_bitmap_view * output);
COIN_DLL_API int coin_render_glyph_advance(
  const coin_render_glyph_handle * glyph, int * x, int * y);
COIN_DLL_API int coin_render_glyph_kerning(
  const coin_render_glyph_handle * left, const coin_render_glyph_handle * right,
  int * x, int * y);

#ifdef __cplusplus
}
#endif

#endif
