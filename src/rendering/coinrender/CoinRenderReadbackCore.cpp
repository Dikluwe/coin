#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "rendering/coinrender/CoinRenderReadbackCore.h"

bool
coin_render_complete_readback_ticket(const CoinRenderReadbackTicket &ticket, const SbVec2i32 &size,
                                     bool depthEnabled, uint64_t serial)
{
  const uint64_t rowBytes = uint64_t(ticket.width) * 4;
  const uint64_t pixels = uint64_t(ticket.width) * ticket.height;
  return ticket.token && serial && ticket.width && ticket.height && ticket.width <= 16384 &&
         ticket.height <= 16384 && ticket.submissionSerial == serial &&
         ticket.width == uint32_t(size[0]) && ticket.height == uint32_t(size[1]) &&
         ticket.colorFormat == 0 && ticket.colorRowPitch >= rowBytes &&
         ticket.colorRowPitch % 4 == 0 && ticket.colorBytes == pixels * 4 &&
         ticket.depthFormat == (depthEnabled ? 1u : 0u) &&
         (depthEnabled ? ticket.depthRowPitch >= rowBytes && ticket.depthRowPitch % 4 == 0 &&
                             ticket.depthBytes == pixels * sizeof(float)
                       : ticket.depthRowPitch == 0 && ticket.depthBytes == 0);
}
