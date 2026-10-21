#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "CoinRenderSamplingTestOptions.h"
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <Inventor/SoDB.h>
#include <Inventor/SbMatrix.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTextureCoordinate3.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTexture3.h>
#include <Inventor/nodes/SoTextureCubeMap.h>
#include <Inventor/nodes/SoTextureMatrixTransform.h>
#include <Inventor/nodes/SoTextureUnit.h>
#include <Inventor/nodes/SoTransparencyType.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {
constexpr int side=128;
bool check(bool good,const std::string & what) {
  if(!good) std::cerr << "cube_reference FAIL " << what << '\n';
  return good;
}
std::array<uint8_t,4> color(int face,int x,int y) {
  return {{uint8_t(17+face*23+x*37+y*11),
           uint8_t(29+face*13+x*17+y*43),
           uint8_t(41+face*19+x*31+y*7),255}};
}
SbVec3f direction(int face,float s,float t) {
  const float sc=2*s-1,tc=2*t-1;
  switch(face) {
    case 0:return SbVec3f(1,-tc,-sc);
    case 1:return SbVec3f(-1,-tc,sc);
    case 2:return SbVec3f(sc,1,tc);
    case 3:return SbVec3f(sc,-1,-tc);
    case 4:return SbVec3f(sc,-tc,1);
    default:return SbVec3f(-sc,-tc,-1);
  }
}
struct Scene {
  SoSeparator * root=new SoSeparator;
  SoTextureCubeMap * cube=new SoTextureCubeMap;
  SoTextureMatrixTransform * matrix=new SoTextureMatrixTransform;
  SoComplexity * quality=new SoComplexity;
  SoMaterial * material=new SoMaterial;
  SoTransparencyType * transparency=new SoTransparencyType;
  std::array<std::vector<uint8_t>,6> pixels;
  std::array<std::vector<uint8_t>,6> rawPixels;
  std::vector<SoTextureCoordinate3 *> coordinates;
  std::vector<SoCoordinate3 *> vertices;
  std::vector<SoSeparator *> panels;
  bool modelStudy=false;
  bool alphaStudy=false;
  bool switchStudy=false;
  bool overrideStudy=false;
  bool filterStudy=false;
  bool filterLinear=false;
  float mixedFactor=1.f;
  int components=4;
  int firstPanelIndex=0;
  explicit Scene(int textureUnit=0) {
    root->ref();
    auto * camera=new SoOrthographicCamera;
    camera->height=8;camera->position.setValue(0,0,10);
    camera->nearDistance=1;camera->farDistance=50;
    root->addChild(camera);
    auto * light=new SoLightModel;light->model=SoLightModel::BASE_COLOR;
    root->addChild(light);
    material->diffuseColor.setValue(1,1,1);
    root->addChild(material);
    quality->textureQuality=.1f;root->addChild(quality);
    transparency->value=SoTransparencyType::NONE;root->addChild(transparency);
    cube->model=SoTextureCubeMap::REPLACE;
    cube->wrapS=SoTextureCubeMap::REPEAT;
    cube->wrapT=SoTextureCubeMap::REPEAT;
    if(textureUnit) {
      auto * select=new SoTextureUnit;select->unit=textureUnit;
      root->addChild(select);
    }
    root->addChild(cube);
    root->addChild(matrix);
    if(textureUnit) {
      auto * restore=new SoTextureUnit;restore->unit=0;
      root->addChild(restore);
    }
    firstPanelIndex=root->getNumChildren();
    for(int face=0;face<6;++face) {
      pixels[face].resize(16);
      for(int y=0;y<2;++y) for(int x=0;x<2;++x) {
        const auto rgba=color(face,x,y);
        for(int c=0;c<4;++c) pixels[face][size_t((y*2+x)*4+c)]=rgba[c];
      }
    }
    publishFaces();
    for(int cell=0;cell<24;++cell) {
      const int face=cell/4,x=cell%2,y=(cell/2)%2;
      const int col=cell%6,row=cell/6;
      const float cx=-2.5f+col,cy=1.5f-row;
      auto * panel=new SoSeparator;
      auto * vertices=new SoCoordinate3;
      const SbVec3f points[]={SbVec3f(cx-.35f,cy-.35f,0),
        SbVec3f(cx+.35f,cy-.35f,0),SbVec3f(cx+.35f,cy+.35f,0),
        SbVec3f(cx-.35f,cy+.35f,0)};
      vertices->point.setValues(0,4,points);panel->addChild(vertices);
      this->vertices.push_back(vertices);
      auto * coordinates=new SoTextureCoordinate3;
      const SbVec3f ray=direction(face,(x+.5f)/2,(y+.5f)/2);
      const SbVec3f rays[]={ray,ray,ray,ray};
      if(textureUnit) {
        auto * select=new SoTextureUnit;select->unit=textureUnit;
        panel->addChild(select);
      }
      coordinates->point.setValues(0,4,rays);panel->addChild(coordinates);
      this->coordinates.push_back(coordinates);
      if(textureUnit) {
        auto * restore=new SoTextureUnit;restore->unit=0;
        panel->addChild(restore);
      }
      auto * shape=new SoIndexedFaceSet;
      const int32_t indices[]={0,1,2,3,-1};
      shape->coordIndex.setValues(0,5,indices);panel->addChild(shape);
      panels.push_back(panel);
      root->addChild(panel);
    }
  }
  ~Scene() {root->unref();}
  void publishFaces(int size=2) {
    for(int face=0;face<6;++face) {
      rawPixels[face].resize(size_t(size)*size*components);
      for(size_t pixel=0;pixel<size_t(size)*size;++pixel) {
        const size_t source=pixel*4,destination=pixel*components;
        rawPixels[face][destination]=pixels[face][source];
        if(components==2) rawPixels[face][destination+1]=pixels[face][source+3];
        if(components>=3) {
          rawPixels[face][destination+1]=pixels[face][source+1];
          rawPixels[face][destination+2]=pixels[face][source+2];
        }
        if(components==4) rawPixels[face][destination+3]=pixels[face][source+3];
      }
    }
    cube->imagePosX.setValue(SbVec2s(size,size),components,rawPixels[0].data());
    cube->imageNegX.setValue(SbVec2s(size,size),components,rawPixels[1].data());
    cube->imagePosY.setValue(SbVec2s(size,size),components,rawPixels[2].data());
    cube->imageNegY.setValue(SbVec2s(size,size),components,rawPixels[3].data());
    cube->imagePosZ.setValue(SbVec2s(size,size),components,rawPixels[4].data());
    cube->imageNegZ.setValue(SbVec2s(size,size),components,rawPixels[5].data());
  }
};
size_t at(int x,int y,int channels) {
  return size_t((side-1-y)*side+x)*channels;
}
bool centers(const std::vector<uint8_t> & pixels,const Scene & scene,
             const std::string & label,bool textured,bool native) {
  if(!check(pixels.size()==size_t(side*side*(native?3:4)),label+" buffer size")) return false;
  bool ok=true;
  int maxError=0;
  for(int cell=0;cell<24;++cell) {
    const int face=cell/4,x=cell%2,y=(cell/2)%2;
    const int col=cell%6,row=cell/6;
    const int px=24+col*16,py=88-row*16;
    std::array<uint8_t,4> expected={{255,255,255,255}};
    if(textured) for(int c=0;c<4;++c)
      expected[c]=scene.pixels[face][size_t((y*2+x)*4+c)];
    if(textured && scene.components<=2)
      expected[1]=expected[2]=expected[0];
    if(scene.modelStudy && textured) {
      const float base[]={.4f,.6f,.8f},blend[]={.2f,.3f,.1f};
      const int model=scene.cube->model.getValue();
      for(int c=0;c<3;++c) {
        const float tex=float(expected[c])/255.f;
        float combined=tex;
        if(model==SoTextureCubeMap::MODULATE) combined=base[c]*tex;
        else if(model==SoTextureCubeMap::BLEND)
          combined=base[c]*(1.f-tex)+blend[c]*tex;
        expected[c]=uint8_t(std::lround(255.f*combined));
      }
    }
    if(scene.alphaStudy && textured) for(int c=0;c<3;++c)
      expected[c]=uint8_t(std::lround(float(expected[c])*float(expected[3])/255.f));
    if(scene.mixedFactor!=1.f) for(int c=0;c<3;++c)
      expected[c]=uint8_t(std::lround(float(expected[c])*scene.mixedFactor));
    if(scene.switchStudy && cell==0) expected={{93,171,29,255}};
    if(scene.switchStudy && cell==1) expected={{177,41,119,255}};
    if(scene.overrideStudy) expected={{29,191,83,255}};
    if(scene.filterStudy && cell==0 && scene.filterLinear) {
      const float weights[]={.5625f,.1875f,.1875f,.0625f};
      for(int c=0;c<3;++c) {
        float channel=0;
        for(int texel=0;texel<4;++texel)
          channel+=weights[texel]*scene.pixels[0][size_t(texel)*4+c];
        expected[c]=uint8_t(std::lround(channel));
      }
    }
    const size_t offset=native?size_t(py*side+px)*3:at(px,py,4);
    for(int c=0;c<3;++c) {
      const int error=std::abs(int(pixels[offset+c])-int(expected[c]));
      maxError=std::max(maxError,error);
      if(error>2) {
        std::cerr << "cube_reference " << label << " cell=" << cell << " face=" << face
                  << " texel=" << x << ',' << y << " xy=" << px << ',' << py
                  << " channel=" << c << " actual=" << int(pixels[offset+c])
                  << " expected=" << int(expected[c]) << '\n';
        ok=false;
      }
    }
  }
  std::cout << "cube_reference " << label << " max_rgb_error=" << maxError << '\n';
  return ok;
}
bool run(CoinRenderTarget * target,const char * label,bool glReference,int textureUnit) {
  if(!check(target!=nullptr,std::string(label)+" target created")) return false;
  if(!check(target->setDepthReadbackEnabled(TRUE),
            std::string(label)+" depth readback enabled")) return false;
  Scene scene(textureUnit);
  CoinRenderAction action(SbViewportRegion(side,side));action.setRenderTarget(target);
  SoOffscreenRenderer gl(SbViewportRegion(side,side));
  gl.setComponents(SoOffscreenRenderer::RGB);
  bool ok=true;
  auto capture=[&](const std::string & phase,bool textured) {
    const uint64_t serial=target->getLastSubmissionSerial();
    action.apply(scene.root);
    std::vector<uint8_t> rgba;
    target->readbackRGBA(rgba);
    const bool published=action.getLastStatus()==CoinRenderAction::SUCCESS &&
      target->getLastSubmissionSerial()>serial;
    std::cout << "cube_reference " << label << ' ' << phase
              << " status=" << action.getLastStatus() << " serial="
              << target->getLastSubmissionSerial() << " error="
              << action.getLastError().getString() << '\n';
    bool passed=check(published,std::string(label)+" "+phase+" published");
    if(published) passed &= centers(rgba,scene,std::string(label)+"/"+phase,textured,false);
    if(glReference) {
      const bool rendered=gl.render(scene.root) && gl.getBuffer();
      passed &= check(rendered,phase+" native CoinGL rendered");
      if(rendered) {
        std::vector<uint8_t> rgb(gl.getBuffer(),gl.getBuffer()+side*side*3);
        passed &= centers(rgb,scene,phase+"/CoinGL",textured,true);
      }
    }
    return passed;
  };
  ok &= capture("six_faces",true);
  scene.pixels[5][0]=222;scene.pixels[5][1]=7;scene.pixels[5][2]=191;
  scene.publishFaces();
  ok &= capture("mutated_face5",true);
  scene.quality->textureQuality=0;
  ok &= capture("quality_zero",false);
  scene.quality->textureQuality=.1f;
  ok &= capture("quality_restored",true);
  for(float q:{0.f,-1.f}) {
    SbMatrix transformed=SbMatrix::identity();
    transformed[3][3]=q;
    scene.matrix->matrix.setValue(transformed);
    ok &= capture(q==0.f?"cube_q_zero":"cube_q_negative",true);
  }
  scene.matrix->matrix.setValue(SbMatrix::identity());
  ok &= capture("cube_q_restored",true);
  scene.modelStudy=true;
  scene.material->diffuseColor.setValue(.4f,.6f,.8f);
  scene.cube->blendColor.setValue(.2f,.3f,.1f);
  for(const auto model:{SoTextureCubeMap::MODULATE,SoTextureCubeMap::REPLACE,
                        SoTextureCubeMap::DECAL,SoTextureCubeMap::BLEND}) {
    scene.cube->model=model;
    ok &= capture("model_"+std::to_string(int(model)),true);
  }
  scene.cube->model=SoTextureCubeMap::REPLACE;
  scene.material->diffuseColor.setValue(1,1,1);
  scene.modelStudy=false;
  ok &= capture("model_restored",true);
  for(int components=1;components<=4;++components) {
    scene.components=components;
    scene.publishFaces();
    ok &= capture("components_"+std::to_string(components),true);
  }
  for(int face=0;face<6;++face) for(int texel=0;texel<4;++texel)
    scene.pixels[face][size_t(texel)*4+3]=texel==0?0:texel==1?128:255;
  scene.publishFaces();
  scene.transparency->value=SoTransparencyType::BLEND;
  scene.alphaStudy=true;
  ok &= capture("rgba_alpha_0_128_255",true);
  scene.alphaStudy=false;
  scene.transparency->value=SoTransparencyType::NONE;
  for(int face=0;face<6;++face) for(int texel=0;texel<4;++texel)
    scene.pixels[face][size_t(texel)*4+3]=255;
  scene.publishFaces();
  ok &= capture("rgba_alpha_restored",true);
  auto * inner2=new SoTexture2;
  const uint8_t inner2Color[]={93,171,29};
  inner2->image.setValue(SbVec2s(1,1),3,inner2Color);
  inner2->model=SoTexture2::REPLACE;
  scene.panels[0]->insertChild(inner2,textureUnit ? 2 : 1);
  auto * inner3=new SoTexture3;
  const uint8_t inner3Color[]={177,41,119};
  inner3->images.setValue(SbVec3s(1,1,1),3,inner3Color);
  inner3->model=SoTexture3::MODULATE;
  scene.panels[1]->insertChild(inner3,textureUnit ? 2 : 1);
  scene.switchStudy=true;
  ok &= capture("separator_cube_2d_3d_cube",true);
  scene.switchStudy=false;
  scene.panels[1]->removeChild(inner3);
  scene.panels[0]->removeChild(inner2);
  ok &= capture("separator_restored_cube",true);
  if(textureUnit==0) {
    auto * predecessor=new SoTexture2;
    const uint8_t constant[]={29,191,83};
    predecessor->image.setValue(SbVec2s(1,1),3,constant);
    predecessor->model=SoTexture2::REPLACE;
    predecessor->setOverride(TRUE);
    scene.root->insertChild(predecessor,scene.root->findChild(scene.cube));
    scene.overrideStudy=true;
    ok &= capture("texture2_override_blocks_cube",true);
    scene.overrideStudy=false;
    scene.root->removeChild(predecessor);
    ok &= capture("texture2_override_removed",true);
  }
  auto rejected=[&](const std::string & phase) {
    std::vector<uint8_t> stableRgba,afterRgba;
    std::vector<float> stableDepth,afterDepth;
    target->readbackRGBA(stableRgba);target->readbackDepth(stableDepth);
    const uint64_t stableSerial=target->getLastSubmissionSerial();
    action.apply(scene.root);
    target->readbackRGBA(afterRgba);target->readbackDepth(afterDepth);
    const bool atomic=action.getLastStatus()!=CoinRenderAction::SUCCESS &&
      target->getLastSubmissionSerial()==stableSerial &&
      afterRgba==stableRgba && afterDepth==stableDepth;
    std::cout << "cube_reference " << label << ' ' << phase
              << " rejected=" << (action.getLastStatus()!=CoinRenderAction::SUCCESS)
              << " atomic=" << atomic << " status=" << action.getLastStatus()
              << " error=" << action.getLastError().getString() << '\n';
    return check(atomic,std::string(label)+" "+phase+" rejects atomically");
  };
  scene.cube->imageNegY.setValue(SbVec2s(1,1),4,scene.pixels[3].data());
  ok &= rejected("mismatched_face");
  scene.publishFaces();
  ok &= capture("recovered_face",true);
  scene.cube->imageNegY.setValue(SbVec2s(0,0),0,nullptr);
  ok &= rejected("missing_face");
  scene.publishFaces();
  ok &= capture("recovered_missing_face",true);
  scene.cube->imagePosX.setValue(SbVec2s(0,0),0,nullptr);
  scene.cube->imageNegX.setValue(SbVec2s(0,0),0,nullptr);
  scene.cube->imagePosY.setValue(SbVec2s(0,0),0,nullptr);
  scene.cube->imageNegY.setValue(SbVec2s(0,0),0,nullptr);
  scene.cube->imagePosZ.setValue(SbVec2s(0,0),0,nullptr);
  scene.cube->imageNegZ.setValue(SbVec2s(0,0),0,nullptr);
  scene.cube->filenames.set1Value(0,"cube-reference-pending-absent.png");
  ok &= rejected("pending_filename_without_faces");
  scene.cube->filenames.setNum(0);
  scene.publishFaces();
  ok &= capture("recovered_pending_filename",true);
  std::array<uint8_t,36> npot{};
  scene.cube->imageNegY.setValue(SbVec2s(3,3),4,npot.data());
  ok &= rejected("non_pot_face");
  scene.publishFaces();
  ok &= capture("recovered_non_pot_face",true);
  const float nan=std::numeric_limits<float>::quiet_NaN();
  for(const auto & bad: {SbVec3f(0,0,0),SbVec3f(nan,0,1)}) {
    const SbVec3f rays[]={bad,bad,bad,bad};
    scene.coordinates[0]->point.setValues(0,4,rays);
    ok &= rejected(std::isnan(bad[0])?"nan_direction":"zero_direction");
    const SbVec3f original=direction(0,.25f,.25f);
    const SbVec3f restore[]={original,original,original,original};
    scene.coordinates[0]->point.setValues(0,4,restore);
    ok &= capture("recovered_direction",true);
  }
  action.setRenderTarget(nullptr);
  return ok;
}
bool maximum(CoinRenderTarget * target,const char * label) {
  if(!check(target!=nullptr,std::string(label)+" maximum target")) return false;
  Scene scene;
  constexpr int n=2048;
  for(int face=0;face<6;++face) {
    scene.pixels[face].resize(size_t(n)*n*4);
    const auto rgba=color(face,0,0);
    for(size_t pixel=0;pixel<size_t(n)*n;++pixel)
      for(int channel=0;channel<4;++channel)
        scene.pixels[face][pixel*4+channel]=rgba[channel];
  }
  const int locations[2][2]={{17,1201},{2047,2047}};
  const uint8_t sentinels[2][4]={{231,17,173,255},{9,242,57,255}};
  for(int sample=0;sample<2;++sample) {
    const int x=locations[sample][0],y=locations[sample][1];
    const size_t offset=(size_t(y)*n+x)*4;
    for(int c=0;c<4;++c) scene.pixels[5][offset+c]=sentinels[sample][c];
    const SbVec3f ray=direction(5,(x+.5f)/n,(y+.5f)/n);
    const SbVec3f rays[]={ray,ray,ray,ray};
    scene.coordinates[20+sample]->point.setValues(0,4,rays);
  }
  scene.publishFaces(n);
  CoinRenderAction action(SbViewportRegion(side,side));action.setRenderTarget(target);
  bool ok=true;
  for(const float quality:{.1f,.8f}) {
    scene.quality->textureQuality=quality;
    const std::string phase=quality<.5f?"base":"full_mip_chain";
    const uint64_t serial=target->getLastSubmissionSerial();
    action.apply(scene.root);
    std::vector<uint8_t> rgba;
    target->readbackRGBA(rgba);
    const bool published=action.getLastStatus()==CoinRenderAction::SUCCESS &&
      target->getLastSubmissionSerial()>serial && rgba.size()==size_t(side*side*4);
    ok &= check(published,std::string(label)+" maximum "+phase+" legal cube published");
    if(!published) {
      std::cerr << "cube_reference " << label << " maximum " << phase
                << " status=" << action.getLastStatus()
                << " error=" << action.getLastError().getString()
                << " serial_before=" << serial
                << " serial_after=" << target->getLastSubmissionSerial()
                << " rgba_bytes=" << rgba.size() << '\n';
      continue;
    }
    for(int sample=0;sample<2;++sample) {
      const int cell=20+sample,col=cell%6,row=cell/6;
      const int px=24+col*16,py=88-row*16;
      const size_t offset=at(px,py,4);
      const int texelX=locations[sample][0],texelY=locations[sample][1];
      const SbVec3f ray=direction(5,(texelX+.5f)/n,(texelY+.5f)/n);
      std::cerr << "cube_reference " << label << " maximum " << phase
                << " sample=" << sample
                << " framebuffer_xy=" << px << ',' << py
                << " face5_texel_xy=" << texelX << ',' << texelY
                << " direction=" << ray[0] << ',' << ray[1] << ',' << ray[2]
                << " actual_rgb=" << int(rgba[offset]) << ',' << int(rgba[offset+1])
                << ',' << int(rgba[offset+2])
                << " expected_rgb=" << int(sentinels[sample][0]) << ','
                << int(sentinels[sample][1]) << ',' << int(sentinels[sample][2]) << '\n';
      for(int c=0;c<3;++c)
        ok &= check(std::abs(int(rgba[offset+c])-int(sentinels[sample][c]))<=2,
          std::string(label)+" maximum "+phase+" face5 odd texel "+std::to_string(sample));
    }
  }
  action.setRenderTarget(nullptr);
  return ok;
}
bool mixed(CoinRenderTarget * target,const char * label,bool glReference) {
  if(!check(target!=nullptr,std::string(label)+" mixed target")) return false;
  Scene scene(7);
  scene.cube->model=SoTextureCubeMap::MODULATE;
  CoinRenderAction action(SbViewportRegion(side,side));action.setRenderTarget(target);
  SoOffscreenRenderer gl(SbViewportRegion(side,side));
  gl.setComponents(SoOffscreenRenderer::RGB);
  auto capture=[&](const char * phase) {
    const uint64_t serial=target->getLastSubmissionSerial();
    action.apply(scene.root);
    std::vector<uint8_t> rgba;
    target->readbackRGBA(rgba);
    bool passed=check(action.getLastStatus()==CoinRenderAction::SUCCESS &&
      target->getLastSubmissionSerial()>serial,std::string(label)+" "+phase+" published");
    if(passed) passed &= centers(rgba,scene,std::string(label)+"/"+phase,true,false);
    if(glReference) {
      const bool rendered=gl.render(scene.root) && gl.getBuffer();
      passed &= check(rendered,std::string(label)+" "+phase+" native rendered");
      if(rendered) {
        std::vector<uint8_t> rgb(gl.getBuffer(),gl.getBuffer()+side*side*3);
        passed &= centers(rgb,scene,std::string(label)+"/"+phase+"/CoinGL",true,true);
      }
    }
    return passed;
  };
  auto * tex2=new SoTexture2;
  const uint8_t gray2[]={128,128,128,255};
  tex2->image.setValue(SbVec2s(1,1),4,gray2);
  tex2->model=SoTexture2::MODULATE;
  scene.root->insertChild(tex2,scene.firstPanelIndex);
  scene.mixedFactor=128.f/255.f;
  bool ok=capture("cube7_with_2d0");
  scene.root->removeChild(tex2);
  auto * tex3=new SoTexture3;
  const uint8_t gray3[]={64,64,64,255};
  tex3->images.setValue(SbVec3s(1,1,1),4,gray3);
  tex3->model=SoTexture3::MODULATE;
  scene.root->insertChild(tex3,scene.firstPanelIndex);
  scene.mixedFactor=64.f/255.f;
  ok &= capture("cube7_with_3d0");
  scene.root->removeChild(tex3);
  scene.mixedFactor=1.f;
  ok &= capture("cube7_lower_unit_removed");
  action.setRenderTarget(nullptr);
  return ok;
}
bool filters(CoinRenderTarget * target,const char * label,bool glReference) {
  if(!check(target!=nullptr,std::string(label)+" filter target")) return false;
  Scene scene;
  const SbVec3f ray=direction(0,.375f,.375f);
  const SbVec3f rays[]={ray,ray,ray,ray};
  scene.coordinates[0]->point.setValues(0,4,rays);
  scene.filterStudy=true;
  CoinRenderAction action(SbViewportRegion(side,side));action.setRenderTarget(target);
  SoOffscreenRenderer gl(SbViewportRegion(side,side));
  gl.setComponents(SoOffscreenRenderer::RGB);
  bool ok=true;
  for(const float quality:{.1f,.3f,.6f,.8f}) {
    scene.quality->textureQuality=quality;
    scene.filterLinear=quality>=.3f;
    const uint64_t serial=target->getLastSubmissionSerial();
    action.apply(scene.root);
    std::vector<uint8_t> rgba;
    target->readbackRGBA(rgba);
    const std::string phase="quality_"+std::to_string(quality);
    const bool published=action.getLastStatus()==CoinRenderAction::SUCCESS &&
      target->getLastSubmissionSerial()>serial;
    if(!published) std::cerr << "cube_reference " << label << ' ' << phase
      << " status=" << action.getLastStatus()
      << " error=" << action.getLastError().getString()
      << " serial_before=" << serial
      << " serial_after=" << target->getLastSubmissionSerial()
      << " rgba_bytes=" << rgba.size() << '\n';
    ok &= check(published,std::string(label)+" "+phase+" published");
    if(published) ok &= centers(rgba,scene,std::string(label)+"/"+phase,true,false);
    if(glReference) {
      const bool rendered=gl.render(scene.root) && gl.getBuffer();
      ok &= check(rendered,phase+" CoinGL rendered");
      if(rendered) {
        std::vector<uint8_t> rgb(gl.getBuffer(),gl.getBuffer()+side*side*3);
        ok &= centers(rgb,scene,phase+"/CoinGL",true,true);
      }
    }
  }
  action.setRenderTarget(nullptr);
  return ok;
}
bool minification(CoinRenderTarget * target,const char * label) {
  if(!check(target!=nullptr,std::string(label)+" minification target")) return false;
  Scene scene;
  constexpr int size=64;
  for(int face=0;face<6;++face) {
    scene.pixels[face].resize(size_t(size)*size*4);
    for(int y=0;y<size;++y) for(int x=0;x<size;++x) {
      const size_t at=(size_t(y)*size+x)*4;
      scene.pixels[face][at]=face==0 && ((x/2)&1)?255:0;
      scene.pixels[face][at+1]=0;
      scene.pixels[face][at+2]=0;
      scene.pixels[face][at+3]=255;
    }
  }
  scene.publishFaces(size);
  const SbVec3f corners[]={direction(0,.25f,.25f),direction(0,.75f,.25f),
    direction(0,.75f,.25f),direction(0,.25f,.25f)};
  scene.coordinates[0]->point.setValues(0,4,corners);
  CoinRenderAction action(SbViewportRegion(side,side));action.setRenderTarget(target);
  const float lod=std::log2(64.f*.5f/(.7f*16.f));
  const float mix=lod-std::floor(lod);
  const float s=.25f+.5f*((24.5f-18.4f)/11.2f);
  const float mip1Position=s*32.f-.5f;
  const float mip1Linear=255.f*(mip1Position-std::floor(mip1Position));
  auto capture=[&](const std::string & phase,float expected) {
    const uint64_t serial=target->getLastSubmissionSerial();
    action.apply(scene.root);
    std::vector<uint8_t> rgba;
    target->readbackRGBA(rgba);
    const bool published=action.getLastStatus()==CoinRenderAction::SUCCESS &&
      target->getLastSubmissionSerial()>serial && rgba.size()==size_t(side*side*4);
    bool passed=check(published,std::string(label)+" "+phase+" published");
    if(published) {
      const size_t pixel=at(24,88,4);
      const int error=std::abs(int(rgba[pixel])-int(std::lround(expected)));
      std::cout << "cube_minification " << label << ' ' << phase
                << " lod=" << lod << " rgb=" << int(rgba[pixel]) << ','
                << int(rgba[pixel+1]) << ',' << int(rgba[pixel+2])
                << " expected_red=" << expected << " error=" << error << '\n';
      passed &= check(error<=2 && rgba[pixel+1]<=2 && rgba[pixel+2]<=2,
        std::string(label)+" "+phase+" numeric mip/LOD sample");
    }
    return passed;
  };
  bool ok=true;
  scene.quality->textureQuality=.1f;
  ok &= capture("nearest_base",0);
  scene.quality->textureQuality=.3f;
  ok &= capture("bilinear_base",0);
  scene.quality->textureQuality=.6f;
  ok &= capture("nearest_mipmap_linear",128.f*mix);
  scene.quality->textureQuality=.8f;
  ok &= capture("linear_mipmap_linear",mip1Linear*(1.f-mix)+128.f*mix);
  // Keep the same pixel center while increasing the direction derivative far
  // beyond 64 texels/pixel. Only the 1x1 mip can be selected at this pixel.
  const float cx=-2.5f+.03125f,cy=1.5f;
  const SbVec3f points[]={SbVec3f(cx-.35f,cy-.35f,0),
    SbVec3f(cx+.35f,cy-.35f,0),SbVec3f(cx+.35f,cy+.35f,0),
    SbVec3f(cx-.35f,cy+.35f,0)};
  scene.vertices[0]->point.setValues(0,4,points);
  const SbVec3f finalRays[]={SbVec3f(.01f,0,.5f),SbVec3f(.01f,0,-.5f),
    SbVec3f(.01f,0,-.5f),SbVec3f(.01f,0,.5f)};
  scene.coordinates[0]->point.setValues(0,4,finalRays);
  ok &= capture("final_mip_1x1",128.f);
  action.setRenderTarget(nullptr);
  return ok;
}
}
int main(int argc,char ** argv) {
  SoDB::init();CoinRenderAction::initClass();
  configureSamplingTest(argc,argv);
  bool captureOnly=false,gpuOnly=false,glReference=false,runMaximum=false,
    unit7=false,runMixed=false,runFilters=false,runMinification=false;
  for(int i=1;i<argc;++i) {
    const std::string arg=argv[i];
    captureOnly |= arg=="--capture";
    gpuOnly |= arg=="--gpu";
    glReference |= arg=="--gl-reference";
    runMaximum |= arg=="--maximum";
    unit7 |= arg=="--unit7";
    runMixed |= arg=="--mixed";
    runFilters |= arg=="--filters";
    runMinification |= arg=="--minification";
  }
  bool ok=true;
  if(!gpuOnly) {
    std::unique_ptr<CoinRenderTarget> cpu(samplingTestOffscreen(SbVec2i32(side,side)));
    if(!cpu) return 1;
    cpu->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);
    ok &= runMaximum?maximum(cpu.get(),"CPU"):
      runMinification?minification(cpu.get(),"CPU"):
      runFilters?filters(cpu.get(),"CPU",glReference):
      runMixed?mixed(cpu.get(),"CPU",glReference):
      run(cpu.get(),"CPU",glReference,unit7?7:0);
  }
  if(!captureOnly) {
    CoinRenderCapabilities caps{};
    const bool available=coin_render_query_capabilities(
      COIN_RENDER_EXPERIMENTAL_OFFSCREEN,&caps,sizeof(caps))==0 && caps.gpu_available;
    std::cout << "cube_reference backend=" << caps.backend << " renderer="
              << caps.renderer << " vendor=" << caps.vendor_id
              << " adapter=" << caps.adapter_name << " gpu=" << available << '\n';
    if(available) {
      std::unique_ptr<CoinRenderTarget> gpu(samplingTestOffscreen(SbVec2i32(side,side)));
      ok &= runMaximum?maximum(gpu.get(),"GPU"):
        runMinification?minification(gpu.get(),"GPU"):
        runFilters?filters(gpu.get(),"GPU",glReference):
        runMixed?mixed(gpu.get(),"GPU",glReference):
        run(gpu.get(),"GPU",glReference,unit7?7:0);
    } else if(gpuOnly || std::getenv("COIN_RENDER_REQUIRE_CUBE_GPU"))
      ok &= check(false,"GPU required for cube execution");
  }
  return ok?0:1;
}
