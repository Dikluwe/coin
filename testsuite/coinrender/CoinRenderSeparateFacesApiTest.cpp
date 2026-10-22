#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "CoinRenderPortableApiFixture.h"
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoMaterialBinding.h>
#include <Inventor/nodes/SoScale.h>
#include <Inventor/nodes/SoShapeHints.h>
#include <Inventor/nodes/SoTranslation.h>
#include <array>

#if !defined(COIN_RENDER_ACTION_OPTIONS_VERSION) || COIN_RENDER_ACTION_OPTIONS_VERSION < 1
int main(){std::cout<<"separate-faces portable public API absent\n";return 78;}
#else
using namespace portable_api;
namespace {
using Color=std::array<double,4>;
const Color red={{1,0,0,.5}},green={{0,1,0,.25}},blue={{0,0,1,.75}},yellow={{1,1,0,.5}};
Color over(const Color &source,const Color &destination) {
  Color result;
  for(unsigned c=0;c<3;++c)result[c]=source[c]*source[3]+destination[c]*(1-source[3]);
  result[3]=source[3]+destination[3]*(1-source[3]);return result;
}
Color expected(std::initializer_list<Color> order) {Color result={{0,0,0,1}};for(const auto &color:order)result=over(color,result);return result;}
bool pixels(const CoinRenderTarget &target,const Color &color,const std::string &label) {
  auto image=read(target);unsigned bad=0;
  if(!check(image.rgba.size()==32*24*4,label+" complete image"))return false;
  for(int y=9;y<15;++y)for(int x=12;x<20;++x)for(unsigned c=0;c<4;++c)
    bad+=std::abs(double(image.rgba[(y*32+x)*4+c])-color[c]*255)>3;
  return check(!bad,label+" independent ordered source-over oracle");
}
struct Shell {
  SoSeparator *root=new SoSeparator;
  SoShapeHints *hints=new SoShapeHints;
  SoScale *mirror=new SoScale;
  SoCoordinate3 *coordinates=new SoCoordinate3;
  SoMaterial *material=new SoMaterial;
  Shell(float z,const Color &front,const Color &back) {
    root->ref();auto *translation=new SoTranslation;translation->translation.setValue(0,0,z);
    root->addChild(translation);root->addChild(mirror);root->addChild(hints);
    hints->vertexOrdering=SoShapeHints::COUNTERCLOCKWISE;hints->shapeType=SoShapeHints::UNKNOWN_SHAPE_TYPE;
    hints->faceType=SoShapeHints::CONVEX;
    root->addChild(material);auto *binding=new SoMaterialBinding;binding->value=SoMaterialBinding::PER_FACE;root->addChild(binding);
    const SbColor colors[]={SbColor(float(front[0]),float(front[1]),float(front[2])),SbColor(float(back[0]),float(back[1]),float(back[2]))};
    const float transparency[]={float(1-front[3]),float(1-back[3])};
    material->diffuseColor.setValues(0,2,colors);material->transparency.setValues(0,2,transparency);
    root->addChild(coordinates);auto *faces=new SoFaceSet;const int32_t counts[]={4,4};faces->numVertices.setValues(0,2,counts);root->addChild(faces);
    setWinding(false);
  }
  void setWinding(bool clockwise) {
    const SbVec3f ccw[]={SbVec3f(-.6f,-.6f,.1f),SbVec3f(.6f,-.6f,.1f),SbVec3f(.6f,.6f,.1f),SbVec3f(-.6f,.6f,.1f),
      SbVec3f(-.6f,-.6f,-.1f),SbVec3f(-.6f,.6f,-.1f),SbVec3f(.6f,.6f,-.1f),SbVec3f(.6f,-.6f,-.1f)};
    SbVec3f vertices[8];for(int face=0;face<2;++face)for(int vertex=0;vertex<4;++vertex)
      vertices[face*4+vertex]=ccw[face*4+(clockwise?3-vertex:vertex)];
    coordinates->point.setValues(0,8,vertices);
    hints->vertexOrdering=clockwise?SoShapeHints::CLOCKWISE:SoShapeHints::COUNTERCLOCKWISE;
  }
  ~Shell(){root->unref();}
};
}
int main(int argc,char **argv) {
  const bool gpu=argc==2&&std::string(argv[1])=="--gpu";
  if(argc!=2||(!gpu&&std::string(argv[1])!="--cpu"))return 2;
  SoDB::init();CoinRenderAction::initClass();Fixture f(gpu);if(!f.target)return gpu&&good()?77:1;
  check(f.action.getTransparentDelayedObjectRenderType()==CoinRenderAction::ONE_PASS,"default ONE_PASS");
  Scene scene;Shell a(.25f,red,green),b(-.25f,blue,yellow);scene.root->addChild(a.root);scene.root->addChild(b.root);
  f.action.setDelayedObjDepthWrite(FALSE);
  for(auto modality:{CoinRenderAction::DELAYED_BLEND,CoinRenderAction::SORTED_OBJECT_BLEND}) {
    f.action.setTransparencyType(modality);
    for(bool clockwise:{false,true,false}) {
      a.setWinding(clockwise);b.setWinding(clockwise);
      for(bool separate:{false,true,false}) {
        const auto type=separate?CoinRenderAction::NONSOLID_SEPARATE_BACKFACE_PASS:CoinRenderAction::ONE_PASS;
        f.action.setTransparentDelayedObjectRenderType(type);
        check(f.action.getTransparentDelayedObjectRenderType()==type,"separate-face getter round trip");
        f.apply(scene.root,"non-solid ordered faces");
        const bool sorted=modality==CoinRenderAction::SORTED_OBJECT_BLEND;
        const auto oracle=separate?(sorted?expected({yellow,blue,green,red}):expected({green,yellow,red,blue})):
          (sorted?expected({blue,yellow,red,green}):expected({red,green,blue,yellow}));
        pixels(*f.target,oracle,"delayed/sorted winding A/B/A");
      }
    }
    a.hints->shapeType=SoShapeHints::SOLID;b.hints->shapeType=SoShapeHints::SOLID;
    for(bool separate:{false,true,false}) {
      f.action.setTransparentDelayedObjectRenderType(separate?CoinRenderAction::NONSOLID_SEPARATE_BACKFACE_PASS:CoinRenderAction::ONE_PASS);
      f.apply(scene.root,"SOLID only fronts");
      pixels(*f.target,modality==CoinRenderAction::SORTED_OBJECT_BLEND?expected({blue,red}):expected({red,blue}),"SOLID independent of split setting");
    }
    a.hints->shapeType=SoShapeHints::UNKNOWN_SHAPE_TYPE;b.hints->shapeType=SoShapeHints::UNKNOWN_SHAPE_TYPE;
  }
  // Mirroring reverses geometric winding. Reversing the authored ordering at
  // the same time restores the same visible front/back partition.
  a.mirror->scaleFactor.setValue(-1,1,1);b.mirror->scaleFactor.setValue(-1,1,1);
  a.hints->vertexOrdering=SoShapeHints::CLOCKWISE;b.hints->vertexOrdering=SoShapeHints::CLOCKWISE;
  f.action.setTransparencyType(CoinRenderAction::DELAYED_BLEND);
  f.action.setTransparentDelayedObjectRenderType(CoinRenderAction::NONSOLID_SEPARATE_BACKFACE_PASS);
  f.apply(scene.root,"mirrored winding");pixels(*f.target,expected({green,yellow,red,blue}),"mirror plus authored winding");
  a.mirror->scaleFactor.setValue(1,1,1);b.mirror->scaleFactor.setValue(1,1,1);a.setWinding(false);b.setWinding(false);
  // Immediate blending must not acquire an extra deferred/backface replay.
  f.action.setTransparencyType(CoinRenderAction::BLEND);
  f.action.setTransparentDelayedObjectRenderType(CoinRenderAction::ONE_PASS);f.apply(scene.root,"immediate control");const auto immediate=read(*f.target);
  f.action.setTransparentDelayedObjectRenderType(CoinRenderAction::NONSOLID_SEPARATE_BACKFACE_PASS);f.apply(scene.root,"immediate split flag");
  const auto after=read(*f.target);check(after.rgba==immediate.rgba&&after.depth==immediate.depth,"immediate mode unaffected by separate deferred faces");
  auto *depthA=new SoDepthBuffer,*depthB=new SoDepthBuffer;depthA->write=TRUE;depthB->write=TRUE;
  a.root->insertChild(depthA,0);b.root->insertChild(depthB,0);
  f.action.setTransparencyType(CoinRenderAction::DELAYED_BLEND);f.apply(scene.root,"separate authored depth writes");
  pixels(*f.target,expected({green,red}),"authored depth rejects later farther backs/fronts in delayed order");
  f.action.setTransparencyType(CoinRenderAction::SORTED_OBJECT_BLEND);f.apply(scene.root,"sorted authored depth writes");
  pixels(*f.target,expected({yellow,blue,green,red}),"authored depth preserves all monotonically nearer sorted faces");
  return good()?0:1;
}
#endif
