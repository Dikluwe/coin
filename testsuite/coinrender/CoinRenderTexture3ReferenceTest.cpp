#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "CoinRenderSamplingTestOptions.h"
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <Inventor/SoDB.h>
#include <Inventor/SbMatrix.h>
#include <Inventor/SbName.h>
#include <Inventor/SoType.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/system/gl.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoCallback.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTexture3.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoTextureCoordinate3.h>
#include <Inventor/nodes/SoTextureMatrixTransform.h>
#include <Inventor/nodes/SoTextureUnit.h>
#include <Inventor/nodes/SoTransparencyType.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <memory>
#include <vector>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <array>
#include <string>
#include <utility>

namespace {
constexpr int side=128;
bool check(bool condition,const char * label) {
  if (!condition) std::cerr << "texture3: " << label << '\n';
  return condition;
}
struct Fixture {
  SoSeparator * root=new SoSeparator;
  SoTexture3 * texture=new SoTexture3;
  SoComplexity * complexity=new SoComplexity;
  std::vector<unsigned char> texels;
  int width=4,height=4,depth=4,components=4;
  Fixture() {
    root->ref();
    auto * camera=new SoOrthographicCamera;
    camera->height=8; camera->position.setValue(0,0,10);
    camera->nearDistance=1; camera->farDistance=50;
    root->addChild(camera);
    auto * lighting=new SoLightModel; lighting->model=SoLightModel::BASE_COLOR;
    root->addChild(lighting);
    auto * material=new SoMaterial; material->diffuseColor.setValue(1,1,1);
    root->addChild(material);
    complexity->textureQuality=.8f; root->addChild(complexity);
    texture->model=SoTexture3::MODULATE;
    texture->wrapS=SoTexture3::CLAMP;
    texture->wrapT=SoTexture3::CLAMP;
    texture->wrapR=SoTexture3::CLAMP;
    root->addChild(texture);
  }
  ~Fixture() { root->unref(); }
  void volume(int sx,int sy,int sz,int channels=4) {
    width=sx;height=sy;depth=sz;components=channels;
    texels.resize(size_t(sx*sy*sz*4));
    std::vector<unsigned char> source(size_t(sx*sy*sz*channels));
    for (int z=0;z<sz;++z) for(int y=0;y<sy;++y) for(int x=0;x<sx;++x) {
      const size_t voxel=size_t(z*sx*sy+y*sx+x),at=voxel*4,raw=voxel*channels;
      const unsigned char red=static_cast<unsigned char>(25+((x*53+y*17+z*31)%205));
      const unsigned char green=static_cast<unsigned char>(20+((x*13+y*61+z*37)%210));
      const unsigned char blue=static_cast<unsigned char>(30+((x*29+y*19+z*59)%200));
      if (channels<=2) {
        source[raw]=red;
        if (channels==2) source[raw+1]=255;
        texels[at]=texels[at+1]=texels[at+2]=red;
      } else {
        source[raw]=red;source[raw+1]=green;source[raw+2]=blue;
        if (channels==4) source[raw+3]=255;
        texels[at]=red;texels[at+1]=green;texels[at+2]=blue;
      }
      texels[at+3]=255;
    }
    texture->images.setValue(SbVec3s(sx,sy,sz),channels,source.data());
    const int voxels=sx*sy*sz;
    const int cells=voxels+(sx==2 && sy==2 && sz==2 ? 1 : 0);
    while (root->getNumChildren()>5) root->removeChild(5);
    for (int cell=0;cell<cells;++cell) {
      const int vx=cell%sx,vy=(cell/sx)%sy,vz=cell/(sx*sy);
      const int col=cell%8,row=cell/8;
      const float cx=-3.5f+col,cy=-3.5f+row;
      auto * panel=new SoSeparator;
      auto * shape=new SoCoordinate3;
      const SbVec3f corners[]={SbVec3f(cx-.34f,cy-.34f,0),SbVec3f(cx+.34f,cy-.34f,0),
                               SbVec3f(cx+.34f,cy+.34f,0),SbVec3f(cx-.34f,cy+.34f,0)};
      shape->point.setValues(0,4,corners); panel->addChild(shape);
      auto * uvw=new SoTextureCoordinate3;
      const SbVec3f sample=cell==voxels ? SbVec3f(.5f,.5f,.5f) :
        SbVec3f((vx+.5f)/sx,(vy+.5f)/sy,(vz+.5f)/sz);
      const SbVec3f samples[]={sample,sample,sample,sample};
      uvw->point.setValues(0,4,samples); panel->addChild(uvw);
      auto * face=new SoIndexedFaceSet;
      const int32_t indices[]={0,1,2,3,-1};
      face->coordIndex.setValues(0,5,indices); panel->addChild(face);
      root->addChild(panel);
    }
  }
  void volume(int n) { volume(n,n,n,4); }
};
bool compareCenters(const std::vector<unsigned char> & rgba,
                    const std::vector<unsigned char> & gl,const Fixture & scene,
                    const char * label) {
  if (rgba.size()!=size_t(side*side*4) || gl.size()!=size_t(side*side*3)) return check(false,"readback size");
  int maximum=0; size_t differing=0,gpuBad=0,glBad=0,pairBad=0;
  bool firstPrinted=false;
  const int voxels=scene.width*scene.height*scene.depth;
  for (int cell=0;cell<voxels+(scene.width==2 && scene.height==2 && scene.depth==2 ? 1 : 0);++cell) {
    const int x=8+16*(cell%8),y=8+16*(cell/8);
    const size_t gpuAt=size_t((side-1-y)*side+x)*4,glAt=size_t(y*side+x)*3;
    for(int c=0;c<3;++c) {
      int expected=0;
      if (cell==voxels) {
        for (int corner=0;corner<8;++corner) expected+=scene.texels[size_t(corner)*4+c];
        expected=(expected+4)/8;
      } else expected=scene.texels[size_t(cell)*4+c];
      const int delta=std::abs(int(rgba[gpuAt+c])-expected);
      const int nativeDelta=std::abs(int(gl[glAt+c])-expected);
      const int pairDelta=std::abs(int(rgba[gpuAt+c])-int(gl[glAt+c]));
      maximum=std::max(maximum,std::max(delta,std::max(nativeDelta,pairDelta)));
      gpuBad+=delta>5; glBad+=nativeDelta>5; pairBad+=pairDelta>5;
      differing+=delta>5 || nativeDelta>5 || pairDelta>5;
      if (!firstPrinted && (delta>5 || nativeDelta>5 || pairDelta>5)) {
        std::cerr << "texture3_first_mismatch " << label << " cell=" << cell
                  << " channel=" << c << " xy=" << x << ',' << y
                  << " GPU=" << int(rgba[gpuAt+c]) << " GL=" << int(gl[glAt+c])
                  << " expected=" << expected << " gpu_at=" << gpuAt
                  << " gl_at=" << glAt << '\n';
        firstPrinted=true;
      }
    }
  }
  std::cout << "texture3 " << label << " extent=" << scene.width << 'x'
            << scene.height << 'x' << scene.depth << " components=" << scene.components
            << " max=" << maximum << " bad=" << differing
            << " gpu_bad=" << gpuBad << " gl_bad=" << glBad
            << " pair_bad=" << pairBad << '\n';
  return check(differing==0,"all volume texel centers must match analytic and CoinGL references");
}
bool run(CoinRenderTarget * target,const char * label,bool skipMaximumVolume) {
  Fixture scene;
  if (!target || !target->setDepthReadbackEnabled(TRUE)) return check(false,"target/depth setup");
  CoinRenderAction action(SbViewportRegion(side,side)); action.setRenderTarget(target);
  SoOffscreenRenderer gl(SbViewportRegion(side,side)); gl.setComponents(SoOffscreenRenderer::RGB);
  bool ok=true;
  auto traceCenter=[&](const char * gate,int x,int y,const std::vector<unsigned char> & pixels) {
    const size_t gpuAt=size_t((side-1-y)*side+x)*4,glAt=size_t(y*side+x)*3;
    std::cerr << "texture3_gate " << label << ' ' << gate << " xy=" << x << ',' << y
              << " status=" << action.getLastStatus() << " error="
              << action.getLastError().getString() << " rgba_size=" << pixels.size();
    if (gpuAt+2<pixels.size()) std::cerr << " GPU=" << int(pixels[gpuAt]) << ','
      << int(pixels[gpuAt+1]) << ',' << int(pixels[gpuAt+2]);
    if (gl.getBuffer()) std::cerr << " GL=" << int(gl.getBuffer()[glAt]) << ','
      << int(gl.getBuffer()[glAt+1]) << ',' << int(gl.getBuffer()[glAt+2]);
    std::cerr << '\n';
  };
  for (const auto spec : {std::array<int,4>{2,2,2,4},
                          std::array<int,4>{4,4,4,4},
                          std::array<int,4>{2,4,8,4},
                          std::array<int,4>{2,2,2,1},
                          std::array<int,4>{2,2,2,2},
                          std::array<int,4>{2,2,2,3}}) {
    const int n=spec[0];
    scene.volume(spec[0],spec[1],spec[2],spec[3]);
    const uint64_t before=target->getLastSubmissionSerial();
    action.apply(scene.root);
    std::vector<unsigned char> rgba,reference;
    std::vector<float> depth;
    target->readbackRGBA(rgba); target->readbackDepth(depth);
    const bool native=gl.render(scene.root) && gl.getBuffer();
    if (native) reference.assign(gl.getBuffer(),gl.getBuffer()+side*side*3);
    std::cout << "texture3_capture " << label << " dimensions=" << spec[0] << 'x'
              << spec[1] << 'x' << spec[2] << " components=" << spec[3]
              << " status=" << action.getLastStatus() << " error="
              << action.getLastError().getString() << " rgba=" << rgba.size()
              << " depth=" << depth.size() << " gl=" << native << '\n';
    ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS &&
                target->getLastSubmissionSerial()>before && native && depth.size()==size_t(side*side),
                "valid 3D volume must render and publish");
    if (native) ok &= compareCenters(rgba,reference,scene,label);
    if (n==2) {
      const auto textured=rgba;
      if (spec[3]==4) {
        scene.complexity->textureQuality=.6f;
        action.apply(scene.root);
        target->readbackRGBA(rgba);
        const bool magnificationGl=gl.render(scene.root) && gl.getBuffer();
        std::vector<unsigned char> magnificationReference;
        if (magnificationGl)
          magnificationReference.assign(gl.getBuffer(),gl.getBuffer()+side*side*3);
        const bool magnification=action.getLastStatus()==CoinRenderAction::SUCCESS &&
          magnificationGl &&
          compareCenters(rgba,magnificationReference,scene,label);
        if (!magnification) traceCenter("quality_point_six_magnification",8,24,rgba);
        ok &= check(magnification,
          "quality .6 must magnify the 2x2x2 midpoint with the trilinear mean");
        scene.complexity->textureQuality=.8f;
        auto * base=new SoTexture2;
        const unsigned char gray[]={128,128,128};
        base->image.setValue(SbVec2s(1,1),3,gray);
        base->model=SoTexture2::MODULATE;
        auto * unit1=new SoTextureUnit; unit1->unit=1;
        scene.root->insertChild(base,4);
        scene.root->insertChild(unit1,5);
        action.apply(scene.root);
        target->readbackRGBA(rgba);
        const bool mixedGl=gl.render(scene.root) && gl.getBuffer();
        const size_t gpuAt=size_t((side-1-8)*side+8)*4,glAt=size_t(8*side+8)*3;
        bool mixed=action.getLastStatus()==CoinRenderAction::SUCCESS && mixedGl &&
          rgba.size()==size_t(side*side*4);
        if (mixed) for(int c=0;c<3;++c) {
          const int composed=(int(scene.texels[c])*128+127)/255;
          mixed &= std::abs(int(rgba[gpuAt+c])-composed)<=6 &&
                   std::abs(int(gl.getBuffer()[glAt+c])-composed)<=6;
        }
        if (!mixed) traceCenter("mixed_units",8,8,rgba);
        ok &= check(mixed,"2D unit zero and 3D unit one must both contribute");
        scene.root->removeChild(unit1);
        scene.root->removeChild(base);
        action.apply(scene.root);
        target->readbackRGBA(rgba);
        ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS && rgba==textured,
                    "removing 2D/3D unit mix must restore the original volume frame");
        auto appendPanel=[&](SoSeparator * destination,int col,bool volumeCoordinates) {
          const float cx=-3.5f+col,cy=-2.5f;
          auto * shape=new SoCoordinate3;
          const SbVec3f corners[]={SbVec3f(cx-.34f,cy-.34f,.2f),SbVec3f(cx+.34f,cy-.34f,.2f),
                                   SbVec3f(cx+.34f,cy+.34f,.2f),SbVec3f(cx-.34f,cy+.34f,.2f)};
          shape->point.setValues(0,4,corners); destination->addChild(shape);
          if (volumeCoordinates) {
            auto * uvw=new SoTextureCoordinate3;
            const SbVec3f value(.5f/scene.width,.5f/scene.height,.5f/scene.depth);
            const SbVec3f values[]={value,value,value,value};
            uvw->point.setValues(0,4,values); destination->addChild(uvw);
          } else {
            auto * uv=new SoTextureCoordinate2;
            const SbVec2f value(.5f,.5f);
            const SbVec2f values[]={value,value,value,value};
            uv->point.setValues(0,4,values); destination->addChild(uv);
          }
          if (std::getenv("COIN_RENDER_TEXTURE3_TRACE_GL")) {
            auto * observer=new SoCallback;
            observer->setCallback([](void * context,SoAction * current) {
              if (!current->isOfType(SoGLRenderAction::getClassTypeId())) return;
              GLint active=0,binding2=0,binding3=0,width2=0,width3=0,depth3=0;
              const GLboolean enabled2=glIsEnabled(GL_TEXTURE_2D);
              const GLboolean enabled3=glIsEnabled(GL_TEXTURE_3D);
              glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
              glGetIntegerv(GL_TEXTURE_BINDING_2D,&binding2);
              glGetIntegerv(GL_TEXTURE_BINDING_3D,&binding3);
              glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&width2);
              glGetTexLevelParameteriv(GL_TEXTURE_3D,0,GL_TEXTURE_WIDTH,&width3);
              glGetTexLevelParameteriv(GL_TEXTURE_3D,0,GL_TEXTURE_DEPTH,&depth3);
              const GLenum error=glGetError();
              std::cerr << "texture3_gl_state panel=" << static_cast<const char *>(context)
                        << " active=" << active << " enabled2/3=" << int(enabled2) << '/'
                        << int(enabled3) << " binding2/3=" << binding2 << '/' << binding3
                        << " width2/3=" << width2 << '/' << width3 << " depth3=" << depth3
                        << " error=0x" << std::hex << unsigned(error) << std::dec << '\n';
            },const_cast<char *>(volumeCoordinates ? "outer3d" : "inner2d"));
            destination->addChild(observer);
          }
          auto * face=new SoIndexedFaceSet;
          const int32_t indices[]={0,1,2,3,-1};
          face->coordIndex.setValues(0,5,indices); destination->addChild(face);
        };
        auto * inner=new SoSeparator;
        auto * innerTexture=new SoTexture2;
        innerTexture->model=SoTexture2::MODULATE;
        const unsigned char innerGray[]={96,96,96};
        innerTexture->image.setValue(SbVec2s(1,1),3,innerGray);
        inner->addChild(innerTexture);
        appendPanel(inner,0,false);
        scene.root->addChild(inner);
        auto * outer=new SoSeparator;
        appendPanel(outer,1,true);
        scene.root->addChild(outer);
        action.apply(scene.root);
        target->readbackRGBA(rgba);
        const bool separatorGl=gl.render(scene.root) && gl.getBuffer();
        bool restored=action.getLastStatus()==CoinRenderAction::SUCCESS && separatorGl &&
          rgba.size()==size_t(side*side*4);
        if (restored) for(int c=0;c<3;++c) {
          const size_t localGpu=size_t((side-1-24)*side+8)*4;
          const size_t localGl=size_t(24*side+8)*3;
          const size_t siblingGpu=size_t((side-1-24)*side+24)*4;
          const size_t siblingGl=size_t(24*side+24)*3;
          restored &= std::abs(int(rgba[localGpu+c])-96)<=5 &&
                      std::abs(int(gl.getBuffer()[localGl+c])-96)<=5 &&
                      std::abs(int(rgba[siblingGpu+c])-int(scene.texels[c]))<=5 &&
                      std::abs(int(gl.getBuffer()[siblingGl+c])-int(scene.texels[c]))<=5;
        }
        if (!restored) {
          traceCenter("separator_base",8,8,rgba);
          traceCenter("separator_2d",8,24,rgba);
          traceCenter("separator_3d",24,24,rgba);
          SoOffscreenRenderer fresh(SbViewportRegion(side,side));
          fresh.setComponents(SoOffscreenRenderer::RGB);
          const bool freshRendered=fresh.render(scene.root) && fresh.getBuffer();
          std::cerr << "texture3_separator_fresh_gl rendered=" << freshRendered;
          if (freshRendered) for (const auto xy : {std::pair<int,int>{8,8},
                                                   std::pair<int,int>{8,24},
                                                   std::pair<int,int>{24,24}}) {
            const size_t at=size_t(xy.second*side+xy.first)*3;
            std::cerr << " xy=" << xy.first << ',' << xy.second << " RGB="
                      << int(fresh.getBuffer()[at]) << ',' << int(fresh.getBuffer()[at+1])
                      << ',' << int(fresh.getBuffer()[at+2]);
          }
          std::cerr << '\n';
          auto * isolated=new SoSeparator; isolated->ref();
          for(int i=0;i<5;++i) isolated->addChild(scene.root->getChild(i));
          isolated->addChild(inner); isolated->addChild(outer);
          SoOffscreenRenderer isolatedGl(SbViewportRegion(side,side));
          isolatedGl.setComponents(SoOffscreenRenderer::RGB);
          const bool isolatedRendered=isolatedGl.render(isolated) && isolatedGl.getBuffer();
          std::cerr << "texture3_separator_isolated_gl rendered=" << isolatedRendered;
          if (isolatedRendered) for (const auto xy : {std::pair<int,int>{8,24},
                                                      std::pair<int,int>{24,24}}) {
            const size_t at=size_t(xy.second*side+xy.first)*3;
            std::cerr << " xy=" << xy.first << ',' << xy.second << " RGB="
                      << int(isolatedGl.getBuffer()[at]) << ','
                      << int(isolatedGl.getBuffer()[at+1]) << ','
                      << int(isolatedGl.getBuffer()[at+2]);
          }
          std::cerr << '\n';
          isolated->unref();
          const int rootCaching=scene.root->renderCaching.getValue();
          const int innerCaching=inner->renderCaching.getValue();
          const int outerCaching=outer->renderCaching.getValue();
          scene.root->renderCaching=SoSeparator::OFF;
          inner->renderCaching=SoSeparator::OFF;
          outer->renderCaching=SoSeparator::OFF;
          SoOffscreenRenderer noCacheGl(SbViewportRegion(side,side));
          noCacheGl.setComponents(SoOffscreenRenderer::RGB);
          const bool noCacheRendered=noCacheGl.render(scene.root) && noCacheGl.getBuffer();
          std::cerr << "texture3_separator_cache_off_gl rendered=" << noCacheRendered;
          if (noCacheRendered) for (const auto xy : {std::pair<int,int>{8,8},
                                                     std::pair<int,int>{8,24},
                                                     std::pair<int,int>{24,24}}) {
            const size_t at=size_t(xy.second*side+xy.first)*3;
            std::cerr << " xy=" << xy.first << ',' << xy.second << " RGB="
                      << int(noCacheGl.getBuffer()[at]) << ','
                      << int(noCacheGl.getBuffer()[at+1]) << ','
                      << int(noCacheGl.getBuffer()[at+2]);
          }
          std::cerr << '\n';
          scene.root->renderCaching=rootCaching;
          inner->renderCaching=innerCaching;
          outer->renderCaching=outerCaching;
        }
        ok &= check(restored,"Separator must restore outer 3D state after local 2D texture");
        scene.root->removeChild(outer);
        scene.root->removeChild(inner);
        action.apply(scene.root);
        target->readbackRGBA(rgba);
        ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS && rgba==textured,
                    "removing nested 2D texture must restore the original published volume frame");
      }
      scene.complexity->textureQuality=0.f;
      action.apply(scene.root);
      target->readbackRGBA(rgba);
      const bool plainGl=gl.render(scene.root) && gl.getBuffer();
      const size_t gpuAt=size_t((side-1-8)*side+8)*4,glAt=size_t(8*side+8)*3;
      bool untextured=action.getLastStatus()==CoinRenderAction::SUCCESS && plainGl &&
        rgba.size()==size_t(side*side*4) && rgba!=textured;
      if (untextured) for(int c=0;c<3;++c)
        untextured &= rgba[gpuAt+c]>=250 && gl.getBuffer()[glAt+c]>=250;
      if (!untextured) traceCenter("quality_zero",8,8,rgba);
      ok &= check(untextured,"texture quality zero must disable volume sampling in CoinRender and CoinGL");
      scene.complexity->textureQuality=.8f;
      action.apply(scene.root);
      target->readbackRGBA(rgba);
      ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS && rgba==textured,
                  "volume texture must reactivate exactly after quality zero");
    }
    if (spec==std::array<int,4>{4,4,4,4}) {
      const size_t changedVoxel=37;
      const size_t changedByte=changedVoxel*4;
      const unsigned char original[]={scene.texels[changedByte],scene.texels[changedByte+1],
                                      scene.texels[changedByte+2]};
      scene.texels[changedByte]=255;
      scene.texels[changedByte+1]=0;
      scene.texels[changedByte+2]=255;
      scene.texture->images.setValue(SbVec3s(4,4,4),4,scene.texels.data());
      const uint64_t mutationSerial=target->getLastSubmissionSerial();
      action.apply(scene.root);
      target->readbackRGBA(rgba);
      const int mx=8+16*(changedVoxel%8),my=8+16*(changedVoxel/8);
      const size_t at=size_t((side-1-my)*side+mx)*4;
      ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS &&
                  target->getLastSubmissionSerial()>mutationSerial &&
                  rgba.size()==size_t(side*side*4) &&
                  rgba[at]>250 && rgba[at+1]<5 && rgba[at+2]>250,
                  "changed 3D voxel must invalidate reuse and reach its own slice");
      for (int c=0;c<3;++c) scene.texels[changedByte+c]=original[c];
      scene.texture->images.setValue(SbVec3s(4,4,4),4,scene.texels.data());
      action.apply(scene.root);
      target->readbackRGBA(rgba); target->readbackDepth(depth);
      ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS &&
                  rgba.size()==size_t(side*side*4),
                  "restored 3D voxel must publish before rollback probe");
      auto * firstPanel=static_cast<SoSeparator *>(scene.root->getChild(5));
      auto * firstCoords=static_cast<SoTextureCoordinate3 *>(firstPanel->getChild(1));
      const SbVec3f wrapped(.125f,.125f,1.125f);
      const SbVec3f wrapPoints[]={wrapped,wrapped,wrapped,wrapped};
      firstCoords->point.setValues(0,4,wrapPoints);
      for (bool repeat : {true,false}) {
        scene.texture->wrapR=repeat ? SoTexture3::REPEAT : SoTexture3::CLAMP;
        action.apply(scene.root);
        target->readbackRGBA(rgba);
        const bool glRendered=gl.render(scene.root) && gl.getBuffer();
        const size_t gpuAt=size_t((side-1-8)*side+8)*4,glAt=size_t(8*side+8)*3;
        const size_t expectedAt=size_t(repeat ? 0 : 48)*4;
        bool correct=action.getLastStatus()==CoinRenderAction::SUCCESS && glRendered &&
          rgba.size()==size_t(side*side*4);
        if (correct) for(int c=0;c<3;++c)
          correct &= std::abs(int(rgba[gpuAt+c])-int(scene.texels[expectedAt+c]))<=5 &&
                     std::abs(int(gl.getBuffer()[glAt+c])-int(scene.texels[expectedAt+c]))<=5;
        if (!correct) traceCenter(repeat ? "wrap_repeat" : "wrap_clamp",8,8,rgba);
        ok &= check(correct,repeat ? "R repeat must return to first slice" :
                           "R clamp must stay on last slice");
      }
      scene.texture->wrapR=SoTexture3::CLAMP;
      scene.texture->wrapS=SoTexture3::REPEAT;
      scene.texture->wrapT=SoTexture3::REPEAT;
      scene.texture->wrapR=SoTexture3::REPEAT;
      const SbVec3f minifiedPoints[]={SbVec3f(0,0,0),SbVec3f(16,0,16),
                                      SbVec3f(16,16,16),SbVec3f(0,16,0)};
      firstCoords->point.setValues(0,4,minifiedPoints);
      action.apply(scene.root);
      target->readbackRGBA(rgba);
      const bool mipGl=gl.render(scene.root) && gl.getBuffer();
      const size_t mipGpuAt=size_t((side-1-8)*side+8)*4,mipGlAt=size_t(8*side+8)*3;
      bool mip=action.getLastStatus()==CoinRenderAction::SUCCESS && mipGl &&
        rgba.size()==size_t(side*side*4);
      if (mip) for(int c=0;c<3;++c) {
        int sum=0;
        for (int voxel=0;voxel<64;++voxel) sum+=scene.texels[size_t(voxel)*4+c];
        const int average=(sum+32)/64;
        mip &= std::abs(int(rgba[mipGpuAt+c])-average)<=7 &&
               std::abs(int(gl.getBuffer()[mipGlAt+c])-average)<=7;
      }
      if (!mip) traceCenter("mip_xyz",8,8,rgba);
      ok &= check(mip,"strong minification must select the 1x1x1 mip averaged over all XYZ voxels");
      scene.texture->wrapS=SoTexture3::CLAMP;
      scene.texture->wrapT=SoTexture3::CLAMP;
      scene.texture->wrapR=SoTexture3::CLAMP;
      const SbVec3f originalCoords(.125f,.125f,.125f);
      const SbVec3f restoredPoints[]={originalCoords,originalCoords,originalCoords,originalCoords};
      firstCoords->point.setValues(0,4,restoredPoints);
      auto * projective=new SoTextureMatrixTransform;
      SbMatrix textureMatrix=SbMatrix::identity();
      textureMatrix[0][0]=6.f;
      textureMatrix[1][1]=2.f;
      textureMatrix[2][2]=2.f;
      textureMatrix[3][3]=2.f;
      projective->matrix=textureMatrix;
      firstPanel->insertChild(projective,0);
      action.apply(scene.root);
      target->readbackRGBA(rgba);
      const bool projectedGl=gl.render(scene.root) && gl.getBuffer();
      const size_t projectedGpuAt=size_t((side-1-8)*side+8)*4;
      const size_t projectedGlAt=size_t(8*side+8)*3;
      bool projected=action.getLastStatus()==CoinRenderAction::SUCCESS && projectedGl &&
        rgba.size()==size_t(side*side*4);
      if (projected) for(int c=0;c<3;++c)
        projected &= std::abs(int(rgba[projectedGpuAt+c])-int(scene.texels[4+c]))<=5 &&
                     std::abs(int(gl.getBuffer()[projectedGlAt+c])-int(scene.texels[4+c]))<=5;
      if (!projected) traceCenter("projective_q",8,8,rgba);
      ok &= check(projected,"texture matrix must divide transformed s/t/r by q=2");
      firstPanel->removeChild(projective);
      scene.texels[3]=128;
      scene.texture->images.setValue(SbVec3s(4,4,4),4,scene.texels.data());
      auto * blend=new SoTransparencyType; blend->value=SoTransparencyType::BLEND;
      firstPanel->insertChild(blend,0);
      action.apply(scene.root);
      target->readbackRGBA(rgba);
      const bool alphaGl=gl.render(scene.root) && gl.getBuffer();
      const size_t alphaGpuAt=size_t((side-1-8)*side+8)*4,alphaGlAt=size_t(8*side+8)*3;
      bool alpha=action.getLastStatus()==CoinRenderAction::SUCCESS && alphaGl &&
        rgba.size()==size_t(side*side*4);
      if (alpha) for(int c=0;c<3;++c) {
        const int composed=(int(scene.texels[c])*128+127)/255;
        alpha &= std::abs(int(rgba[alphaGpuAt+c])-composed)<=6 &&
                 std::abs(int(gl.getBuffer()[alphaGlAt+c])-composed)<=6;
      }
      if (!alpha) traceCenter("alpha_blend",8,8,rgba);
      ok &= check(alpha,"RGBA volume alpha must BLEND over black like CoinGL and analytic composition");
      firstPanel->removeChild(blend);
      scene.texels[3]=255;
      scene.texture->images.setValue(SbVec3s(4,4,4),4,scene.texels.data());
      action.apply(scene.root);
      target->readbackRGBA(rgba); target->readbackDepth(depth);
      const uint64_t serial=target->getLastSubmissionSerial();
      const auto stable=rgba; const auto stableDepth=depth;
      std::vector<unsigned char> odd(size_t(257*2*2*4),123);
      scene.texture->images.setValue(SbVec3s(257,2,2),4,odd.data());
      action.apply(scene.root);
      target->readbackRGBA(rgba); target->readbackDepth(depth);
      ok &= check(action.getLastStatus()!=CoinRenderAction::SUCCESS &&
                  target->getLastSubmissionSerial()==serial &&
                  rgba==stable && depth==stableDepth,
                  "257-axis volume must reject without publishing color/depth/serial");
      scene.texture->images.setValue(SbVec3s(4,4,4),4,scene.texels.data());
      action.apply(scene.root);
      target->readbackRGBA(rgba); target->readbackDepth(depth);
      ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS &&
                  target->getLastSubmissionSerial()>serial &&
                  rgba==stable && depth==stableDepth,
                  "valid volume must recover exactly after rejected dimensions");
      std::vector<unsigned char> edge(size_t(256*4));
      for(int x=0;x<256;++x) {
        edge[size_t(x)*4]=static_cast<unsigned char>(x);
        edge[size_t(x)*4+1]=static_cast<unsigned char>(255-x);
        edge[size_t(x)*4+2]=64;
        edge[size_t(x)*4+3]=255;
      }
      scene.texture->images.setValue(SbVec3s(256,1,1),4,edge.data());
      const uint64_t beforeBoundary=target->getLastSubmissionSerial();
      action.apply(scene.root);
      target->readbackRGBA(rgba); target->readbackDepth(depth);
      const bool boundary=action.getLastStatus()==CoinRenderAction::SUCCESS &&
        target->getLastSubmissionSerial()>beforeBoundary &&
        rgba.size()==size_t(side*side*4) && depth.size()==size_t(side*side);
      ok &= check(boundary,"POT volume at the 256-texel axis limit must render");
      const uint64_t boundarySerial=target->getLastSubmissionSerial();
      const auto boundaryRgba=rgba; const auto boundaryDepth=depth;
      std::vector<unsigned char> oversized(size_t(512*4),255);
      scene.texture->images.setValue(SbVec3s(512,1,1),4,oversized.data());
      action.apply(scene.root);
      target->readbackRGBA(rgba); target->readbackDepth(depth);
      ok &= check(action.getLastStatus()!=CoinRenderAction::SUCCESS &&
                  target->getLastSubmissionSerial()==boundarySerial &&
                  rgba==boundaryRgba && depth==boundaryDepth,
                  "volume axis beyond 256 must reject without altering publication");
      scene.texture->images.setValue(SbVec3s(4,4,4),4,scene.texels.data());
      action.apply(scene.root);
      target->readbackRGBA(rgba); target->readbackDepth(depth);
      ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS &&
                  target->getLastSubmissionSerial()>boundarySerial &&
                  rgba==stable && depth==stableDepth,
                  "volume must recover after 256-axis admission failure");
      if (!skipMaximumVolume) {
      constexpr int maximumAxis=256;
      std::vector<unsigned char> maximumVolume(size_t(maximumAxis)*maximumAxis*maximumAxis*4);
      uint64_t sums[3]={0,0,0};
      for(int z=0;z<maximumAxis;++z) for(int y=0;y<maximumAxis;++y)
        for(int x=0;x<maximumAxis;++x) {
          const size_t at=(size_t(z)*maximumAxis*maximumAxis+size_t(y)*maximumAxis+x)*4;
          const unsigned char channels[]={static_cast<unsigned char>(17*x+29*y+43*z),
                                          static_cast<unsigned char>(47*x+19*y+31*z),
                                          static_cast<unsigned char>(11*x+53*y+7*z)};
          for(int c=0;c<3;++c) { maximumVolume[at+c]=channels[c]; sums[c]+=channels[c]; }
          maximumVolume[at+3]=255;
        }
      auto setPanelCoords=[&](int panel,const SbVec3f values[4]) {
        auto * shape=static_cast<SoSeparator *>(scene.root->getChild(5+panel));
        static_cast<SoTextureCoordinate3 *>(shape->getChild(1))->point.setValues(0,4,values);
      };
      const int slices[]={0,85,170,255};
      for(int panel=0;panel<4;++panel) {
        const float r=(slices[panel]+.5f)/maximumAxis;
        const SbVec3f value(.5f/maximumAxis,.5f/maximumAxis,r);
        const SbVec3f values[]={value,value,value,value};
        setPanelCoords(panel,values);
      }
      const SbVec3f minifiedVolume[]={SbVec3f(0,0,0),SbVec3f(64,0,64),
                                      SbVec3f(64,64,64),SbVec3f(0,64,0)};
      setPanelCoords(4,minifiedVolume);
      scene.texture->wrapS=SoTexture3::REPEAT;
      scene.texture->wrapT=SoTexture3::REPEAT;
      scene.texture->wrapR=SoTexture3::REPEAT;
      scene.texture->images.setValue(SbVec3s(256,256,256),4,maximumVolume.data());
      for(float quality : {.1f,.8f}) {
        scene.complexity->textureQuality=quality;
        const uint64_t previous=target->getLastSubmissionSerial();
        action.apply(scene.root);
        target->readbackRGBA(rgba);
        const bool maximumGl=gl.render(scene.root) && gl.getBuffer();
        bool admitted=action.getLastStatus()==CoinRenderAction::SUCCESS &&
          target->getLastSubmissionSerial()>previous && maximumGl &&
          rgba.size()==size_t(side*side*4);
        if (admitted) for(int panel=0;panel<4;++panel) {
          const int x=8+16*panel,y=8;
          const size_t gpuAt=size_t((side-1-y)*side+x)*4,glAt=size_t(y*side+x)*3;
          const size_t voxelAt=size_t(slices[panel])*maximumAxis*maximumAxis*4;
          for(int c=0;c<3;++c)
            admitted &= std::abs(int(rgba[gpuAt+c])-int(maximumVolume[voxelAt+c]))<=6 &&
                        std::abs(int(gl.getBuffer()[glAt+c])-int(maximumVolume[voxelAt+c]))<=6;
        }
        if (admitted && quality==.8f) {
          const size_t gpuAt=size_t((side-1-8)*side+72)*4,glAt=size_t(8*side+72)*3;
          for(int c=0;c<3;++c) {
            const int average=int((sums[c]+size_t(256*256*256/2))/(256*256*256));
            admitted &= std::abs(int(rgba[gpuAt+c])-average)<=8 &&
                        std::abs(int(gl.getBuffer()[glAt+c])-average)<=8;
          }
        }
        if (!admitted) { traceCenter("maximum_slice",8,8,rgba); traceCenter("maximum_mip",72,8,rgba); }
        ok &= check(admitted,quality==.8f ?
          "256-cubed volume mip-on must retain distinct slices and XYZ average" :
          "256-cubed volume mip-off must retain distinct selected slices");
      }
      scene.complexity->textureQuality=.8f;
      scene.texture->wrapS=SoTexture3::CLAMP;
      scene.texture->wrapT=SoTexture3::CLAMP;
      scene.texture->wrapR=SoTexture3::CLAMP;
      scene.texture->images.setValue(SbVec3s(4,4,4),4,scene.texels.data());
      for(int panel=0;panel<5;++panel) {
        const SbVec3f value(((panel%4)+.5f)/4.f,(((panel/4)%4)+.5f)/4.f,.125f);
        const SbVec3f values[]={value,value,value,value};
        setPanelCoords(panel,values);
      }
      action.apply(scene.root);
      target->readbackRGBA(rgba); target->readbackDepth(depth);
      ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS &&
                  rgba==stable && depth==stableDepth,
                  "maximum volume must release and restore the smaller frame exactly");
      }
    }
  }
  {
    Fixture callbackScene;
    callbackScene.volume(2);
    while (callbackScene.root->getNumChildren()>5) callbackScene.root->removeChild(5);
    callbackScene.texels[0]=255;
    callbackScene.texels[1]=0;
    callbackScene.texels[2]=0;
    auto resetImage=[&]() {
      callbackScene.texture->images.setValue(SbVec3s(2,2,2),4,callbackScene.texels.data());
    };
    resetImage();
    auto panel=[&](int col) {
      auto * group=new SoSeparator;
      const float cx=-3.5f+col,cy=-3.5f;
      auto * positions=new SoCoordinate3;
      const SbVec3f corners[]={SbVec3f(cx-.34f,cy-.34f,0),SbVec3f(cx+.34f,cy-.34f,0),
                               SbVec3f(cx+.34f,cy+.34f,0),SbVec3f(cx-.34f,cy+.34f,0)};
      positions->point.setValues(0,4,corners); group->addChild(positions);
      auto * coords=new SoTextureCoordinate3;
      const SbVec3f sample(.25f,.25f,.25f);
      const SbVec3f values[]={sample,sample,sample,sample};
      coords->point.setValues(0,4,values); group->addChild(coords);
      auto * face=new SoIndexedFaceSet;
      const int32_t indices[]={0,1,2,3,-1};
      face->coordIndex.setValues(0,5,indices); group->addChild(face);
      return group;
    };
    callbackScene.root->addChild(panel(0));
    struct MutationContext {
      SoTexture3 * texture=nullptr;
      unsigned callbackTraversals=0,glTraversals=0,ignoredTraversals=0;
    } context;
    context.texture=callbackScene.texture;
    auto * mutation=new SoCallback;
    mutation->setCallback([](void * context,SoAction * current) {
      auto * data=static_cast<MutationContext *>(context);
      const bool capture=current->isOfType(SoCallbackAction::getClassTypeId());
      const bool native=current->isOfType(SoGLRenderAction::getClassTypeId());
      data->callbackTraversals+=capture;
      data->glTraversals+=native;
      data->ignoredTraversals+=!capture && !native;
      if (std::getenv("COIN_RENDER_TEXTURE3_TRACE_GL"))
        std::cerr << "texture3_callback_action type="
                  << current->getTypeId().getName().getString()
                  << " capture=" << capture << " GL=" << native << '\n';
      if (!capture && !native) return;
      auto * texture=data->texture;
      SbVec3s size; int components=0;
      unsigned char * bytes=texture->images.startEditing(size,components);
      if (bytes && size==SbVec3s(2,2,2) && components==4) {
        bytes[0]=0; bytes[1]=0; bytes[2]=255;
      }
      texture->images.finishEditing();
    },&context);
    callbackScene.root->addChild(mutation);
    callbackScene.root->addChild(callbackScene.texture); // Same node, not a copied image.
    callbackScene.root->addChild(panel(1));
    action.apply(callbackScene.root);
    std::vector<unsigned char> gpu;
    target->readbackRGBA(gpu);
    resetImage();
    const bool callbackGl=gl.render(callbackScene.root) && gl.getBuffer();
    if (std::getenv("COIN_RENDER_TEXTURE3_TRACE_GL"))
      std::cerr << "texture3_callback_counts capture=" << context.callbackTraversals
                << " GL=" << context.glTraversals
                << " ignored=" << context.ignoredTraversals << '\n';
    bool ordered=action.getLastStatus()==CoinRenderAction::SUCCESS && callbackGl &&
      gpu.size()==size_t(side*side*4);
    if (ordered) for(int panelIndex=0;panelIndex<2;++panelIndex) {
      const int x=8+16*panelIndex,y=8;
      const size_t gpuAt=size_t((side-1-y)*side+x)*4,glAt=size_t(y*side+x)*3;
      const int expected[3]={panelIndex==0 ? 255 : 0,0,panelIndex==0 ? 0 : 255};
      for(int c=0;c<3;++c)
        ordered &= std::abs(int(gpu[gpuAt+c])-expected[c])<=5 &&
                   std::abs(int(gl.getBuffer()[glAt+c])-expected[c])<=5;
    }
    if (!ordered) { traceCenter("callback_before",8,8,gpu); traceCenter("callback_after",24,8,gpu); }
    ok &= check(ordered,"same SoTexture3 node edited between draws must publish old then new voxel");
  }
  {
    Fixture bands;
    bands.volume(2);
    while (bands.root->getNumChildren()>5) bands.root->removeChild(5);
    std::vector<unsigned char> source(size_t(8*8*8*4));
    for(int z=0;z<8;++z) for(int y=0;y<8;++y) for(int x=0;x<8;++x) {
      const size_t at=size_t(z*64+y*8+x)*4;
      source[at]=static_cast<unsigned char>(8+2*x*x+y*y+z*z);
      source[at+1]=static_cast<unsigned char>(8+x*x+2*y*y+z*z);
      source[at+2]=static_cast<unsigned char>(8+x*x+y*y+2*z*z);
      source[at+3]=255;
    }
    bands.texture->images.setValue(SbVec3s(8,8,8),4,source.data());
    auto * positions=new SoCoordinate3;
    const SbVec3f corners[]={SbVec3f(-4,-4,0),SbVec3f(4,-4,0),
                             SbVec3f(4,4,0),SbVec3f(-4,4,0)};
    positions->point.setValues(0,4,corners); bands.root->addChild(positions);
    auto * coords=new SoTextureCoordinate3; bands.root->addChild(coords);
    auto * face=new SoIndexedFaceSet;
    const int32_t indices[]={0,1,2,3,-1};
    face->coordIndex.setValues(0,5,indices); bands.root->addChild(face);
    auto averageSquare=[](int first,int length) {
      return double(first*first+first*(length-1))+
        double((length-1)*(2*length-1))/6.0;
    };
    auto mipValue=[&](int level,int x,int y,int z,int channel) {
      const int block=1<<level;
      const double sx=averageSquare(x*block,block);
      const double sy=averageSquare(y*block,block);
      const double sz=averageSquare(z*block,block);
      return 8.0+(channel==0 ? 2*sx+sy+sz :
                   channel==1 ? sx+2*sy+sz : sx+sy+2*sz);
    };
    auto levelValue=[&](int level,bool linear,int channel) {
      const int dimension=8>>level;
      const double coordinate=.425*dimension-.5;
      const double limited=std::max(0.0,std::min(double(dimension-1),coordinate));
      const int lo=int(std::floor(limited));
      const int hi=std::min(dimension-1,lo+1);
      const double fraction=linear ? limited-lo : 0.0;
      const int nearest=std::min(dimension-1,int(std::floor(limited+.5)));
      if (!linear) return mipValue(level,nearest,nearest,nearest,channel);
      double value=0;
      for(int dz=0;dz<2;++dz) for(int dy=0;dy<2;++dy) for(int dx=0;dx<2;++dx) {
        const double weight=(dx ? fraction : 1-fraction)*
                            (dy ? fraction : 1-fraction)*
                            (dz ? fraction : 1-fraction);
        value+=weight*mipValue(level,dx ? hi : lo,dy ? hi : lo,dz ? hi : lo,channel);
      }
      return value;
    };
    int observed[4][4][3]={};
    const float qualities[]={.1f,.3f,.6f,.8f};
    const float lods[]={1.f,1.5f,2.f,2.5f};
    for(int q=0;q<4;++q) {
      // CoinGL chooses the spatial filter when it creates the GL image.
      // Give each quality an independently authored node and upload.
      auto * qualifiedTexture=new SoTexture3;
      qualifiedTexture->model=SoTexture3::MODULATE;
      qualifiedTexture->wrapS=SoTexture3::CLAMP;
      qualifiedTexture->wrapT=SoTexture3::CLAMP;
      qualifiedTexture->wrapR=SoTexture3::CLAMP;
      qualifiedTexture->images.setValue(SbVec3s(8,8,8),4,source.data());
      bands.root->replaceChild(4,qualifiedTexture);
      bands.texture=qualifiedTexture;
      bands.complexity->textureQuality=qualities[q];
      for(int l=0;l<4;++l) {
      const double rho=std::pow(2.0,double(lods[l]));
      const float left=float(.425-2*rho*(4+.03125));
      const float right=float(.425+2*rho*(4-.03125));
      const SbVec3f uvw[]={SbVec3f(.425f,.425f,left),SbVec3f(.425f,.425f,right),
                           SbVec3f(.425f,.425f,right),SbVec3f(.425f,.425f,left)};
      coords->point.setValues(0,4,uvw);
      action.apply(bands.root);
      std::vector<unsigned char> pixels;
      target->readbackRGBA(pixels);
      const bool native=gl.render(bands.root) && gl.getBuffer();
      const size_t gpuAt=size_t((side-1-64)*side+64)*4,glAt=size_t(64*side+64)*3;
      const bool captured=action.getLastStatus()==CoinRenderAction::SUCCESS && native &&
        pixels.size()==size_t(side*side*4);
      bool gpuAnalytic=captured,nativeAnalytic=captured,pair=captured;
      double expected[3]={};
      for(int c=0;c<3;++c) {
        if (q<2) expected[c]=levelValue(0,q==1,c);
        else {
          const int base=int(std::floor(lods[l]));
          const double blend=lods[l]-base;
          expected[c]=(1-blend)*levelValue(base,q==3,c)+
                      blend*levelValue(base+1,q==3,c);
        }
        if (captured) {
          observed[q][l][c]=pixels[gpuAt+c];
          gpuAnalytic &= std::abs(double(pixels[gpuAt+c])-expected[c])<=5;
          nativeAnalytic &= std::abs(double(gl.getBuffer()[glAt+c])-expected[c])<=5;
          pair &= std::abs(int(pixels[gpuAt+c])-int(gl.getBuffer()[glAt+c]))<=5;
        }
      }
      const bool accurate=gpuAnalytic && nativeAnalytic && pair;
      std::cout << "texture3_filter_band " << label << " quality=" << qualities[q]
                << " lod=" << lods[l] << " expected=" << expected[0] << ','
                << expected[1] << ',' << expected[2];
      if (pixels.size()==size_t(side*side*4))
        std::cout << " actual=" << int(pixels[gpuAt]) << ',' << int(pixels[gpuAt+1])
                  << ',' << int(pixels[gpuAt+2]);
      if (native) std::cout << " GL=" << int(gl.getBuffer()[glAt]) << ','
                            << int(gl.getBuffer()[glAt+1]) << ','
                            << int(gl.getBuffer()[glAt+2]);
      std::cout << " gpu_analytic=" << gpuAnalytic
                << " gl_analytic=" << nativeAnalytic << " pair=" << pair
                << " pass=" << accurate << '\n';
      if (!accurate) traceCenter("filter_band",64,64,pixels);
      ok &= check(accurate,"3D filter band and R-dominated intermediate LOD must match analytic and CoinGL");
      }
    }
    bool discriminating=true;
    for(int c=0;c<3;++c) {
      discriminating &= std::abs(observed[0][0][c]-observed[1][0][c])>=2;
      discriminating &= std::abs(observed[2][0][c]-observed[2][2][c])>=2;
      discriminating &= std::abs(observed[3][1][c]-observed[3][3][c])>=2;
    }
    ok &= check(discriminating,"nonlinear source must distinguish spatial filters and intermediate mip levels");
    // Diagnostic only: CoinGL may retain the first mip filter on one image.
    auto * transitionTexture=new SoTexture3;
    transitionTexture->model=SoTexture3::MODULATE;
    transitionTexture->images.setValue(SbVec3s(8,8,8),4,source.data());
    bands.root->replaceChild(4,transitionTexture);
    bands.complexity->textureQuality=.6f;
    const bool beforeTransition=gl.render(bands.root) && gl.getBuffer();
    int beforeRgb[3]={};
    const size_t transitionAt=size_t(64*side+64)*3;
    if (beforeTransition) for(int c=0;c<3;++c) beforeRgb[c]=gl.getBuffer()[transitionAt+c];
    bands.complexity->textureQuality=.8f;
    const bool afterTransition=gl.render(bands.root) && gl.getBuffer();
    std::cout << "texture3_native_quality_transition rendered="
              << beforeTransition << '/' << afterTransition
              << " before=" << beforeRgb[0] << ',' << beforeRgb[1] << ',' << beforeRgb[2];
    if (afterTransition) std::cout << " after=" << int(gl.getBuffer()[transitionAt]) << ','
                                   << int(gl.getBuffer()[transitionAt+1]) << ','
                                   << int(gl.getBuffer()[transitionAt+2]);
    std::cout << " diagnostic_only=1\n";
  }
  action.setRenderTarget(nullptr);
  return ok;
}
}
int main(int argc,char ** argv) {
  SoDB::init(); CoinRenderAction::initClass();
  configureSamplingTest(argc,argv);
  bool skipMaximumVolume=false;
  for(int i=1;i<argc;++i)
    if (std::string(argv[i])=="--skip-maximum-volume") skipMaximumVolume=true;
  if (skipMaximumVolume)
    std::cout << "texture3 diagnostic_only skip_maximum_volume=1 (not R23 qualification)\n";
  std::unique_ptr<CoinRenderTarget> cpu(samplingTestOffscreen(SbVec2i32(side,side)));
  if (!cpu) return 1;
  cpu->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);
  bool ok=run(cpu.get(),"CPU",skipMaximumVolume);
  CoinRenderCapabilities caps{};
  const bool gpu=coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,&caps,sizeof(caps))==0 && caps.gpu_available;
  std::cout << "texture3 runtime backend=" << caps.backend << " renderer=" << caps.renderer
            << " vendor=" << caps.vendor_id << " adapter=" << caps.adapter_name
            << " gpu_available=" << gpu << '\n';
  if (gpu) {
    std::unique_ptr<CoinRenderTarget> target(samplingTestOffscreen(SbVec2i32(side,side)));
    ok &= run(target.get(),"GPU",skipMaximumVolume);
  } else if (std::getenv("COIN_RENDER_REQUIRE_TEXTURE3_GPU")) {
    ok &= check(false,"GPU required for 3D volume qualification");
  }
  return ok ? 0 : 1;
}
