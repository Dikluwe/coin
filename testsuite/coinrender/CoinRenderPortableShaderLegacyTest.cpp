// Public-only control: no new R22 include or symbol is used by this executable.
#include "CoinRenderPortableShaderModuleScene.h"
int main(int argc, char **argv) {
  if (argc > 1 && (argc != 2 || std::string(argv[1]) != "--gpu"))
    return 2;
  SoDB::init();
  CoinRenderAction::initClass();
  CoinRenderCapabilities caps{};
  const int query = coin_render_query_capabilities(
      COIN_RENDER_EXPERIMENTAL_OFFSCREEN, &caps, sizeof(caps));
  std::cout << "portable_legacy_environment query=" << query
            << " gpu=" << caps.gpu_available << " renderer=" << caps.renderer
            << " adapter=" << caps.adapter_name << std::endl;
  if (query)
    return 1;
  if (!caps.gpu_available)
    return 77;
  using namespace portable_module_test;
  Scene scene;
  CoinRenderOptions options{};
  options.renderer = static_cast<CoinRenderRenderer>(caps.renderer);
  options.sampleCount = 1;
  std::unique_ptr<CoinRenderTarget> target(
      CoinRenderTarget::createOffscreen(SbVec2i32(64, 64), options));
  if (!target)
    return 1;
  target->setDepthReadbackEnabled(TRUE);
  CoinRenderAction action(SbViewportRegion(64, 64));
  action.setRenderTarget(target.get());
  action.setTransparencyType(CoinRenderAction::NONE);
  action.setBackgroundColor(SbColor4f(0, 0, 0, 0));
  auto read = [&]() {
    Frame f;
    target->readbackRGBA(f.rgba);
    target->readbackDepth(f.depth);
    f.serial = target->getLastSubmissionSerial();
    return f;
  };
  action.apply(scene.root);
  const Frame stable = read();
  bool ok = check(action.getLastStatus() == CoinRenderAction::SUCCESS &&
                      stable.serial > 0,
                  "public ordinary predecessor succeeds") &&
            image(stable, {{.6f, .3f, .8f, 1}});
  scene.attach(true);
  for (int source :
       {int(SoShaderObject::GLSL_PROGRAM), int(SoShaderObject::ARB_PROGRAM),
        int(SoShaderObject::CG_PROGRAM)}) {
    scene.fragment->sourceType = source;
    action.apply(scene.root);
    const auto after = read();
    std::cout << "portable_legacy_refusal source=" << source
              << " status=" << action.getLastStatus()
              << " serial=" << after.serial
              << " error=" << action.getLastError().getString() << std::endl;
    ok &= check(action.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
                    action.getLastError().getLength() > 0 &&
                    after.serial == stable.serial &&
                    after.rgba == stable.rgba && after.depth == stable.depth,
                "unadapted source refuses explicitly and preserves exact "
                "public publication");
  }
  scene.attach(false);
  action.apply(scene.root);
  const Frame recovered = read();
  ok &= check(action.getLastStatus() == CoinRenderAction::SUCCESS &&
                  recovered.serial > stable.serial,
              "same public target recovers after unadapted shader refusal") &&
        image(recovered, {{.6f, .3f, .8f, 1}});
  action.setRenderTarget(nullptr);
  return ok ? 0 : 1;
}
