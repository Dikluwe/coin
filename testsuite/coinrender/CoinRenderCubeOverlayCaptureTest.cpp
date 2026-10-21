#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/SoPath.h>
#include <Inventor/SoPickedPoint.h>
#include <Inventor/actions/SoRayPickAction.h>
#include <Inventor/SoPrimitiveVertex.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/elements/SoLazyElement.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoDrawStyle.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoMaterialBinding.h>
#include <Inventor/nodes/SoNormal.h>
#include <Inventor/nodes/SoNormalBinding.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoSubNode.h>
#include <Inventor/nodes/SoTextureCoordinatePlane.h>
#include <Inventor/nodes/SoTransform.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include "actions/CoinRenderActionP.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinrender/CoinRenderStateCore.h"
#include "CoinRenderTestEnvironment.h"
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace cube_overlay_test {
bool check(bool value,const std::string & label) {
  if(!value)std::cerr<<"CubeOverlayCapture FAIL "<<label<<'\n';
  return value;
}
struct Environment {
  const char * names[3]={"COIN_RENDER_DISABLE_TRANSLATION_OVERLAY","COIN_RENDER_DISABLE_MATERIAL_OVERLAY","COIN_RENDER_DISABLE_CUBE_OVERLAY"};
  std::string saved[3]; bool present[3];
  Environment(){for(unsigned i=0;i<3;++i){const char *s=std::getenv(names[i]);present[i]=s;saved[i]=s?s:"";coinRenderTestSetEnvironment(names[i],"0");}}
  ~Environment(){for(unsigned i=0;i<3;++i)coinRenderTestSetEnvironment(names[i],present[i]?saved[i].c_str():nullptr);}
};
struct Backend:CoinRenderBackend {
  CoinRenderFrameReuseDecision reuse;
  unsigned submissions=0;bool failNext=false;std::string error;
  bool isGpuBackend()const override{return false;}
  CoinRenderBackendStatus getStatus()const override{return CoinRenderBackendStatus::SUCCESS;}
  CoinRenderBackendStatus prepare(CoinRenderTargetP &)override{return CoinRenderBackendStatus::SUCCESS;}
  void poll()override{}
  const std::string &getLastError()const override{return error;}
  CoinRenderSubmitResult submit(const CoinRenderFramePlan &f,CoinRenderTargetP&t)override{return submit(f,t,CoinRenderFrameReuseDecision{});}
  CoinRenderSubmitResult submit(const CoinRenderFramePlan &,CoinRenderTargetP&t,const CoinRenderFrameReuseDecision &r)override{
    ++submissions;reuse=r;
    if(failNext){failNext=false;return {CoinRenderBackendStatus::UNSUPPORTED,"injected cube-overlay publication failure"};}
    const size_t pixels=size_t(t.size[0])*size_t(t.size[1]);t.colorBuffer.resize(pixels*4);
    for(size_t i=0;i<pixels;++i){t.colorBuffer[4*i]=13;t.colorBuffer[4*i+1]=29;t.colorBuffer[4*i+2]=71;t.colorBuffer[4*i+3]=255;}return {};
  }
};
struct Harness {
  std::unique_ptr<CoinRenderTarget> target;
  CoinRenderAction action;
  Backend *backend=nullptr;
  explicit Harness(int side=1):target(CoinRenderTarget::createOffscreen(SbVec2i32(side,side))),action(SbViewportRegion(side,side)){if(target){backend=new Backend;target->getPimpl()->backend.reset(backend);target->getPimpl()->depthReadbackEnabled=false;action.setRenderTarget(target.get());}action.setTransparencyType(CoinRenderAction::NONE);}
};
struct Scene {
  SoSeparator *root=new SoSeparator;
  SoCube *shared;SoCube *unique=new SoCube;
  std::array<SoSeparator*,3> branch;
  std::array<SoTransform*,3> transform;
  std::array<SoMaterial*,3> material;
  explicit Scene(SoCube *cube=nullptr):shared(cube?cube:new SoCube){
    root->ref();auto *camera=new SoOrthographicCamera;camera->position.setValue(0,0,20);camera->height=16;camera->nearDistance=1;camera->farDistance=40;root->addChild(camera);
    auto *light=new SoDirectionalLight;light->direction.setValue(.25f,-.5f,-1);root->addChild(light);
    auto *model=new SoLightModel;model->model=SoLightModel::PHONG;root->addChild(model);
    shared->width=2;shared->height=3;shared->depth=4;unique->width=3;unique->height=2;unique->depth=1;
    for(unsigned i=0;i<3;++i){
      branch[i]=new SoSeparator;material[i]=new SoMaterial;transform[i]=new SoTransform;
      material[i]->diffuseColor.setValue(.25f+.125f*i,.375f,.75f-.125f*i);
      material[i]->ambientColor.setValue(.125f,.25f,.375f);material[i]->specularColor.setValue(.5f,.25f,.125f);material[i]->shininess=.25f+.125f*i;
      transform[i]->translation.setValue(float(i)*3-3,.25f*i,0);
      transform[i]->rotation.setValue(SbVec3f(1,2,3),.125f*i);
      transform[i]->scaleFactor.setValue(i==1?-.75f:1.25f,.5f,1.5f);
      branch[i]->addChild(material[i]);branch[i]->addChild(transform[i]);branch[i]->addChild(i==1?unique:shared);root->addChild(branch[i]);
    }
  }
  ~Scene(){root->unref();}
};
struct Reference {
  struct Corner{SbVec3f position,normal;SbVec2f uv;SbColor ambient,diffuse,specular,emission;float shininess,alpha;SbMatrix model;SbUniqueId source;};
  std::vector<Corner> corners;
  static void triangle(void *data,SoCallbackAction *action,const SoPrimitiveVertex *a,const SoPrimitiveVertex *b,const SoPrimitiveVertex *c){
    auto &self=*static_cast<Reference*>(data);
    for(const auto *v:{a,b,c}){
      Corner out;out.position=v->getPoint();out.normal=v->getNormal();out.uv.setValue(v->getTextureCoords()[0],v->getTextureCoords()[1]);
      float transparency;action->getMaterial(out.ambient,out.diffuse,out.specular,out.emission,out.shininess,transparency,v->getMaterialIndex());out.alpha=1-transparency;
      out.model=action->getModelMatrix();out.source=action->getCurPath()->getTail()->getNodeId();self.corners.push_back(out);
    }
  }
  void install(SoCallbackAction &action){action.addTriangleCallback(SoCube::getClassTypeId(),triangle,this);}
};
bool payload(const CoinRenderFramePlan &plan,const Reference &reference,const std::string &label){
  bool ok=check(plan.isValid(),label+": valid plan");size_t cursor=0;
  for(const auto &draw:plan.draws){
    if(!check(draw.renderStateSlot<plan.renderStates.size(),label+": state slot"))return false;
    const auto &state=plan.renderStates[draw.renderStateSlot];
    for(unsigned j=0;j<draw.geometry.indexCount;++j,++cursor){
      const size_t offset=size_t(draw.geometry.firstIndex)+j;
      if(!check(offset<plan.indices.size()&&cursor<reference.corners.size(),label+": consumed index count"))return false;
      const size_t index=plan.indices[offset];
      if(!check(index<plan.vertices.size()&&index>=draw.geometry.firstVertex&&index<size_t(draw.geometry.firstVertex)+draw.geometry.vertexCount,label+": consumed range"))return false;
      const auto &v=plan.vertices[index];const auto &r=reference.corners[cursor];
      if(!check(v.materialSlot<plan.materials.size(),label+": material slot"))return false;
      const auto &m=plan.materials[v.materialSlot];
      ok &= check(SbVec3f(v.position)==r.position&&SbVec3f(v.normal)==r.normal&&SbVec2f(v.texcoord)==r.uv&&state.model==r.model&&draw.sourceNodeId==r.source,label+": native attributes/model/source order");
      const float *actual[]={m.ambient,m.diffuse,m.specular,m.emission};const SbColor *expected[]={&r.ambient,&r.diffuse,&r.specular,&r.emission};
      for(unsigned attr=0;attr<4;++attr)ok &= check(!std::memcmp(actual[attr],expected[attr]->getValue(),3*sizeof(float)),label+": native material channels");
      ok &= check(m.shininess==r.shininess&&m.diffuse[3]==r.alpha,label+": native shininess/alpha");
    }
  }
  return check(cursor==reference.corners.size(),label+": all native corners delivered")&&ok;
}
bool compare(Harness &h,Scene &scene,SoCallbackAction &native,Reference &reference,const std::string &label,SoPath *path=nullptr){
  reference.corners.clear();const unsigned before=h.backend->submissions;
  if(path){native.apply(path);h.action.apply(path);}else{native.apply(scene.root);h.action.apply(scene.root);}
  if(!check(h.action.getLastStatus()==CoinRenderAction::SUCCESS,label+": successful first apply: "+h.action.getLastError().getString()))return false;
  return check(h.backend->submissions==before+1,label+": exactly one submission per apply")&&payload(h.action.getPimpl()->lastValidPlan,reference,label);
}
bool authoredBox(const CoinRenderFramePlan &plan,const Scene &scene){
  size_t count=0;bool ok=true;
  for(const auto &d:plan.draws){
    const auto *cube=d.sourceNodeId==scene.unique->getNodeId()?scene.unique:scene.shared;
    const float half[]={cube->width.getValue()*.5f,cube->height.getValue()*.5f,cube->depth.getValue()*.5f};
    for(unsigned j=0;j<d.geometry.indexCount;++j){
      const auto &v=plan.vertices[plan.indices[d.geometry.firstIndex+j]];unsigned normals=0;
      for(unsigned a=0;a<3;++a){ok &= check(std::abs(v.position[a])==half[a],"authored box half-extent");if(v.normal[a]!=0){++normals;ok &= check(std::abs(v.normal[a])==1&&v.position[a]==v.normal[a]*half[a],"authored outward face normal");}}
      ok &= check(normals==1,"exactly one native face normal axis");++count;
    }
  }
  return check(count==108,"three authored box occurrences")&&ok;
}
bool incremental(){
  Environment env;Scene scene;Harness h;if(!check(bool(h.target),"CPU target"))return false;
  Reference ref;SoCallbackAction native;ref.install(native);
  bool ok=compare(h,scene,native,ref,"initial");auto &p=h.action.getPimpl();
  ok &= check(p->translationProofValid&&p->cubeByNode.count(scene.shared)&&p->cubeByNode.count(scene.unique),"eligible captured shared/distinct cubes admitted for real incremental updates");
  const auto *vertices=p->lastValidPlan.vertices.data();const auto *indices=p->lastValidPlan.indices.data();const uint64_t generation=p->translationProofGeneration;
  for(unsigned phase=0;phase<3;++phase){
    const bool changed=phase!=1;
    scene.shared->width=changed?3.5f:2;scene.shared->height=changed?1.25f:3;scene.shared->depth=changed?2.5f:4;
    scene.unique->width=changed?1.5f:3;scene.unique->depth=changed?2.25f:1;
    scene.transform[0]->translation.setValue(changed?-2.25f:-3,changed?.375f:0,0);
    scene.material[2]->diffuseColor.setValue(changed?.875f:.5f,.375f,changed?.25f:.5f);
    scene.material[2]->shininess=changed?.75f:.5f;
    const uint64_t previous=p->lastValidPlan.revision;
    ok &= compare(h,scene,native,ref,"incremental A/B/A");ok &= authoredBox(p->lastValidPlan,scene);
    // Stable storage and proof generation, updated authored values and a
    // backend submission together distinguish real overlay from a fake tag.
    ok &= check(p->translationProofValid&&p->translationProofGeneration==generation&&
      p->lastValidPlan.vertices.data()==vertices&&p->lastValidPlan.indices.data()==indices&&
      h.backend->reuse.kind==CoinRenderFrameReuseKind::RESOURCE_REBUILD&&h.backend->reuse.baseRevision==previous,
      "real overlay retains geometry storage/proof and submits current resource delta");
  }
  const auto old=p->lastValidPlan;scene.shared->width=2.75f;h.backend->failNext=true;h.action.apply(scene.root);
  ok &= check(h.action.getLastStatus()==CoinRenderAction::UNSUPPORTED&&p->lastValidPlan.revision==old.revision&&p->lastValidPlan.hasSamePayload(old),"failed publication preserves previous complete plan");
  ok &= compare(h,scene,native,ref,"explicit recovery after publication failure");
  std::cout<<"cube_overlay_incremental proof="<<p->translationProofValid<<" shared_sources="<<p->cubeByNode.count(scene.shared)<<'\n';return ok;
}

struct Observer {
  unsigned pre=0,post=0,triangles=0,lines=0,points=0,late=0;
  bool mutate=false,registerDuring=false,installed=false;std::vector<char> events;
  const SbColor color=SbColor(.8125f,.1875f,.4375f);
  static void lateTriangle(void *data,SoCallbackAction*,const SoPrimitiveVertex*,const SoPrimitiveVertex*,const SoPrimitiveVertex*){auto &o=*static_cast<Observer*>(data);++o.late;o.events.push_back('L');}
  static SoCallbackAction::Response before(void *data,SoCallbackAction *a,const SoNode *n){
    auto &o=*static_cast<Observer*>(data);if(!n->isOfType(SoCube::getClassTypeId()))return SoCallbackAction::CONTINUE;
    ++o.pre;o.events.push_back('P');if(o.mutate)SoLazyElement::setDiffuse(a->getState(),const_cast<SoNode*>(n),1,&o.color,nullptr);
    if(o.registerDuring&&!o.installed){o.installed=true;a->addTriangleCallback(SoCube::getClassTypeId(),lateTriangle,&o);}
    return SoCallbackAction::CONTINUE;
  }
  static SoCallbackAction::Response after(void *data,SoCallbackAction*,const SoNode *n){auto &o=*static_cast<Observer*>(data);if(n->isOfType(SoCube::getClassTypeId())){++o.post;o.events.push_back('Q');}return SoCallbackAction::CONTINUE;}
  static void triangle(void *data,SoCallbackAction*,const SoPrimitiveVertex*,const SoPrimitiveVertex*,const SoPrimitiveVertex*){auto &o=*static_cast<Observer*>(data);++o.triangles;o.events.push_back('T');}
  static void line(void *data,SoCallbackAction*,const SoPrimitiveVertex*,const SoPrimitiveVertex*){++static_cast<Observer*>(data)->lines;}
  static void point(void *data,SoCallbackAction*,const SoPrimitiveVertex*){++static_cast<Observer*>(data)->points;}
  void install(SoCallbackAction &a,unsigned mask=31,bool tail=false){
    if(tail){a.addPreTailCallback(before,this);a.addPostTailCallback(after,this);}
    else{if(mask&1)a.addPreCallback(SoNode::getClassTypeId(),before,this);if(mask&2)a.addPostCallback(SoCube::getClassTypeId(),after,this);}
    if(mask&4)a.addTriangleCallback(SoCube::getClassTypeId(),triangle,this);
    if(mask&8)a.addLineSegmentCallback(SoCube::getClassTypeId(),line,this);
    if(mask&16)a.addPointCallback(SoCube::getClassTypeId(),point,this);
  }
  bool same(const Observer &o)const{return pre==o.pre&&post==o.post&&triangles==o.triangles&&lines==o.lines&&points==o.points&&late==o.late&&events==o.events;}
};
bool callbacks(){
  Environment env;bool ok=true;
  for(unsigned mode=0;mode<9;++mode){
    Scene scene;Harness h;if(!h.target)return false;Reference ref;SoCallbackAction native;ref.install(native);
    ok &= compare(h,scene,native,ref,"before late observer installation");
    Observer expected,actual,expectedSecond,actualSecond;expected.mutate=actual.mutate=mode==1;expected.registerDuring=actual.registerDuring=mode==2;
    const unsigned mask=mode<4?31u:1u<<(mode-4);
    const auto stamp=scene.root->getNodeId();
    expected.install(native,mask);actual.install(h.action,mask);
    if(mode==3){expectedSecond.install(native);actualSecond.install(h.action);}
    ok &= compare(h,scene,native,ref,"observer registration without scene notification");
    ok &= check(scene.root->getNodeId()==stamp&&actual.same(expected),"new callback registrations invalidate cached capture without touching scene revision");
    for(unsigned phase=0;phase<3;++phase){
      scene.shared->width=phase==1?3:2;scene.unique->height=phase==1?1.5f:2;
      ok &= compare(h,scene,native,ref,"late observer A/B/A");
      ok &= check(actual.same(expected)&&actualSecond.same(expectedSecond),"author callbacks/state effects occur exactly once in native order");
      ok &= check(!h.action.getPimpl()->cubeByNode.count(scene.shared),"external callbacks cannot retain unsafe cube overlay ownership");
    }
    ok &= check(actual.pre==((mask&1)?12u:0u)&&actual.post==((mask&2)?12u:0u)&&actual.triangles==((mask&4)?144u:0u),"four frames times three cube occurrences deliver each registered callback family");
  }
  Scene scene;Harness h;if(!h.target)return false;Reference ref;SoCallbackAction native;ref.install(native);
  SoPath *path=new SoPath(scene.root);path->ref();path->append(scene.branch[2]);path->append(scene.shared);
  Observer expected,actual;expected.install(native,31,true);actual.install(h.action,31,true);
  for(float width:{2.f,3.f,2.f}){scene.shared->width=width;ok &= compare(h,scene,native,ref,"tail callback path",path);ok &= check(actual.same(expected),"tail callbacks preserve exact effects/order");}
  ok &= check(actual.pre==3&&actual.post==3&&actual.triangles==36,"one shared tail occurrence per apply");path->unref();return ok;
}

class OwnTypeCube:public SoCube {
  SO_NODE_HEADER(OwnTypeCube);
public:OwnTypeCube(){SO_NODE_CONSTRUCTOR(OwnTypeCube);}static void initClass();unsigned generated=0;
protected:void generatePrimitives(SoAction *a)override{++generated;SoCube::generatePrimitives(a);}
};
SO_NODE_SOURCE(OwnTypeCube);
void OwnTypeCube::initClass(){SO_NODE_INIT_CLASS(OwnTypeCube,SoCube,"Cube");}
class InheritedTypeCube:public SoCube {
public:unsigned generated=0;
protected:void generatePrimitives(SoAction *a)override{
  ++generated;SoCube::generatePrimitives(a);
  // Observable extra geometry makes bypassing this override a payload error,
  // even when the derived C++ object inherits SoCube's registered SoType.
  SoPrimitiveVertex v;v.setNormal(SbVec3f(0,0,1));v.setMaterialIndex(0);v.setTextureCoords(SbVec4f(0,0,0,1));
  beginShape(a,SoShape::TRIANGLES);v.setPoint(SbVec3f(4,0,0));shapeVertex(&v);v.setPoint(SbVec3f(5,0,0));shapeVertex(&v);v.setPoint(SbVec3f(4,1,0));shapeVertex(&v);endShape();
}
};
template<class Cube>bool subclass(){
  auto *cube=new Cube;Scene scene(cube);Harness h;if(!h.target)return false;Reference ref;SoCallbackAction native;ref.install(native);bool ok=true;
  // Populate the native Cube cache before encountering an equivalent derived
  // object. Different dimensions or visiting the subclass first would fail to
  // exercise an unsafe cache hit on the inherited SoType.
  auto *nativeCube=new SoCube;nativeCube->height=3;nativeCube->depth=4;
  auto *prefix=new SoSeparator;auto *position=new SoTransform;position->translation.setValue(-6,0,0);
  prefix->addChild(scene.material[0]);prefix->addChild(position);prefix->addChild(nativeCube);
  scene.root->insertChild(prefix,scene.root->findChild(scene.branch[0]));
  for(float width:{2.f,3.f,2.f}){
    nativeCube->width=width;cube->width=width;const unsigned before=cube->generated;ok &= compare(h,scene,native,ref,"native-cache-before-subclass A/B/A");
    ok &= check(ref.corners.size()==(cube->getTypeId()==SoCube::getClassTypeId()?150u:144u),"native prefix and both subclass occurrences retain all authored primitives");
    ok &= check(cube->generated==before+4,"both shared subclass occurrences generate on native and captured traversal");
    ok &= check(!h.action.getPimpl()->cubeByNode.count(cube),"subclass never receives base-cube payload ownership");
  }return ok;
}
bool fallback(){
  Environment env;bool ok=true;
  for(unsigned mode=0;mode<5;++mode){
    Scene scene;Harness h;if(!h.target)return false;Reference ref;SoCallbackAction native;ref.install(native);
    if(mode==0){auto *binding=new SoMaterialBinding;binding->value=SoMaterialBinding::PER_FACE;scene.root->insertChild(binding,3);for(auto *m:scene.material)for(unsigned i=1;i<6;++i)m->diffuseColor.set1Value(i,.125f*i,.25f,.75f);}
    if(mode==1){auto *normal=new SoNormal;normal->vector.setValue(.25f,.5f,.75f);auto *binding=new SoNormalBinding;binding->value=SoNormalBinding::OVERALL;scene.root->insertChild(normal,3);scene.root->insertChild(binding,4);}
    if(mode==2){auto *texgen=new SoTextureCoordinatePlane;texgen->directionS.setValue(.25f,.5f,0);scene.root->insertChild(texgen,3);}
    if(mode==3)scene.transform[1]->scaleFactor.setValue(0,1,1);
    for(float width:{2.f,0.f,-2.f,2.f}){scene.shared->width=width;ok &= compare(h,scene,native,ref,"legitimate unsupported/degenerate fallback");}
  }
  return ok;
}
#include "CoinRenderCubeOverlayExtraTests.h"
#include "CoinRenderCubeOverlayTransitionsTests.h"
#include "CoinRenderCubeOverlayGpuTests.h"
}
int main(int argc,char **argv){
  SoDB::init();CoinRenderAction::initClass();cube_overlay_test::OwnTypeCube::initClass();
  const std::string mode=argc==2?argv[1]:"--incremental";
  if(mode=="--incremental")return cube_overlay_test::incremental()?0:1;
  if(mode=="--callbacks")return cube_overlay_test::callbacks()?0:1;
  if(mode=="--subclasses"){cube_overlay_test::Environment env;bool ok=cube_overlay_test::subclass<cube_overlay_test::OwnTypeCube>();ok &= cube_overlay_test::subclass<cube_overlay_test::InheritedTypeCube>();return ok?0:1;}
  if(mode=="--fallback")return cube_overlay_test::fallback()?0:1;
  if(mode=="--drawstyle")return cube_overlay_test::drawstyle()?0:1;
  if(mode=="--picking")return cube_overlay_test::picking()?0:1;
  if(mode=="--invalid")return cube_overlay_test::invalid()?0:1;
  if(mode=="--materials")return cube_overlay_test::materials()?0:1;
  if(mode=="--notify")return cube_overlay_test::notifyBatch()?0:1;
  if(mode=="--transforms")return cube_overlay_test::transforms()?0:1;
  if(mode=="--material-transitions")return cube_overlay_test::materialTransitions()?0:1;
  if(mode=="--gpu")return cube_overlay_test::gpu();
  return 2;
}
