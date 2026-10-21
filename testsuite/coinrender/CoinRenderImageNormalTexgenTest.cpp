#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoImage.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoNormal.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoText2.h>
#include <Inventor/nodes/SoSwitch.h>
#include <Inventor/nodes/SoDrawStyle.h>
#include <Inventor/nodes/SoPointSet.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoBumpMap.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoNormalBinding.h>
#include <cstdlib>
#include "actions/CoinRenderActionP.h"
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCubeMap.h>
#include <Inventor/nodes/SoTexture3.h>
#include <Inventor/nodes/SoTextureCoordinateEnvironment.h>
#include <Inventor/nodes/SoTextureCoordinateReflectionMap.h>
#include <Inventor/nodes/SoTextureCoordinateNormalMap.h>
#include <Inventor/nodes/SoTextureCoordinateObject.h>
#include <Inventor/nodes/SoTextureMatrixTransform.h>
#include <Inventor/nodes/SoTextureUnit.h>
#include <Inventor/nodes/SoTransform.h>
#include <Inventor/nodes/SoResetTransform.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinrender/CoinRenderTextureSamplingCore.h"
#include "rendering/coinwgpu/CoinWgpuFfiFrame.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>
// Existing internal test friend; reads captured state only, adds no exported symbol.
struct CoinRenderFramePlanBuilderTestAccess {
  static bool inspect(const CoinRenderFramePlanBuilder&builder,CoinRenderFramePlan&plan,std::string&error){plan=builder.currentPlan;error=builder.builderError;return !builder.hasError&&!builder.isUnsupported;}
};
namespace {
constexpr int side=64;
bool check(bool yes,const std::string&label){if(!yes)std::cerr<<"image_normal_texgen FAIL "<<label<<'\n';return yes;}
class Capture:public CoinRenderCpuReferenceBackend {
public:CoinRenderFramePlan plan;unsigned calls=0;
  CoinRenderSubmitResult submit(const CoinRenderFramePlan&frame,CoinRenderTargetP&target)override{++calls;plan=frame;return CoinRenderCpuReferenceBackend::submit(frame,target);}
};
struct Scene {
  SoSeparator*root=new SoSeparator;SoImage*image=new SoImage;SoComplexity*quality=new SoComplexity;SoTransform*model=new SoTransform;
  std::vector<int>units;std::array<int,8>kinds{};std::array<double,8>q{};std::array<SoTextureCoordinateObject*,8>prefixes{};std::array<SoTextureMatrixTransform*,8>matrices{};SoNormal*elementNormal=new SoNormal;
  Scene(int kind,int selected,bool objectPrefix,bool clipped,bool normalNode,bool eight=false,bool spatialQ=false){root->ref();auto*camera=new SoOrthographicCamera;
    camera->position.setValue(0,0,10);camera->height=64;camera->nearDistance=1;camera->farDistance=30;root->addChild(camera);
    auto*lighting=new SoLightModel;lighting->model=SoLightModel::BASE_COLOR;root->addChild(lighting);quality->textureQuality=.1f;root->addChild(quality);
    // The old model/camera may locate the anchor, but must not transform the
    // current normal after SoImage installs identity MODELVIEW for RasterPos.
    model->translation.setValue(clipped?-28.f:0.f,clipped?-20.f:0.f,0);model->rotation.setValue(SbVec3f(0,1,0),.71f);model->scaleFactor.setValue(2,3,4);root->addChild(model);
    if(normalNode){elementNormal->vector.setValue(1,0,0);root->addChild(elementNormal);}
    units=eight?std::vector<int>{0,1,2,3,4,5,6,7}:std::vector<int>{selected};
    for(int unit:units){auto*active=new SoTextureUnit;active->unit=unit;root->addChild(active);auto*texture=new SoTexture2;
      std::vector<uint8_t>pixels(4*4*4,255);for(int y=0;y<4;++y)for(int x=0;x<4;++x){const size_t p=size_t(y*4+x)*4;
        pixels[p]=uint8_t(20+50*x);pixels[p+1]=uint8_t(30+40*y);pixels[p+2]=uint8_t(40+15*(x+y));}
      if(unit!=units.back())std::fill(pixels.begin(),pixels.end(),255);
      texture->image.setValue(SbVec2s(4,4),4,pixels.data());texture->model=SoTexture2::MODULATE;texture->wrapS=texture->wrapT=SoTexture2::CLAMP;root->addChild(texture);
      // All four generators are enabled natively. Reflection/Normal preserve
      // the Q generator mode/plane, so seed a defined nonzero Q explicitly.
      if(objectPrefix){auto*prefix=new SoTextureCoordinateObject;prefix->factorQ.setValue(spatialQ?.01f:0.f,spatialQ?.02f:0.f,spatialQ?.03f:0.f,2);prefixes[unit]=prefix;root->addChild(prefix);}
      else root->addChild(new SoTextureCoordinateEnvironment);
      kinds[unit]=eight?unit%3:kind;q[unit]=kinds[unit]==0?1:(objectPrefix?2:1);if(spatialQ&&objectPrefix&&kinds[unit]!=0)q[unit]+=.01*(clipped?0.:24.)+.02*(clipped?4.:24.)+.03*(1.-2.*9./29.);
      SoNode*generator=kinds[unit]==0?static_cast<SoNode*>(new SoTextureCoordinateEnvironment):kinds[unit]==1?static_cast<SoNode*>(new SoTextureCoordinateReflectionMap):static_cast<SoNode*>(new SoTextureCoordinateNormalMap);root->addChild(generator);
      auto*matrix=new SoTextureMatrixTransform;matrices[unit]=matrix;SbMatrix m=SbMatrix::identity();m[0][0]=m[1][1]=.25f;m[2][2]=.5f;m[3][0]=m[3][1]=.125f;matrix->matrix=m;root->addChild(matrix);
    }
    image->horAlignment=SoImage::CENTER;image->vertAlignment=SoImage::HALF;std::vector<uint8_t>white(16*16*3,255);image->image.setValue(SbVec2s(16,16),3,white.data());root->addChild(image);
  }
  ~Scene(){root->unref();}
};
// GL1.4 texgen with SoImage's identity modelview. Current normal is initially
// (0,0,1); a bare SoNormal does not issue glNormal. DepthCoin is NDC depth.
std::array<double,4>coordinate(int kind,double q,bool clipped,std::array<double,3>normal={{0,0,1}},int extent=side){
  const double x=clipped?std::max(0,int(extent/16)-8):extent/2-8,y=clipped?std::max(0,int(3*extent/16)-8):extent/2-8;const double depthCoin=2.*9./29.-1.;
  const double z=-depthCoin,length=std::sqrt(x*x+y*y+z*z),dot=(x*normal[0]+y*normal[1]+z*normal[2])/length;
  const double rx=x/length-2*dot*normal[0],ry=y/length-2*dot*normal[1],rz=z/length-2*dot*normal[2];
  if(kind==2)return {{normal[0],normal[1],normal[2],q}};
  if(kind==1)return {{rx,ry,rz,q}};
  const double m=2.*std::sqrt(rx*rx+ry*ry+(rz+1)*(rz+1));return {{rx/m+.5,ry/m+.5,1,1}};
}
bool delivery(const Scene&s,const CoinRenderFramePlan&plan,bool clipped,std::array<double,3>normal={{0,0,1}},int extent=side){
  CoinWgpuFfiFrame ffi;std::string why;bool ok=check(ffi.prepare(plan,extent,extent,why,false),"generated image FFI prepares: "+why);if(!ok)return false;
  std::vector<uint32_t>imageStates;for(const auto&draw:plan.draws)if(draw.sourceNodeId==s.image->getNodeId()&&std::find(imageStates.begin(),imageStates.end(),draw.renderStateSlot)==imageStates.end())imageStates.push_back(draw.renderStateSlot);
  unsigned rasters=0;
  for(size_t slot=0;slot<plan.renderStates.size();++slot){const auto&raster=plan.renderStates[slot];if(!raster.rasterPixels||std::find(imageStates.begin(),imageStates.end(),uint32_t(slot))==imageStates.end())continue;++rasters;
    if(!check(raster.rasterTextureSourceSlot<plan.renderStates.size(),"image keeps explicit inherited nonraster source state"))return false;
    const auto&source=plan.renderStates[raster.rasterTextureSourceSlot];ok &= check(!source.rasterPixels,"source is not recursive raster");
    const auto&packed=ffi.getView().states[slot];ok &= check(packed.raster_texture_source_slot==raster.rasterTextureSourceSlot+1,"FFI preserves source-state ownership");
    for(int unit:s.units){const auto layer=coin_render_texture_unit(source,size_t(unit));const auto expected=coordinate(s.kinds[unit],s.q[unit],clipped,normal,extent);
      ok &= check(layer.enabled&&layer.imageSlot<plan.textures.size()&&layer.samplerSlot<plan.samplers.size(),"all authored generated units keep their independent image/sampler");
      ok &= check(layer.matrix==s.matrices[unit]->matrix.getValue(),"texture matrix remains exactly authored in source, separate from raw generated coordinate");
      for(int c=0;c<4;++c){const double bound=8.*std::numeric_limits<float>::epsilon()*std::max(1.,std::abs(expected[c]));
        const double actual=raster.rasterTextureCoordinates[unit][c];if(std::abs(actual-expected[c])>bound)std::cout<<"normal_texgen_coordinate unit="<<unit<<" kind="<<s.kinds[unit]<<" component="<<c<<" expected="<<std::hexfloat<<expected[c]<<" actual="<<actual<<" q="<<s.q[unit]<<std::defaultfloat<<'\n';
        ok &= check(std::isfinite(actual)&&std::abs(actual-expected[c])<=bound,"raw GL sphere/reflection/normal formula uses aligned clipped raster eye-position and native Q history");
        ok &= check(packed.raster_texture_coordinates[unit][c]==raster.rasterTextureCoordinates[unit][c],"FFI owns exactly the captured raw raster component");}
    }
  }return check(rasters==1,"one raster image state")&&ok;
}
bool capture(){
  std::unique_ptr<CoinRenderTarget>target(CoinRenderTarget::createOffscreen(SbVec2i32(side,side)));if(!check(bool(target),"CPU target exists"))return false;
  auto*record=new Capture;target->getPimpl()->backend.reset(record);target->setDepthReadbackEnabled(TRUE);CoinRenderAction action(SbViewportRegion(side,side));action.setRenderTarget(target.get());action.setBackgroundColor(SbColor4f(0,0,0,1));bool ok=true;
  for(int kind=0;kind<3;++kind)for(int unit:{0,3,7})for(bool clipped:{false,true})for(bool objectPrefix:{false,true}){
    Scene scene(kind,unit,objectPrefix,clipped,true);const auto before=target->getLastSubmissionSerial();const unsigned calls=record->calls;action.apply(scene.root);
    const bool success=check(action.getLastStatus()==CoinRenderAction::SUCCESS&&target->getLastSubmissionSerial()==before+1&&record->calls==calls+1,"defined normal texgen image publishes exactly once");ok &= success;
    std::cout<<"normal_texgen_capture kind="<<kind<<" unit="<<unit<<" clipped="<<clipped<<" object_q_prefix="<<objectPrefix<<" status="<<int(action.getLastStatus())<<" error="<<action.getLastError().getString()<<'\n';if(!success)continue;
    ok &= delivery(scene,record->plan,clipped);std::vector<uint8_t>rgba;std::vector<float>depth;target->readbackRGBA(rgba);target->readbackDepth(depth);
    const auto raw=coordinate(kind,scene.q[unit],clipped);const double u=.25*raw[0]/raw[3]+.125,v=.25*raw[1]/raw[3]+.125;
    const int tx=std::max(0,std::min(3,int(std::floor(4*u)))),ty=std::max(0,std::min(3,int(std::floor(4*v))));const int expected[]={20+50*tx,30+40*ty,40+15*(tx+ty)};
    const int px=clipped?2:32,py=clipped?52:32;const size_t p=size_t(py*side+px);bool color=rgba.size()==size_t(side*side*4)&&depth.size()==size_t(side*side);
    if(color)for(int c=0;c<3;++c)color &= std::abs(int(rgba[4*p+c])-expected[c])<=1;
    ok &= check(color,"CPU image samples gradient at generated coordinate with texture matrix exactly once");
    const auto first=record->plan;action.apply(scene.root);ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS&&first.hasSamePayload(record->plan),"image/frame/SoNormal preserve defined normal history and owned payload");
  }
  Scene eight(0,7,true,false,false,true);action.apply(eight.root);ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS,"eight independent normal generator units are admitted");if(action.getLastStatus()==CoinRenderAction::SUCCESS)ok &= delivery(eight,record->plan,false);
  action.setRenderTarget(nullptr);return ok;
}
bool history(){
  std::unique_ptr<CoinRenderTarget>target(CoinRenderTarget::createOffscreen(SbVec2i32(side,side)));if(!check(bool(target),"history target exists"))return false;
  auto*record=new Capture;target->getPimpl()->backend.reset(record);CoinRenderAction action(SbViewportRegion(side,side));action.setRenderTarget(target.get());
  // Cube's native path is glBegin/glEnd. PHONG guarantees sendNormals and its
  // last explicit glNormal is(0,-1,0); this is not an inference from arrays.
  auto*predecessor=new SoSeparator;predecessor->ref();auto*phong=new SoLightModel;phong->model=SoLightModel::PHONG;predecessor->addChild(phong);
  auto*move=new SoTransform;move->translation.setValue(-20,0,0);move->rotation.setValue(SbVec3f(1,0,0),.8f);move->scaleFactor.setValue(3,2,4);predecessor->addChild(move);predecessor->addChild(new SoCube);
  Scene first(2,7,true,false,true);first.root->insertChild(predecessor,first.root->findChild(first.model));
  action.apply(first.root);bool ok=check(action.getLastStatus()==CoinRenderAction::SUCCESS,"PHONG cube normal history is defined across Separator/model restore");
  if(action.getLastStatus()==CoinRenderAction::SUCCESS)ok &= delivery(first,record->plan,false,{{0,-1,0}});
  // No geometry in these subsequent frames issues glNormal. SoNormal writes
  // only the Inventor element; DrawPixels, bitmap text and clear preserve GL.
  Scene second(2,3,true,false,true);action.apply(second.root);ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS,"new frame preserves emitted current normal");
  if(action.getLastStatus()==CoinRenderAction::SUCCESS)ok &= delivery(second,record->plan,false,{{0,-1,0}});
  auto*empty=new SoSeparator;empty->ref();action.apply(empty);ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS,"clear-only frame is admitted");empty->unref();
  auto*text=new SoText2;text->string="normal history";second.root->insertChild(text,second.root->getNumChildren()-1);
  action.apply(second.root);ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS,"Text2 does not invent a normal before Image");
  if(action.getLastStatus()==CoinRenderAction::SUCCESS){
    // Text2 adds its own raster. Preserve the whole captured plan for FFI;
    // delivery() selects the image by its actual draw source identity.
    ok &= delivery(second,record->plan,false,{{0,-1,0}});
  }
  second.root->removeChild(text);
  auto*colorOnly=new SoSeparator;auto*base=new SoLightModel;base->model=SoLightModel::BASE_COLOR;colorOnly->addChild(base);colorOnly->addChild(new SoCube);
  second.root->insertChild(colorOnly,second.root->findChild(second.model));action.apply(second.root);
  ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS,"untextured BASE_COLOR cube preserves prior GL normal");if(action.getLastStatus()==CoinRenderAction::SUCCESS)ok &= delivery(second,record->plan,false,{{0,-1,0}});
  action.setRenderTarget(nullptr);predecessor->unref();return ok;
}

bool qHistory(){
  std::unique_ptr<CoinRenderTarget>target(CoinRenderTarget::createOffscreen(SbVec2i32(side,side)));if(!check(bool(target),"Q history CPU target"))return false;
  auto*record=new Capture;target->getPimpl()->backend.reset(record);CoinRenderAction action(SbViewportRegion(side,side));action.setRenderTarget(target.get());bool ok=true;
  for(int kind=0;kind<3;++kind)for(bool clipped:{false,true}){Scene scene(kind,7,true,clipped,true,false,true);action.apply(scene.root);
    const bool success=check(action.getLastStatus()==CoinRenderAction::SUCCESS,"normal generator retains nonconstant Q object-plane history");ok &= success;if(success)ok &= delivery(scene,record->plan,clipped);}
  for(int kind:{0,2}){Scene scene(kind,7,false,false,true);auto*branch=new SoSeparator;
    auto*object=new SoTextureCoordinateObject;object->factorQ.setValue(0,0,0,2);branch->addChild(object);branch->addChild(new SoTextureCoordinateNormalMap);
    auto*inner=new SoImage;inner->horAlignment=SoImage::CENTER;inner->vertAlignment=SoImage::HALF;std::vector<uint8_t>white(16*16*3,255);inner->image.setValue(SbVec2s(16,16),3,white.data());branch->addChild(inner);
    scene.root->insertChild(branch,scene.root->getNumChildren()-1);
    // Pop replays the outer callback. Environment writes Q=1; NormalMap
    // touches onlySTR, so the child Object's Q=2 survives that callback.
    scene.q[7]=kind==0?1:2;action.apply(scene.root);
    const bool success=check(action.getLastStatus()==CoinRenderAction::SUCCESS,"Separator restores native callback semantics rather than resetting Q arbitrarily");ok &= success;if(success)ok &= delivery(scene,record->plan,false);
  }
  action.setRenderTarget(nullptr);return ok;
}
bool atomic(){
  std::unique_ptr<CoinRenderTarget>target(CoinRenderTarget::createOffscreen(SbVec2i32(side,side)));if(!check(bool(target),"atomic CPU target"))return false;
  auto*record=new Capture;target->getPimpl()->backend.reset(record);target->setDepthReadbackEnabled(TRUE);CoinRenderAction action(SbViewportRegion(side,side));action.setRenderTarget(target.get());
  Scene scene(1,7,true,false,true);action.apply(scene.root);if(!check(action.getLastStatus()==CoinRenderAction::SUCCESS,"atomic normal generator anchor"))return false;
  std::vector<uint8_t>rgba,after;std::vector<float>depth,afterDepth;target->readbackRGBA(rgba);target->readbackDepth(depth);bool ok=true;
  for(int invalid=0;invalid<3;++invalid){const auto serial=target->getLastSubmissionSerial();const float nan=std::numeric_limits<float>::quiet_NaN();
    if(invalid==0)scene.prefixes[7]->factorQ.setValue(0,0,0,nan);
    if(invalid==1)scene.prefixes[7]->factorQ.setValue(0,0,0,0);
    if(invalid==2){auto matrix=scene.matrices[7]->matrix.getValue();matrix[0][0]=nan;scene.matrices[7]->matrix=matrix;}
    action.apply(scene.root);target->readbackRGBA(after);target->readbackDepth(afterDepth);
    ok &= check(action.getLastStatus()!=CoinRenderAction::SUCCESS&&target->getLastSubmissionSerial()==serial&&after==rgba&&afterDepth==depth&&!std::string(action.getLastError().getString()).empty(),"invalid effective raster Q/matrix refuses atomically without publication");
    scene.prefixes[7]->factorQ.setValue(0,0,0,2);SbMatrix matrix=SbMatrix::identity();matrix[0][0]=matrix[1][1]=.25f;matrix[2][2]=.5f;matrix[3][0]=matrix[3][1]=.125f;scene.matrices[7]->matrix=matrix;
    action.apply(scene.root);target->readbackRGBA(after);target->readbackDepth(afterDepth);ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS&&target->getLastSubmissionSerial()==serial+1&&after==rgba&&afterDepth==depth,"normal generator invalid-state recovery A is exact on same target");
  }
  scene.quality->textureQuality=0;scene.prefixes[7]->factorQ.setValue(0,0,0,0);action.apply(scene.root);target->readbackRGBA(after);
  const size_t p=size_t(32*side+32)*4;ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS&&after.size()==size_t(side*side*4)&&after[p]==255&&after[p+1]==255&&after[p+2]==255,"qualityzero disables effective inherited texgen and preserves white Image PRIMARY even with Qzero");
  action.setRenderTarget(nullptr);return ok;
}

SoTexture3*installVolume(Scene&scene,int unit){
  for(int i=scene.root->getNumChildren()-1;i>=0;--i)if(scene.root->getChild(i)->isOfType(SoTexture2::getClassTypeId()))scene.root->removeChild(i);
  auto*texture=new SoTexture3;texture->model=SoTexture3::MODULATE;texture->wrapS=texture->wrapT=texture->wrapR=SoTexture3::CLAMP;
  std::vector<uint8_t>voxels(4*4*4*4,255);for(int z=0;z<4;++z)for(int y=0;y<4;++y)for(int x=0;x<4;++x){const size_t p=size_t((z*4+y)*4+x)*4;voxels[p]=uint8_t(20+50*x);voxels[p+1]=uint8_t(30+40*y);voxels[p+2]=uint8_t(30+50*z);}
  texture->images.setValue(SbVec3s(4,4,4),4,voxels.data());scene.root->insertChild(texture,scene.root->findChild(scene.matrices[unit]));return texture;
}
std::array<int,3>volumeColor(const Scene&scene,int extent=side){const int unit=scene.units.back();const auto raw=coordinate(scene.kinds[unit],scene.q[unit],false,{{0,0,1}},extent);
  const int x=std::max(0,std::min(3,int(std::floor(4*(.25*raw[0]/raw[3]+.125))))),y=std::max(0,std::min(3,int(std::floor(4*(.25*raw[1]/raw[3]+.125))))),z=std::max(0,std::min(3,int(std::floor(4*.5*raw[2]/raw[3]))));
  return {{20+50*x,30+40*y,30+50*z}};
}
bool volume(){
  std::unique_ptr<CoinRenderTarget>target(CoinRenderTarget::createOffscreen(SbVec2i32(side,side)));if(!check(bool(target),"volume normal texgen CPU target"))return false;auto*record=new Capture;target->getPimpl()->backend.reset(record);CoinRenderAction action(SbViewportRegion(side,side));action.setRenderTarget(target.get());bool ok=true;
  for(int kind=0;kind<3;++kind)for(int unit:{0,3,7}){Scene scene(kind,unit,true,false,true);installVolume(scene,unit);action.apply(scene.root);const bool success=check(action.getLastStatus()==CoinRenderAction::SUCCESS,"volume normal-dependent texgen with defined positive Q is admitted");ok &= success;if(!success)continue;
    ok &= delivery(scene,record->plan,false);std::vector<uint8_t>rgba;target->readbackRGBA(rgba);const auto expected=volumeColor(scene);const size_t p=size_t(32*side+32)*4;bool matches=rgba.size()==size_t(side*side*4);if(matches)for(int c=0;c<3;++c)matches &= std::abs(int(rgba[p+c])-expected[c])<=1;
    ok &= check(matches,"volume lookup uses generated S/T/R, retained positive Q and texture matrix once to select analytic voxel");}
  action.setRenderTarget(nullptr);return ok;
}
SoTextureCubeMap*installCube(Scene&scene,int unit){
  for(int index=scene.root->getNumChildren()-1;index>=0;--index){auto*node=scene.root->getChild(index);if(node->isOfType(SoTexture2::getClassTypeId())||node->isOfType(SoTextureCoordinateEnvironment::getClassTypeId()))scene.root->removeChild(index);}
  auto*cube=new SoTextureCubeMap;cube->model=SoTextureCubeMap::REPLACE;std::array<std::array<uint8_t,16>,6>faces{};
  for(int face=0;face<6;++face)for(int pixel=0;pixel<4;++pixel){auto&bytes=faces[face];bytes[4*pixel]=uint8_t(23+face*29);bytes[4*pixel+1]=uint8_t(37+face*17);bytes[4*pixel+2]=uint8_t(61+face*13);bytes[4*pixel+3]=255;}
  cube->imagePosX.setValue(SbVec2s(2,2),4,faces[0].data());cube->imageNegX.setValue(SbVec2s(2,2),4,faces[1].data());cube->imagePosY.setValue(SbVec2s(2,2),4,faces[2].data());cube->imageNegY.setValue(SbVec2s(2,2),4,faces[3].data());cube->imagePosZ.setValue(SbVec2s(2,2),4,faces[4].data());cube->imageNegZ.setValue(SbVec2s(2,2),4,faces[5].data());
  scene.root->insertChild(cube,scene.root->findChild(scene.matrices[unit]));scene.q[unit]=0;return cube;
}
bool cubeQzero(){
  std::unique_ptr<CoinRenderTarget>target(CoinRenderTarget::createOffscreen(SbVec2i32(side,side)));if(!check(bool(target),"cube Qzero CPU target"))return false;
  auto*record=new Capture;target->getPimpl()->backend.reset(record);CoinRenderAction action(SbViewportRegion(side,side));action.setRenderTarget(target.get());bool ok=true;
  for(int kind:{1,2})for(int unit:{0,3,7}){Scene scene(kind,unit,false,true,true);
    installCube(scene,unit);action.apply(scene.root);
    const bool success=check(action.getLastStatus()==CoinRenderAction::SUCCESS,"cube normal/reflection direction is valid with fresh native generated Qzero");ok &= success;if(!success)continue;
    ok &= delivery(scene,record->plan,true);std::vector<uint8_t>rgba;target->readbackRGBA(rgba);const int face=kind==2?4:2;const size_t p=size_t(52*side+2)*4;
    const int expected[]={23+face*29,37+face*17,61+face*13};bool matches=rgba.size()==size_t(side*side*4);if(matches)for(int c=0;c<3;++c)matches &= std::abs(int(rgba[p+c])-expected[c])<=1;
    ok &= check(matches,"cube lookup ignores Q and selects analytic nonzero +Z normal or +Y reflection direction face");
  }
  action.setRenderTarget(nullptr);return ok;
}

bool transactionalHistory(){
  std::unique_ptr<CoinRenderTarget>target(CoinRenderTarget::createOffscreen(SbVec2i32(side,side)));if(!check(bool(target),"transactional history target"))return false;
  auto*record=new Capture;target->getPimpl()->backend.reset(record);target->setDepthReadbackEnabled(TRUE);CoinRenderAction action(SbViewportRegion(side,side));action.setRenderTarget(target.get());
  Scene seed(0,7,false,false,true);action.apply(seed.root);if(!check(action.getLastStatus()==CoinRenderAction::SUCCESS,"delivered Environment seeds native Q1 with initial normalZ"))return false;
  Scene scene(2,7,false,false,true);for(int i=scene.root->getNumChildren()-1;i>=0;--i)if(scene.root->getChild(i)->isOfType(SoTextureCoordinateEnvironment::getClassTypeId()))scene.root->removeChild(i);
  action.apply(scene.root);bool ok=check(action.getLastStatus()==CoinRenderAction::SUCCESS,"A uses previously delivered Q1 without reauthoring Q");if(!ok)return false;
  ok &= delivery(scene,record->plan,false);std::vector<uint8_t>rgba,after;std::vector<float>depth,afterDepth;target->readbackRGBA(rgba);target->readbackDepth(depth);
  auto*toggle=new SoSwitch;toggle->whichChild=SO_SWITCH_NONE;auto*branch=new SoSeparator;toggle->addChild(branch);
  auto*unit=new SoTextureUnit;unit->unit=7;branch->addChild(unit);auto*object=new SoTextureCoordinateObject;object->factorQ.setValue(0,0,0,2);branch->addChild(object);
  auto*phong=new SoLightModel;phong->model=SoLightModel::PHONG;branch->addChild(phong);auto*move=new SoTransform;move->translation.setValue(-20,0,0);branch->addChild(move);branch->addChild(new SoCube);
  scene.root->insertChild(toggle,scene.root->findChild(scene.model));const auto matrix=scene.matrices[7]->matrix.getValue();
  for(bool planOnly:{true,false}){const auto serial=target->getLastSubmissionSerial();const unsigned calls=record->calls;toggle->whichChild=SO_SWITCH_ALL;
    if(planOnly){action.getPimpl()->executionOptions=target->getOptions();action.getPimpl()->planOnly=true;}
    else{auto invalid=matrix;invalid[0][0]=std::numeric_limits<float>::quiet_NaN();scene.matrices[7]->matrix=invalid;}
    action.apply(scene.root);target->readbackRGBA(after);target->readbackDepth(afterDepth);
    if(planOnly&&action.getLastStatus()==CoinRenderAction::SUCCESS){scene.q[7]=2;ok &= delivery(scene,action.getPimpl()->lastValidPlan,false,{{0,-1,0}});scene.q[7]=1;}
    ok &= check((planOnly?action.getLastStatus()==CoinRenderAction::SUCCESS:action.getLastStatus()!=CoinRenderAction::SUCCESS)&&record->calls==calls&&target->getLastSubmissionSerial()==serial&&after==rgba&&afterDepth==depth,
      planOnly?"planOnly B captures different normal/Q without executing or publishing":"invalid B captures different normal/Q but refuses before execution/publication");
    action.getPimpl()->planOnly=false;toggle->whichChild=SO_SWITCH_NONE;scene.matrices[7]->matrix=matrix;
    action.apply(scene.root);target->readbackRGBA(after);target->readbackDepth(afterDepth);
    const bool published=check(action.getLastStatus()==CoinRenderAction::SUCCESS&&record->calls==calls+1&&target->getLastSubmissionSerial()==serial+1&&after==rgba&&afterDepth==depth,"A without Q/normal reauthoring recovers exactly after unexecuted B");
    ok &= published;if(published)ok &= delivery(scene,record->plan,false);
  }
  action.setRenderTarget(nullptr);return ok;
}

bool eligibility(){
  std::unique_ptr<CoinRenderTarget>target(CoinRenderTarget::createOffscreen(SbVec2i32(side,side)));if(!check(bool(target),"eligibility CPU target"))return false;
  auto*record=new Capture;target->getPimpl()->backend.reset(record);CoinRenderAction action(SbViewportRegion(side,side));action.setRenderTarget(target.get());bool ok=true;
  for(int kind=0;kind<4;++kind){Scene scene(2,7,true,false,true);auto*branch=new SoSeparator;auto*light=new SoLightModel;light->model=kind==3?SoLightModel::BASE_COLOR:SoLightModel::PHONG;branch->addChild(light);
    auto*move=new SoTransform;move->translation.setValue(-20,0,0);branch->addChild(move);
    if(kind==0){auto*draw=new SoDrawStyle;draw->style=SoDrawStyle::INVISIBLE;branch->addChild(draw);branch->addChild(new SoCube);}
    if(kind==1){auto*skip=new SoSwitch;skip->whichChild=SO_SWITCH_NONE;skip->addChild(new SoCube);branch->addChild(skip);}
    if(kind==2){auto*empty=new SoPointSet;empty->numPoints=0;branch->addChild(empty);}
    if(kind==3){auto*complexity=new SoComplexity;complexity->type=SoComplexity::BOUNDING_BOX;branch->addChild(complexity);branch->addChild(new SoCube);}
    scene.root->insertChild(branch,scene.root->findChild(scene.model));action.apply(scene.root);
    const bool success=check(action.getLastStatus()==CoinRenderAction::SUCCESS,"native eligibility control is admitted without fabricating array history");ok &= success;
    if(success)ok &= delivery(scene,record->plan,false,kind==3?std::array<double,3>{{0,-1,0}}:std::array<double,3>{{0,0,1}});
  }
  action.setRenderTarget(nullptr);return ok;
}

bool sameGraphHistory(){
  std::unique_ptr<CoinRenderTarget>target(CoinRenderTarget::createOffscreen(SbVec2i32(side,side)));if(!check(bool(target),"same graph history CPU target"))return false;
  auto*record=new Capture;target->getPimpl()->backend.reset(record);CoinRenderAction action(SbViewportRegion(side,side));action.setRenderTarget(target.get());Scene scene(2,7,true,false,true);
  auto matrix=scene.matrices[7]->matrix.getValue();matrix[1][1]=1;matrix[3][1]=.5f;scene.matrices[7]->matrix=matrix;
  auto*after=new SoSeparator;after->addChild(new SoResetTransform);auto*phong=new SoLightModel;phong->model=SoLightModel::PHONG;after->addChild(phong);auto*move=new SoTransform;move->translation.setValue(-20,0,0);after->addChild(move);after->addChild(new SoCube);scene.root->addChild(after);
  const auto revision=scene.root->getNodeId();std::vector<uint8_t>first,second;bool ok=true;CoinRenderFramePlan initial;
  for(int frame=0;frame<2;++frame){const auto serial=target->getLastSubmissionSerial();action.apply(scene.root);std::vector<uint8_t>pixels;target->readbackRGBA(pixels);
    std::cout<<"same_graph_history frame="<<frame<<" status="<<int(action.getLastStatus())<<" error="<<action.getLastError().getString()<<" serial="<<serial<<','<<target->getLastSubmissionSerial()<<" root_revision="<<scene.root->getNodeId()<<" expected_revision="<<revision<<'\n';
    const bool success=check(action.getLastStatus()==CoinRenderAction::SUCCESS&&target->getLastSubmissionSerial()==serial+1&&scene.root->getNodeId()==revision,"identical graph traversal publishes each frame despite unchanged node revision");ok &= success;if(!success)continue;
    ok &= delivery(scene,record->plan,false,frame==0?std::array<double,3>{{0,0,1}}:std::array<double,3>{{0,-1,0}});
    const int expected[]={20,frame==0?110:30,frame==0?70:40};const size_t p=size_t(32*side+32)*4;bool color=pixels.size()==size_t(side*side*4);if(color)for(int c=0;c<3;++c)color &= std::abs(int(pixels[p+c])-expected[c])<=1;
    ok &= check(color,"same graph Image before PHONG cube samples distinct analytic color on frame2 using delivered normal history");
    if(frame==0){first=pixels;initial=record->plan;}else{second=pixels;ok &= check(first!=second&&!initial.hasSamePayload(record->plan),"frame reuse must not hide normal history change with identical graph revision");}
  }
  action.setRenderTarget(nullptr);return ok;
}

SoSwitch*pointHistory(bool array){auto*toggle=new SoSwitch;toggle->whichChild=SO_SWITCH_NONE;auto*branch=new SoSeparator;toggle->addChild(branch);auto*phong=new SoLightModel;phong->model=SoLightModel::PHONG;branch->addChild(phong);
  auto*points=new SoCoordinate3;auto*normals=new SoNormal;auto*binding=new SoNormalBinding;binding->value=array?SoNormalBinding::PER_VERTEX:SoNormalBinding::OVERALL;
  const int count=array?32:1;for(int i=0;i<count;++i){points->point.set1Value(i,SbVec3f(-20,float(i)*.01f,0));normals->vector.set1Value(i,array?SbVec3f(i%2?0.f:1.f,i%2?1.f:0.f,0):SbVec3f(0,0,1));}
  if(array)normals->vector.set1Value(31,SbVec3f(0,-1,0));branch->addChild(points);branch->addChild(normals);branch->addChild(binding);auto*shape=new SoPointSet;shape->numPoints=count;branch->addChild(shape);return toggle;
}
bool indeterminate(){
  const char*arrays=std::getenv("COIN_VERTEX_ARRAYS"),*minimum=std::getenv("COIN_VBO_MIN_LIMIT");
  if(!check((!arrays||std::string(arrays)=="1")&&(!minimum||std::atoi(minimum)<=32),"array-history fixture requires native arrays enabled and threshold at most32"))return false;
  std::unique_ptr<CoinRenderTarget>target(CoinRenderTarget::createOffscreen(SbVec2i32(side,side)));if(!check(bool(target),"indeterminate history CPU target"))return false;
  auto*record=new Capture;target->getPimpl()->backend.reset(record);target->setDepthReadbackEnabled(TRUE);CoinRenderAction action(SbViewportRegion(side,side));action.setRenderTarget(target.get());Scene scene(2,7,true,false,true);
  auto*reset=pointHistory(false),*array=pointHistory(true);scene.root->insertChild(reset,scene.root->findChild(scene.model));scene.root->insertChild(array,scene.root->findChild(scene.model));reset->whichChild=SO_SWITCH_ALL;
  action.apply(scene.root);if(!check(action.getLastStatus()==CoinRenderAction::SUCCESS,"OVERALL point explicitly establishes normalZ before Image"))return false;
  bool ok=delivery(scene,record->plan,false);std::vector<uint8_t>a,b,c;std::vector<float>ad,bd,cd;target->readbackRGBA(a);target->readbackDepth(ad);
  reset->whichChild=SO_SWITCH_NONE;array->whichChild=SO_SWITCH_ALL;scene.quality->textureQuality=0;action.apply(scene.root);target->readbackRGBA(b);target->readbackDepth(bd);
  ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS,"delivered normal-array geometry is legal when Image's effective texture unit is disabled");
  const auto serial=target->getLastSubmissionSerial();const unsigned calls=record->calls;array->whichChild=SO_SWITCH_NONE;scene.quality->textureQuality=.1f;action.apply(scene.root);target->readbackRGBA(c);target->readbackDepth(cd);
  const std::string error=action.getLastError().getString();std::cout<<"normal_array_history status="<<int(action.getLastStatus())<<" error="<<error<<'\n';
  ok &= check(action.getLastStatus()==CoinRenderAction::UNSUPPORTED&&error.find("normal")!=std::string::npos&&target->getLastSubmissionSerial()==serial&&record->calls==calls&&b==c&&bd==cd,
    "delivered enabled normal arrays leave persistent current normal indeterminate; no last-vertex inference or frame reset is allowed");
  reset->whichChild=SO_SWITCH_ALL;action.apply(scene.root);target->readbackRGBA(c);target->readbackDepth(cd);
  const bool restored=check(action.getLastStatus()==CoinRenderAction::SUCCESS&&target->getLastSubmissionSerial()==serial+1&&a==c&&ad==cd,"explicit OVERALL emission recovers normalZ and exact known A after indeterminate array history");ok &= restored;if(restored)ok &= delivery(scene,record->plan,false);
  action.setRenderTarget(nullptr);return ok;
}

SoSeparator*baseFilledFace(){auto*branch=new SoSeparator;auto*base=new SoLightModel;base->model=SoLightModel::BASE_COLOR;branch->addChild(base);auto*style=new SoDrawStyle;style->style=SoDrawStyle::FILLED;branch->addChild(style);auto*coords=new SoCoordinate3;const SbVec3f p[]={SbVec3f(-20,-10,0),SbVec3f(-18,-10,0),SbVec3f(-18,-8,0),SbVec3f(-20,-8,0)};coords->point.setValues(0,4,p);branch->addChild(coords);auto*face=new SoFaceSet;face->numVertices=4;branch->addChild(face);return branch;}
struct GuardCapture {const SoNode*root=nullptr;CoinRenderFramePlan plan;std::string error;bool built=false;bool enabled=false;};
SoCallbackAction::Response inspectGuardRoot(void*data,SoCallbackAction*action,const SoNode*node){
  auto&capture=*static_cast<GuardCapture*>(data);
  if(node!=capture.root||!capture.enabled)return SoCallbackAction::CONTINUE;
  auto &p=static_cast<CoinRenderAction*>(action)->getPimpl().get();
  if(!p.planOnly){capture.built=false;capture.error="finalization probe requires planOnly";return SoCallbackAction::ABORT;}
  // This is a finalization probe, not a snapshot of pre-build placeholders.
  // Move the COMPLETE builder into an isolated instance, then stop traversal.
  // Its single build may resolve execution history; the action's live builder
  // is empty and is never built a second time with the captured payload.
  CoinRenderFramePlanBuilder isolated(std::move(p.builder));
  p.builder=CoinRenderFramePlanBuilder();
  capture.error.clear();capture.plan=CoinRenderFramePlan();
  auto transparency=p.transparencyOptions;transparency.mode=p.executionOptions.transparency;
  capture.built=isolated.build(capture.plan,&capture.error,false,&transparency);
  return SoCallbackAction::ABORT;
}
bool unknownGuards(){
  const char*arrays=std::getenv("COIN_VERTEX_ARRAYS"),*minimum=std::getenv("COIN_VBO_MIN_LIMIT");if(!check((!arrays||std::string(arrays)=="1")&&(!minimum||std::atoi(minimum)<=32),"unknown-guards requires enabled native arrays and threshold at most32"))return false;bool ok=true;
  {std::unique_ptr<CoinRenderTarget>target(CoinRenderTarget::createOffscreen(SbVec2i32(side,side)));if(!target)return false;auto*record=new Capture;target->getPimpl()->backend.reset(record);target->setDepthReadbackEnabled(TRUE);CoinRenderAction action(SbViewportRegion(side,side));action.setRenderTarget(target.get());Scene scene(2,7,true,false,true);auto*reset=pointHistory(false),*array=pointHistory(true),*face=new SoSwitch;face->whichChild=SO_SWITCH_NONE;face->addChild(baseFilledFace());for(SoNode*node:{static_cast<SoNode*>(reset),static_cast<SoNode*>(array),static_cast<SoNode*>(face)})scene.root->insertChild(node,scene.root->findChild(scene.model));reset->whichChild=SO_SWITCH_ALL;action.apply(scene.root);ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS,"unknown-guard known normalZ A");std::vector<uint8_t>a,b,c;std::vector<float>ad,bd,cd;target->readbackRGBA(a);target->readbackDepth(ad);reset->whichChild=SO_SWITCH_NONE;array->whichChild=SO_SWITCH_ALL;scene.quality->textureQuality=0;action.apply(scene.root);ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS,"unknown-guard normal arrays delivered without normal-dependent image sampling");target->readbackRGBA(b);target->readbackDepth(bd);const auto serial=target->getLastSubmissionSerial();const unsigned calls=record->calls;array->whichChild=SO_SWITCH_NONE;face->whichChild=SO_SWITCH_ALL;scene.quality->textureQuality=.1f;action.apply(scene.root);target->readbackRGBA(c);target->readbackDepth(cd);const std::string error=action.getLastError().getString();std::cout<<"normal_unknown_base_filled status="<<int(action.getLastStatus())<<" error="<<error<<'\n';ok &= check(action.getLastStatus()==CoinRenderAction::UNSUPPORTED&&error.find("normal")!=std::string::npos&&error.find("not determined")!=std::string::npos&&record->calls==calls&&target->getLastSubmissionSerial()==serial&&c==b&&cd==bd,"opaque BASE_COLOR untextured FILLED FaceSet preserves preexisting Unknown; never invents normalZ and refuses atomically");face->whichChild=SO_SWITCH_NONE;reset->whichChild=SO_SWITCH_ALL;action.apply(scene.root);target->readbackRGBA(c);target->readbackDepth(cd);ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS&&a==c&&ad==cd,"explicit OVERALL normal recovers known A after Unknown/base-face refusal");action.setRenderTarget(nullptr);}
  for(bool cube:{false,true}){std::unique_ptr<CoinRenderTarget>target(CoinRenderTarget::createOffscreen(SbVec2i32(side,side)));if(!target)return false;auto*record=new Capture;target->getPimpl()->backend.reset(record);CoinRenderAction action(SbViewportRegion(side,side));action.setRenderTarget(target.get());action.getPimpl()->executionOptions=target->getOptions();action.getPimpl()->planOnly=true;Scene scene(2,7,true,false,true);GuardCapture inspected;inspected.root=scene.root;action.addPostCallback(SoSeparator::getClassTypeId(),inspectGuardRoot,&inspected);auto*reset=pointHistory(false);reset->whichChild=SO_SWITCH_ALL;scene.root->insertChild(reset,scene.root->findChild(scene.model));auto*branch=new SoSeparator;auto*base=new SoLightModel;base->model=SoLightModel::BASE_COLOR;branch->addChild(base);auto*light=new SoDirectionalLight;light->direction.setValue(0,0,-1);branch->addChild(light);auto*material=new SoMaterial;material->specularColor.setValue(.7f,.6f,.5f);branch->addChild(material);auto*bump=new SoBumpMap;const uint8_t pixels[]={128,128,255,255,128,128,255,255,128,128,255,255,128,128,255,255};bump->image.setValue(SbVec2s(2,2),4,pixels);branch->addChild(bump);if(cube)branch->addChild(new SoCube);else branch->addChild(baseFilledFace());scene.root->insertChild(branch,scene.root->findChild(scene.model));
    // Separate public control executes the normal planOnly action pipeline.
    // It must refuse Unknown without publishing before the isolated probe.
    action.apply(scene.root);
    ok &= check(action.getLastStatus()==CoinRenderAction::UNSUPPORTED&&record->calls==0&&target->getLastSubmissionSerial()==0,
                "public nonblack bump/Unknown planOnly refuses without execution");
    inspected.enabled=true;action.apply(scene.root);std::cout<<"normal_unknown_bump_specular cube="<<cube<<" status="<<int(action.getLastStatus())<<" action_error="<<action.getLastError().getString()<<" capture_built="<<inspected.built<<" capture_error="<<inspected.error<<'\n';ok &= check(!inspected.built&&inspected.error.find("normal")!=std::string::npos&&inspected.error.find("not determined")!=std::string::npos&&record->calls==0&&target->getLastSubmissionSerial()==0,"active RGBA bump+light+nonblack specular may emit normal arrays through ARB: capture explicitly records Unknown, independently of later executor refusal");
    // Diffuse bump/renderNormal use TEXCOORD/COLOR arrays only. Black
    // specular skips the optional ARB normal-array pass and preserves Z.
    material->specularColor.setValue(0,0,0);action.apply(scene.root);bool normalZ=false;if(inspected.built)for(const auto&draw:inspected.plan.draws)if(draw.sourceNodeId==scene.image->getNodeId()){const auto&state=inspected.plan.renderStates[draw.renderStateSlot];if(state.rasterPixels){const auto&raw=state.rasterTextureCoordinates[7];normalZ=raw[0]==0&&raw[1]==0&&raw[2]==1&&raw[3]==2;}}std::cout<<"normal_unknown_bump_black cube="<<cube<<" status="<<int(action.getLastStatus())<<" capture_built="<<inspected.built<<" capture_error="<<inspected.error<<" normalZ="<<normalZ<<'\n';ok &= check(inspected.built&&normalZ&&record->calls==0&&target->getLastSubmissionSerial()==0,"black specular active bump preserves previous current normalZ, never substitutes ordinary Cube/FaceSet emission; pure capture only");
    inspected.enabled=false;bump->image.setValue(SbVec2s(0,0),4,nullptr);action.apply(scene.root);std::cout<<"normal_unknown_bump_empty cube="<<cube<<" status="<<int(action.getLastStatus())<<" error="<<action.getLastError().getString()<<'\n';const bool empty=check(action.getLastStatus()==CoinRenderAction::SUCCESS&&record->calls==0&&target->getLastSubmissionSerial()==0,"empty bump disables override and BASE geometry preserves explicitly known normalZ in pure capture");ok &= empty;if(empty)ok &= delivery(scene,action.getPimpl()->lastValidPlan,false);action.getPimpl()->planOnly=false;action.setRenderTarget(nullptr);}

  return ok;
}

}

#ifndef COIN_RENDER_IMAGE_NORMAL_TEXGEN_FIXTURE_ONLY
int main(int argc,char**argv){if(argc>2)return 2;const std::string mode=argc==2?argv[1]:"--capture";if(mode!="--capture"&&mode!="--history"&&mode!="--q-history"&&mode!="--atomic"&&mode!="--cube-qzero"&&mode!="--transactional-history"&&mode!="--eligibility"&&mode!="--same-graph-history"&&mode!="--volume"&&mode!="--indeterminate"&&mode!="--unknown-guards")return 2;SoDB::init();CoinRenderAction::initClass();return (mode=="--history"?history():mode=="--q-history"?qHistory():mode=="--atomic"?atomic():mode=="--cube-qzero"?cubeQzero():mode=="--transactional-history"?transactionalHistory():mode=="--eligibility"?eligibility():mode=="--same-graph-history"?sameGraphHistory():mode=="--volume"?volume():mode=="--indeterminate"?indeterminate():mode=="--unknown-guards"?unknownGuards():capture())?0:1;}

#endif
