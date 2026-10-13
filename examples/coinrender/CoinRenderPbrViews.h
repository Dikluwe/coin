#ifndef COIN_RENDER_PBR_VIEWS_H
#define COIN_RENDER_PBR_VIEWS_H
#include "CoinRenderPbrFixtures.h"
#include <Inventor/rendering/SoPBRMaterial.h>
#include <Inventor/C/base/time.h>
#include <Inventor/SoDB.h>
#include <Inventor/SoInput.h>
#include <Inventor/SoOutput.h>
#include <Inventor/SbViewVolume.h>
#include <Inventor/actions/SoWriteAction.h>
#include <Inventor/actions/SoSearchAction.h>
#include <Inventor/nodes/SoGroup.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include <Inventor/rendering/CoinRenderOptions.h>
#include <Inventor/rendering/CoinRenderSceneManager.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoLightModel.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#if defined(Status)
#undef Status
#endif
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector> // Required by the existing public readback API.
#include <unistd.h>

struct CoinRenderPbrViewOptions {
  SbBool active = FALSE, offscreen = FALSE, cut = FALSE, pbr = FALSE;
  float exposureStops = 0.0f;
  SbString fixture = "flange", scene, view = "iso-perspective";
  SbString capturePrefix, exportScene;
  SbVec2i32 size = SbVec2i32(1280, 960);
  int orbitFrames = 0;
};

inline SbString coin_pbr_json_string(const char * value) {
  SbString result("\"");
  for (const unsigned char * p = reinterpret_cast<const unsigned char *>(value); *p; ++p) {
    if (*p == '"' || *p == '\\') { result += '\\'; result += char(*p); }
    else if (*p < 32) { SbString escaped; escaped.sprintf("\\u%04x", unsigned(*p)); result += escaped; }
    else result += char(*p);
  }
  result += '"'; return result;
}

inline void coin_pbr_write_matrix(FILE * output, const SbMatrix & matrix) {
  std::fputc('[', output);
  for (int row = 0; row < 4; ++row)
    for (int column = 0; column < 4; ++column)
      std::fprintf(output, "%s%.9g", row || column ? "," : "", double(matrix[row][column]));
  std::fputc(']', output);
}

inline SbBool coin_pbr_capture(const CoinRenderPbrViewOptions & options,
  const CoinRenderCapabilities & caps, SoCamera * camera, CoinRenderTarget * target,
  int frame, double returnMs) {
  std::vector<uint8_t> rgba; target->readbackRGBA(rgba);
  const size_t count = size_t(options.size[0]) * size_t(options.size[1]);
  if (rgba.size() != count * 4) return FALSE;
  SbString imageName, manifestName;
  imageName.sprintf("%s-%03d.ppm", options.capturePrefix.getString(), frame);
  manifestName.sprintf("%s-%03d.json", options.capturePrefix.getString(), frame);
  FILE * image = std::fopen(imageName.getString(), "wb"); if (!image) return FALSE;
  bool written = std::fprintf(image, "P6\n%d %d\n255\n", options.size[0], options.size[1]) > 0;
  for (size_t pixel = 0; written && pixel < count; ++pixel)
    written = std::fwrite(&rgba[pixel * 4], 1, 3, image) == 3;
  if (std::fclose(image) != 0 || !written) return FALSE;
  FILE * manifest = std::fopen(manifestName.getString(), "wb"); if (!manifest) return FALSE;
  SbMatrix viewMatrix, projection;
  camera->getViewVolume(float(options.size[0]) / options.size[1]).getMatrices(viewMatrix, projection);
  std::fprintf(manifest,
    "{\n  \"profile\":%s,\n  \"exposure_stops\":%.9g,\n  \"fixture\":%s,\n  \"source_scene\":%s,\n"
    "  \"view\":%s,\n  \"width\":%d,\n  \"height\":%d,\n  \"frame\":%d,\n"
    "  \"cut\":%s,\n  \"axis_up\":\"Z\",\n  \"unit\":\"fixture-unit\",\n"
    "  \"renderer\":%u,\n  \"adapter\":%s,\n  \"vendor_id\":%u,\n  \"device_id\":%u,\n"
    "  \"submission\":%llu,\n  \"render_return_with_capture_ms\":%.6f,\n  \"view_matrix\":",
    options.pbr ? "\"pbr-direct-m2\"" : "\"legacy-baseline\"", double(options.exposureStops),
    coin_pbr_json_string(options.fixture.getString()).getString(),
    coin_pbr_json_string(options.scene.getString()).getString(),
    coin_pbr_json_string(options.view.getString()).getString(), options.size[0], options.size[1], frame,
    options.cut ? "true" : "false", caps.renderer, coin_pbr_json_string(caps.adapter_name).getString(),
    caps.vendor_id, caps.device_id, static_cast<unsigned long long>(target->getLastSubmissionSerial()), returnMs);
  coin_pbr_write_matrix(manifest, viewMatrix); std::fprintf(manifest, ",\n  \"projection_matrix\":");
  coin_pbr_write_matrix(manifest, projection); std::fprintf(manifest, "\n}\n");
  return std::fclose(manifest) == 0;
}

inline SoNode * coin_pbr_physical_material(const SbColor & color, float metallic, float roughness) {
  auto * result = new SoPBRMaterial; result->baseColor = color;
  result->metallic = metallic; result->roughness = roughness; return result;
}

inline int coin_render_run_cad_views(const CoinRenderPbrViewOptions & options, int requestedFrames) {
  const SbBool interactive = requestedFrames < 0 && !options.capturePrefix.getLength() &&
                             !options.orbitFrames && !options.offscreen;
  const int frames = options.orbitFrames > 0 ? options.orbitFrames : (requestedFrames > 0 ? requestedFrames : 1);
  if (options.size[0] < 32 || options.size[1] < 32 || options.size[0] > 8192 ||
      options.size[1] > 8192 || frames > 100000) return 2;
  SoDB::init(); CoinRenderAction::initClass();
  CoinRenderOptions renderOptions; renderOptions.renderer = COIN_RENDER_RENDERER_VULKAN;
  renderOptions.pbrExposureStops = options.exposureStops;
  CoinRenderCapabilities caps{};
  const uint32_t kind = options.offscreen ? COIN_RENDER_EXPERIMENTAL_OFFSCREEN : COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW;
  if (coin_render_query_capabilities_for_renderer(kind, renderOptions.renderer, &caps, sizeof(caps)) || !caps.gpu_available) {
    std::fprintf(stderr, "CAD Vulkan unavailable: %s\n", caps.diagnostic); return 2;
  }
  SoSeparator * geometry = NULL;
  if (options.scene.getLength()) {
    SoInput input; if (!input.openFile(options.scene.getString())) return 2;
    geometry = SoDB::readAll(&input);
  } else geometry = coin_pbr_fixture::create(options.fixture, options.cut,
    options.pbr ? coin_pbr_physical_material : coin_pbr_fixture::material);
  if (!geometry) { std::fprintf(stderr, "Unknown or unreadable CAD fixture\n"); return 2; }
  geometry->ref(); SbVec3f center;
  if (options.pbr && options.scene.getLength()) {
    std::fprintf(stderr, "PBR view fixtures are authored explicitly; imported CAD material conversion is not yet qualified\n");
    geometry->unref(); return 2;
  }
  // Presets own the camera. Remove input cameras through Coin's search/path
  // facilities so an embedded camera cannot invalidate the capture manifest.
  SoSearchAction cameraSearch;
  cameraSearch.setType(SoCamera::getClassTypeId());
  cameraSearch.setInterest(SoSearchAction::ALL);
  cameraSearch.setSearchingAll(TRUE); cameraSearch.apply(geometry);
  struct InputCamera { SoGroup * parent; SoNode * node; };
  SbList<InputCamera> inputCameras;
  const SoPathList & cameraPaths = cameraSearch.getPaths();
  for (int i = 0; i < cameraPaths.getLength(); ++i) {
    SoPath * path = cameraPaths[i];
    if (path->getLength() > 1) {
      SoNode * parent = path->getNode(path->getLength() - 2);
      if (parent->isOfType(SoGroup::getClassTypeId()))
        inputCameras.append({static_cast<SoGroup *>(parent), path->getTail()});
    }
  }
  for (int i = 0; i < inputCameras.getLength(); ++i) {
    const InputCamera & entry = inputCameras[i];
    const int child = entry.parent->findChild(entry.node);
    if (child >= 0) entry.parent->removeChild(child);
  }
  SoCamera * camera = coin_pbr_fixture::camera(geometry, options.view, options.size, center);
  if (!camera) { geometry->unref(); std::fprintf(stderr, "Invalid CAD camera/scene\n"); return 2; }
  SoSeparator * root = new SoSeparator; root->ref(); root->addChild(camera);
  SoLightModel * model = new SoLightModel; model->model = SoLightModel::PHONG; root->addChild(model);
  SoDirectionalLight * light = new SoDirectionalLight;
  light->direction.setValue(-0.3f, 0.4f, -1.0f); root->addChild(light);
  root->addChild(geometry); geometry->unref();
  if (options.exportScene.getLength()) {
    SoOutput output; if (!output.openFile(options.exportScene.getString())) { root->unref(); return 2; }
    SoWriteAction writer(&output); writer.apply(geometry); output.closeFile();
  }
  Display * display = NULL; Window window = 0;
  CoinRenderNativeSurfaceDescriptor surface{};
  if (!options.offscreen) {
    display = XOpenDisplay(NULL); if (!display) { root->unref(); return 2; }
    window = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, options.size[0], options.size[1], 0, 0, 0);
    XStoreName(display, window, "CoinRender CAD material views");
    XSelectInput(display, window, StructureNotifyMask | KeyPressMask);
    XMapWindow(display, window); XSync(display, False);
    surface.abiVersion = COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
    surface.structSize = sizeof(surface); surface.type = COIN_RENDER_SURFACE_XLIB;
    surface.native.xlib.display = display; surface.native.xlib.window = window;
  }
  int exitCode = 0;
  {
    std::unique_ptr<CoinRenderSceneManager> manager(options.offscreen ?
      new CoinRenderSceneManager(options.size, renderOptions) : new CoinRenderSceneManager(surface, options.size, renderOptions));
    manager->setSceneGraph(root); root->unref();
    manager->setBackgroundColor(SbColor4f(0.025f, 0.035f, 0.05f, 1.0f));
    if (options.offscreen) manager->getRenderTarget()->setDepthReadbackEnabled(FALSE);
    const float radius = (camera->position.getValue() - center).length();
    int frame = 0; SbBool running = TRUE;
    while (running && (interactive || frame < frames)) {
      if (display) while (XPending(display)) {
        XEvent event; XNextEvent(display, &event);
        if (event.type == KeyPress) {
          const KeySym key = XLookupKeysym(&event.xkey, 0);
          if (key == XK_Escape || key == XK_q) running = FALSE;
          else if (key == XK_plus || key == XK_equal) camera->scaleHeight(0.9f);
          else if (key == XK_minus) camera->scaleHeight(1.1f);
        }
      }
      if (!running) break;
      if (options.orbitFrames > 0) {
        const float angle = 6.28318530718f * frame / options.orbitFrames;
        SbVec3f offset(std::cos(angle), std::sin(angle), 0.8f); offset.normalize();
        camera->position = center + offset * radius; camera->pointAt(center, SbVec3f(0, 0, 1));
      }
      CoinRenderTarget * target = manager->getRenderTarget();
      if (options.capturePrefix.getLength() && !options.offscreen && !target->requestWindowReadbackRGBA()) { exitCode = 1; break; }
      const cc_time begin = cc_time_gettimeofday();
      const CoinRenderAction::Status status = manager->render();
      const double returnMs = (cc_time_gettimeofday() - begin) * 1000.0;
      if (status != CoinRenderAction::SUCCESS) {
        std::fprintf(stderr, "CAD render failed: %s\n", manager->getLastError().getString()); exitCode = 1; break;
      }
      if (options.capturePrefix.getLength() && !coin_pbr_capture(options, caps, camera, target, frame, returnMs)) {
        std::fprintf(stderr, "CAD capture incomplete\n"); exitCode = 1; break;
      }
      ++frame; if (interactive) usleep(16000);
    }
  }
  if (display) {
    XDestroyWindow(display, window);
    // A BGFX/Vulkan driver callback can remain registered until process exit;
    // keep the process-owned Xlib connection alive through that teardown.
  }
  return exitCode;
}
#endif
