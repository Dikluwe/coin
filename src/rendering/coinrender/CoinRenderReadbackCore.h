#ifndef COIN_RENDER_READBACK_CORE_H
#define COIN_RENDER_READBACK_CORE_H

#include <Inventor/CoinRenderExport.h>

#include <Inventor/rendering/CoinRenderTarget.h>

// Ticket pitches describe backend staging rows; output vectors are packed.
// Padding/alignment is a GPU mechanism. Core checks a complete descriptor,
// while the connector validates its exact layout again when polling.
COIN_RENDER_DLL_API bool coin_render_complete_readback_ticket(const CoinRenderReadbackTicket& ticket,
                                                 const SbVec2i32& size, bool depthEnabled,
                                                 uint64_t serial);

#endif
