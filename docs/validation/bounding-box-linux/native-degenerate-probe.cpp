#define main bounding_fixture_main
#include "/tmp/coin-render-first-frame/testsuite/coinrender/CoinRenderBoundingBoxTest.cpp"
#undef main
int main() {
  SoDB::init(); CoinRenderAction::initClass(); BoundingFixtureShape::initClass();
  Scene scene; Harness test(true);
  scene.shape->minimum.setValue(-.4875f,-.0125f,0); scene.shape->maximum.setValue(.5125f,-.0125f,0);
  scene.style->style = SoDrawStyle::LINES;
  bool ok = test.nativeVisibilityProbe(scene.root, "native-degenerate/line");
  scene.shape->minimum.setValue(.0125f,-.0125f,0); scene.shape->maximum = scene.shape->minimum.getValue();
  scene.style->style = SoDrawStyle::POINTS;
  ok = test.nativeVisibilityProbe(scene.root, "native-degenerate/point") && ok;
  return ok ? 0 : 1;
}
