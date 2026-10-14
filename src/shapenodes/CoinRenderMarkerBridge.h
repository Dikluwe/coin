#ifndef COIN_RENDER_MARKER_BRIDGE_H
#define COIN_RENDER_MARKER_BRIDGE_H

#ifndef COIN_INTERNAL
#error this is a private header file
#endif

#include <Inventor/C/basic.h>
#include <stddef.h>

/* Private additive Coin service, provided only with COIN_BUILD_RENDER.
 * Registration owns the bottom-up/MSB-first bytes. Wiring must copy them
 * immediately: addMarker/removeMarker can replace or compact the registry.
 * No public node API, class layout, or renderer ABI is changed. */
typedef struct coin_render_marker_bitmap_view {
  const unsigned char * bytes;
  size_t byte_count;
  size_t row_stride;
  int width;
  int height;
  int alignment;
} coin_render_marker_bitmap_view;

#ifdef __cplusplus
extern "C" {
#endif

/* 1: defined bitmap (possibly empty), 0: no registered marker, -1: malformed.
 * Failure leaves output untouched. This shares native registry concurrency
 * requirements; it does not license concurrent registration and rendering. */
COIN_DLL_API int coin_render_marker_bitmap(int index, coin_render_marker_bitmap_view * output);

#ifdef __cplusplus
}
#endif

#endif
