#define COIN_RENDER_IMAGE_NORMAL_TEXGEN_FIXTURE_ONLY
#include "CoinRenderImageNormalTexgenTest.cpp"
#include <Inventor/nodes/SoVertexProperty.h>
#include <Inventor/nodes/SoAnnotation.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoTextureCoordinateCylinder.h>
#include <Inventor/nodes/SoTextureCoordinateDefault.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoIndexedTriangleStripSet.h>
namespace antecedents {
struct Frame {std::vector<uint8_t>rgba;std::vector<float>depth;uint64_t serial=0;};
struct Harness {
  std::unique_ptr<CoinRenderTarget>target;
  Capture *backend=nullptr;
  CoinRenderAction action{SbViewportRegion(side,side)};
  Harness(){target.reset(CoinRenderTarget::createOffscreen(SbVec2i32(side,side)));if(target){backend=new Capture;target->getPimpl()->backend.reset(backend);target->setDepthReadbackEnabled(TRUE);action.setRenderTarget(target.get());}}
  ~Harness(){action.setRenderTarget(nullptr);}
  Frame read(){Frame f;target->readbackRGBA(f.rgba);target->readbackDepth(f.depth);f.serial=target->getLastSubmissionSerial();return f;}
  bool apply(Scene&s,const std::string&label){const auto serial=target->getLastSubmissionSerial();const unsigned calls=backend->calls;action.apply(s.root);const Frame f=read();return check(action.getLastStatus()==CoinRenderAction::SUCCESS&&f.serial>serial&&backend->calls==calls+1&&f.rgba.size()==size_t(side*side*4)&&f.depth.size()==size_t(side*side),label+": "+action.getLastError().getString());}
  bool refuse(Scene&s,const std::string&label){const Frame before=read();const unsigned calls=backend->calls;action.apply(s.root);const Frame after=read();const std::string error=action.getLastError().getString();return check(action.getLastStatus()==CoinRenderAction::UNSUPPORTED&&error.find("normal")!=std::string::npos&&error.find("not determined")!=std::string::npos&&calls==backend->calls&&before.serial==after.serial&&before.rgba==after.rgba&&before.depth==after.depth,label+": "+error);}
};
struct Predecessor {
  SoSwitch *toggle=new SoSwitch;
  SoVertexProperty *property=new SoVertexProperty;
  SoLightModel *lighting=new SoLightModel;
  SoMaterial *material=new SoMaterial;
  SoNormal *inherited=new SoNormal;
  SoNormalBinding *binding=new SoNormalBinding;
  Predecessor(int shapeKind,int normals,bool phong) {
    toggle->whichChild=SO_SWITCH_NONE;auto *branch=new SoSeparator;toggle->addChild(branch);
    lighting->model=phong?SoLightModel::PHONG:SoLightModel::BASE_COLOR;branch->addChild(lighting);branch->addChild(material);
    binding->value=SoNormalBinding::OVERALL;branch->addChild(binding);
    // Empty VP normal fields inherit the Inventor element. PointSet with no
    // normal at all disables submission; indexed shapes here inherit +X.
    if(normals==0&&shapeKind)inherited->vector.setValue(1,0,0);else inherited->vector.setNum(0);
    branch->addChild(inherited);
    std::vector<SbVec3f> positions;
    for(int t=0;t<12;++t){const float x=-20.f+.6f*(t%4),y=-5.f+.6f*(t/4);positions.push_back(SbVec3f(x,y,0));positions.push_back(SbVec3f(x+.4f,y,0));positions.push_back(SbVec3f(x,y+.4f,0));}
    property->vertex.setValues(0,36,positions.data());
    if(normals==0)property->normal.setNum(0);
    else if(normals==1){property->normal.setValue(0,1,0);property->normalBinding=SoVertexProperty::OVERALL;}
    else {for(int i=0;i<36;++i)property->normal.set1Value(i,i%2?SbVec3f(1,0,0):SbVec3f(0,1,0));property->normal.set1Value(35,SbVec3f(0,-1,0));property->normalBinding=SoVertexProperty::PER_VERTEX_INDEXED;}
    if(shapeKind==0){auto *shape=new SoPointSet;shape->numPoints=36;shape->vertexProperty=property;branch->addChild(shape);}
    else {std::vector<int32_t>indices;for(int t=0;t<12;++t){indices.push_back(3*t);indices.push_back(3*t+1);indices.push_back(3*t+2);indices.push_back(-1);}
      if(shapeKind==1){auto *shape=new SoIndexedFaceSet;shape->vertexProperty=property;shape->coordIndex.setValues(0,int(indices.size()),indices.data());branch->addChild(shape);}
      else {auto *shape=new SoIndexedTriangleStripSet;shape->vertexProperty=property;shape->coordIndex.setValues(0,int(indices.size()),indices.data());branch->addChild(shape);}
    }
  }
};
bool vertexProperty() {
  bool ok=true;
  for(int generator=0;generator<3;++generator)for(int kind=0;kind<3;++kind)for(bool phong:{false,true})for(int normals=0;normals<3;++normals) {
    Harness h;if(!h.target)return false;Scene scene(generator,7,true,false,true);
    auto *reset=pointHistory(false);scene.root->insertChild(reset,scene.root->findChild(scene.model));reset->whichChild=SO_SWITCH_ALL;
    Predecessor previous(kind,normals,phong);scene.root->insertChild(previous.toggle,scene.root->findChild(scene.model));
    const std::string label="VP kind="+std::to_string(kind)+" normalMode="+std::to_string(normals)+" phong="+std::to_string(phong)+" generator="+std::to_string(generator);
    if(!h.apply(scene,label+" known initial normalZ")){ok=false;continue;}
    const Frame initial=h.read();reset->whichChild=SO_SWITCH_NONE;previous.toggle->whichChild=SO_SWITCH_ALL;
    const bool unknown=phong&&normals==2;
    if(unknown) {
      ok &= h.refuse(scene,label+" consumed normal array is Unknown, never its last corner");
      // Unknown is legal when no Image generator consumes it, and persists
      // only after that frame is actually published.
      scene.quality->textureQuality=0;
      if(!h.apply(scene,label+" unconsumed Unknown publishes")){ok=false;continue;}
      previous.toggle->whichChild=SO_SWITCH_NONE;scene.quality->textureQuality=.1f;
      ok &= h.refuse(scene,label+" published array history persists across frames");
    } else {
      std::array<double,3> expected={{0,0,1}};
      if(phong&&normals==1)expected={{0,1,0}};
      else if(phong&&normals==0&&kind)expected={{1,0,0}};
      const bool emitted=h.apply(scene,label+" native defined emission or preservation");ok &= emitted;
      if(emitted)ok &= delivery(scene,h.backend->plan,false,expected);
      previous.toggle->whichChild=SO_SWITCH_NONE;
      const bool persistent=h.apply(scene,label+" next frame retains published current normal");ok &= persistent;
      if(persistent)ok &= delivery(scene,h.backend->plan,false,expected);
    }
    reset->whichChild=SO_SWITCH_ALL;
    const bool recovered=h.apply(scene,label+" first defined OVERALL recovery");ok &= recovered;
    if(recovered){ok &= delivery(scene,h.backend->plan,false);const Frame final=h.read();ok &= check(initial.rgba==final.rgba&&initial.depth==final.depth,label+" exact known A recovery");}
  }
  return ok;
}
bool planOnly() {
  Harness h;if(!h.target)return false;Scene scene(2,7,true,false,true);
  auto *reset=pointHistory(false);reset->whichChild=SO_SWITCH_ALL;scene.root->insertChild(reset,scene.root->findChild(scene.model));
  Predecessor previous(0,1,true);scene.root->insertChild(previous.toggle,scene.root->findChild(scene.model));
  if(!h.apply(scene,"planOnly seed normalZ"))return false;
  const Frame initial=h.read();const unsigned calls=h.backend->calls;
  reset->whichChild=SO_SWITCH_NONE;previous.toggle->whichChild=SO_SWITCH_ALL;
  h.action.getPimpl()->executionOptions=h.target->getOptions();h.action.getPimpl()->planOnly=true;h.action.apply(scene.root);
  const Frame unpublished=h.read();
  bool ok=check(h.action.getLastStatus()==CoinRenderAction::SUCCESS&&h.backend->calls==calls&&
    unpublished.serial==initial.serial&&unpublished.rgba==initial.rgba&&unpublished.depth==initial.depth,
    "planOnly VP OVERALL is captured without execution and preserves exact published buffers/serial");
  if(h.action.getLastStatus()==CoinRenderAction::SUCCESS)ok &= delivery(scene,h.action.getPimpl()->lastValidPlan,false,{{0,1,0}});
  h.action.getPimpl()->planOnly=false;previous.toggle->whichChild=SO_SWITCH_NONE;
  const bool after=h.apply(scene,"planOnly must not commit candidate normalY");ok &= after;
  if(after){
    ok &= delivery(scene,h.backend->plan,false);const Frame final=h.read();
    // The seed PointSet is visible at x=12 and has now been removed. The
    // authored 16x16 Image occupies x/y24..39, unchanged by that removal.
    // Compare its complete footprint, then check the NORMAL_MAP Z sample:
    // raw(0,0,1,2), texture matrix(.25,.25,.5; translation .125,.125)
    // gives ST(.125,.125), nearest 4x4 texel(0,0) RGB(20,30,40).
    unsigned bad=0;
    const int expected[]={20,30,40};
    for(int y=24;y<40;++y)for(int x=24;x<40;++x){const size_t at=size_t(y*side+x);
      for(int c=0;c<4;++c)bad+=initial.rgba[4*at+c]!=final.rgba[4*at+c];
      bad+=initial.depth[at]!=final.depth[at];
      for(int c=0;c<3;++c)bad+=std::abs(int(final.rgba[4*at+c])-expected[c])>1;
      bad+=final.rgba[4*at+3]!=255;bad+=std::abs(final.depth[at]-float(9./29.))>1e-6f;
    }
    ok &= check(bad==0,"planOnly does not commit normalY: unchanged full Image footprint has authored normalZ RGBA/depth");
  }
  return ok;
}
#include "CoinRenderImageNormalExecutionHistory.h"
#include "CoinRenderImageNormalOccurrenceHistory.h"
#include "CoinRenderImageNormalClassificationHistory.h"
}
int main(int argc,char**argv) {
  const std::string mode=argc==2?argv[1]:"--vertex-property";if(argc>2||(mode!="--vertex-property"&&mode!="--plan-only"&&mode!="--deferred"&&mode!="--culled"&&mode!="--sorted-annotation"&&mode!="--texture-units"))return 2;
  SoDB::init();CoinRenderAction::initClass();
  if(mode=="--sorted-annotation")return antecedents::sortedAnnotation()?0:1;
  if(mode=="--texture-units")return antecedents::textureUnits()?0:1;
  return (mode=="--plan-only"?antecedents::planOnly():mode=="--deferred"?antecedents::deferred():mode=="--culled"?antecedents::culled():(antecedents::vertexProperty()&&antecedents::sharedOccurrences(false)&&antecedents::sharedOccurrences(true)))?0:1;
}
