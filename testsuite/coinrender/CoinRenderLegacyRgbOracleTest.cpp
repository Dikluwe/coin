#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include <Inventor/SoDB.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoCube.h>
#include "rendering/coinrender/CoinRenderLegacyColorCore.h"

#include <cstdio>
#include <iostream>
#include <vector>

int main() {
  SoDB::init();
  SoSeparator * root = new SoSeparator;
  root->ref();
  root->addChild(new SoPerspectiveCamera);
  root->addChild(new SoCube);
  bool ok = true;
  for (unsigned channels = 1; channels <= 4; ++channels) {
    SoOffscreenRenderer gl(SbViewportRegion(13, 9));
    gl.setComponents(static_cast<SoOffscreenRenderer::Components>(channels));
    gl.setBackgroundColor(SbColor(0.2f, 0.4f, 0.6f));
    if (!gl.render(root)) {
      std::cerr << "CoinGL offscreen context unavailable at channels=" << channels << '\n';
      root->unref();
      return 77;
    }
    const unsigned char * pixels = gl.getBuffer();
    FILE * file = std::tmpfile();
    if (!pixels || !file || !gl.writeToRGB(file) || std::fflush(file) != 0 ||
        std::fseek(file, 0, SEEK_END) != 0) {
      std::cerr << "file write failed at channels=" << channels << '\n';
      if (file) std::fclose(file);
      ok = false; break;
    }
    const long bytes = std::ftell(file);
    if (bytes < 0 || std::fseek(file, 0, SEEK_SET) != 0) {
      std::fclose(file); ok = false; break;
    }
    std::vector<uint8_t> legacy(static_cast<size_t>(bytes));
    const bool read = std::fread(legacy.data(), 1, legacy.size(), file) == legacy.size();
    std::fclose(file);
    std::vector<uint8_t> encoded;
    const bool encodedOk = coin_render_encode_legacy_rgb(pixels, 13, 9,
      static_cast<CoinRenderLegacyComponents>(channels), encoded);
    if (!read || !encodedOk || encoded != legacy) {
      std::cerr << "channels=" << channels << " read=" << read
                << " encoded=" << encodedOk << " sizes=" << legacy.size()
                << ',' << encoded.size() << '\n';
      for (size_t i = 0; i < legacy.size() && i < encoded.size(); ++i)
        if (legacy[i] != encoded[i]) {
          std::cerr << "first difference " << i << " legacy=" << unsigned(legacy[i])
                    << " encoded=" << unsigned(encoded[i]) << '\n'; break;
        }
      ok = false; break;
    }
    FILE * psfile = std::tmpfile();
    const SbVec2f printSize(8.5f, 11.0f);
    if (!psfile || !gl.writeToPostScript(psfile, printSize) ||
        std::fflush(psfile) != 0 || std::fseek(psfile, 0, SEEK_END) != 0) {
      if (psfile) std::fclose(psfile);
      std::cerr << "PostScript write failed at channels=" << channels << '\n';
      ok = false; break;
    }
    const long psbytes = std::ftell(psfile);
    if (psbytes < 0 || std::fseek(psfile, 0, SEEK_SET) != 0) {
      std::fclose(psfile); ok = false; break;
    }
    std::vector<uint8_t> legacyPs(static_cast<size_t>(psbytes));
    const bool psread = std::fread(legacyPs.data(), 1, legacyPs.size(), psfile) == legacyPs.size();
    std::fclose(psfile);
    std::vector<uint8_t> encodedPs;
    const bool psencoded = coin_render_encode_legacy_postscript(pixels, 13, 9,
      static_cast<CoinRenderLegacyComponents>(channels), printSize,
      SoOffscreenRenderer::getScreenPixelsPerInch(), encodedPs);
    if (!psread || !psencoded || encodedPs != legacyPs) {
      std::cerr << "PostScript difference: channels=" << channels
                << " sizes=" << legacyPs.size() << ',' << encodedPs.size() << '\n';
      for (size_t i = 0; i < legacyPs.size() && i < encodedPs.size(); ++i)
        if (legacyPs[i] != encodedPs[i]) {
          std::cerr << "first difference " << i << " legacy=" << unsigned(legacyPs[i])
                    << " encoded=" << unsigned(encodedPs[i]) << '\n'; break;
        }
      ok = false; break;
    }
  }
  root->unref();
  if (!ok) {
    std::cerr << "offscreen export bytes differ from SoOffscreenRenderer\n";
    return 1;
  }
  std::cout << "CoinRenderLegacyRgbOracleTest passed (SGI RGB and PostScript)\n";
  return 0;
}
