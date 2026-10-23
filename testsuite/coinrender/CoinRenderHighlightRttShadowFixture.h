#ifndef COIN_RENDER_HIGHLIGHT_RTT_SHADOW_FIXTURE_H
#define COIN_RENDER_HIGHLIGHT_RTT_SHADOW_FIXTURE_H
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "CoinRenderBumpProfileFixture.h"
#include "CoinRenderTexture2dStartupTrace.h"
#include "rendering/coinrender/CoinRenderShadowCore.h"
#include <Inventor/SoInteraction.h>
#include <Inventor/SoPath.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/actions/SoBoxHighlightRenderAction.h>
#include <Inventor/actions/SoLineHighlightRenderAction.h>
#include <Inventor/annex/FXViz/nodes/SoShadowGroup.h>
#include <Inventor/annex/FXViz/nodes/SoShadowDirectionalLight.h>
#include <Inventor/annex/FXViz/nodes/SoShadowSpotLight.h>
#include <Inventor/annex/FXViz/nodes/SoShadowStyle.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoDrawStyle.h>
#include <Inventor/nodes/SoSelection.h>
#include <Inventor/nodes/SoSwitch.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/rendering/CoinRenderManagerAdapter.h>
#include <Inventor/rendering/CoinRenderSceneManager.h>
#include <set>
#include <array>
#include <chrono>
#ifdef COIN_HIGHLIGHT_SHADOW_X11
#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#ifdef Status
#undef Status
#endif
#endif
namespace highlight_rtt_shadow {
using Color=std::array<double,3>;
using Frame=bump_profile::Frame;
bool check(bool value,const std::string &message){if(!value)std::cerr<<"highlight_shadow FAIL "<<message<<std::endl;return value;}
struct Config {
  unsigned samples=1,maps=1;bool mips=false,coreQuality=false,producerControl=false,line=false,portable=false,spot=false,core=false,window=false,ordinaryStyle=false,noReceive=false,firstOnly=false,projected=false,coreScope=false;
  CoinRenderRenderer renderer=COIN_RENDER_RENDERER_UNKNOWN;
  CoinRenderSceneTextureMode route=COIN_RENDER_SCENE_TEXTURE_STAGED;
};
template<class Base> struct CountAction:Base {
  unsigned calls=0;
  void apply(SoNode *n)override{++calls;Base::apply(n);}
  void apply(SoPath *p)override{++calls;Base::apply(p);}
  void apply(const SoPathList &p,SbBool obey=FALSE)override{++calls;Base::apply(p,obey);}
};
struct Scene {
  SoSeparator *root=new SoSeparator,*shared=new SoSeparator;
  SoShadowGroup *group=new SoShadowGroup;
  SoSelection *selection=new SoSelection;
  SoIndexedFaceSet *mesh=new SoIndexedFaceSet;
  SoSwitch *foreground=new SoSwitch;
  SoIndexedFaceSet *frontShape=nullptr,*ordinaryShape=nullptr;
  std::vector<SoLight*> lights;
  std::vector<SoOrthographicCamera*> mapCameras;
  std::vector<SoSeparator*> mapRoots;
  std::vector<SoTranslation*> casterMoves;
  std::vector<SbVec3f> casterHome;
  double lookupScale=1;bool projected=false;
  std::vector<SoCube*> casters;
  SoPath *paths[2]{};
  int selected=-1;bool moved=false,front=false;
  explicit Scene(const Config &c) {
    projected=c.projected;lookupScale=c.spot?3*std::tan(.7):std::sqrt(6.);
    root->ref();auto *camera=new SoOrthographicCamera;
    camera->position.setValue(0,0,3);camera->height=2;camera->nearDistance=1;camera->farDistance=5;
    root->addChild(camera);
    auto *phong=new SoLightModel;phong->model=SoLightModel::PHONG;root->addChild(phong);
    group->quality=1;group->precision=.125f;group->epsilon=0;group->threshold=0;group->intensity=1;
    root->addChild(group);
    for(unsigned i=0;i<c.maps;++i) {
      auto *map=new SoSeparator;mapRoots.push_back(map);auto *view=new SoOrthographicCamera;
      view->position.setValue(float(.01*i),0,3);view->height=2;view->nearDistance=1;view->farDistance=float(5+.125*i);
      map->addChild(view);mapCameras.push_back(view);
      auto *cast=new SoShadowStyle;cast->style=SoShadowStyle::CASTS_SHADOW;map->addChild(cast);
      // Source ortho NDC=(worldXY-cameraXY). Receiver lookup at z=0 is
      // NDC=worldXY/K: K=sqrt(6) for the directional fitted sphere, or
      // 3*tan(.7) for the spot frustum. This maps to a .26-square shadow
      // centered at (-.64,-.14), rather than assuming both cameras coincide.
      const SbVec3f home(c.projected?float(.01*i-.64/lookupScale):-.64f,
                         c.projected?float(-.14/lookupScale):-.14f,.75f);
      casterHome.push_back(home);
      auto *move=new SoTranslation;move->translation.setValue(home);map->addChild(move);casterMoves.push_back(move);
      auto *cube=new SoCube;cube->width=cube->height=c.projected?float(.26/lookupScale):.26f;
      cube->depth=.2f;map->addChild(cube);casters.push_back(cube);
      SoLight *light=nullptr;
      if(c.spot) {
        auto *lamp=new SoShadowSpotLight;lamp->location.setValue(0,0,3);lamp->direction.setValue(0,0,-1);
        lamp->cutOffAngle=.7f;lamp->dropOffRate=0;lamp->nearDistance=1;lamp->farDistance=5;
        lamp->shadowMapScene=map;light=lamp;
      } else {
        auto *lamp=new SoShadowDirectionalLight;lamp->direction.setValue(0,0,-1);
        lamp->bboxCenter.setValue(0,0,0);lamp->bboxSize.setValue(2,2,4);lamp->shadowMapScene=map;light=lamp;
      }
      light->intensity=float(.8/c.maps);light->on=TRUE;group->addChild(light);lights.push_back(light);
    }
    auto *style=new SoShadowStyle;style->style=c.noReceive?SoShadowStyle::NO_SHADOWING:SoShadowStyle::SHADOWED;shared->addChild(style);
    auto *material=new SoMaterial;material->diffuseColor.setValue(.8f,.6f,.4f);
    material->ambientColor.setValue(0,0,0);material->emissiveColor.setValue(0,0,0);material->specularColor.setValue(0,0,0);
    material->transparency=0;shared->addChild(material);
    auto *quality=new SoComplexity;quality->textureQuality=.1f;shared->addChild(quality);
    auto *texture=new SoTexture2;const uint8_t texel[]={128,192,64,255};texture->image.setValue(SbVec2s(1,1),4,texel);shared->addChild(texture);
    auto *uv=new SoTextureCoordinate2;const SbVec2f st[]={SbVec2f(.5f,.5f),SbVec2f(.5f,.5f),SbVec2f(.5f,.5f)};uv->point.setValues(0,3,st);shared->addChild(uv);
    auto *normal=new SoNormal;
    const SbVec3f normals[]={SbVec3f(0,0,1),SbVec3f(0,0,1),SbVec3f(0,0,1)};
    normal->vector.setValues(0,3,normals);shared->addChild(normal);
    auto *coordinates=new SoCoordinate3;
    const SbVec3f p[]={SbVec3f(-.36f,-.34f,0),SbVec3f(.36f,-.34f,0),SbVec3f(-.36f,.38f,0)};
    coordinates->point.setValues(0,3,p);shared->addChild(coordinates);
    const int32_t tri[]={0,1,2,-1};mesh->coordIndex.setValues(0,4,tri);shared->addChild(mesh);
    for(unsigned i=0;i<2;++i) {
      auto *branch=new SoSeparator;auto *move=new SoTranslation;move->translation.setValue(i?.48f:-.48f,0,0);
      branch->addChild(move);branch->addChild(shared);selection->addChild(branch);
      paths[i]=new SoPath(selection);paths[i]->ref();paths[i]->append(int(i));paths[i]->append(1);
    }
    group->addChild(selection);
    if(c.ordinaryStyle) {
      auto *ordinary=new SoSeparator;
      auto *style=new SoShadowStyle;style->style=SoShadowStyle::NO_SHADOWING;ordinary->addChild(style);
      auto *base=new SoLightModel;base->model=SoLightModel::BASE_COLOR;ordinary->addChild(base);
      auto *quality=new SoComplexity;quality->textureQuality=0;ordinary->addChild(quality);
      auto *lines=new SoDrawStyle;lines->style=SoDrawStyle::LINES;lines->lineWidth=3;lines->linePattern=0xffff;ordinary->addChild(lines);
      auto *material=new SoMaterial;material->diffuseColor.setValue(.6f,.2f,.9f);ordinary->addChild(material);
      auto *coordinates=new SoCoordinate3;
      const SbVec3f wire[]={SbVec3f(-.16f,.56f,.1f),SbVec3f(.16f,.56f,.1f),SbVec3f(-.16f,.88f,.1f)};
      coordinates->point.setValues(0,3,wire);ordinary->addChild(coordinates);
      ordinaryShape=new SoIndexedFaceSet;const int32_t triangle[]={0,1,2,-1};ordinaryShape->coordIndex.setValues(0,4,triangle);ordinary->addChild(ordinaryShape);
      group->addChild(ordinary);
    }
    // Foreground is outside the shadow group and has explicitly authored color.
    auto *frontRoot=new SoSeparator;auto *base=new SoLightModel;base->model=SoLightModel::BASE_COLOR;frontRoot->addChild(base);
    auto *blue=new SoMaterial;blue->diffuseColor.setValue(.08f,.12f,.88f);frontRoot->addChild(blue);
    auto *fc=new SoCoordinate3;
    const SbVec3f fp[]={SbVec3f(-.43f,-.44f,.35f),SbVec3f(-.08f,-.44f,.35f),SbVec3f(-.08f,.46f,.35f),SbVec3f(-.43f,.46f,.35f)};
    fc->point.setValues(0,4,fp);frontRoot->addChild(fc);
    frontShape=new SoIndexedFaceSet;const int32_t quad[]={0,1,2,-1,0,2,3,-1};frontShape->coordIndex.setValues(0,8,quad);frontRoot->addChild(frontShape);
    foreground->addChild(frontRoot);foreground->whichChild=SO_SWITCH_NONE;root->addChild(foreground);
  }
  ~Scene(){paths[0]->unref();paths[1]->unref();root->unref();}
  void select(int i){selection->deselectAll();selected=i;if(i>=0)selection->select(paths[i]);}
  void moveLast(bool away) {
    moved=away;
    const SbVec3f displaced(projected?float(.01*(casterMoves.size()-1)):0.f,projected?float(.7/lookupScale):.7f,.75f);
    casterMoves.back()->translation.setValue(away?displaced:casterHome.back());
  }
  std::vector<SbUniqueId> stamp()const {
    std::vector<SbUniqueId> result{root->getNodeId(),group->getNodeId(),selection->getNodeId(),mesh->getNodeId()};
    for(auto *n:lights)result.push_back(n->getNodeId());
    for(auto *n:mapCameras)result.push_back(n->getNodeId());
    for(auto *n:casterMoves)result.push_back(n->getNodeId());return result;
  }
};
class BaseAudit:public bump_profile::Proxy {
public:
  using Proxy::Proxy;unsigned roots=0;
  CoinRenderSubmitResult submit(const CoinRenderFramePlan &f,CoinRenderTargetP &t)override{++roots;return Proxy::submit(f,t);}
  CoinRenderSubmitResult submit(const CoinRenderFramePlan &f,CoinRenderTargetP &t,const CoinRenderFrameReuseDecision &r)override{++roots;return Proxy::submit(f,t,r);}
};
bool matrixNear(const SbMatrix &a,const SbMatrix &b) {
  for(unsigned row=0;row<4;++row)for(unsigned column=0;column<4;++column)
    if(std::abs(a[row][column]-b[row][column])>1e-5)return false;
  return true;
}
// Row-vector Coin matrices, derived from the authorial camera and bounding box.
// The custom caster camera is deliberately distinct from the light lookup.
SbMatrix sourceView(unsigned slot) {
  SbMatrix result=SbMatrix::identity();result[3][0]=-float(.01*slot);result[3][2]=-3;return result;
}
SbMatrix sourceProjection(unsigned slot) {
  const float far=float(5+.125*slot);SbMatrix result=SbMatrix::identity();
  result[2][2]=-2/(far-1);result[3][2]=-(far+1)/(far-1);return result;
}
SbMatrix lookupView(const Config &c) {
  SbMatrix result=SbMatrix::identity();result[3][2]=c.spot?-3.f:-float(std::sqrt(6.));return result;
}
SbMatrix lookupProjection(const Config &c) {
  SbMatrix result=SbMatrix::identity();
  if(c.spot) {
    result[0][0]=result[1][1]=float(1/std::tan(.7));result[2][2]=-1.5f;
    result[2][3]=-1;result[3][2]=-2.5f;result[3][3]=0;
  } else {
    const float radius=float(std::sqrt(6.));
    const float near=(radius-2)*1.01f,far=(radius+2)*1.01f;
    result[0][0]=result[1][1]=1/radius;result[2][2]=-2/(far-near);result[3][2]=-(far+near)/(far-near);
  }
  return result;
}
bool delivery(const Scene &scene,const Config &c,const CoinRenderFramePlan &frame,bool stroke) {
  const auto policy=c.portable?COIN_RENDER_SAMPLING_PORTABLE:COIN_RENDER_SAMPLING_NATIVE;
  bool ok=check(frame.textureSamplingPolicy==policy && frame.shadowLights.size()==c.maps && frame.shadowGroups.size()==1,
                "explicit policy and authored map/light/group counts");
  CoinRenderShadowPlan plan;std::string error;
  if(!check(coin_render_plan_shadows(frame,plan,error),"actual Core shadow plan: "+error))return false;
  ok &= check(plan.passes.size()==c.maps,"highlight creates no extra map pass");
  if(frame.shadowGroups.size()==1) {
    const auto &group=frame.shadowGroups.front();
    ok &= check(group.sourceRevision==scene.group->getNodeId() && group.hasEntryCamera &&
      matrixNear(group.entryModel,SbMatrix::identity()) && matrixNear(group.entryCamera.viewMatrix,sourceView(0)) &&
      matrixNear(group.entryCamera.projectionMatrixCoin,sourceProjection(0)) &&
      group.entryCamera.nearDistance==1 && group.entryCamera.farDistance==5,
      "group identity and original entry-camera/model preserved");
  }
  unsigned baseIndices=0,strokeIndices=0,ordinaryIndices=0;
  std::set<unsigned> strokeDraws;
  for(unsigned i=0;i<frame.draws.size();++i) {
    const auto &draw=frame.draws[i];
    if(draw.renderStateSlot>=frame.renderStates.size()){ok=false;continue;}
    const auto &state=frame.renderStates[draw.renderStateSlot];
    // Layer numbers order authored occurrences; generated purpose identifies
    // highlight geometry even when the authored base has a nonzero layer.
    if(!draw.isSceneLayer()) {
      strokeIndices+=draw.geometry.indexCount;strokeDraws.insert(i);
      bool textured=false;for(unsigned unit=0;unit<8;++unit)textured |= coin_render_texture_unit_enabled(state,unit);
      ok &= check(!draw.shadowLightSlot && !textured && state.lightModel==CoinRenderLightModel::BASE_COLOR,
                  "stroke is unlit/untextured main geometry, not a map occurrence");
    } else if(!draw.shadowLightSlot && draw.sourceNodeId==scene.mesh->getNodeId()) {
      baseIndices+=draw.geometry.indexCount;
      for(unsigned corner=0;corner<draw.geometry.indexCount;++corner) {
        const size_t offset=size_t(draw.geometry.firstIndex)+corner;
        if(offset>=frame.indices.size() || frame.indices[offset]>=frame.vertices.size()){ok=false;continue;}
        const auto &vertex=frame.vertices[frame.indices[offset]];
        ok &= check(vertex.normal[0]==0 && vertex.normal[1]==0 && vertex.normal[2]==1,
                    "every shared receiver vertex retains an explicitly authored unit normal");
      }
    }
    else if(scene.ordinaryShape && draw.sourceNodeId==scene.ordinaryShape->getNodeId()) {
      ordinaryIndices+=draw.geometry.indexCount;
      ok &= check(!draw.shadowLightSlot && state.lightModel==CoinRenderLightModel::BASE_COLOR && !state.hasTexture && state.shadowStyle==0,
                  "ordinary authorial BASE_COLOR/quality0/LINES retains its own main occurrence and shadow policy");
    }
  }
  ok &= check((scene.ordinaryShape?ordinaryIndices>0:ordinaryIndices==0),"authorial lookalike is not mistaken for replay");
  ok &= check(baseIndices==6 && (stroke?strokeIndices>0:strokeIndices==0),"two shared base occurrences and expected stroke");
  std::set<unsigned> lightSlots;
  for(const auto &pass:plan.passes) {
    lightSlots.insert(pass.lightSlot);
    if(pass.lightSlot>=c.maps){ok=false;continue;}
    const unsigned slot=pass.lightSlot;
    unsigned casterIndices=0,receiverIndices=0;
    for(unsigned index:pass.casterDraws) {
      if(index>=frame.draws.size()){ok=false;continue;}
      const auto &draw=frame.draws[index];casterIndices+=draw.geometry.indexCount;
      ok &= check(draw.isSceneLayer() && draw.sourceNodeId==scene.casters[slot]->getNodeId() && draw.shadowLightSlot==slot+1 && !strokeDraws.count(index),
                  "map caster belongs only to its authored light/source");
      if(draw.renderStateSlot<frame.renderStates.size()) {
        const auto &state=frame.renderStates[draw.renderStateSlot];
        if(state.cameraSlot>=frame.cameras.size()){ok=false;continue;}
        const auto &camera=frame.cameras[state.cameraSlot];
        ok &= check(state.shadowMapCameraOverride && state.shadowMapSize==256 && !camera.isPerspective &&
          camera.nearDistance==1 && camera.farDistance==float(5+.125*slot) &&
          matrixNear(camera.viewMatrix,sourceView(slot)) && matrixNear(camera.projectionMatrixCoin,sourceProjection(slot)),
          "each caster retains its own raw camera, projection and map viewport");
      }
    }
    for(unsigned index:pass.receiverDraws) {
      if(index>=frame.draws.size()){ok=false;continue;}
      const auto &draw=frame.draws[index];receiverIndices+=draw.geometry.indexCount;
      ok &= check(draw.isSceneLayer() && draw.sourceNodeId==scene.mesh->getNodeId() && !draw.shadowLightSlot && !strokeDraws.count(index),
                  "only original base occurrences receive the map");
      if(draw.renderStateSlot>=pass.lightingIndexByState.size() || draw.renderStateSlot>=frame.renderStates.size()){ok=false;continue;}
      const auto &state=frame.renderStates[draw.renderStateSlot];const auto lightIndex=pass.lightingIndexByState[draw.renderStateSlot];
      if(state.lightingSlot>=frame.lightingStates.size() || lightIndex<0 || size_t(lightIndex)>=frame.lightingStates[state.lightingSlot].lights.size()){ok=false;continue;}
      ok &= check(frame.lightingStates[state.lightingSlot].lights[size_t(lightIndex)].sourceRevision==scene.lights[slot]->getNodeId(),
                  "each receiver associates the map with its actual authorial light");
    }
    ok &= check(pass.visible && pass.groupSlot==1 && pass.mapSize==256 && casterIndices==36 && receiverIndices==(c.noReceive?0u:6u),
                "visible authored map/camera and exact caster/receiver coverage");
    const auto &light=frame.shadowLights[slot];
    ok &= check(light.sourceRevision==scene.lights[slot]->getNodeId() && light.enabled && light.groupSlot==1 &&
      light.hasCustomScene && light.mapSceneCaptured && light.customSceneNodeId==scene.mapRoots[slot]->getNodeId() &&
      matrixNear(light.model,SbMatrix::identity()) && matrixNear(pass.view,lookupView(c)) && matrixNear(pass.projectionCoin,lookupProjection(c)),
      "map identity/model and distinct light-owned lookup camera preserved");
  }
  ok &= check(lightSlots.size()==c.maps,"all authored map slots consumed once");return ok;
}
double segment(double x,double y,double ax,double ay,double bx,double by) {
  const double dx=bx-ax,dy=by-ay,t=std::max(0.,std::min(1.,((x-ax)*dx+(y-ay)*dy)/(dx*dx+dy*dy)));
  return std::hypot(x-ax-t*dx,y-ay-t*dy);
}

} // namespace highlight_rtt_shadow

#endif
