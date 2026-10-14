#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/SbViewVolume.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/nodes/SoClipPlane.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/nodes/SoScale.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoLineSet.h>
#include <Inventor/nodes/SoPointSet.h>
#include <Inventor/nodes/SoShape.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include "rendering/coinrender/CoinRenderClipCore.h"
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderFramePlanBuilder.h"
#include "rendering/coinrender/CoinRenderFrameReuseCore.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <cmath>
#include <iostream>
#include <memory>
#include <limits>

namespace {
bool check(bool c, const char * m) {
  if (!c) std::cerr << "CoinRenderClipPlaneTest: " << m << '\n';
  return c;
}
void triangle(void * p, SoCallbackAction * a, const SoPrimitiveVertex * x,
              const SoPrimitiveVertex * y, const SoPrimitiveVertex * z) {
  static_cast<CoinRenderFramePlanBuilder *>(p)->addTriangle(a,x,y,z);
}
void line(void * p, SoCallbackAction * a, const SoPrimitiveVertex * x, const SoPrimitiveVertex * y) {
  static_cast<CoinRenderFramePlanBuilder *>(p)->addLine(a,x,y);
}
void point(void * p, SoCallbackAction * a, const SoPrimitiveVertex * x) {
  static_cast<CoinRenderFramePlanBuilder *>(p)->addPoint(a,x);
}
bool capture(SoNode * scene, CoinRenderFramePlan & frame, bool * unsupported = nullptr) {
  CoinRenderFramePlanBuilder builder;
  builder.beginFrame(SbColor4f(0,0,0,1), SbViewportRegion(64,64));
  CoinRenderAction action(SbViewportRegion(64,64));
  action.addTriangleCallback(SoShape::getClassTypeId(),triangle,&builder);
  action.addLineSegmentCallback(SoShape::getClassTypeId(),line,&builder);
  action.addPointCallback(SoShape::getClassTypeId(),point,&builder);
  action.apply(scene);
  std::string diagnostic;
  const bool ok=builder.build(frame,&diagnostic);
  if (unsupported) *unsupported=builder.isUnsupportedBuild();
  return ok;
}
bool captureContract() {
  SoSeparator * root=new SoSeparator;root->ref();
  root->addChild(new SoCube);
  SoSeparator * nested=new SoSeparator;root->addChild(nested);
  SoTranslation * before=new SoTranslation;before->translation=SbVec3f(.25f,0,0);nested->addChild(before);
  SoScale * scale=new SoScale;scale->scaleFactor=SbVec3f(2,3,1);nested->addChild(scale);
  SoClipPlane * plane=new SoClipPlane;plane->plane=SbPlane(SbVec3f(1,0,0),.1f);nested->addChild(plane);
  SoTranslation * after=new SoTranslation;after->translation=SbVec3f(1,0,0);nested->addChild(after);
  nested->addChild(new SoCube);
  SoClipPlane * off=new SoClipPlane;off->on=FALSE;nested->addChild(off);nested->addChild(new SoCube);
  root->addChild(new SoCube);
  CoinRenderFramePlan frame;bool ok=check(capture(root,frame),"capture transformed planes");root->unref();
  size_t clipped=0, unclipped=0;
  for (const auto & draw:frame.draws) {
    const auto & state=frame.renderStates[draw.renderStateSlot];
    if (state.clipPlanesWorld.empty()) ++unclipped;
    else {
      ++clipped;
      ok=check(state.clipPlanesWorld.size()==1,"off plane should not accumulate")&&ok;
      ok=check(std::abs(state.clipPlanesWorld[0].getDistanceFromOrigin()-.45f)<1e-5f,
               "plane uses transform at plane traversal, not at shape")&&ok;
    }
  }
  return check(clipped==2 && unclipped==2,"separator restores clipping state")&&ok;
}
void quad(CoinRenderFramePlan & p, bool front, bool alpha=false) {
  const uint32_t mat=static_cast<uint32_t>(p.materials.size());
  CoinRenderMaterialSnapshot m;m.diffuse[0]=front?1:0;m.diffuse[1]=0;m.diffuse[2]=front?0:1;
  m.diffuse[3]=alpha?.5f:1;m.transparency=alpha?.5f:0;
  for(int c=0;c<3;++c)m.ambient[c]=m.diffuse[c];
  p.materials.push_back(m);
  CoinRenderRenderStateSnapshot s;s.materialSlot=mat;s.lightModel=CoinRenderLightModel::BASE_COLOR;
  s.cullMode=CoinRenderCullMode::NONE;s.transparencyType=SoGLRenderAction::SORTED_OBJECT_BLEND;
  if(front)s.clipPlanesWorld.push_back(SbPlane(SbVec3f(1,0,0),0));
  const uint32_t rs=static_cast<uint32_t>(p.renderStates.size());p.renderStates.push_back(s);
  const uint32_t first=static_cast<uint32_t>(p.vertices.size());
  const float xy[4][2]={{-.9f,-.9f},{.9f,-.9f},{.9f,.9f},{-.9f,.9f}};
  for(int i=0;i<4;++i) {CoinRenderVertexSnapshot v;v.position[0]=xy[i][0];v.position[1]=xy[i][1];
    v.position[2]=front?-.5f:.5f;v.materialSlot=mat;p.vertices.push_back(v);}
  CoinRenderDrawPacket d;d.renderStateSlot=rs;d.geometry.firstVertex=first;d.geometry.vertexCount=4;
  d.geometry.firstIndex=static_cast<uint32_t>(p.indices.size());d.geometry.indexCount=6;
  const uint32_t ix[]={0,1,2,0,2,3};for(auto i:ix)p.indices.push_back(first+i);p.draws.push_back(d);
}
CoinRenderFramePlan plan(bool alpha=false) {
  CoinRenderFramePlan p;p.revision=1;p.lightingStates.push_back(CoinRenderLightingSnapshot{});
  p.cameras.push_back(CoinRenderCameraSnapshot{});CoinRenderViewportSnapshot vp;vp.width=vp.height=64;
  p.viewports.push_back(vp);quad(p,false);quad(p,true,alpha);return p;
}
bool coreContract() {
  auto p=plan();auto & s=p.renderStates[1];
  CoinRenderVertexSnapshot a,b;a.position[0]=-1;b.position[0]=1;
  float first,last;bool ok=check(coin_render_clip_segment(s,a,b,first,last) && first==.5f && last==1,
                                "segment interval clips negative side");
  s.clipPlanesWorld.push_back(SbPlane(SbVec3f(-1,0,0),-.25f));
  ok=check(coin_render_clip_segment(s,a,b,first,last) && first==.5f && last==.625f,"plane intersection")&&ok;
  a.position[0]=-.5f;ok=check(!coin_render_clip_point(s,a),"negative point is clipped")&&ok;
  a.position[0]=0;ok=check(coin_render_clip_point(s,a),"boundary point is retained")&&ok;
  auto copied=p;ok=check(p.hasSamePayload(copied),"deep-copy payload equality")&&ok;
  copied.renderStates[1].clipPlanesWorld[0]=SbPlane(SbVec3f(1,0,0),.1f);
  ok=check(!p.hasSamePayload(copied),"plane change invalidates payload")&&ok;
  auto decision=CoinRenderFrameReuseCore::classify(p,copied);
  ok=check(decision.kind!=CoinRenderFrameReuseKind::REUSE && decision.kind!=CoinRenderFrameReuseKind::CAMERA_PATCH,
           "plane change cannot reuse camera-only state")&&ok;
  s.view.setTranslate(SbVec3f(.25f,0,0));float eq[8][4]={};std::string error;
  ok=check(coin_render_clip_equations(s,eq,error) && std::abs(eq[0][3]+.25f)<1e-5f,"eye-space conversion")&&ok;
  s.clipPlanesWorld[0]=SbPlane(SbVec3f(1,0,0),std::numeric_limits<float>::quiet_NaN());
  ok=check(!p.isValid(&error),"non-finite plane rejected")&&ok;
  return ok;
}
bool pixels(const std::vector<uint8_t> & image,int x,int y,int r,int b) {
  const size_t i=static_cast<size_t>(y*64+x)*4;
  if(image.size()!=64*64*4)return false;
  if(std::abs(int(image[i])-r)>6 || image[i+1]>6 || std::abs(int(image[i+2])-b)>6) {
    std::cerr<<"pixel "<<x<<','<<y<<": "<<int(image[i])<<','<<int(image[i+1])<<','<<int(image[i+2])<<'\n';return false;
  }return true;
}
bool gpuContract(bool cpu) {
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(64,64)));
  if(cpu)target->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);
#if defined(HAVE_COIN_BGFX)
  if(cpu)target->getPimpl()->depthBuffer.assign(64*64,1);else target->setDepthReadbackEnabled(FALSE);
#endif
  auto p=plan();std::vector<uint8_t> image;
  auto render=[&](){auto r=target->getPimpl()->executeFrame(p);if(r.status!=CoinRenderBackendStatus::SUCCESS){std::cerr<<r.diagnostic<<'\n';return false;}
    target->readbackRGBA(image);return image.size()==64*64*4;};
  if(!check(render() && pixels(image,16,40,0,255) && pixels(image,48,40,255,0),"opaque half-plane color/depth"))return false;
  std::vector<float> depth;target->readbackDepth(depth);
  if(!depth.empty() && !check(std::abs(depth[40*64+16]-.75f)<.02f &&
                              std::abs(depth[40*64+48]-.25f)<.02f,
                              "discarded foreground must not write depth"))return false;
  auto & s=p.renderStates[1];s.clipPlanesWorld.push_back(SbPlane(SbVec3f(0,1,0),0));
  ++p.revision;
  if(!check(render() && pixels(image,48,16,255,0) && pixels(image,48,48,0,255),"multiple planes intersect"))return false;
  s.clipPlanesWorld.assign(8,SbPlane(SbVec3f(1,0,0),0));s.clipPlanesWorld[7]=SbPlane(SbVec3f(0,1,0),0);
  ++p.revision;
  if(!check(render() && pixels(image,48,16,255,0) && pixels(image,48,48,0,255),"eighth plane is executed"))return false;
  s.clipPlanesWorld.resize(1);for(auto & state:p.renderStates)state.view.setTranslate(SbVec3f(.25f,0,0));
  ++p.revision;
  if(!check(render() && pixels(image,36,40,0,255) && pixels(image,48,40,255,0),"camera movement keeps world plane"))return false;
  for(auto & state:p.renderStates)state.view.setRotate(SbRotation(SbVec3f(0,0,1),1.570796327f));
  ++p.revision;
  if(!check(render() && pixels(image,40,16,255,0) && pixels(image,40,48,0,255),"rotated camera transforms plane normal"))return false;
  for(auto & state:p.renderStates)state.view=SbMatrix::identity();
  s.model.setScale(SbVec3f(-1,1,1));
  ++p.revision;
  if(!check(render() && pixels(image,16,40,0,255) && pixels(image,48,40,255,0),"mirrored shape does not mirror world plane"))return false;
  s.clipPlanesWorld[0]=SbPlane(SbVec3f(0,0,1),0);
  ++p.revision;
  if(!check(render() && pixels(image,48,40,0,255),"fully clipped foreground does not occlude background"))return false;
  p=plan();p.revision=9;
  SbViewVolume volume;volume.perspective(.785398163f,1,.1f,10);
  SbMatrix affine,projection;volume.getMatrices(affine,projection);
  for(auto & state:p.renderStates)state.projectionCoin=projection;
  for(size_t i=0;i<p.vertices.size();++i)p.vertices[i].position[2]=i<4?-3.0f:-2.0f;
  if(!check(render() && pixels(image,16,40,0,255) && pixels(image,48,40,255,0),"perspective clipping"))return false;
  p=plan(true);p.revision=10;
  if(!check(render() && pixels(image,16,40,0,255) && pixels(image,48,40,128,127),"transparent clipping"))return false;
  p=plan(true);p.revision=11;auto & lit=p.renderStates[1];lit.lightModel=CoinRenderLightModel::PHONG;
  p.lightingStates[0].ambientIntensity=1;lit.hasTexture=true;
  CoinRenderTextureImageSnapshot tex;tex.width=tex.height=1;tex.pixelsRgba.assign(4,255);p.textures.push_back(tex);
  p.samplers.push_back(CoinRenderSamplerSnapshot{});
  if(!check(render() && pixels(image,16,40,0,255) && pixels(image,48,40,128,127),"lit/textured transparent clipping"))return false;
  const auto before=image;const auto serial=target->getLastSubmissionSerial();
  SoSeparator * excessive=new SoSeparator;excessive->ref();
  for(int i=0;i<9;++i)excessive->addChild(new SoClipPlane);
  excessive->addChild(new SoCube);
  CoinRenderAction action(SbViewportRegion(64,64));action.setRenderTarget(target.get());action.apply(excessive);excessive->unref();
  target->readbackRGBA(image);
  if(!check(action.getLastStatus()==CoinRenderAction::UNSUPPORTED && image==before &&
            target->getLastSubmissionSerial()==serial,"plane limit rejects without publication"))return false;
  p.revision=12;if(!check(render(),"valid frame recovers after clipping rejection"))return false;
  return true;
}

SoSeparator * scene() {
  SoSeparator * root=new SoSeparator;root->ref();
  SoOrthographicCamera * cam=new SoOrthographicCamera;cam->height=2;cam->nearDistance=.1f;cam->farDistance=10;
  cam->position=SbVec3f(0,0,3);root->addChild(cam);
  SoLightModel * lm=new SoLightModel;lm->model=SoLightModel::BASE_COLOR;root->addChild(lm);
  for(int layer=0;layer<2;++layer) {
    if(layer==1)root->addChild(new SoClipPlane);
    SoMaterial * material=new SoMaterial;material->diffuseColor=layer?SbColor(1,0,0):SbColor(0,0,1);root->addChild(material);
    SoCoordinate3 * coords=new SoCoordinate3;
    const float z=layer?0:-.5f;const SbVec3f v[]={SbVec3f(-.9f,-.9f,z),SbVec3f(.9f,-.9f,z),SbVec3f(.9f,.9f,z),SbVec3f(-.9f,.9f,z)};
    coords->point.setValues(0,4,v);root->addChild(coords);
    SoIndexedFaceSet * faces=new SoIndexedFaceSet;const int32_t ix[]={0,1,2,3,-1};faces->coordIndex.setValues(0,5,ix);root->addChild(faces);
  }
  return root;
}
bool sceneIntegration(bool cpu) {
  SoSeparator * root=scene();
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(64,64)));
  if(cpu)target->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);
#if defined(HAVE_COIN_BGFX)
  if(cpu)target->getPimpl()->depthBuffer.assign(64*64,1);else target->setDepthReadbackEnabled(FALSE);
#endif
  CoinRenderAction action(SbViewportRegion(64,64));action.setRenderTarget(target.get());
  bool ok=true;for(int fast=0;fast<2;++fast) {
    action.setFastPathEnabled(fast?TRUE:FALSE);action.apply(root);
    std::vector<uint8_t> image;target->readbackRGBA(image);
    ok=check(action.getLastStatus()==CoinRenderAction::SUCCESS && pixels(image,16,40,0,255) &&
             pixels(image,48,40,255,0),"action captures clipping in indexed and callback paths")&&ok;
  }
  root->unref();return ok;
}
bool glReference() {
  SoSeparator * root=scene();SoOffscreenRenderer renderer(SbViewportRegion(64,64));
  renderer.setComponents(SoOffscreenRenderer::RGB);const bool rendered=renderer.render(root);root->unref();
  if(!rendered) {std::cerr<<"[SKIP] Optional Coin/GL clipping reference unavailable\n";return true;}
  const unsigned char * image=renderer.getBuffer();if(!image)return false;
  const size_t left=(40*64+16)*3,right=(40*64+48)*3;
  const bool ok=check(image[left]<6 && image[left+2]>249 && image[right]>249 && image[right+2]<6,
                      "Coin/GL preserves positive half-space");
  if(ok)std::cout<<"Coin/GL reference passed\n";
  return ok;
}
bool strokes() {
  SoSeparator * root=new SoSeparator;root->ref();root->addChild(new SoClipPlane);
  SoCoordinate3 * c=new SoCoordinate3;const SbVec3f xy[]={SbVec3f(-.8f,0,0),SbVec3f(.8f,0,0)};
  c->point.setValues(0,2,xy);root->addChild(c);
  SoLineSet * l=new SoLineSet;l->numVertices=2;root->addChild(l);
  SoPointSet * points=new SoPointSet;points->numPoints=2;root->addChild(points);
  CoinRenderFramePlan p;bool ok=check(capture(root,p),"capture clipped strokes");root->unref();
  if(!ok)return false;
  size_t lineDraws=0,pointDraws=0;
  for(const auto & d:p.draws) {
    const auto & s=p.renderStates[d.renderStateSlot];
    ok=check(s.clipPlanesWorld.empty(),"expanded strokes already clipped")&&ok;
    if(s.polygonOffsetPrimitiveStyle==2) {++lineDraws;
      const float coverageBias = 2.0f / (256.0f * p.viewports[s.viewportSlot].width);
      for(uint32_t i=0;i<d.geometry.vertexCount;++i)
        ok=check(p.vertices[d.geometry.firstVertex+i].position[0]+coverageBias>=-1e-5f,
          "centreline clipped before width expansion and top/left coverage bias")&&ok;
    }
    if(s.polygonOffsetPrimitiveStyle==4) {++pointDraws;ok=check(d.geometry.vertexCount==4,"negative point removed as whole primitive")&&ok;}
  }
  return check(lineDraws==1 && pointDraws==1,"both stroke topologies captured")&&ok;
}
}
int main() {
  SoDB::init();CoinRenderAction::initClass();
  if(!captureContract() || !coreContract() || !strokes() || !gpuContract(true) || !sceneIntegration(true) || !glReference())return 1;
  if(!CoinRenderAction::isGpuBackendAvailable()) {std::cerr<<"[SKIP] GPU adapter unavailable\n";return 77;}
  if(!gpuContract(false) || !sceneIntegration(false))return 1;
  std::cout<<"ClipPlane capture/Core/CPU/GPU contracts passed\n";return 0;
}
