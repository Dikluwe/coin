#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "rendering/coinrender/CoinRenderText2Capture.h"
#include "fonts/CoinRenderGlyphBridge.h"

#include <Inventor/C/base/string.h>
#include <Inventor/elements/SoComplexityElement.h>
#include <Inventor/elements/SoFontNameElement.h>
#include <Inventor/elements/SoFontSizeElement.h>
#include <Inventor/elements/SoLazyElement.h>
#include <Inventor/elements/SoModelMatrixElement.h>
#include <Inventor/elements/SoProjectionMatrixElement.h>
#include <Inventor/elements/SoViewingMatrixElement.h>
#include <Inventor/elements/SoViewportRegionElement.h>
#include <Inventor/nodes/SoText2.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <utility>

namespace {

using Status = CoinRenderText2Capture::Status;
struct FontDelete {
  void operator()(coin_render_glyph_font * value) const { coin_render_glyph_font_close(value); }
};
struct GlyphDelete {
  void operator()(coin_render_glyph_handle * value) const { coin_render_glyph_release(value); }
};
using FontOwner = std::unique_ptr<coin_render_glyph_font, FontDelete>;
using GlyphOwner = std::unique_ptr<coin_render_glyph_handle, GlyphDelete>;

struct Glyph {
  GlyphOwner owner;
  coin_render_glyph_bitmap_view bitmap;
  int x, y;
  size_t line;
  Glyph(GlyphOwner && value, const coin_render_glyph_bitmap_view & view,
        int rasterX, int rasterY, size_t lineNumber)
    : owner(std::move(value)), bitmap(view), x(rasterX), y(rasterY), line(lineNumber) {}
  Glyph(Glyph &&) = default;
  Glyph & operator=(Glyph &&) = default;
};

struct Bounds {
  int minX = 0, minY = 0, maxX = 0, maxY = 0;
  bool empty = true;
  void point(int x, int y) {
    if (empty) { minX = maxX = x; minY = maxY = y; empty = false; }
    else { minX = std::min(minX, x); minY = std::min(minY, y);
           maxX = std::max(maxX, x); maxY = std::max(maxY, y); }
  }
  void extend(const Bounds & other) {
    if (!other.empty) { point(other.minX, other.minY); point(other.maxX, other.maxY); }
  }
};

bool fail(Status value, const char * message, Status & status, std::string & diagnostic)
{
  status = value;
  diagnostic = message;
  return false;
}

bool shortRange(int64_t value)
{
  return value >= std::numeric_limits<short>::min() && value <= std::numeric_limits<short>::max();
}

bool fontNameValid(const char * name)
{
  const char * colon = std::strchr(name, ':');
  if (!colon) return true;
  const char * familyEnd = colon;
  while (familyEnd > name && familyEnd[-1] == ' ') --familyEnd;
  const char * style = colon + 1;
  while (*style == ' ') ++style;
  return familyEnd != name && *style;
}

// Count every owned RGBA+mask byte together with glyph/pass/layout metadata.
// Native glyph bytes remain owned by Coin; their aggregate is bounded too.
bool charge(size_t bytes, size_t & used)
{
  if (used > CoinRenderText2Capture::MAX_OWNED_BYTES ||
      bytes > size_t(CoinRenderText2Capture::MAX_OWNED_BYTES) - used) return false;
  used += bytes;
  return true;
}

bool initializePass(CoinRenderText2RasterPass & pass, int width, int height,
                    size_t & used, Status & status, std::string & diagnostic)
{
  if (width <= 0 || height <= 0 || !shortRange(width) || !shortRange(height))
    return fail(Status::UNSUPPORTED, "SoText2 raster dimensions exceed the short-coordinate profile", status, diagnostic);
  if (width > 8192 || height > 8192)
    return fail(Status::UNSUPPORTED, "SoText2 raster exceeds the 8192-pixel texture dimension limit", status, diagnostic);
  const size_t count = size_t(width) * size_t(height);
  if (!charge(count * 5, used))
    return fail(Status::UNSUPPORTED, "SoText2 captured raster exceeds the 16 MiB owned-byte limit", status, diagnostic);
  pass.image.width = uint32_t(width);
  pass.image.height = uint32_t(height);
  pass.image.components = 4;
  pass.image.pixelsRgba.assign(count * 4, 0);
  pass.coverageMask.assign(count, 0);
  return true;
}

bool captureImpl(const SoText2 & node, SoState * state, CoinRenderText2Raster & candidate,
                 Status & status, std::string & diagnostic)
{
  if (!state) return fail(Status::INVALID_INPUT, "SoText2 capture requires Coin state", status, diagnostic);
  const int lines = node.string.getNum();
  if (lines > CoinRenderText2Capture::MAX_LINES)
    return fail(Status::UNSUPPORTED, "SoText2 exceeds the 256-line capture limit", status, diagnostic);
  const int justification = node.justification.getValue();
  if (justification != SoText2::LEFT && justification != SoText2::RIGHT && justification != SoText2::CENTER)
    return fail(Status::INVALID_INPUT, "SoText2 justification is invalid", status, diagnostic);
  const float spacing = node.spacing.getValue();
  if (!std::isfinite(spacing))
    return fail(Status::INVALID_INPUT, "SoText2 spacing is not finite", status, diagnostic);

  size_t characters = 0, stringBytes = 0;
  std::vector<size_t> lineCharacters;
  lineCharacters.reserve(size_t(lines));
  for (int line = 0; line < lines; ++line) {
    const char * text = node.string[line].getString();
    const size_t bytes = size_t(node.string[line].getLength());
    if (bytes > size_t(CoinRenderText2Capture::MAX_STRING_BYTES) - stringBytes)
      return fail(Status::UNSUPPORTED, "SoText2 strings exceed the 1 MiB input limit", status, diagnostic);
    stringBytes += bytes;
    // Use Coin's UTF8 rules, including COIN_DISABLE_UTF8, rather than a second
    // decoder/font fallback policy. The same functions drive SoText2 GLRender.
    const size_t length = cc_string_utf8_validate_length(text);
    if (bytes && !length)
      return fail(Status::INVALID_INPUT, "SoText2 contains invalid UTF8", status, diagnostic);
    if (length > size_t(CoinRenderText2Capture::MAX_GLYPHS) - characters)
      return fail(Status::UNSUPPORTED, "SoText2 exceeds the 4096-glyph capture limit", status, diagnostic);
    characters += length;
    lineCharacters.push_back(length);
  }
  candidate.lineWidths.assign(size_t(lines), 0);
  if (!characters) return true; // Empty strings generate no raster/state draw.

  const float fontSize = SoFontSizeElement::get(state);
  const float complexity = SoComplexityElement::get(state);
  const char * fontName = SoFontNameElement::get(state).getString();
  if (!std::isfinite(fontSize) || fontSize <= 0.0f || !std::isfinite(complexity))
    return fail(Status::INVALID_INPUT, "SoText2 font size/complexity is invalid", status, diagnostic);
  if (fontSize > CoinRenderText2Capture::MAX_FONT_SIZE || std::strlen(fontName) > 1024)
    return fail(Status::UNSUPPORTED, "SoText2 font exceeds size/name capture limits", status, diagnostic);
  if (!fontNameValid(fontName))
    return fail(Status::INVALID_INPUT, "SoText2 font family/style is empty", status, diagnostic);
  const float stepValue = float(int(fontSize)) * spacing;
  // Test the floating result before any integer conversion. A finite spacing
  // can still make the float-to-integer conversion undefined for huge values.
  if (!std::isfinite(stepValue) ||
      double(stepValue) < double(std::numeric_limits<short>::min()) ||
      double(stepValue) > double(std::numeric_limits<short>::max()))
    return fail(Status::UNSUPPORTED, "SoText2 spacing exceeds the short-coordinate profile", status, diagnostic);
  const int lineStep = int(stepValue); // GL truncates size and then size*spacing.
  FontOwner font(coin_render_glyph_font_open(fontName, fontSize, complexity));
  if (!font) return fail(Status::RESOURCE_ERROR, "SoText2 font service allocation failed", status, diagnostic);

  std::vector<Glyph> glyphs;
  glyphs.reserve(characters);
  size_t used = characters * sizeof(Glyph) + size_t(lines) * (sizeof(size_t) + sizeof(int));
  size_t nativeBytes = 0;
  if (!charge(0, used))
    return fail(Status::UNSUPPORTED, "SoText2 glyph metadata exceeds the capture byte limit", status, diagnostic);
  Bounds bounds;
  int maxOverhang = std::numeric_limits<int>::min();
  int64_t ypos = 0;
  for (int line = 0; line < lines; ++line) {
    const char * text = node.string[line].getString();
    Bounds lineBounds;
    int64_t xpos = 0;
    const coin_render_glyph_handle * previous = nullptr;
    for (size_t index = 0; index < lineCharacters[size_t(line)]; ++index) {
      const uint32_t character = cc_string_utf8_get_char(text);
      text = cc_string_utf8_next_char(text);
      GlyphOwner owner(coin_render_glyph_ref(font.get(), character));
      if (!owner) return fail(Status::RESOURCE_ERROR, "SoText2 glyph service allocation failed", status, diagnostic);
      coin_render_glyph_bitmap_view bitmap = {};
      int advanceX = 0, advanceY = 0, kerningX = 0, kerningY = 0;
      if (!coin_render_glyph_bitmap(owner.get(), &bitmap) ||
          !coin_render_glyph_advance(owner.get(), &advanceX, &advanceY) ||
          (previous && !coin_render_glyph_kerning(previous, owner.get(), &kerningX, &kerningY)))
        return fail(Status::RESOURCE_ERROR, "SoText2 glyph service returned invalid metrics", status, diagnostic);
      if (!shortRange(bitmap.width) || !shortRange(bitmap.height) ||
          !charge(bitmap.byte_count, nativeBytes))
        return fail(Status::UNSUPPORTED, "SoText2 native glyph bitmap exceeds capture limits", status, diagnostic);
      const int64_t x = xpos + int64_t(kerningX) + bitmap.offset_x;
      const int64_t y = ypos + int64_t(bitmap.offset_y) - bitmap.height;
      const int64_t right = x + bitmap.width, top = y + bitmap.height;
      if (!shortRange(x) || !shortRange(y) || !shortRange(right) || !shortRange(top))
        return fail(Status::UNSUPPORTED, "SoText2 layout exceeds the short-coordinate profile", status, diagnostic);
      lineBounds.point(int(x), int(y)); lineBounds.point(int(right), int(top));
      xpos += int64_t(advanceX) + kerningX;
      if (!shortRange(xpos))
        return fail(Status::UNSUPPORTED, "SoText2 line advance exceeds the short-coordinate profile", status, diagnostic);
      previous = owner.get();
      glyphs.emplace_back(std::move(owner), bitmap, int(x), int(y), size_t(line));
    }
    bounds.extend(lineBounds);
    candidate.lineWidths[size_t(line)] = int(xpos);
    candidate.maxWidth = std::max(candidate.maxWidth, int(xpos));
    if (!lineBounds.empty) maxOverhang = std::max(maxOverhang, lineBounds.maxX - int(xpos));
    ypos -= lineStep;
  }
  // Precisely matches SoText2P::buildGlyphCache's global overhang extension.
  if (maxOverhang > std::numeric_limits<int>::min()) {
    const int64_t right = int64_t(candidate.maxWidth) + maxOverhang;
    if (!shortRange(right))
      return fail(Status::UNSUPPORTED, "SoText2 overhang exceeds the short-coordinate profile", status, diagnostic);
    bounds.point(int(right), bounds.maxY);
  }
  if (bounds.empty) return true;
  const int width = bounds.maxX - bounds.minX, height = bounds.maxY - bounds.minY;
  if (!shortRange(width) || !shortRange(height))
    return fail(Status::UNSUPPORTED, "SoText2 bounding box exceeds the short-coordinate profile", status, diagnostic);
  candidate.bboxMin[0] = bounds.minX; candidate.bboxMin[1] = bounds.minY;
  candidate.bboxMax[0] = bounds.maxX; candidate.bboxMax[1] = bounds.maxY;

  const auto & sourceViewport = SoViewportRegionElement::get(state);
  const SbVec2s viewportSize = sourceViewport.getViewportSizePixels();
  const SbVec2s viewportOrigin = sourceViewport.getViewportOriginPixels();
  CoinRenderViewportSnapshot viewport;
  viewport.width = viewportSize[0]; viewport.height = viewportSize[1];
  viewport.x = viewportOrigin[0]; viewport.y = viewportOrigin[1];
  const SbMatrix mvp = SoModelMatrixElement::get(state) * SoViewingMatrixElement::get(state) *
                       SoProjectionMatrixElement::get(state);
  if (!CoinRenderScreenRasterCore::matrixAnchor(mvp, viewport, candidate.anchor, diagnostic)) {
    status = Status::INVALID_INPUT;
    return false;
  }
  // Text2's raster helper first calls glRasterPos at nonnegative x/y. A right
  // or top raster origin beyond the viewport invalidates that raster position,
  // whereas negative x/y are shifted back with glBitmap(0,...).
  if (candidate.anchor.depthCoin < -1.0f || candidate.anchor.depthCoin > 1.0f) return true;
  if (candidate.anchor.pixelY < float(std::numeric_limits<int>::min()) ||
      double(candidate.anchor.pixelY) > double(std::numeric_limits<int>::max()))
    return fail(Status::UNSUPPORTED, "SoText2 projected anchor exceeds raster integer limits", status, diagnostic);
  const int monoAnchorY = int(candidate.anchor.pixelY);
  float screenOffsetX = candidate.anchor.pixelX + float(bounds.minX);
  if (justification == SoText2::RIGHT) screenOffsetX -= float(candidate.maxWidth);
  else if (justification == SoText2::CENTER) screenOffsetX -= float(candidate.maxWidth) / 2.0f;
  if (!std::isfinite(screenOffsetX))
    return fail(Status::INVALID_INPUT, "SoText2 projected text offset is not finite", status, diagnostic);

  const SbColor & diffuse = SoLazyElement::getDiffuse(state, 0);
  const float opacity = 1.0f - SoLazyElement::getTransparency(state, 0);
  for (int channel = 0; channel < 3; ++channel)
    if (!std::isfinite(diffuse[channel]) || diffuse[channel] < 0.0f || diffuse[channel] > 1.0f)
      return fail(Status::INVALID_INPUT, "SoText2 diffuse color is outside finite [0,1]", status, diagnostic);
  if (!std::isfinite(opacity) || opacity < 0.0f || opacity > 1.0f)
    return fail(Status::INVALID_INPUT, "SoText2 opacity is outside finite [0,1]", status, diagnostic);
  const uint8_t color[3] = {uint8_t(diffuse[0] * 255.0f), uint8_t(diffuse[1] * 255.0f), uint8_t(diffuse[2] * 255.0f)};
  const unsigned alpha = unsigned(opacity * 256.0f);
  CoinRenderText2RasterPass gray;
  bool hasGray = false;
  if (!charge((glyphs.size() + 1) * sizeof(CoinRenderText2RasterPass), used))
    return fail(Status::UNSUPPORTED, "SoText2 raster metadata exceeds the capture byte limit", status, diagnostic);
  candidate.passes.reserve(glyphs.size() + 1);
  for (const auto & glyph : glyphs) {
    const auto & bitmap = glyph.bitmap;
    if (!bitmap.bytes || !bitmap.width || !bitmap.height) continue;
    int lineOffset = 0;
    if (justification == SoText2::RIGHT) lineOffset = candidate.maxWidth - candidate.lineWidths[glyph.line];
    else if (justification == SoText2::CENTER) lineOffset = (candidate.maxWidth - candidate.lineWidths[glyph.line]) / 2;
    const int rasterX = glyph.x + lineOffset;
    const int rasterY = glyph.y;
    if (bitmap.mono) {
      CoinRenderText2RasterPass pass;
      if (!initializePass(pass, bitmap.width, bitmap.height, used, status, diagnostic)) return false;
      pass.mono = true;
      pass.pixelX = float(rasterX) + screenOffsetX;
      pass.pixelY = float(rasterY) + float(monoAnchorY);
      pass.depthCoin = candidate.anchor.depthCoin;
      // GLBitmap is one ordered operation per glyph; its material/alpha blend
      // policy is inherited, unlike the final gray glDrawPixels operation.
      const size_t rowBytes = size_t(bitmap.width) / 8;
      for (int y = 0; y < bitmap.height; ++y) for (int x = 0; x < bitmap.width; ++x) {
        const bool covered = (bitmap.bytes[size_t(y) * rowBytes + size_t(x) / 8] & (0x80u >> (x & 7))) != 0;
        const size_t pixel = size_t(y) * size_t(bitmap.width) + size_t(x);
        if (covered) {
          pass.coverageMask[pixel] = 1;
          std::fill_n(&pass.image.pixelsRgba[pixel * 4], 4, uint8_t(255));
        }
      }
      pass.image.contentDigest = CoinRenderImageCore::rgba8Digest(pass.image.pixelsRgba);
      if (pass.pixelX <= float(viewport.width) && pass.pixelY <= float(viewport.height))
        candidate.passes.push_back(std::move(pass));
    } else {
      if (!hasGray) {
        if (width == 0 || height == 0) continue;
        if (!initializePass(gray, width, height, used, status, diagnostic)) return false;
        gray.pixelX = float(std::floor(double(screenOffsetX) + 0.5));
        gray.pixelY = float(std::floor(double(candidate.anchor.pixelY) + 0.5) - height + bounds.maxY);
        gray.depthCoin = candidate.anchor.depthCoin;
        gray.alphaCutoff = 0.3f;
        gray.forceBlend = true;
        gray.materialColorBaked = true;
        hasGray = true;
      }
      const int memX = rasterX - bounds.minX;
      const int memY = height - (bounds.maxY - rasterY - 1) - 1;
      // GL logs and skips a glyph which doesn't fit its accumulated bbox.
      // Keep this literal skip rather than clipping individual glyph bytes.
      if (memX < 0 || memX + bitmap.width > width || memY < 0 || memY + bitmap.height > height) continue;
      for (int y = 0; y < bitmap.height; ++y) for (int x = 0; x < bitmap.width; ++x) {
        const size_t pixel = size_t(memY + y) * size_t(width) + size_t(memX + x);
        uint8_t * destination = &gray.image.pixelsRgba[pixel * 4];
        const unsigned source = bitmap.bytes[size_t(y) * size_t(bitmap.width) + size_t(x)];
        destination[0] = color[0]; destination[1] = color[1]; destination[2] = color[2];
        destination[3] = uint8_t((unsigned(destination[3]) * (256u - source) + alpha * source) >> 8);
      }
    }
  }
  if (hasGray) {
    for (size_t pixel = 0; pixel < gray.coverageMask.size(); ++pixel)
      gray.coverageMask[pixel] = gray.image.pixelsRgba[pixel * 4 + 3] > 76 ? 1 : 0;
    gray.image.contentDigest = CoinRenderImageCore::rgba8Digest(gray.image.pixelsRgba);
    if (gray.pixelX <= float(viewport.width) && gray.pixelY <= float(viewport.height))
      candidate.passes.push_back(std::move(gray)); // GL emits this after every mono glyph.
  }
  candidate.empty = candidate.passes.empty();
  return true;
}

} // namespace

bool
CoinRenderText2Capture::capture(const SoText2 & node, SoState * state,
                               CoinRenderText2Raster & output, Status & status,
                               std::string & diagnostic)
{
  status = Status::SUCCESS;
  diagnostic.clear();
  try {
    CoinRenderText2Raster candidate;
    if (!captureImpl(node, state, candidate, status, diagnostic)) return false;
    output = std::move(candidate);
    return true;
  } catch (const std::bad_alloc &) {
    return fail(Status::RESOURCE_ERROR, "SoText2 captured raster allocation failed", status, diagnostic);
  }
}
