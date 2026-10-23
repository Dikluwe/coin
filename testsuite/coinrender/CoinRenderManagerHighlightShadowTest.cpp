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
namespace highlight_shadow {
using Color=std::array<double,3>;
using Frame=bump_profile::Frame;
bool check(bool value,const std::string &message){if(!value)std::cerr<<"highlight_shadow FAIL "<<message<<std::endl;return value;}
struct Config {
  unsigned samples=1,maps=1;bool line=false,portable=false,spot=false,core=false,window=false,ordinaryStyle=false,noReceive=false,firstOnly=false,projected=false,coreScope=false;
  CoinRenderRenderer renderer=COIN_RENDER_RENDERER_UNKNOWN;
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
class Audit:public bump_profile::Proxy {
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
struct Metrics {unsigned lit=0,dark=0,stroke=0,box=0,line=0,outside=0,front=0,authored=0,bad=0,depthBad=0;};
Metrics image(const Scene &scene,const Config &c,const Frame &frame,const Frame &reference,unsigned side,
              bool visible,float width,uint16_t pattern,const Color &color,bool report=true,bool withDepth=true) {
  Metrics m;
  if(frame.rgba.size()!=size_t(side)*side*4 || reference.rgba.size()!=frame.rgba.size() || (withDepth&&frame.depth.size()!=size_t(side)*side) || reference.depth.size()!=size_t(side)*side){m.bad=1;return m;}
  const double pixel=2./side,margin=2.1*pixel,band=(width*.5+2.1)*pixel;
  const bool active=visible&&scene.selected>=0&&pattern;
  struct Sample {unsigned x,y;bool dark;Color actual,reference,expected;};
  std::vector<Sample> samples;unsigned sampleCounts[2]={0,0};
  Color minimum[2]={{{255,255,255}},{{255,255,255}}},maximum[2]={{{0,0,0}},{{0,0,0}}};
  Color referenceMin[2]={{{255,255,255}},{{255,255,255}}},referenceMax[2]={{{0,0,0}},{{0,0,0}}};
  for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x) {
    const size_t p=size_t(y)*side+x;const double wx=(x+.5)*pixel-1,wy=1-(y+.5)*pixel,lx=wx-(scene.selected==1?.48:-.48);
    double contour=1e9;
    if(active) {
      contour=std::min({segment(lx,wy,-.36,-.34,.36,-.34),segment(lx,wy,-.36,.38,-.36,-.34),
        c.line?segment(lx,wy,.36,-.34,-.36,.38):segment(lx,wy,.36,-.34,.36,.38)});
      if(!c.line)contour=std::min(contour,segment(lx,wy,-.36,.38,.36,.38));
    }
    if(scene.ordinaryShape) {
      const double wire=std::min({segment(wx,wy,-.16,.56,.16,.56),segment(wx,wy,.16,.56,-.16,.88),segment(wx,wy,-.16,.88,-.16,.56)});
      const Color author{{.6,.2,.9}};bool pure=true;
      for(unsigned k=0;k<3;++k)pure &= std::abs(frame.rgba[4*p+k]-255*author[k])<=3;
      if(pure){++m.authored;m.bad+=wire>(1.5+2.1)*pixel;m.depthBad+=std::abs(reference.depth[p]-.475)>2e-5;if(withDepth)m.depthBad+=std::abs(frame.depth[p]-.475)>2e-5;}
      if(wire<=(1.5+2.1)*pixel)continue;
    }
    const bool front=scene.front&&wx>-.43+margin&&wx<-.08-margin&&wy>-.44+margin&&wy<.46-margin;
    bool full=true;for(unsigned k=0;k<3;++k)full &= std::abs(frame.rgba[4*p+k]-255*color[k])<=3;
    if(full) {
      ++m.stroke;m.bad+=!active||contour>band||front;
      if(std::abs(lx)<.17&&std::abs(wy-.38)<band)++m.box;
      if(std::abs(lx)<.17&&std::abs(lx+wy-.02)/std::sqrt(2.)<band)++m.line;
    }
    const bool nearFront=scene.front&&wx>-.43-margin&&wx<-.08+margin&&wy>-.44-margin&&wy<.46+margin;
    if(front) {
      ++m.front;const Color blue{{.08,.12,.88}};
      for(unsigned k=0;k<3;++k)m.bad+=std::abs(frame.rgba[4*p+k]-255*blue[k])>3;
      m.depthBad+=std::abs(reference.depth[p]-.4125)>2e-5;if(withDepth)m.depthBad+=std::abs(frame.depth[p]-.4125)>2e-5;continue;
    }
    if(nearFront||(active&&contour<=band))continue;
    bool outside=true;
    for(unsigned occurrence=0;occurrence<2;++occurrence) {
      const double local=wx-(occurrence?.48:-.48);
      const double edge=std::min({local+.36,wy+.34,(.02-local-wy)/std::sqrt(2.)});
      outside &= edge < -margin;
      if(edge<=margin)continue;
      for(unsigned k=0;k<3;++k)m.bad+=std::abs(int(frame.rgba[4*p+k])-int(reference.rgba[4*p+k]))>3;
      m.bad+=frame.rgba[4*p+3]!=255;m.depthBad+=std::abs(reference.depth[p]-.5)>2e-5;if(withDepth)m.depthBad+=std::abs(frame.depth[p]-.5)>2e-5;
      const bool dark=occurrence==0&&std::abs(wx+.64)<.13-margin&&std::abs(wy+.14)<.13-margin;
      const bool lit=occurrence==1;
      if(!dark&&!lit)continue;
      if(dark)++m.dark;else ++m.lit;
      // All maps have the same world-space silhouette. Moving the last caster
      // admits exactly its own .8/N light contribution, not all the other maps.
      const double visibility=(dark&&c.projected&&!c.noReceive)?(scene.moved?1./c.maps:0.):1.;
      const Color diffuse{{.8,.6,.4}},texel{{128./255,192./255,64./255}};
      Sample diagnostic;diagnostic.x=x;diagnostic.y=y;diagnostic.dark=dark;
      for(unsigned k=0;k<3;++k) {
        const double expected=255*diffuse[k]*texel[k]*.8*visibility;
        const unsigned roi=dark?1:0;
        diagnostic.actual[k]=frame.rgba[4*p+k];diagnostic.reference[k]=reference.rgba[4*p+k];diagnostic.expected[k]=expected;
        minimum[roi][k]=std::min(minimum[roi][k],diagnostic.actual[k]);maximum[roi][k]=std::max(maximum[roi][k],diagnostic.actual[k]);
        referenceMin[roi][k]=std::min(referenceMin[roi][k],diagnostic.reference[k]);referenceMax[roi][k]=std::max(referenceMax[roi][k],diagnostic.reference[k]);
        // Directional lighting is exactly constant; spot is independently
        // checked by the ordinary control plus its finite Lambert bounds.
        if(!c.spot)m.bad+=std::abs(frame.rgba[4*p+k]-expected)>3;
        else m.bad+=frame.rgba[4*p+k]+3<expected*.9 || frame.rgba[4*p+k]>expected+3;
      }
      if(sampleCounts[dark?1:0]++<4)samples.push_back(diagnostic);
    }
    if(outside){++m.outside;for(unsigned k=0;k<3;++k)m.bad+=frame.rgba[4*p+k]>3;m.depthBad+=reference.depth[p]!=1;if(withDepth)m.depthBad+=frame.depth[p]!=1;}
  }
  if(report)std::cout<<"highlight_shadow_image lit="<<m.lit<<" dark="<<m.dark<<" stroke="<<m.stroke
    <<" box="<<m.box<<" line="<<m.line<<" authored="<<m.authored<<" front="<<m.front<<" outside="<<m.outside<<" bad="<<m.bad<<" depth_bad="<<m.depthBad<<std::endl;
  if(report && (m.bad||m.depthBad)) {
    for(unsigned roi=0;roi<2;++roi) {
      std::cerr<<"highlight_shadow_roi "<<(roi?"dark":"lit")<<" samples="<<sampleCounts[roi];
      for(unsigned k=0;k<3;++k)std::cerr<<" channel"<<k<<" actual="<<minimum[roi][k]<<".."<<maximum[roi][k]
        <<" reference="<<referenceMin[roi][k]<<".."<<referenceMax[roi][k];
      std::cerr<<std::endl;
    }
    for(const auto &sample:samples) {
      std::cerr<<"highlight_shadow_pixel "<<(sample.dark?"dark":"lit")<<" xy="<<sample.x<<","<<sample.y;
      for(unsigned k=0;k<3;++k)std::cerr<<" channel"<<k<<" actual="<<sample.actual[k]<<" reference="<<sample.reference[k]<<" expected="<<sample.expected[k];
      std::cerr<<std::endl;
    }
  }
  return m;
}
class Inspector:public CoinRenderBackend {
public:
  CoinRenderFramePlan frame;unsigned calls=0;std::string error="independent capture stops before GPU";
  bool isGpuBackend()const override{return true;}bool supportsOffscreenShadows()const override{return true;}
  CoinRenderBackendStatus getStatus()const override{return CoinRenderBackendStatus::SUCCESS;}
  CoinRenderBackendStatus prepare(CoinRenderTargetP&)override{return CoinRenderBackendStatus::SUCCESS;}
  CoinRenderSubmitResult submit(const CoinRenderFramePlan &f,CoinRenderTargetP&)override{frame=f;++calls;return {CoinRenderBackendStatus::UNSUPPORTED,error};}
  void poll()override{}const std::string &getLastError()const override{return error;}
};
void diagnosticLighting(const Scene &scene,const CoinRenderFramePlan &frame,const char *label) {
  for(unsigned ordinal=0;ordinal<frame.draws.size();++ordinal) {
    const auto &draw=frame.draws[ordinal];
    if(draw.shadowLightSlot || draw.sourceNodeId!=scene.mesh->getNodeId() || draw.renderStateSlot>=frame.renderStates.size())continue;
    const auto &state=frame.renderStates[draw.renderStateSlot];
    std::cerr<<"highlight_shadow_input "<<label<<" draw="<<ordinal<<" source="<<draw.sourceNodeId
      <<" state="<<draw.renderStateSlot<<" light_model="<<int(state.lightModel)<<" lighting_slot="<<state.lightingSlot
      <<" shadow_group="<<state.shadowGroupSlot<<" shadow_style="<<state.shadowStyle<<" has_texture="<<state.hasTexture
      <<" material_slot="<<state.materialSlot<<std::endl;
    if(state.lightingSlot<frame.lightingStates.size()) {
      const auto &lighting=frame.lightingStates[state.lightingSlot];
      std::cerr<<"highlight_shadow_ambient intensity="<<lighting.ambientIntensity<<" rgb="<<lighting.ambientColor[0]<<","<<lighting.ambientColor[1]<<","<<lighting.ambientColor[2]<<" lights="<<lighting.lights.size()<<std::endl;
      for(unsigned index=0;index<lighting.lights.size();++index) {
        const auto &light=lighting.lights[index];
        std::cerr<<"highlight_shadow_light index="<<index<<" source="<<light.sourceRevision<<" type="<<int(light.type)
          <<" intensity="<<light.intensity<<" color="<<light.color[0]<<","<<light.color[1]<<","<<light.color[2]
          <<" direction="<<light.direction[0]<<","<<light.direction[1]<<","<<light.direction[2]
          <<" position="<<light.position[0]<<","<<light.position[1]<<","<<light.position[2]<<std::endl;
      }
    }
    for(unsigned corner=0;corner<std::min(3u,draw.geometry.indexCount);++corner) {
      const auto offset=size_t(draw.geometry.firstIndex)+corner;if(offset>=frame.indices.size())continue;
      const auto index=frame.indices[offset];if(index>=frame.vertices.size())continue;const auto &vertex=frame.vertices[index];
      std::cerr<<"highlight_shadow_vertex index="<<index<<" normal="<<vertex.normal[0]<<","<<vertex.normal[1]<<","<<vertex.normal[2]<<" material="<<vertex.materialSlot;
      if(vertex.materialSlot<frame.materials.size()) {
        const auto &material=frame.materials[vertex.materialSlot];
        std::cerr<<" diffuse="<<material.diffuse[0]<<","<<material.diffuse[1]<<","<<material.diffuse[2]<<","<<material.diffuse[3];
      }
      std::cerr<<std::endl;
    }
  }
}
template<class Highlight> struct Run {
  Config config;Scene scene;Highlight highlight;SoRenderManager manager;
  std::unique_ptr<CoinRenderManagerAdapter> adapter;CoinRenderTarget *target=nullptr;
  Audit *audit=nullptr;std::unique_ptr<CoinRenderTarget> referenceTarget;
  CoinRenderAction referenceAction{SbViewportRegion(96,96)};Audit *referenceAudit=nullptr;
  #ifdef COIN_HIGHLIGHT_SHADOW_X11
  Display *display=nullptr;Window window=0;
#endif
  std::vector<uint8_t> stablePhysical;
  unsigned side=96;bool visible=false;float width=3;uint16_t pattern=0xffff;Color color{{1,0,1}};Frame stable;
  explicit Run(Config c):config(c),scene(c) {
    manager.setViewportRegion(SbViewportRegion(side,side));manager.setAutoClipping(SoRenderManager::NO_AUTO_CLIPPING);
    manager.setStereoMode(SoRenderManager::MONO);manager.setRenderMode(SoRenderManager::AS_IS);
    manager.setBackgroundColor(SbColor4f(0,0,0,1));manager.setSceneGraph(scene.root);manager.setGLRenderAction(&highlight);
    highlight.setTransparencyType(SoGLRenderAction::NONE);style(false,3,0xffff,{{1,0,1}});
    auto options=samplingTestOptions();options.sampleCount=c.samples;options.renderer=c.renderer;
    options.textureSamplingPolicy=c.portable?COIN_RENDER_SAMPLING_PORTABLE:COIN_RENDER_SAMPLING_NATIVE;
#ifdef COIN_HIGHLIGHT_SHADOW_X11
    if(c.window) {
      display=XOpenDisplay(nullptr);if(!display)return;
      window=XCreateSimpleWindow(display,DefaultRootWindow(display),16,16,side,side,0,0,0);
      XSetWindowAttributes attributes{};attributes.override_redirect=True;
      XChangeWindowAttributes(display,window,CWOverrideRedirect,&attributes);XMapRaised(display,window);XSync(display,False);
      CoinRenderNativeSurfaceDescriptor surface{};surface.abiVersion=COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
      surface.structSize=sizeof(surface);surface.type=COIN_RENDER_SURFACE_XLIB;
      surface.native.xlib.display=display;surface.native.xlib.window=window;
      adapter.reset(new CoinRenderManagerAdapter(manager,surface,SbVec2i32(side,side),options));
    } else
#endif
      adapter.reset(new CoinRenderManagerAdapter(manager,SbVec2i32(side,side),options));
    target=adapter->getSceneManager()->getRenderTarget();
  }
  ~Run(){
    referenceAction.setRenderTarget(nullptr);referenceTarget.reset();adapter.reset();manager.setGLRenderAction(nullptr);manager.setSceneGraph(nullptr);
#ifdef COIN_HIGHLIGHT_SHADOW_X11
    if(window)XDestroyWindow(display,window);if(display)XCloseDisplay(display);
#endif
  }
  void style(bool show,float w,uint16_t bits,const Color &rgb){visible=show;width=w;pattern=bits;color=rgb;highlight.setVisible(show);highlight.setLineWidth(w);highlight.setLinePattern(bits);highlight.setColor(SbColor(float(rgb[0]),float(rgb[1]),float(rgb[2])));}
  bool prepare() {
    if(!target)return false;auto &p=target->getPimpl().get();
    if(p.prepareBackend()!=CoinRenderBackendStatus::SUCCESS||!p.backend||!p.backend->isGpuBackend())return false;
    audit=new Audit(std::move(p.backend));p.backend.reset(audit);if(!config.window)target->setDepthReadbackEnabled(TRUE);
    const auto policy=config.portable?COIN_RENDER_SAMPLING_PORTABLE:COIN_RENDER_SAMPLING_NATIVE;
    if(!check(target->getOptions().sampleCount==config.samples&&target->getOptions().textureSamplingPolicy==policy,"real target samples and sampling policy"))return false;
    referenceTarget.reset(CoinRenderTarget::createOffscreen(SbVec2i32(side,side),target->getOptions()));
    if(!referenceTarget)return false;auto &r=referenceTarget->getPimpl().get();
    if(r.prepareBackend()!=CoinRenderBackendStatus::SUCCESS||!r.backend||!r.backend->isGpuBackend())return false;
    referenceAudit=new Audit(std::move(r.backend));r.backend.reset(referenceAudit);referenceTarget->setDepthReadbackEnabled(TRUE);
    referenceAction.setRenderTarget(referenceTarget.get());referenceAction.setTransparencyType(CoinRenderAction::NONE);referenceAction.setBackgroundColor(SbColor4f(0,0,0,1));return true;
  }
  Frame read()const{Frame f;target->readbackRGBA(f.rgba);target->readbackDepth(f.depth);f.serial=target->getLastSubmissionSerial();return f;}
  bool draw(const char *label,Metrics *metrics=nullptr) {
    const auto stamp=scene.stamp();const unsigned roots=audit->roots;const uint64_t previous=target->getLastSubmissionSerial();
    referenceAction.apply(scene.root);Frame reference;referenceTarget->readbackRGBA(reference.rgba);referenceTarget->readbackDepth(reference.depth);
    if(!check(referenceAction.getLastStatus()==CoinRenderAction::SUCCESS,"ordinary equivalent shadow control: "+std::string(referenceAction.getLastError().getString())))return false;
    if(config.window&&!check(target->requestWindowReadbackRGBA(),"explicit same-apply window capture request"))return false;
    const auto status=adapter->render();const Frame current=read();
    std::cout<<"highlight_shadow "<<label<<" status="<<status<<" serial="<<previous<<"->"<<current.serial
      <<" root="<<audit->roots-roots<<" error="<<adapter->getLastError().getString()<<std::endl;
    bool ok=check(status==CoinRenderAction::SUCCESS&&current.serial>previous&&audit->roots==roots+1,"one successful current main publication");
    if(!ok)return false;
    ok &= check(scene.stamp()==stamp&&highlight.calls==0,"source/selection unchanged, no GL action fallback");
    ok &= delivery(scene,config,referenceAudit->plan,false);
    ok &= delivery(scene,config,audit->plan,visible&&scene.selected>=0&&pattern);
    const auto m=image(scene,config,current,reference,side,visible,width,pattern,color,true,!config.window);
    if(m.bad||m.depthBad || config.noReceive) {
      diagnosticLighting(scene,audit->plan,"manager");diagnosticLighting(scene,referenceAudit->plan,"ordinary_control");
      CoinRenderShadowPlan diagnostic;std::string message;
      if(coin_render_plan_shadows(audit->plan,diagnostic,message))for(const auto &pass:diagnostic.passes) {
        std::cerr<<"highlight_shadow_map slot="<<pass.lightSlot<<" group="<<pass.groupSlot<<" visible="<<pass.visible
          <<" size="<<pass.mapSize<<" near="<<pass.nearDistance<<" far="<<pass.farDistance<<" vsm_far="<<pass.vsmFarDistance
          <<" epsilon="<<pass.epsilon<<" threshold="<<pass.threshold<<" casters="<<pass.casterDraws.size()<<" receivers="<<pass.receiverDraws.size()<<std::endl;
        for(unsigned row=0;row<4;++row) {
          std::cerr<<"highlight_shadow_map_matrix row="<<row;
          for(unsigned col=0;col<4;++col)std::cerr<<" view"<<col<<"="<<pass.view[row][col]<<" projection"<<col<<"="<<pass.projectionCoin[row][col];
          std::cerr<<std::endl;
        }
      }
    }
    ok &= check(!m.bad&&!m.depthBad&&m.lit>30&&m.dark>8&&m.outside>500&&(!scene.front||m.front>50)&&(!scene.ordinaryShape||m.authored>4)&&
      (!(visible&&scene.selected>=0&&pattern)||(m.stroke>8&&(config.line?m.line>4&&m.box==0:m.box>4&&m.line==0))),
      "independent lit/dark, ordinary base equivalence, stroke and depth witnesses");
    if(ok&&config.window)ok &= observe(current,reference);
    if(metrics)*metrics=m;if(ok)stable=current;return ok;
  }
  bool refuse(const char *label) {
    const auto stamp=scene.stamp();const auto roots=audit->roots,producers=audit->producers;
    const auto status=adapter->render();const auto current=read();
    std::cout<<"highlight_shadow_refuse "<<label<<" status="<<status<<" error="<<adapter->getLastError().getString()<<std::endl;
    return check(status!=CoinRenderAction::SUCCESS&&audit->roots==roots&&audit->producers==producers&&
      current.serial==stable.serial&&current.rgba==stable.rgba&&current.depth==stable.depth&&scene.stamp()==stamp&&highlight.calls==0,
      "invalid preflight preserves exact publication with zero root/RTT execution") &&
      (!config.window || check(physical()==stablePhysical,"refusal preserves complete physical image exactly"));
  }
  bool refuseConsumedSlot() {
    CoinRenderFramePlan packet=audit->plan;packet.revision=0; // Edited IR has no immutable source revision.
    CoinRenderShadowPlan shadow;std::string diagnostic;
    if(!check(coin_render_plan_shadows(packet,shadow,diagnostic)&&!shadow.passes.empty()&&!shadow.passes.back().casterDraws.empty(),
              "private invalid slot starts from a valid consumed last-map caster"))return false;
    const auto index=shadow.passes.back().casterDraws.front();
    if(index>=packet.draws.size())return check(false,"consumed private caster index");
    packet.draws[index].renderStateSlot=uint32_t(packet.renderStates.size());
    const unsigned roots=audit->roots,producers=audit->producers;const auto stamp=scene.stamp();
    const auto result=target->getPimpl().get().executeFrame(packet);const auto current=read();
    std::cout<<"highlight_shadow_private_refuse status="<<int(result.status)<<" diagnostic="<<result.diagnostic<<std::endl;
    return check(result.status!=CoinRenderBackendStatus::SUCCESS&&!result.diagnostic.empty()&&audit->roots==roots&&
      audit->producers==producers&&current.serial==stable.serial&&current.rgba==stable.rgba&&current.depth==stable.depth&&
      scene.stamp()==stamp&&(!config.window||physical()==stablePhysical),
      "consumed last-map slot refused before connector with exact public/physical preservation");
  }
  std::vector<uint8_t> physical()const {
    std::vector<uint8_t> bytes;
#ifdef COIN_HIGHLIGHT_SHADOW_X11
    XSync(display,False);XWindowAttributes attributes{};
    if(!XGetWindowAttributes(display,window,&attributes)||attributes.width!=int(side)||attributes.height!=int(side))return bytes;
    XImage *capture=XGetImage(display,window,0,0,side,side,AllPlanes,ZPixmap);if(!capture)return bytes;
    bytes.resize(size_t(side)*side*4);
    auto channel=[](unsigned long pixel,unsigned long mask) {
      if(!mask)return uint8_t(0);unsigned shift=0;while(!(mask&1)){mask>>=1;++shift;}
      return uint8_t(((((pixel>>shift)&mask)*255)+mask/2)/mask);
    };
    for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x) {
      const auto pixel=XGetPixel(capture,x,y);const size_t p=4*(size_t(y)*side+x);
      bytes[p]=channel(pixel,capture->red_mask);bytes[p+1]=channel(pixel,capture->green_mask);
      bytes[p+2]=channel(pixel,capture->blue_mask);bytes[p+3]=255;
    }
    XDestroyImage(capture);
#endif
    return bytes;
  }
  bool observe(const Frame &current,const Frame &reference) {
    const auto start=std::chrono::steady_clock::now(),deadline=start+std::chrono::seconds(5);
    unsigned attempts=0,maxError=0;
    do {
      Frame shown;shown.rgba=physical();++attempts;bool agrees=shown.rgba.size()==current.rgba.size();maxError=0;
      if(agrees)for(size_t p=0;p<current.rgba.size();p+=4)for(unsigned k=0;k<3;++k) {
        const unsigned error=unsigned(std::abs(int(shown.rgba[p+k])-int(current.rgba[p+k])));
        maxError=std::max(maxError,error);agrees &= error<=3;
      }
      if(agrees) {
        const auto m=image(scene,config,shown,reference,side,visible,width,pattern,color,false,false);
        if(!m.bad&&!m.depthBad&&m.lit>30&&m.dark>8&&m.outside>500&&(!scene.front||m.front>50)&&(!scene.ordinaryShape||m.authored>4)&&
           (!(visible&&scene.selected>=0&&pattern)||(m.stroke>8&&(config.line?m.line>4&&m.box==0:m.box>4&&m.line==0)))) {
          stablePhysical=std::move(shown.rgba);
          std::cout<<"highlight_shadow_present attempts="<<attempts<<" max_rgb="<<maxError
            <<" present_ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<std::endl;
          return true;
        }
      }
    }while(std::chrono::steady_clock::now()<deadline);
    std::cerr<<"highlight_shadow_present timeout attempts="<<attempts<<" last_max_rgb="<<maxError<<std::endl;return false;
  }
  bool resize(unsigned extent) {
#ifdef COIN_HIGHLIGHT_SHADOW_X11
    if(config.window){XResizeWindow(display,window,extent,extent);XSync(display,False);}
#endif
    side=extent;manager.setViewportRegion(SbViewportRegion(short(side),short(side)));
    referenceAction.setViewportRegion(SbViewportRegion(short(side),short(side)));
    return check(adapter->resize(SbVec2i32(side,side))&&referenceTarget->resize(SbVec2i32(side,side)),"same targets framebuffer resize");
  }
  bool exercise() {
    if(!config.projected&&!config.noReceive)std::cout<<"highlight_shadow_original diagnostic_only=1 shadow_roi_not_present=1"<<std::endl;
    if(!draw(config.noReceive?"PHONG no-RECEIVE lighting control":"ordinary shadow predecessor"))return false;
    if(config.firstOnly)return true;
    scene.select(0);style(true,3,0xffff,{{1,0,1}});Metrics solid,wide,stipple;
    if(!draw("first selected occurrence",&solid))return false;
    scene.moveLast(true);if(!draw("last map caster mutation"))return false;
    scene.moveLast(false);if(!draw("last map restoration"))return false;
    style(true,3,0xffff,{{0,1,1}});if(!draw("color mutation"))return false;
    style(true,3,0xffff,{{1,0,1}});if(!draw("color restoration"))return false;
    style(true,5,0xffff,{{1,0,1}});if(!draw("width mutation",&wide)||!check(wide.stroke>solid.stroke,"larger authored stroke coverage"))return false;
    style(true,3,0xffff,{{1,0,1}});if(!draw("width restoration"))return false;
    style(true,3,0x00ff,{{1,0,1}});if(!draw("pattern mutation",&stipple)||!check(stipple.stroke<solid.stroke,"stipple removes authored fragments"))return false;
    style(true,3,0,{{1,0,1}});if(!draw("zero pattern"))return false;
    style(true,3,0xffff,{{1,0,1}});if(!draw("pattern restoration"))return false;
    scene.select(1);if(!draw("other shared occurrence selected"))return false;
    scene.select(0);if(!draw("selection restoration"))return false;
    scene.select(-1);if(!draw("deselected base"))return false;
    scene.select(0);style(false,3,0xffff,{{1,0,1}});if(!draw("invisible highlight"))return false;
    style(true,3,0xffff,{{1,0,1}});if(!draw("visible restoration"))return false;
    scene.front=true;scene.foreground->whichChild=SO_SWITCH_ALL;if(!draw("foreground occlusion"))return false;
    scene.front=false;scene.foreground->whichChild=SO_SWITCH_NONE;if(!draw("foreground restoration"))return false;
    if(!refuseConsumedSlot()||!draw("consumed slot recovery"))return false;
    style(true,std::numeric_limits<float>::quiet_NaN(),0xffff,{{1,0,1}});if(!refuse("nonfinite active style"))return false;
    style(true,3,0xffff,{{1,0,1}});if(!draw("style recovery"))return false;
    scene.mapCameras.back()->farDistance=0;if(!refuse("invalid last map camera"))return false;
    scene.mapCameras.back()->farDistance=float(5+.125*(config.maps-1));if(!draw("map camera recovery"))return false;
    scene.lights.back()->intensity=std::numeric_limits<float>::quiet_NaN();if(!refuse("invalid last light"))return false;
    scene.lights.back()->intensity=float(.8/config.maps);if(!draw("light recovery"))return false;
    if(config.window)for(unsigned extent:{129u,160u,96u})if(!resize(extent)||!draw("resized current frame"))return false;
    return true;
  }
};
template<class H> bool core(Config c) {
  Run<H> run(c);auto *inspector=new Inspector;run.target->getPimpl().get().backend.reset(inspector);
  run.scene.select(0);run.style(true,3,0xffff,{{1,0,1}});const auto stamp=run.scene.stamp();
  const auto status=run.adapter->render();
  std::cout<<"highlight_shadow_core status="<<status<<" captures="<<inspector->calls<<" error="<<run.adapter->getLastError().getString()<<std::endl;
  return check(status==CoinRenderAction::UNSUPPORTED&&inspector->calls==1&&run.scene.stamp()==stamp&&run.highlight.calls==0,"actual manager capture reaches CPU inspection")&&delivery(run.scene,c,inspector->frame,true);
}
// Public manager ordering is scene -> highlight -> foreground. A real shadow
// group in that foreground proves that the special purpose has ended in the
// same capture, rather than merely being reset at the next render call.
template<class H> bool coreScope(Config c) {
  c.samples=1;c.maps=1;c.projected=true;c.window=false;
  Run<H> run(c);Scene after(c);
  if(c.ordinaryStyle) {
    // These are ordinary authored entry states, not a highlight marker. The
    // group's children explicitly choose their own PHONG/FILLED receiver.
    auto *entryColor=new SoLightModel;entryColor->model=SoLightModel::BASE_COLOR;
    auto *entryQuality=new SoComplexity;entryQuality->textureQuality=0;
    auto *entryLines=new SoDrawStyle;entryLines->style=SoDrawStyle::LINES;
    after.root->insertChild(entryColor,2);after.root->insertChild(entryQuality,3);after.root->insertChild(entryLines,4);
    auto *phong=new SoLightModel;phong->model=SoLightModel::PHONG;
    auto *filled=new SoDrawStyle;filled->style=SoDrawStyle::FILLED;
    auto *quality=new SoComplexity;quality->textureQuality=.1f;
    after.group->insertChild(phong,0);after.group->insertChild(filled,1);after.group->insertChild(quality,2);
    // The separate custom map inherits the group's entry LINES state; the
    // group's child FILLED node is not an authorial reset inside that map.
    auto *mapFilled=new SoDrawStyle;mapFilled->style=SoDrawStyle::FILLED;
    after.mapRoots[0]->insertChild(mapFilled,0);
  }
  auto *layer=run.manager.addSuperimposition(after.root,
    SoRenderManager::Superimposition::AUTOREDRAW|SoRenderManager::Superimposition::ZBUFFERON);
  auto *inspector=new Inspector;run.target->getPimpl().get().backend.reset(inspector);
  run.scene.select(0);
  auto capture=[&](bool visible,const char *label) {
    run.style(visible,3,0xffff,{{1,0,1}});
    const auto before=inspector->calls;const auto mainStamp=run.scene.stamp(),afterStamp=after.stamp();
    const auto status=run.adapter->render();const auto &frame=inspector->frame;
    std::cout<<"highlight_shadow_scope "<<label<<" status="<<status<<" captures="<<inspector->calls-before
      <<" groups="<<frame.shadowGroups.size()<<" lights="<<frame.shadowLights.size()
      <<" error="<<run.adapter->getLastError().getString()<<std::endl;
    bool ok=check(status==CoinRenderAction::UNSUPPORTED&&inspector->calls==before+1&&run.highlight.calls==0&&
      run.target->getLastSubmissionSerial()==0&&run.scene.stamp()==mainStamp&&after.stamp()==afterStamp,
      "one actual CPU composition capture, no device/publication/source mutation");
    ok &= check(frame.textureSamplingPolicy==(c.portable?COIN_RENDER_SAMPLING_PORTABLE:COIN_RENDER_SAMPLING_NATIVE)&&
      frame.shadowGroups.size()==2&&frame.shadowLights.size()==2,
      "foreground retains its own ordinary shadow group/light after highlight capture");
    CoinRenderShadowPlan plan;std::string diagnostic;
    if(!check(coin_render_plan_shadows(frame,plan,diagnostic),"composed scope Core plan: "+diagnostic))return false;
    ok &= check(plan.passes.size()==2,"one main and one later authorial map, without replay duplication");
    std::set<unsigned> seenGroups,seenLights;unsigned laterLayer=0,mainLayer=0;
    for(const auto &pass:plan.passes) {
      if(!pass.groupSlot||pass.groupSlot>frame.shadowGroups.size()||pass.lightSlot>=frame.shadowLights.size()){ok=false;continue;}
      const auto &group=frame.shadowGroups[pass.groupSlot-1];const auto &light=frame.shadowLights[pass.lightSlot];
      const Scene *owner=nullptr;
      if(group.sourceRevision==run.scene.group->getNodeId())owner=&run.scene;
      else if(group.sourceRevision==after.group->getNodeId())owner=&after;
      if(!check(owner!=nullptr,"map belongs to one of the two real authorial groups")){ok=false;continue;}
      seenGroups.insert(pass.groupSlot);seenLights.insert(pass.lightSlot);
      ok &= check(pass.visible&&pass.mapSize==256&&group.hasEntryCamera&&group.parentGroupSlot==0&&
        light.sourceRevision==owner->lights[0]->getNodeId()&&light.customSceneNodeId==owner->mapRoots[0]->getNodeId()&&
        light.enabled&&light.shadowEligible&&light.groupSlot==pass.groupSlot,
        "post-highlight map preserves independent group, entry camera, light and custom scene identity");
      unsigned casterIndices=0,receiverIndices=0;
      for(unsigned index:pass.casterDraws) {
        if(index>=frame.draws.size()){ok=false;continue;}const auto &draw=frame.draws[index];casterIndices+=draw.geometry.indexCount;
        ok &= check(draw.sourceNodeId==owner->casters[0]->getNodeId()&&draw.shadowLightSlot==pass.lightSlot+1,
                    "later group's authored caster is captured in its own map");
      }
      for(unsigned index:pass.receiverDraws) {
        if(index>=frame.draws.size()){ok=false;continue;}const auto &draw=frame.draws[index];receiverIndices+=draw.geometry.indexCount;
        if(draw.renderStateSlot>=frame.renderStates.size()){ok=false;continue;}const auto &state=frame.renderStates[draw.renderStateSlot];
        ok &= check(draw.sourceNodeId==owner->mesh->getNodeId()&&!draw.shadowLightSlot&&state.shadowGroupSlot==pass.groupSlot&&
          state.shadowStyle==2&&state.lightModel==CoinRenderLightModel::PHONG&&state.hasTexture,
          "later ordinary receiver retains authored PHONG/texturing/receive state");
        if(owner==&after)laterLayer=std::max(laterLayer,draw.renderLayer);else mainLayer=std::max(mainLayer,draw.renderLayer);
        if(draw.renderStateSlot>=pass.lightingIndexByState.size()||state.lightingSlot>=frame.lightingStates.size()){ok=false;continue;}
        const auto lightIndex=pass.lightingIndexByState[draw.renderStateSlot];const auto &lighting=frame.lightingStates[state.lightingSlot];
        if(lightIndex<0||size_t(lightIndex)>=lighting.lights.size()){ok=false;continue;}
        ok &= check(lighting.lights[size_t(lightIndex)].sourceRevision==owner->lights[0]->getNodeId()&&
          std::abs(lighting.lights[size_t(lightIndex)].intensity-.8f)<1e-6,
          "post-highlight receiver resolves its real light contribution");
      }
      ok &= check(casterIndices==36&&receiverIndices==6,"both occurrences and original caster survive in each group");
    }
    ok &= check(seenGroups.size()==2&&seenLights.size()==2&&laterLayer>mainLayer,
                "actual foreground is captured after the scene/highlight layer, in the same frame");
    return ok;
  };
  bool ok=capture(false,"ordinary foreground control");
  if(ok)ok=capture(true,"foreground after active highlight");
  if(ok)ok=capture(false,"ordinary composition restoration");
  run.manager.removeSuperimposition(layer);return ok;
}
} // namespace highlight_shadow
int main(int argc,char **argv) {
  using namespace highlight_shadow;Config c;
  for(int i=1;i<argc;++i) {
    const std::string arg=argv[i];
    if(arg=="--core")c.core=true;else if(arg=="--core-scope"){c.core=true;c.coreScope=true;}else if(arg=="--no-receive")c.noReceive=true;else if(arg=="--predecessor-only")c.firstOnly=true;else if(arg=="--projected-witness")c.projected=true;else if(arg=="--window")c.window=true;else if(arg=="--ordinary-style")c.ordinaryStyle=true;else if(arg=="--line")c.line=true;else if(arg=="--box")c.line=false;
    else if(arg=="--spot")c.spot=true;else if(arg=="--portable-sampling")c.portable=true;
    else if((arg=="--samples"||arg=="--maps")&&i+1<argc) {
      const std::string value=argv[++i];
      if(arg=="--samples"&&(value=="1"||value=="2"||value=="4"))c.samples=unsigned(std::stoul(value));
      else if(arg=="--maps"&&(value=="1"||value=="4"||value=="8"))c.maps=unsigned(std::stoul(value));
      else {std::cerr<<"invalid count\n";return 2;}
    } else {std::cerr<<"unknown/incomplete option "<<arg<<std::endl;return 2;}
  }
#ifdef COIN_HIGHLIGHT_SHADOW_X11
  if(c.window)XInitThreads();
#else
  if(c.window){std::cerr<<"X11 support unavailable\n";return 77;}
#endif
  if(!c.core&&!c.projected&&!c.noReceive&&!c.firstOnly) {
    std::cerr<<"use --projected-witness for observable shadow positives; original caster is --predecessor-only diagnostic\n";return 2;
  }
  SoDB::init();SoInteraction::init();CoinRenderAction::initClass();
  if(c.coreScope)return(c.line?coreScope<CountAction<SoLineHighlightRenderAction>>(c):coreScope<CountAction<SoBoxHighlightRenderAction>>(c))?0:1;
  if(c.core)return(c.line?core<CountAction<SoLineHighlightRenderAction>>(c):core<CountAction<SoBoxHighlightRenderAction>>(c))?0:1;
  CoinRenderCapabilities caps{};const int query=coin_render_query_capabilities(c.window?COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW:COIN_RENDER_EXPERIMENTAL_OFFSCREEN,&caps,sizeof(caps));
  std::cout<<"highlight_shadow_environment query="<<query<<" gpu="<<caps.gpu_available<<" backend="<<caps.backend<<" renderer="<<caps.renderer<<" adapter="<<caps.adapter_name<<" diagnostic="<<caps.diagnostic<<std::endl;
  if(query!=0)return 1;if(!caps.gpu_available)return 77;c.renderer=static_cast<CoinRenderRenderer>(caps.renderer);
  if(c.line){Run<CountAction<SoLineHighlightRenderAction>> run(c);if(!run.prepare())return 1;return run.exercise()?0:1;}
  Run<CountAction<SoBoxHighlightRenderAction>> run(c);if(!run.prepare())return 1;return run.exercise()?0:1;
}
