#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/annex/FXViz/nodes/SoShadowDirectionalLight.h>
#include <Inventor/annex/FXViz/nodes/SoShadowGroup.h>
#include <Inventor/annex/FXViz/nodes/SoShadowSpotLight.h>
#include <Inventor/annex/FXViz/nodes/SoShadowStyle.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoImage.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureUnit.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {
constexpr int side=128;
struct Frame { std::vector<uint8_t> rgba; std::vector<float> depth; uint64_t serial=0; };
bool check(bool value,const std::string & label) {
  if(!value) std::cerr << "image_inherited_shadow FAIL " << label << '\n';
  return value;
}
void face(SoGroup * parent,float half,float z,const SbColor & color) {
  auto * branch=new SoSeparator;parent->addChild(branch);
  auto * material=new SoMaterial;material->diffuseColor.setValue(color);branch->addChild(material);
  auto * coordinates=new SoCoordinate3;
  const SbVec3f vertices[]={SbVec3f(-half,-half,z),SbVec3f(half,-half,z),
    SbVec3f(half,half,z),SbVec3f(-half,half,z)};
  coordinates->point.setValues(0,4,vertices);branch->addChild(coordinates);
  auto * shape=new SoFaceSet;shape->numVertices.set1Value(0,4);branch->addChild(shape);
}
struct Scene {
  SoSeparator * root=new SoSeparator;
  SoShadowGroup * group=new SoShadowGroup;
  SoSeparator * imageBranch=new SoSeparator;
  SoTranslation * blockerMove=new SoTranslation;
  SoShadowStyle * blockerStyle=new SoShadowStyle;
  SoShadowStyle * imageStyle=nullptr;
  SoOrthographicCamera * camera=nullptr;
  SoTexture2 * lastTexture=nullptr;
  std::vector<SoTexture2 *> textures;
  std::vector<SoShadowSpotLight *> spots;
  std::vector<SoShadowDirectionalLight *> directionals;
  bool directional;
  explicit Scene(int maps,bool directional):directional(directional) {
    root->ref();
    camera=new SoOrthographicCamera;
    camera->position.setValue(0,0,10);camera->height=4;
    camera->nearDistance=1;camera->farDistance=30;root->addChild(camera);
    auto * lighting=new SoLightModel;lighting->model=SoLightModel::PHONG;root->addChild(lighting);
    group->quality=.2f;group->precision=.125f;root->addChild(group);
    for(int i=0;i<maps;++i) {
      if(directional) {
        auto * light=new SoShadowDirectionalLight;
        light->direction.setValue(-.18f-.025f*i,-.27f+.015f*i,-1);
        light->intensity=.8f;group->addChild(light);directionals.push_back(light);
      } else {
        auto * light=new SoShadowSpotLight;
        const float x=1.35f+.1f*i,y=1.8f-.06f*i,z=6.f+.04f*i;
        light->location.setValue(x,y,z);
        light->direction.setValue(-x,-y,1.2f-z);
        light->cutOffAngle=.35f;light->intensity=.8f;
        group->addChild(light);spots.push_back(light);
      }
    }
    auto * receiver=new SoSeparator;group->addChild(receiver);
    auto * receiverStyle=new SoShadowStyle;
    receiverStyle->style=SoShadowStyle::SHADOWED;receiver->addChild(receiverStyle);
    face(receiver,1.9f,-1,SbColor(.22f,.24f,.27f));
    auto * blocker=new SoSeparator;group->addChild(blocker);
    blockerStyle->style=SoShadowStyle::CASTS_SHADOW;blocker->addChild(blockerStyle);
    blocker->addChild(blockerMove);face(blocker,.12f,4,SbColor(.2f,.2f,.2f));
    group->addChild(imageBranch);
    imageStyle=new SoShadowStyle;
    imageStyle->style=SoShadowStyle::SHADOWED;imageBranch->addChild(imageStyle);
    auto * material=new SoMaterial;
    material->diffuseColor.setValue(.95f,.8f,.7f);
    material->specularColor.setValue(0,0,0);
    material->transparency=0;imageBranch->addChild(material);
    auto * move=new SoTranslation;move->translation.setValue(0,0,1.2f);
    imageBranch->addChild(move);
    auto * quality=new SoComplexity;quality->textureQuality=.1f;imageBranch->addChild(quality);
    for(int unit=0;unit<8;++unit) {
      auto * selector=new SoTextureUnit;selector->unit=unit;imageBranch->addChild(selector);
      auto * texture=new SoTexture2;texture->model=SoTexture2::MODULATE;
      const unsigned char texel[]={249,246,243,255};
      texture->image.setValue(SbVec2s(1,1),4,texel);imageBranch->addChild(texture);
      textures.push_back(texture);
      if(unit==7) lastTexture=texture;
    }
    auto * selector=new SoTextureUnit;selector->unit=0;imageBranch->addChild(selector);
    auto * image=new SoImage;
    image->horAlignment=SoImage::CENTER;image->vertAlignment=SoImage::HALF;
    std::vector<unsigned char> white(17*17*3,255);
    image->image.setValue(SbVec2s(17,17),3,white.data());imageBranch->addChild(image);
  }
  ~Scene(){root->unref();}
  void select(int slot) {
    for(size_t i=0;i<spots.size();++i) spots[i]->intensity=i==size_t(slot)? .8f:0.f;
    for(size_t i=0;i<directionals.size();++i) directionals[i]->intensity=i==size_t(slot)? .8f:0.f;
    float x,y;
    if(directional) {
      x=(.18f+.025f*slot)*2.8f;
      y=(.27f-.015f*slot)*2.8f;
    } else {
      const float lx=1.35f+.1f*slot,ly=1.8f-.06f*slot,lz=6.f+.04f*slot;
      x=lx*2.8f/(lz-1.2f);y=ly*2.8f/(lz-1.2f);
    }
    blockerMove->translation.setValue(x,y,0);
  }
  void clearBlocker() {
    SbVec3f value=blockerMove->translation.getValue();
    blockerMove->translation.setValue(value[0]+1.5f,value[1],0);
  }
  void lastTexel(const std::array<unsigned char,4> & texel) {
    lastTexture->image.setValue(SbVec2s(1,1),4,texel.data());
  }
};
bool capture(CoinRenderAction & action,CoinRenderTarget & target,SoNode * scene,
             Frame & frame,const std::string & label) {
  action.apply(scene);
  target.readbackRGBA(frame.rgba);target.readbackDepth(frame.depth);
  frame.serial=target.getLastSubmissionSerial();
  const bool good=action.getLastStatus()==CoinRenderAction::SUCCESS &&
    frame.rgba.size()==size_t(side*side*4) && frame.depth.size()==size_t(side*side);
  if(!good) std::cerr << label << " status=" << int(action.getLastStatus())
                      << " diagnostic=" << action.getLastError().getString() << '\n';
  return check(good,label);
}
bool exercise(CoinRenderAction & action,CoinRenderTarget & target,int maps,
              bool directional) {
  Scene scene(maps,directional);
  bool ok=true;
  for(int slot=0;slot<maps;++slot) {
    scene.select(slot);
    const std::string label="maps="+std::to_string(maps)+" directional="+
      std::to_string(directional)+" slot="+std::to_string(slot);
    Frame blocked,clear,mutated;
    const bool a=capture(action,target,scene.root,blocked,label+" blocked");
    scene.clearBlocker();
    const bool b=capture(action,target,scene.root,clear,label+" clear");
    const std::array<unsigned char,4> changed{{180,240,215,255}};
    scene.lastTexel(changed);
    const bool c=capture(action,target,scene.root,mutated,label+" unit7 changed");
    if(a && b && c) {
      size_t darkened=0,colorChanged=0,covered=0,numericError=0;
      int maximumDepth=0,maximumAlpha=0;
      for(int y=59;y<=69;++y) for(int x=59;x<=69;++x) {
        const size_t pixel=size_t(y*side+x),at=pixel*4;
        int loss=0,difference=0;
        for(int channel=0;channel<3;++channel) {
          loss+=int(clear.rgba[at+channel])-int(blocked.rgba[at+channel]);
          difference+=std::abs(int(clear.rgba[at+channel])-
            int(mutated.rgba[at+channel]));
        }
        darkened+=loss>3;colorChanged+=difference>3;
        covered+=clear.rgba[at]>10;
        const unsigned char original[]={249,246,243};
        for(int channel=0;channel<3;++channel) {
          const int expected=int(std::lround(double(clear.rgba[at+channel])*
            double(changed[channel])/double(original[channel])));
          numericError=std::max(numericError,size_t(std::abs(expected-
            int(mutated.rgba[at+channel]))));
        }
        maximumDepth=std::max(maximumDepth,int(std::abs(clear.depth[pixel]-
          blocked.depth[pixel])>1e-6f));
        maximumAlpha=std::max(maximumAlpha,std::abs(int(clear.rgba[at+3])-
          int(blocked.rgba[at+3])));
      }
      std::cout << "image_inherited_shadow " << label << " covered=" << covered
                << " darkened=" << darkened << " unit7_changed=" << colorChanged
                << " product_error=" << numericError << " depth_changed="
                << maximumDepth << " alpha_delta=" << maximumAlpha
                << " serial=" << blocked.serial << ',' << clear.serial << ','
                << mutated.serial << '\n';
      ok &= check(covered>0 && darkened>0 && colorChanged>0 && numericError<=3 &&
        maximumDepth==0 && maximumAlpha==0 && blocked.serial<clear.serial &&
        clear.serial<mutated.serial,label+" map and unit7 both contribute");
    } else ok=false;
    scene.lastTexel({{249,246,243,255}});
  }
  // Changing every inherited unit separately is a metamorphic sampler
  // identity check while all map resources remain allocated. It catches a
  // shadow binding that silently replaces any one of the eight textures.
  scene.select(0);
  scene.clearBlocker();
  Frame allUnits;
  const bool base=capture(action,target,scene.root,allUnits,
    "all eight inherited units with maps allocated");
  ok &= base;
  const unsigned char original[]={249,246,243,255};
  const unsigned char changed[]={180,240,215,255};
  for(int unit=0;unit<8;++unit) {
    scene.textures[size_t(unit)]->image.setValue(SbVec2s(1,1),4,changed);
    Frame variant;
    const bool got=capture(action,target,scene.root,variant,
      "mutated inherited unit "+std::to_string(unit));
    if(base && got) {
      size_t changedPixels=0;int maxNumericError=0;
      for(int y=61;y<=67;++y) for(int x=61;x<=67;++x) {
        const size_t at=(size_t(y)*side+x)*4;
        int difference=0;
        for(int channel=0;channel<3;++channel) {
          difference+=std::abs(int(variant.rgba[at+channel])-
            int(allUnits.rgba[at+channel]));
          const int expected=int(std::lround(double(allUnits.rgba[at+channel])*
            double(changed[channel])/double(original[channel])));
          maxNumericError=std::max(maxNumericError,
            std::abs(expected-int(variant.rgba[at+channel])));
        }
        changedPixels+=difference>3;
      }
      std::cout << "image_inherited_shadow maps=" << maps
                << " directional=" << directional << " texture_unit=" << unit
                << " changed_pixels=" << changedPixels
                << " product_error=" << maxNumericError << '\n';
      ok &= check(changedPixels>0 && maxNumericError<=3 &&
        variant.serial>allUnits.serial,
        "each inherited texture unit contributes beside shadow resources");
    } else ok=false;
    scene.textures[size_t(unit)]->image.setValue(SbVec2s(1,1),4,original);
  }
  return ok;
}
bool casterExercise(CoinRenderAction & action,CoinRenderTarget & target,
                    int maps,bool directional) {
  Scene scene(maps,directional);
  scene.blockerStyle->style=SoShadowStyle::NO_SHADOWING;
  scene.camera->position.setValue(0,0,3);
  scene.camera->nearDistance=2.5f;
  scene.camera->farDistance=10.f;
  bool ok=true;
  for(int slot=0;slot<maps;++slot) {
    scene.select(slot);
    scene.imageStyle->style=SoShadowStyle::NO_SHADOWING;
    Frame noCast,cast;
    const std::string label="caster/maps="+std::to_string(maps)+
      "/directional="+std::to_string(directional)+"/slot="+
      std::to_string(slot);
    const bool a=capture(action,target,scene.root,noCast,label+" no-cast");
    scene.imageStyle->style=SoShadowStyle::CASTS_SHADOW;
    const bool b=capture(action,target,scene.root,cast,label+" cast");
    if(a && b) {
      size_t affected=0;int maxDepth=0,maxAlpha=0;
      for(size_t pixel=0;pixel<size_t(side*side);++pixel) {
        const size_t at=pixel*4;
        int loss=0;
        for(int channel=0;channel<3;++channel)
          loss+=int(noCast.rgba[at+channel])-int(cast.rgba[at+channel]);
        affected+=loss>3;
        maxDepth=std::max(maxDepth,int(std::abs(noCast.depth[pixel]-
          cast.depth[pixel])>1e-6f));
        maxAlpha=std::max(maxAlpha,std::abs(int(noCast.rgba[at+3])-
          int(cast.rgba[at+3])));
      }
      std::cout << "image_inherited_shadow " << label << " affected=" << affected
                << " depth_changed=" << maxDepth << " alpha_delta=" << maxAlpha
                << " serial=" << noCast.serial << ',' << cast.serial << '\n';
      ok &= check(affected>0 && maxDepth==0 && maxAlpha==0 &&
        cast.serial>noCast.serial,
        label+" inherited-texture image casts from the map camera");
    } else ok=false;
  }
  return ok;
}
}
int main(int argc,char ** argv) {
  int maps=1;bool directional=false,canonical=false,caster=false;
  for(int i=1;i<argc;++i) {
    const std::string arg=argv[i];
    if(arg=="--maps4") maps=4;
    else if(arg=="--maps8") maps=8;
    else if(arg=="--directional") directional=true;
    else if(arg=="--canonical-raster") canonical=true;
    else if(arg=="--caster") caster=true;
    else {std::cerr << "unknown argument " << arg << '\n';return 2;}
  }
  SoDB::init();CoinRenderAction::initClass();
  CoinRenderCapabilities caps{};
  const bool available=coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,
    &caps,sizeof(caps))==0 && caps.gpu_available &&
    caps.renderer!=COIN_RENDER_RENDERER_UNKNOWN;
  if(!check(available,"GPU adapter and renderer required")) return 1;
  CoinRenderOptions options{};
  options.renderer=static_cast<CoinRenderRenderer>(caps.renderer);
  if(canonical) options.rasterizationPolicy=COIN_RENDER_RASTER_CANONICAL_8BIT;
  std::unique_ptr<CoinRenderTarget> target(
    CoinRenderTarget::createOffscreen(SbVec2i32(side,side),options));
  if(!check(bool(target) && target->getOptions().renderer==options.renderer,
    "offscreen target uses queried renderer")) return 1;
  target->setDepthReadbackEnabled(TRUE);
  CoinRenderAction action(SbViewportRegion(side,side));
  action.setRenderTarget(target.get());
  action.setBackgroundColor(SbColor4f(0,0,0,1));
  action.setTransparencyType(CoinRenderAction::BLEND);
  std::cout << "image_inherited_shadow backend=" << caps.backend
            << " renderer=" << caps.renderer << " vendor=" << caps.vendor_id
            << " adapter=" << caps.adapter_name << " gpu_available=" << available
            << " raster=" << int(target->getOptions().rasterizationPolicy) << '\n';
  bool ok=check(available,"GPU adapter required");
  if(ok) ok=caster ? casterExercise(action,*target,maps,directional) :
    exercise(action,*target,maps,directional);
  action.setRenderTarget(nullptr);
  return ok?0:1;
}
