#define main bounding_fixture_main
#include "/tmp/coin-render-first-frame/testsuite/coinrender/CoinRenderBoundingBoxTest.cpp"
#undef main
int main() {
  SoDB::init(); CoinRenderAction::initClass(); BoundingFixtureShape::initClass();
  Scene scene; Harness test(true); scene.style->style=SoDrawStyle::POINTS; scene.style->pointSize=5;
  scene.material->transparency=.5f;
  const SbVec3f lo(-.4875f,-.4875f,-.25f),hi(.5125f,.5125f,.25f);
  scene.shape->minimum=lo;scene.shape->maximum=hi;
  auto * depth=new SoDepthBuffer; depth->test=TRUE;depth->write=FALSE;depth->function=SoDepthBuffer::LEQUAL;
  scene.root->insertChild(depth,scene.root->findChild(scene.transform));
  auto * clip=new SoClipPlane;clip->plane=SbPlane(SbVec3f(lo[0],hi[1],lo[2]),SbVec3f(lo[0],hi[1],hi[2]),SbVec3f(hi[0],lo[1],lo[2]));
  scene.root->insertChild(clip,scene.root->findChild(scene.transform));
  test.render(scene.root,"native-clip-probe");
  for (const auto & point : std::vector<std::pair<int,int>>{{28,19},{68,19},{28,59},{68,59},{48,39}}) {
    const size_t offset=size_t(point.second*width+point.first)*4;
    std::cout<<"sample "<<point.first<<','<<point.second<<" GL="<<int(test.reference[offset])<<','<<int(test.reference[offset+1])<<','<<int(test.reference[offset+2])<<" GPU="<<int(test.lastNative[offset])<<','<<int(test.lastNative[offset+1])<<','<<int(test.lastNative[offset+2])<<'\n';
  }
  return 0;
}
