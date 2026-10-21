#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "rendering/coinrender/CoinRenderLegacyColorCore.h"
#include <limits>
#include <new>
#include <cmath>
#include <locale>
#include <sstream>
#include <string>

bool
coin_render_convert_legacy_components(const uint8_t * rgba,
                                      uint32_t width, uint32_t height,
                                      CoinRenderLegacyComponents components,
                                      std::vector<uint8_t> & output)
{
  output.clear();
  const unsigned channels = static_cast<unsigned>(components);
  if (!rgba || width == 0 || height == 0 || channels < 1 || channels > 4)
    return false;
  const uint64_t pixels = uint64_t(width) * uint64_t(height);
  if (pixels > std::numeric_limits<size_t>::max() / channels ||
      pixels > std::numeric_limits<size_t>::max() / 4u) return false;
  if (pixels * channels > output.max_size()) return false;
  try { output.resize(static_cast<size_t>(pixels) * channels); }
  catch (const std::bad_alloc &) { return false; }
  for (uint32_t y = 0; y < height; ++y) {
    const size_t sourceRow = size_t(height - 1u - y) * width * 4u;
    const size_t outputRow = size_t(y) * width * channels;
    for (uint32_t x = 0; x < width; ++x) {
      const uint8_t * source = rgba + sourceRow + size_t(x) * 4u;
      uint8_t * destination = output.data() + outputRow + size_t(x) * channels;
      if (channels < 3) {
        const double luminance = double(source[0]) * 0.30 +
                                 double(source[1]) * 0.59 +
                                 double(source[2]) * 0.11;
        destination[0] = static_cast<uint8_t>(luminance);
        if (channels == 2) destination[1] = source[3];
      } else {
        destination[0] = source[0];
        destination[1] = source[1];
        destination[2] = source[2];
        if (channels == 4) destination[3] = source[3];
      }
    }
  }
  return true;
}

bool
coin_render_encode_legacy_rgb(const uint8_t * pixels,
                              uint32_t width, uint32_t height,
                              CoinRenderLegacyComponents components,
                              std::vector<uint8_t> & output)
{
  output.clear();
  const unsigned channels = static_cast<unsigned>(components);
  if (!pixels || !width || !height || width > 65535u || height > 65535u ||
      channels < 1 || channels > 4) return false;
  const uint64_t pixelBytes = uint64_t(width) * height * channels;
  if (pixelBytes > std::numeric_limits<size_t>::max() - 512u ||
      pixelBytes + 512u > output.max_size()) return false;
  try { output.resize(static_cast<size_t>(pixelBytes) + 512u, 0); }
  catch (const std::bad_alloc &) { return false; }
  const uint16_t header[] = {0x01da, 1, uint16_t(channels == 1 ? 2 : 3),
                             uint16_t(width), uint16_t(height), uint16_t(channels)};
  for (unsigned i = 0; i < 6; ++i) {
    output[i * 2] = uint8_t(header[i] >> 8);
    output[i * 2 + 1] = uint8_t(header[i]);
  }
  output[19] = 255;
  const char name[] = "https://github.com/coin3d/";
  for (unsigned i = 0; i < sizeof(name); ++i) output[20 + i] = uint8_t(name[i]);
  size_t destination = 512;
  for (unsigned channel = 0; channel < channels; ++channel)
    for (uint32_t y = 0; y < height; ++y)
      for (uint32_t x = 0; x < width; ++x)
        output[destination++] = pixels[(size_t(y) * width + x) * channels + channel];
  return true;
}

bool
coin_render_encode_legacy_postscript(const uint8_t * pixels,
                                    uint32_t width, uint32_t height,
                                    CoinRenderLegacyComponents components,
                                    const SbVec2f & printSize, float dpi,
                                    std::vector<uint8_t> & output)
{
  output.clear();
  const unsigned channels = static_cast<unsigned>(components);
  if (!pixels || !width || !height || width > 32767u || height > 32767u ||
      channels < 1 || channels > 4 || !std::isfinite(dpi) || dpi <= 0.0f ||
      !std::isfinite(printSize[0]) || !std::isfinite(printSize[1]) ||
      printSize[0] <= 0.0f || printSize[1] <= 0.0f) return false;
  const float pageWidth = printSize[0] * 72.0f;
  const float pageHeight = printSize[1] * 72.0f;
  const float scaledWidth = std::ceil(float(width) * 72.0f / dpi);
  const float scaledHeight = std::ceil(float(height) * 72.0f / dpi);
  if (!std::isfinite(pageWidth) || !std::isfinite(pageHeight) ||
      !std::isfinite(scaledWidth) || !std::isfinite(scaledHeight) ||
      pageWidth > 32767.0f || pageHeight > 32767.0f ||
      scaledWidth > 32767.0f || scaledHeight > 32767.0f ||
      uint64_t(width) * height > std::numeric_limits<int>::max()) return false;
  const int rasterChannels = channels <= 2 ? 1 : 3;
  try {
    std::ostringstream ps;
    ps.imbue(std::locale::classic());
    ps << "%!PS-Adobe-2.0 EPSF-1.2\n"
       << "%%BoundingBox: 0 " << int(short(pageHeight)) - int(short(scaledHeight))
       << ' ' << int(short(scaledWidth)) << ' ' << int(short(pageHeight)) << "\n"
       << "%%Creator: Coin <https://github.com/coin3d/>\n"
       << "%%EndComments\n\n/origstate save def\n\n"
       << "% workaround for bug in some PS interpreters\n"
       << "% which doesn't skip the ASCII85 EOD marker.\n"
       << "/~ {currentfile read pop pop} def\n\n"
       << "/image_wd " << width << " def\n/image_ht " << height
       << " def\n/pos_wd " << width << " def\n/pos_ht " << height
       << " def\n/image_dpi " << dpi << " def\n/image_scale 72 image_dpi div def\n"
       << "/image_chan " << rasterChannels << " def\n"
       << "/xpos_offset 0 image_scale mul def\n"
       << "/ypos_offset 0 image_scale mul def\n"
       << "/pix_buf_size " << width * rasterChannels << " def\n\n"
       << "/page_ht " << printSize[1] << " 72 mul def\n"
       << "/page_wd " << printSize[0] << " 72 mul def\n"
       << "/image_xpos 0 def\n"
       << "/image_ypos page_ht pos_ht image_scale mul sub def\n"
       << "image_xpos xpos_offset add image_ypos ypos_offset add translate\n\n"
       << "/pix pix_buf_size string def\n"
       << "image_wd image_scale mul image_ht image_scale mul scale\n\n"
       << "image_wd image_ht 8\n[image_wd 0 0 image_ht 0 0]\n"
       << "currentfile\n/ASCII85Decode filter\n";
    if (rasterChannels == 3) ps << "false 3\ncolorimage\n";
    else ps << "image\n";
    uint8_t tuple[4] = {0,0,0,0};
    unsigned tupleCount = 0;
    std::string line;
    const uint64_t count = uint64_t(width) * height;
    for (uint64_t pixel = 0; pixel < count; ++pixel) {
      for (int c = 0; c < rasterChannels; ++c) {
        tuple[tupleCount++] = pixels[pixel * channels + c];
        if (tupleCount != 4) continue;
        uint32_t value = (uint32_t(tuple[0]) << 24) | (uint32_t(tuple[1]) << 16) |
                         (uint32_t(tuple[2]) << 8) | uint32_t(tuple[3]);
        char digits[5];
        if (!value) line += 'z';
        else {
          for (int d = 4; d >= 0; --d) { digits[d] = char(value % 85 + '!'); value /= 85; }
          line.append(digits, 5);
        }
        tupleCount = 0;
        if (line.size() >= 72) { ps << line.substr(0, 72) << '\n'; line.erase(0, 72); }
      }
    }
    if (tupleCount) {
      for (unsigned i = tupleCount; i < 4; ++i) tuple[i] = 0;
      uint32_t value = (uint32_t(tuple[0]) << 24) | (uint32_t(tuple[1]) << 16) |
                       (uint32_t(tuple[2]) << 8) | uint32_t(tuple[3]);
      char digits[5];
      if (!value) line.append(tupleCount + 1, '!');
      else {
        for (int d = 4; d >= 0; --d) { digits[d] = char(value % 85 + '!'); value /= 85; }
        line.append(digits, tupleCount + 1);
      }
      if (line.size() >= 72) { ps << line.substr(0, 72) << '\n'; line.erase(0, 72); }
    }
    if (!line.empty()) ps << line << '\n';
    ps << "~>\n\norigstate restore\n\n%%Trailer\n\n%%EOF\n";
    const std::string bytes = ps.str();
    output.assign(bytes.begin(), bytes.end());
  } catch (const std::bad_alloc &) { output.clear(); return false; }
  return true;
}
