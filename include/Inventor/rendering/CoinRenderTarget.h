#ifndef COIN_RENDER_TARGET_H
#define COIN_RENDER_TARGET_H
#define COIN_RENDER_LEGACY_IO_VERSION 1
#include <Inventor/CoinRenderExport.h>

/**************************************************************************\
 * Copyright (c) Kongsberg Oil & Gas Technologies AS
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
\**************************************************************************/

#include <Inventor/SbBasic.h>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <Inventor/SbVec2i32.h>
#include <Inventor/SbVec2f.h>
#include <Inventor/SbString.h>
#include <Inventor/tools/SbPimplPtr.h>
#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include <Inventor/rendering/CoinRenderOptions.h>

class CoinRenderTargetP;
class CoinRenderActionP;

/**
 * Resource-cache counters for the most recent and cumulative submissions.
 *
 * Values are snapshots and do not transfer ownership of backend resources.
 */
struct CoinRenderCacheTelemetry {
  uint64_t cumulativeUploads = 0;
  uint64_t cumulativeHits = 0;
  uint64_t cumulativeMisses = 0;
  uint64_t cumulativeUploadedBytes = 0;
  uint64_t frameUploadedBytes = 0;
  uint64_t frameUploads = 0;
  uint64_t frameHits = 0;
  uint64_t activeEntries = 0;
  uint64_t retiredEntries = 0;
  uint64_t completedSerial = 0;
  uint64_t submissionSerial = 0;
};

/**
 * Opaque experimental handle for an asynchronous offscreen readback.
 *
 * A ticket remains valid independently of the action and target lifetime.
 * Successful polling or explicit cancellation consumes it.
 */
struct CoinRenderReadbackTicket {
  uint64_t token = 0;
  uint64_t generation = 0;
  uint64_t submissionSerial = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t colorFormat = 0; // RGBA8_UNORM
  uint32_t depthFormat = 0; // 0=none, 1=DEPTH32_FLOAT
  uint32_t colorRowPitch = 0;
  uint32_t depthRowPitch = 0;
  uint64_t colorBytes = 0;
  uint64_t depthBytes = 0;
};

/** Component layouts used by the legacy SoOffscreenRenderer::getBuffer(). */
enum class CoinRenderLegacyComponents : uint8_t {
  LUMINANCE = 1,
  LUMINANCE_TRANSPARENCY = 2,
  RGB = 3,
  RGB_TRANSPARENCY = 4
};

/**
 * Experimental offscreen or native-window destination for CoinRender.
 *
 * The target owns backend attachments and presentation state, but never owns
 * native window handles supplied through CoinRenderNativeSurfaceDescriptor.
 *
 * \see coin_render_experimental
 * \see CoinRenderAction
 */
class COIN_RENDER_DLL_API CoinRenderTarget {
public:
  typedef struct ::CoinRenderCacheTelemetry CoinRenderCacheTelemetry;
  enum Status {
    TARGET_READY = 0,
    TARGET_NOT_READY,
    TARGET_LOST,
    TARGET_ERROR,
    TARGET_SURFACE_LOST
  };

  enum ReadbackStatus {
    READBACK_READY = 0,
    READBACK_NOT_READY,
    READBACK_INVALID_TICKET,
    READBACK_DEVICE_LOST,
    READBACK_ERROR,
    READBACK_UNSUPPORTED
  };

  static CoinRenderTarget * createOffscreen(const SbVec2i32 & size);
  static CoinRenderTarget * createWindow(
    const CoinRenderNativeSurfaceDescriptor & descriptor,
    const SbVec2i32 & framebufferSize);

  /** Explicit options are independent of environment defaults. */
  static CoinRenderTarget* createOffscreen(const SbVec2i32& size, const CoinRenderOptions& options);
  static CoinRenderTarget* createWindow(const CoinRenderNativeSurfaceDescriptor& descriptor,
                                        const SbVec2i32& framebufferSize,
                                        const CoinRenderOptions& options);
  const CoinRenderOptions& getOptions(void) const;

  ~CoinRenderTarget(void);
  CoinRenderTarget(const CoinRenderTarget&) = delete;
  CoinRenderTarget& operator=(const CoinRenderTarget&) = delete;

  Status getStatus(void) const;
  const char * getLastError(void) const;
  const SbVec2i32 & getSize(void) const;
  /**
   * Reconfigure the target dimensions. For offscreen targets, a valid resize
   * clears terminal submission errors and invalidates the synchronous readback
   * and submission serial, even when the size is unchanged. Render again to
   * publish a new image. Clearing the cause of an OUT_OF_MEMORY submission
   * alone does not recover a target in TARGET_ERROR.
   */
  SbBool resize(const SbVec2i32 & size);
  /**
   * Request a depth readback on offscreen renders. Enabled by default for
   * single-sample targets; multisample targets require an explicit opt-in.
   * Supported MSAA offscreen profiles resolve color by averaging samples
   * and optional depth by their minimum, for synchronous output and tickets.
   * Disabling it leaves depth testing intact, but readbackDepth() returns
   * an empty vector and async tickets omit depth. Changing the mode
   * invalidates synchronous readback until the next successful render.
   * Window targets do not support this setting.
   */
  SbBool setDepthReadbackEnabled(SbBool enabled);
  SbBool isDepthReadbackEnabled(void) const;
  /** Request RGBA8 pixels from the next window frame submission only. A normal window
   * render never allocates CPU staging. The request is consumed even if the
   * submission fails; readbackRGBA() publishes only a complete successful frame.
   * Returns FALSE for offscreen targets or an unready window.
   */
  SbBool requestWindowReadbackRGBA(void);
  void readbackRGBA(std::vector<uint8_t> & outPixels) const;
  /**
   * Convert the last synchronous offscreen RGBA8 frame to the component
   * layout and bottom-up row order of SoOffscreenRenderer::getBuffer().
   * Luminance uses Coin's 0.30 R + 0.59 G + 0.11 B truncation rule.
   * Returns FALSE and clears outPixels for an invalid layout, unavailable
   * frame, window target or unsupported output policy.
   */
  SbBool readbackLegacyComponents(CoinRenderLegacyComponents components,
                                  std::vector<uint8_t> & outPixels) const;
  /** Encode the last offscreen frame as an uncompressed SGI RGB file.
   * The requested layout selects one to four image channels. Returns FALSE
   * and clears outBytes when dimensions exceed the format or pixels are absent.
   */
  SbBool encodeLegacyRGB(CoinRenderLegacyComponents components,
                         std::vector<uint8_t> & outBytes) const;
  /** Encode an EPS image with CoinGL's ASCII85 layout. The caller supplies
   * page size in inches and pixels per inch explicitly for portability.
   */
  SbBool encodeLegacyPostScript(CoinRenderLegacyComponents components,
                                const SbVec2f & printSize, float dpi,
                                std::vector<uint8_t> & outBytes) const;
  /** Save the last offscreen frame through Coin's optional simage codecs.
   * TIFF stores LUMINANCE_TRANSPARENCY as equivalent RGBA because the simage
   * TIFF writer does not preserve interleaved two-channel LA pixels.
   */
  SbBool writeLegacyImageFile(const SbString & filename,
                              const SbString & extension,
                              CoinRenderLegacyComponents components) const;
  /** Enumerate the image savers actually supplied by the optional simage library.
   * Extensions are case-insensitive bare aliases. Invalid queries clear outputs.
   */
  static SbBool isLegacyImageFileSupported(const SbString & extension);
  static int getNumLegacyImageFileTypes();
  static SbBool getLegacyImageFileTypeInfo(int index,
    std::vector<SbString> & extensions, SbString & fullname, SbString & description);
  /** Borrow converted, bottom-up legacy pixels. Repeated queries of the same
   * layout/publication keep the pointer stable. Changing layout or a successful
   * render, resize or output-policy change invalidates it. Failed exports and
   * submissions preserve the previous publication and its borrowed storage.
   */
  const uint8_t * borrowLegacyComponents(CoinRenderLegacyComponents components,
                                         std::size_t & byteCount) const;
  /** Encode before touching the destination. FILE* is borrowed and stays open;
   * filename overloads own and close their stream, including on I/O failure.
   * An I/O error may leave partial output; it never changes target publication.
   */
  SbBool writeLegacyRGB(FILE * stream, CoinRenderLegacyComponents components) const;
  SbBool writeLegacyRGB(const char * filename, CoinRenderLegacyComponents components) const;
  SbBool writeLegacyPostScript(FILE * stream, CoinRenderLegacyComponents components,
                                const SbVec2f & printSize, float dpi) const;
  SbBool writeLegacyPostScript(const char * filename, CoinRenderLegacyComponents components,
                                const SbVec2f & printSize, float dpi) const;
  /**
   * Read the center pixel of each odd-sized block from a CPU-readable
   * offscreen target. Render at size (logical width * scale, logical height *
   * scale), with the action viewport at that same physical size, then use
   * this method to obtain the logical RGBA8 image. Scale 3 passed selected
   * AMD/Vulkan CoinGL comparisons, but can worsen other scenes and NVIDIA.
   * It is an explicit per-scene workaround, not an automatic raster policy;
   * it renders nine times as many pixels.
   * Returns FALSE and clears outPixels for invalid scale, target or readback.
   */
  SbBool readbackCenterSampledRGBA(unsigned int scale,
                                    std::vector<uint8_t> & outPixels) const;
  /**
   * Borrow the last synchronous RGBA buffer without copying it.
   * Returns NULL and sets byteCount to zero when no synchronous pixels are
   * available. The pointer belongs to this target; it is invalidated by the
   * next successful render, resize, output-policy change or target destruction.
   * Failed submissions preserve the previously published pixels and pointer. Copy it
   * if pixels must outlive that operation. This is an experimental API only.
   */
  const uint8_t * borrowRGBA(std::size_t & byteCount) const;
  void readbackDepth(std::vector<float> & outDepth) const;
  /**
   * Polls a ticket produced by CoinRenderAction::applyAsync().
   *
   * The original action and target need not remain alive. Outputs change
   * together only on READBACK_READY, which also consumes the ticket.
   */
  static ReadbackStatus pollReadback(const CoinRenderReadbackTicket & ticket,
                                     std::vector<uint8_t> & outColor,
                                     std::vector<float> & outDepth,
                                     SbString * diagnostic = nullptr);
  static SbBool cancelReadback(const CoinRenderReadbackTicket & ticket);
  /** Target-scoped increasing publication identity, not a native window presentation fence.
   * Backend/cache counters and asynchronous tickets retain their own submission
   * domain; their numeric serial need not equal this publication identity.
   * X11/compositor observation can still contain an earlier frame after this
   * serial advances. Window RGBA readback, when supported and requested,
   * captures the submitted render image rather than physical display timing.
   */
  uint64_t getLastSubmissionSerial(void) const;
  SbBool getCacheTelemetry(CoinRenderCacheTelemetry & outTelemetry) const;
  void pollDevice(void);

private:
  CoinRenderTarget(void);
  SbPimplPtr<CoinRenderTargetP> pimpl;
  friend class CoinRenderAction;
  friend class CoinRenderActionP;
  friend class CoinRenderTargetP;
#ifdef COIN_INTERNAL
public:
  SbPimplPtr<CoinRenderTargetP> & getPimpl(void) { return this->pimpl; }
  const SbPimplPtr<CoinRenderTargetP> & getPimpl(void) const { return this->pimpl; }
#endif
};

#endif // !COIN_RENDER_TARGET_H
