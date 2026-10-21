#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "glue/simage_wrapper.h"
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderDiagnosticShell.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/nodes/SoImage.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {
const int width = 17, height = 13;
bool good = true;
bool require(bool value, const std::string &label) {
  if (!value) {
    good = false;
    std::cerr << "legacy_codec FAIL " << label << '\n';
  }
  return value;
}
struct Directory {
  std::string path;
  std::vector<std::string> files;
  Directory() {
    const char *base = std::getenv("TMPDIR");
    std::string pattern =
        std::string(base ? base : "/tmp") + "/coin-codecs-XXXXXX";
    std::vector<char> bytes(pattern.begin(), pattern.end());
    bytes.push_back(0);
    char *result = mkdtemp(bytes.data());
    if (result)
      path = result;
  }
  std::string file(const std::string &name) {
    std::string result = path + "/" + name;
    files.push_back(result);
    return result;
  }
  ~Directory() {
    for (const auto &f : files)
      std::remove(f.c_str());
    if (!path.empty())
      rmdir(path.c_str());
  }
};
std::vector<uint8_t> readFile(const std::string &name) {
  std::ifstream f(name.c_str(), std::ios::binary);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(f),
                              std::istreambuf_iterator<char>());
}
std::vector<uint8_t> authored(int phase) {
  std::vector<uint8_t> rgba(width * height * 4);
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x) {
      size_t p = (y * width + x) * 4;
      rgba[p] = uint8_t(19 + (x * 11 + y * 3 + phase * 37) % 211);
      rgba[p + 1] = uint8_t(13 + (x * 2 + y * 17 + phase * 29) % 223);
      rgba[p + 2] = uint8_t(7 + (x * 7 + y * 5 + phase * 43) % 239);
      rgba[p + 3] = uint8_t(31 + (x * 5 + y * 9 + phase * 17) % 211);
    }
  return rgba;
}
std::vector<uint8_t> topDown(const std::vector<uint8_t> &source) {
  std::vector<uint8_t> out(source.size());
  for (int y = 0; y < height; ++y)
    std::copy(source.begin() + y * width * 4,
              source.begin() + (y + 1) * width * 4,
              out.begin() + (height - 1 - y) * width * 4);
  return out;
}
// Public legacy buffers and simage use bottom-up rows. Derive luminance from
// authored bytes, never readbackLegacyComponents or an encoder under test.
std::vector<uint8_t> components(const std::vector<uint8_t> &source,
                                int channels) {
  std::vector<uint8_t> out(width * height * channels);
  for (int p = 0; p < width * height; ++p) {
    const uint8_t *q = &source[p * 4];
    uint8_t *d = &out[p * channels];
    if (channels <= 2) {
      d[0] =
          uint8_t(.30 * double(q[0]) + .59 * double(q[1]) + .11 * double(q[2]));
      if (channels == 2)
        d[1] = q[3];
    } else {
      for (int c = 0; c < channels; ++c)
        d[c] = q[c];
    }
  }
  return out;
}
std::vector<uint8_t> tiffExpand(const std::vector<uint8_t> &la) {
  std::vector<uint8_t> rgba(la.size() * 2);
  for (size_t p = 0; p < la.size() / 2; ++p) {
    rgba[p * 4] = rgba[p * 4 + 1] = rgba[p * 4 + 2] = la[p * 2];
    rgba[p * 4 + 3] = la[p * 2 + 1];
  }
  return rgba;
}
struct Scene {
  SoSeparator *root = new SoSeparator;
  SoImage *image = new SoImage;
  Scene() {
    root->ref();
    auto *camera = new SoOrthographicCamera;
    camera->position.setValue(0, 0, 4);
    camera->height = 2;
    camera->nearDistance = 1;
    camera->farDistance = 10;
    root->addChild(camera);
    auto *model = new SoLightModel;
    model->model = SoLightModel::BASE_COLOR;
    root->addChild(model);
    image->horAlignment = SoImage::CENTER;
    image->vertAlignment = SoImage::HALF;
    root->addChild(image);
    set(0);
  }
  void set(int phase) {
    auto bytes = authored(phase);
    image->image.setValue(SbVec2s(width, height), 4, bytes.data());
  }
  ~Scene() { root->unref(); }
};
struct Publication {
  std::vector<uint8_t> rgba;
  std::vector<float> depth;
  uint64_t serial = 0;
};
Publication publication(CoinRenderTarget &t) {
  Publication out;
  t.readbackRGBA(out.rgba);
  t.readbackDepth(out.depth);
  out.serial = t.getLastSubmissionSerial();
  return out;
}
bool same(CoinRenderTarget &t, const Publication &old,
          const std::string &label) {
  auto now = publication(t);
  return require(now.rgba == old.rgba && now.depth == old.depth &&
                     now.serial == old.serial,
                 label + " preserves exact RGBA/depth/serial");
}
bool render(CoinRenderAction &a, CoinRenderTarget &t, Scene &s, int phase) {
  s.set(phase);
  const auto id = s.root->getNodeId(), imageId = s.image->getNodeId();
  const auto serial = t.getLastSubmissionSerial();
  a.apply(s.root);
  auto p = publication(t);
  bool pass = require(a.getLastStatus() == CoinRenderAction::SUCCESS &&
                          p.serial == serial + 1 &&
                          p.rgba.size() == width * height * 4 &&
                          p.depth.size() == width * height,
                      "one successful logical publication");
  if (pass) {
    auto expected = topDown(authored(phase));
    unsigned bad = 0;
    for (size_t i = 0; i < expected.size(); ++i)
      bad += p.rgba[i] != expected[i];
    unsigned badDepth = 0;
    for (float z : p.depth)
      badDepth += std::abs(z - 1.f / 3.f) > 2e-5f;
    std::cout << "legacy_publication phase=" << phase << " rgba_bad=" << bad
              << " depth_bad=" << badDepth << " serial=" << p.serial << '\n';
    pass &= require(bad == 0 && badDepth == 0,
                    "unlit authored RGBA copy and analytic interior depth");
  }
  return require(s.root->getNodeId() == id && s.image->getNodeId() == imageId,
                 "apply preserves source revision") &&
         pass;
}
struct Family {
  std::string name;
  std::vector<std::string> aliases;
};
std::vector<Family> families() {
  return {{"jpeg", {"jpg", "jpeg"}},
          {"gif", {"gif"}},
          {"sgi", {"rgb", "rgba", "bw", "inta", "int"}},
          {"ps", {"eps", "ps"}},
          {"png", {"png"}},
          {"tiff", {"tiff", "tif"}}};
}
bool signature(const std::vector<uint8_t> &b, const std::string &family) {
  if (b.size() < 12)
    return false;
  if (family == "jpeg")
    return b[0] == 255 && b[1] == 216 && b[b.size() - 2] == 255 &&
           b.back() == 217;
  if (family == "gif")
    return std::memcmp(b.data(), "GIF8", 4) == 0 &&
           b[6] + 256 * b[7] == width && b[8] + 256 * b[9] == height;
  if (family == "sgi")
    return b[0] == 1 && b[1] == 218 && b[6] * 256 + b[7] == width &&
           b[8] * 256 + b[9] == height;
  if (family == "ps")
    return b[0] == '%' && b[1] == '!' &&
           std::string(b.begin(), b.end()).find("BoundingBox") !=
               std::string::npos;
  if (family == "png")
    return std::memcmp(b.data(), "\211PNG\r\n\032\n", 8) == 0;
  return (b[0] == 'I' && b[1] == 'I' && b[2] == 42 && b[3] == 0) ||
         (b[0] == 'M' && b[1] == 'M' && b[2] == 0 && b[3] == 42);
}
// PostScript has no simage decoder. Compare the entire saver stream, including
// dimensions, image matrix, operators and pixel data, ignoring ONLY descriptive
// filename/date metadata. This does not compare encodeLegacyPostScript output.
std::string psContent(const std::vector<uint8_t> &bytes,
                      const std::string &filename) {
  std::string text(bytes.begin(), bytes.end());
  size_t pos = 0;
  while ((pos = text.find(filename, pos)) != std::string::npos) {
    text.replace(pos, filename.size(), "<filename>");
    pos += 10;
  }
  std::istringstream lines(text);
  std::string line, result;
  while (std::getline(lines, line)) {
    if (line.compare(0, 8, "%%Title:") == 0 ||
        line.compare(0, 15, "%%CreationDate:") == 0)
      continue;
    result += line + '\n';
  }
  return result;
}
struct Decoded {
  int w = 0, h = 0, n = 0;
  std::vector<uint8_t> bytes;
};
Decoded decode(const simage_wrapper_t *codec, const std::string &filename) {
  Decoded d;
  unsigned char *raw =
      codec->simage_read_image(filename.c_str(), &d.w, &d.h, &d.n);
  if (raw && d.w > 0 && d.h > 0 && d.n > 0 && d.n <= 4) {
    d.bytes.assign(raw, raw + size_t(d.w) * d.h * d.n);
  }
  if (raw)
    codec->simage_free_image(raw);
  return d;
}
unsigned accepted = 0, nativeRefused = 0, unsupportedCase = 0;
bool one(CoinRenderTarget &target, const simage_wrapper_t *codec,
         Directory &dir, const Family &family, const std::string &alias,
         int channels, int phase) {
  const auto stable = publication(target);
  const auto layout = static_cast<CoinRenderLegacyComponents>(channels);
  std::string tag = family.name + "_" + alias + "_" + std::to_string(channels) +
                    "_" + std::to_string(phase);
  const std::string ref = dir.file(tag + "-native.wrong"),
                    actual = dir.file(tag + "-product.other");
  auto bytes = components(authored(phase), channels);
  int nativeChannels = channels;
  // Preserve the established TIFF LA->RGBA policy as the sole explicit
  // conversion exception. Other savers receive the exact requested channels.
  if (family.name == "tiff" && channels == 2) {
    bytes = tiffExpand(bytes);
    nativeChannels = 4;
  }
  const bool supported = codec->simage_check_save_supported(alias.c_str()) != 0;
  if (!supported) {
    ++unsupportedCase;
    bool pass =
        require(!target.writeLegacyImageFile(SbString(actual.c_str()),
                                             SbString(alias.c_str()), layout),
                tag + " declared unsupported casing/extension refuses");
    return same(target, stable, tag) && pass;
  }
  const bool expected =
      codec->simage_save_image(ref.c_str(), bytes.data(), width, height,
                               nativeChannels, alias.c_str()) != 0;
  const char *nativeMessage =
      codec->simage_get_last_error ? codec->simage_get_last_error() : nullptr;
  const std::string nativeError = nativeMessage ? nativeMessage : "";
  const bool written =
      target.writeLegacyImageFile(SbString(actual.c_str()),
                                  SbString(alias.c_str()), layout) != FALSE;
  std::cout << "legacy_codec family=" << family.name << " alias=" << alias
            << " channels=" << channels << " native_channels=" << nativeChannels
            << " native=" << expected << " product=" << written
            << " native_error=" << nativeError << '\n';
  bool pass = require(written == expected,
                      tag + " saver-native acceptance/refusal parity");
  if (!expected) {
    ++nativeRefused;
    return same(target, stable, tag) && pass;
  }
  ++accepted;
  auto a = readFile(actual), b = readFile(ref);
  pass &= require(
      signature(a, family.name) && signature(b, family.name),
      tag +
          " explicit extension controls structure despite divergent filenames");
  if (family.name == "ps")
    pass &= require(psContent(a, actual) == psContent(b, ref),
                    tag + " complete PostScript saver image stream matches "
                          "independent components");
  else {
    auto x = decode(codec, actual), y = decode(codec, ref);
    std::cout << "legacy_native_decoded alias=" << alias
              << " requested=" << nativeChannels << " decoded=" << y.n
              << " exact_source=" << (y.n == nativeChannels && y.bytes == bytes)
              << '\n';
    pass &= require(
        x.w == width && x.h == height && y.w == width && y.h == height &&
            x.n == y.n && !x.bytes.empty() && x.bytes == y.bytes,
        tag + " independently decoded orientation/dimensions/channels/pixels "
              "match native saver");
    // Existing PNG/TIFF lossless channel contracts remain exact. JPEG/GIF
    // quantization is deliberately judged against the direct saver.
    if (family.name == "png" || family.name == "tiff")
      pass &= require(y.n == nativeChannels && y.bytes == bytes,
                      tag + " lossless native channels/alpha retained");
  }
  return same(target, stable, tag) && pass;
}
int run(bool gpu, const std::string &selected, int onlyChannels) {
  const auto *codec = simage_wrapper();
  if (!codec->available || !codec->versionMatchesAtLeast(1, 1, 0) ||
      !codec->simage_save_image || !codec->simage_check_save_supported ||
      !codec->simage_read_image || !codec->simage_free_image) {
    std::cout << "simage saver/readback API unavailable\n";
    return 77;
  }
  std::cout << "simage_version=" << codec->version.major << '.'
            << codec->version.minor << '.' << codec->version.micro << '\n';
  if (codec->simage_get_num_savers && codec->simage_get_saver_handle &&
      codec->simage_get_saver_extensions)
    for (int i = 0; i < codec->simage_get_num_savers(); ++i)
      std::cout << "simage_saver="
                << codec->simage_get_saver_extensions(
                       codec->simage_get_saver_handle(i))
                << '\n';
  CoinRenderOptions options;
  CoinRenderCapabilities caps{};
  if (gpu) {
    if (coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,
                                       &caps, sizeof(caps)) != 0 ||
        !caps.gpu_available || caps.renderer == COIN_RENDER_RENDERER_UNKNOWN)
      return 77;
    std::string diagnostic;
    options = CoinRenderDiagnosticShell::renderOptions(diagnostic);
    options.renderer = static_cast<CoinRenderRenderer>(caps.renderer);
    std::cout << "legacy_gpu backend=" << caps.backend
              << " renderer=" << caps.renderer
              << " adapter=" << caps.adapter_name
              << " diagnostic=" << diagnostic << '\n';
  }
  options.sampleCount = 1;
  std::unique_ptr<CoinRenderTarget> target(
      CoinRenderTarget::createOffscreen(SbVec2i32(width, height), options));
  if (!require(bool(target), "public offscreen target factory"))
    return 1;
  if (!gpu)
    target->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);
  auto &p = target->getPimpl().get();
  if (!require(p.prepareBackend() == CoinRenderBackendStatus::SUCCESS &&
                   p.backend && p.backend->isGpuBackend() == gpu,
               "requested real GPU or explicit CPU backend"))
    return 1;
  target->setDepthReadbackEnabled(TRUE);
  CoinRenderAction action(SbViewportRegion(width, height));
  action.setRenderTarget(target.get());
  action.setTransparencyType(CoinRenderAction::NONE);
  action.setBackgroundColor(SbColor4f(0, 0, 0, 1));
  Scene scene;
  Directory dir;
  if (!require(!dir.path.empty(), "private temporary destination directory"))
    return 1;
  const std::string empty = dir.file("unpublished.jpg");
  require(!target->writeLegacyImageFile(SbString(empty.c_str()),
                                        SbString("jpg"),
                                        CoinRenderLegacyComponents::RGB),
          "publication absent refuses export");
  require(readFile(empty).empty() && target->getLastSubmissionSerial() == 0,
          "absent export cannot publish or invent pixels");
  if (!render(action, *target, scene, 0))
    return 1;
  const auto stable = publication(*target);
  require(!target->writeLegacyImageFile(SbString(dir.path.c_str()),
                                        SbString("png"),
                                        CoinRenderLegacyComponents::RGB),
          "directory destination refuses");
  require(!target->writeLegacyImageFile(
              SbString((dir.path + "/missing/image").c_str()), SbString("png"),
              CoinRenderLegacyComponents::RGB),
          "missing parent destination refuses");
  for (const char *extension : {"", "coin-render-no-such-codec"})
    require(!target->writeLegacyImageFile(
                SbString(dir.file("invalid.codec").c_str()),
                SbString(extension), CoinRenderLegacyComponents::RGB),
            "invalid extension refuses");
  require(!target->writeLegacyImageFile(SbString(""), SbString("png"),
                                        CoinRenderLegacyComponents::RGB),
          "empty destination refuses");
  for (unsigned value : {0u, 5u})
    require(!target->writeLegacyImageFile(
                SbString(dir.file("invalid-components.png").c_str()),
                SbString("png"),
                static_cast<CoinRenderLegacyComponents>(value)),
            "invalid component domain refuses");
  same(*target, stable, "all refused export operations");
  for (int phase : {0, 1, 0}) {
    if (phase != 0 || target->getLastSubmissionSerial() > 1) {
      if (!render(action, *target, scene, phase))
        break;
    }
    for (int n = 1; n <= 4; ++n) {
      std::vector<uint8_t> actualComponents;
      require(
          target->readbackLegacyComponents(
              static_cast<CoinRenderLegacyComponents>(n), actualComponents) &&
              actualComponents == components(authored(phase), n),
          "independent bottom-up luminance/RGB/alpha component buffer");
    }
    for (const auto &family : families()) {
      if (!selected.empty() && family.name != selected)
        continue;
      for (const auto &alias : family.aliases) {
        std::string upper = alias;
        std::transform(upper.begin(), upper.end(), upper.begin(),
                       [](unsigned char c) { return char(std::toupper(c)); });
        for (const auto &extension : {alias, upper})
          for (int channels = 1; channels <= 4; ++channels)
            if (!onlyChannels || channels == onlyChannels)
              one(*target, codec, dir, family, extension, channels, phase);
      }
    }
  }
  // A focused LA/JPEG process may legitimately contain only native refusals.
  // Do not label that as a missing CoinRender implementation. An absent saver
  // is an explicit environment skip, never an untested feature success.
  if (!accepted && !nativeRefused) {
    action.setRenderTarget(nullptr);
    std::cout << "requested optional savers absent; no qualified codec cell\n";
    return good ? 77 : 1;
  }
  if (selected.empty())
    require(accepted > 0, "unfiltered suite exercises actual saver positives");
  std::cout << "legacy_codec accepted=" << accepted
            << " native_refused=" << nativeRefused
            << " unsupported_case=" << unsupportedCase << '\n';
  action.setRenderTarget(nullptr);
  return good ? 0 : 1;
}
} // namespace
int main(int argc, char **argv) {
  bool gpu = false;
  std::string family;
  int channels = 0;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--cpu")
      gpu = false;
    else if (arg == "--gpu")
      gpu = true;
    else if ((arg == "--codec" || arg == "--components") && i + 1 < argc) {
      std::string value = argv[++i];
      if (arg == "--codec") {
        bool found = false;
        for (const auto &f : families())
          found |= f.name == value;
        if (!found)
          return 2;
        family = value;
      } else {
        if (value != "1" && value != "2" && value != "3" && value != "4")
          return 2;
        channels = std::atoi(value.c_str());
      }
    } else
      return 2;
  }
  SoDB::init();
  CoinRenderAction::initClass();
  return run(gpu, family, channels);
}
