#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/SoPath.h>
#include <Inventor/SoPrimitiveVertex.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/elements/SoLazyElement.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoMaterialBinding.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoShape.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTransform.h>
#include <Inventor/nodes/SoSubNode.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoLineSet.h>
#include <Inventor/nodes/SoPointSet.h>
#include <Inventor/nodes/SoImage.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/nodes/SoTextureCoordinatePlane.h>
#include "actions/CoinRenderActionP.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace compact_cube {
bool check(bool value,const std::string &label) {
  if(!value)std::cerr<<"compact_cube FAIL "<<label<<std::endl;
  return value;
}
struct Vertex {
  SbVec3f position,normal;
  SbVec2f uv;
  SbColor4f diffuse;
  SbMatrix model;
};
struct Reference {
  std::vector<Vertex> vertices;
  static void triangle(void *data,SoCallbackAction *a,const SoPrimitiveVertex *x,const SoPrimitiveVertex *y,const SoPrimitiveVertex *z) {
    auto &self=*static_cast<Reference*>(data);
    for(const auto *v:{x,y,z}) {
      Vertex record;record.position=v->getPoint();record.normal=v->getNormal();
      record.uv.setValue(v->getTextureCoords()[0],v->getTextureCoords()[1]);
      SbColor ambient,diffuse,specular,emission;float shininess,transparency;
      a->getMaterial(ambient,diffuse,specular,emission,shininess,transparency,v->getMaterialIndex());
      record.diffuse.setValue(diffuse[0],diffuse[1],diffuse[2],1-transparency);
      record.model=a->getModelMatrix();self.vertices.push_back(record);
    }
  }
};
struct Scene {
  SoSeparator *root=new SoSeparator;
  SoCube *cube;
  SoMaterial *faceMaterial=new SoMaterial;
  SoMaterialBinding *binding=new SoMaterialBinding;
  std::array<SoMaterial*,2> materials;
  std::array<SoTransform*,2> transforms;
  std::array<SoSeparator*,2> occurrences;
  explicit Scene(int mode=SoMaterialBinding::OVERALL,bool textured=false,SoCube *shape=nullptr):cube(shape?shape:new SoCube) {
    root->ref();auto *camera=new SoOrthographicCamera;camera->position.setValue(0,0,12);
    camera->height=12;camera->nearDistance=1;camera->farDistance=30;root->addChild(camera);
    auto *model=new SoLightModel;model->model=SoLightModel::BASE_COLOR;root->addChild(model);
    for(unsigned i=0;i<6;++i) {
      faceMaterial->diffuseColor.set1Value(int(i),.125f+.125f*i,.25f,.875f-.125f*i);
      faceMaterial->transparency.set1Value(int(i),.125f);
    }
    root->addChild(faceMaterial);binding->value=mode;root->addChild(binding);
    if(textured) {
      auto *texture=new SoTexture2;const unsigned char pixels[]={17,37,71,255,173,29,101,255,41,211,83,255,233,131,19,255};
      texture->image.setValue(SbVec2s(2,2),4,pixels);texture->model=SoTexture2::MODULATE;root->addChild(texture);
    }
    cube->width=2;cube->height=3;cube->depth=4;
    for(unsigned i=0;i<2;++i) {
      occurrences[i]=new SoSeparator;transforms[i]=new SoTransform;
      transforms[i]->translation.setValue(i?2.5f:-2.5f,.25f*i,0);
      transforms[i]->rotation.setValue(SbVec3f(1,2,3),i?-.4f:.3f);
      transforms[i]->scaleFactor.setValue(.5f,1,.75f);occurrences[i]->addChild(transforms[i]);
      materials[i]=new SoMaterial;materials[i]->ref();
      materials[i]->diffuseColor.setValue(i?.25f:.75f,.375f,i?.875f:.125f);
      materials[i]->transparency=i?.25f:.125f;
      if(mode==SoMaterialBinding::OVERALL)occurrences[i]->addChild(materials[i]);
      occurrences[i]->addChild(cube);root->addChild(occurrences[i]);
    }
  }
  ~Scene(){root->unref();for(auto *material:materials)material->unref();}
  std::vector<SbUniqueId> stamp()const {
    return {root->getNodeId(),cube->getNodeId(),binding->getNodeId(),faceMaterial->getNodeId(),materials[0]->getNodeId(),materials[1]->getNodeId(),transforms[0]->getNodeId(),transforms[1]->getNodeId()};
  }
};
struct Observer {
  const SoNode *cube;
  unsigned pre=0,post=0,triangles=0,lines=0,points=0;
  std::vector<char> order;
  bool mutate=false;
  const SbColor color=SbColor(.375f,.625f,.875f);
  explicit Observer(const SoNode *c):cube(c){}
  static SoCallbackAction::Response before(void *data,SoCallbackAction *action,const SoNode *node) {
    auto &self=*static_cast<Observer*>(data);if(node!=self.cube)return SoCallbackAction::CONTINUE;
    ++self.pre;self.order.push_back('P');
    if(self.mutate)SoLazyElement::setDiffuse(action->getState(),const_cast<SoNode*>(node),1,&self.color,nullptr);
    return SoCallbackAction::CONTINUE;
  }
  static SoCallbackAction::Response after(void *data,SoCallbackAction*,const SoNode *node) {
    auto &self=*static_cast<Observer*>(data);if(node==self.cube){++self.post;self.order.push_back('Q');}return SoCallbackAction::CONTINUE;
  }
  static void triangle(void *data,SoCallbackAction*,const SoPrimitiveVertex*,const SoPrimitiveVertex*,const SoPrimitiveVertex*) {
    auto &self=*static_cast<Observer*>(data);++self.triangles;self.order.push_back('T');
  }
  static void line(void *data,SoCallbackAction*,const SoPrimitiveVertex*,const SoPrimitiveVertex*){++static_cast<Observer*>(data)->lines;}
  static void point(void *data,SoCallbackAction*,const SoPrimitiveVertex*){++static_cast<Observer*>(data)->points;}
  void install(SoCallbackAction &action,bool ancestor,bool tail=false) {
    if(tail){action.addPreTailCallback(before,this);action.addPostTailCallback(after,this);}
    else {action.addPreCallback(ancestor?SoNode::getClassTypeId():SoCube::getClassTypeId(),before,this);action.addPostCallback(ancestor?SoShape::getClassTypeId():SoCube::getClassTypeId(),after,this);}
    action.addTriangleCallback(SoCube::getClassTypeId(),triangle,this);
    action.addLineSegmentCallback(SoCube::getClassTypeId(),line,this);
    action.addPointCallback(SoCube::getClassTypeId(),point,this);
  }
};
bool delivery(CoinRenderAction &action,Scene &scene,const Reference &reference,bool compact,unsigned occurrenceCount=2) {
  const auto &frame=action.getPimpl()->lastValidPlan;
  bool ok=check(action.getLastStatus()==CoinRenderAction::SUCCESS&&frame.isValid(),"valid native Cube capture and plan");
  if(!ok){std::cerr<<action.getLastError().getString()<<std::endl;return false;}
  ok &= check(reference.vertices.size()==36*occurrenceCount&&!frame.draws.empty(),"exact native triangle and occurrence counts");
  // Switching a PER_FACE/PER_PART scene to OVERALL leaves only the common
  // ancestor material: no local occurrence material was authored in that scene.
  // Those equal payloads legitimately share a mesh while models stay distinct.
  const bool equivalent=occurrenceCount==2&&scene.binding->value.getValue()==SoMaterialBinding::OVERALL&&
    scene.occurrences[0]->findChild(scene.materials[0])<0&&scene.occurrences[1]->findChild(scene.materials[1])<0;
  const bool shared=compact&&equivalent;
  if(compact)ok &= check(frame.vertices.size()==(shared?24u:24u*occurrenceCount)&&
    frame.indices.size()==(shared?36u:36u*occurrenceCount),
    shared?"identical OVERALL payloads retain one24/36 mesh":"different occurrence payloads retain separate24/36 meshes");
  if(equivalent&&reference.vertices.size()==72)for(unsigned i=0;i<36;++i) {
    const auto &a=reference.vertices[i],&b=reference.vertices[i+36];
    ok &= check(a.position==b.position&&a.normal==b.normal&&a.uv==b.uv&&a.diffuse==b.diffuse,
      "shared-mesh fixture has exactly equal native local payloads despite distinct models");
  }
  size_t cursor=0;std::array<unsigned,2> occurrenceVertices{{0,0}},occurrenceIndices{{0,0}};
  std::vector<std::pair<unsigned,CoinRenderGeometryRange>> ranges;
  for(const auto &draw:frame.draws) {
    const unsigned occurrence=unsigned(cursor/36);
    ok &= check(draw.sourceNodeId==scene.cube->getNodeId()&&draw.geometry.indexCount>0&&draw.geometry.indexCount%3==0&&
      cursor%36+draw.geometry.indexCount<=36&&occurrence<occurrenceCount&&draw.renderStateSlot<frame.renderStates.size(),
      "material-split draws stay within their native source occurrence");
    if(draw.renderStateSlot>=frame.renderStates.size()||occurrence>=occurrenceCount)continue;
    occurrenceVertices[occurrence]+=draw.geometry.vertexCount;occurrenceIndices[occurrence]+=draw.geometry.indexCount;
    ranges.push_back({occurrence,draw.geometry});
    for(unsigned i=0;i<draw.geometry.indexCount;++i,++cursor) {
      const size_t at=size_t(draw.geometry.firstIndex)+i;
      if(at>=frame.indices.size()||cursor>=reference.vertices.size()){ok=false;continue;}
      const size_t index=frame.indices[at];
      if(index>=frame.vertices.size()){ok=false;continue;}
      ok &= check(index>=draw.geometry.firstVertex&&index<size_t(draw.geometry.firstVertex)+draw.geometry.vertexCount,"indices belong to their own occurrence range");
      const auto &v=frame.vertices[index];const auto &expected=reference.vertices[cursor];
      ok &= check(SbVec3f(v.position)==expected.position&&SbVec3f(v.normal)==expected.normal&&SbVec2f(v.texcoord)==expected.uv,"expanded position/normal/UV equals native callback exactly");
      ok &= check(frame.renderStates[draw.renderStateSlot].model==expected.model,"noncommuting occurrence model matches native state");
      if(v.materialSlot>=frame.materials.size()){ok=false;continue;}
      ok &= check(std::memcmp(frame.materials[v.materialSlot].diffuse,expected.diffuse.getValue(),sizeof(float)*4)==0,"per-corner diffuse and alpha equal native material semantics exactly");
    }
  }
  for(unsigned occurrence=0;occurrence<occurrenceCount;++occurrence) {
    ok &= check(occurrenceIndices[occurrence]==36,"each native occurrence consumes exactly36 indices despite material splitting");
    if(compact)ok &= check(occurrenceVertices[occurrence]==24,"material-split ranges total exactly24 vertices per compact occurrence");
  }
  ok &= check(cursor==reference.vertices.size(),"all native indices are consumed through real draws, including shared ranges");
  for(size_t i=0;i<ranges.size();++i)for(size_t j=i+1;j<ranges.size();++j)if(ranges[i].first!=ranges[j].first) {
    const auto &a=ranges[i].second,&b=ranges[j].second;
    const bool identical=a.firstVertex==b.firstVertex&&a.vertexCount==b.vertexCount&&
      a.firstIndex==b.firstIndex&&a.indexCount==b.indexCount;
    const bool disjoint=size_t(a.firstVertex)+a.vertexCount<=b.firstVertex||size_t(b.firstVertex)+b.vertexCount<=a.firstVertex;
    if(shared)ok &= check(identical,"equal compact payload occurrences share the same complete mesh ranges");
    else if(equivalent)ok &= check(identical||disjoint,
      "native-equivalent degenerate/negative/fallback payloads may share a complete range without requiring compactness");
    else ok &= check(disjoint,"different occurrence payloads do not alias across material groups");
  }
  std::cout<<"compact_cube_capture vertices="<<frame.vertices.size()<<" indices="<<frame.indices.size()<<" draws="<<frame.draws.size()<<" required_compact="<<compact<<" expected_shared="<<shared<<" consumed_indices="<<cursor<<std::endl;
  return ok;
}
bool apply(CoinRenderAction &capture,Scene &scene,bool compact,Observer *expectedObserver=nullptr,Observer *actualObserver=nullptr,SoPath *path=nullptr) {
  Reference reference;SoCallbackAction native;
  if(expectedObserver)expectedObserver->install(native,true,path!=nullptr);
  native.addTriangleCallback(SoCube::getClassTypeId(),Reference::triangle,&reference);
  const auto stamp=scene.stamp();
  if(path)native.apply(path);else native.apply(scene.root);
  if(path)capture.apply(path);else capture.apply(scene.root);
  bool ok=check(stamp==scene.stamp(),"all source fields/revisions unchanged during both traversals");
  ok &= delivery(capture,scene,reference,compact,path?1:2);
  if(expectedObserver&&actualObserver)ok &= check(expectedObserver->pre==actualObserver->pre&&expectedObserver->post==actualObserver->post&&
    expectedObserver->triangles==actualObserver->triangles&&expectedObserver->lines==actualObserver->lines&&expectedObserver->points==actualObserver->points&&expectedObserver->order==actualObserver->order,
    "all observer counts and per-occurrence order equal native dispatch");
  return ok;
}
bool compact() {
  bool ok=true;
  for(int binding:{SoMaterialBinding::OVERALL,SoMaterialBinding::PER_PART,SoMaterialBinding::PER_FACE}) {
    Scene scene(binding);CoinRenderAction action(SbViewportRegion(128,128));action.getPimpl()->planOnly=true;action.setTransparencyType(CoinRenderAction::NONE);
    ok &= apply(action,scene,true);scene.cube->width=3;ok &= apply(action,scene,true);
    scene.cube->width=2;ok &= apply(action,scene,true);
    scene.faceMaterial->diffuseColor.set1Value(2,.875f,.125f,.5f);scene.materials[0]->diffuseColor.setValue(.25f,.625f,.5f);
    ok &= apply(action,scene,true);
    scene.binding->value=SoMaterialBinding::OVERALL;ok &= apply(action,scene,true);
    for(float width:{0.f,-2.f}){scene.cube->width=width;ok &= apply(action,scene,false);}
    scene.cube->width=2;action.setFastPathEnabled(FALSE);ok &= apply(action,scene,false);
    action.setFastPathEnabled(TRUE);ok &= apply(action,scene,true);
  }
  // A texture can legitimately make template replay ineligible; attributes and
  // callbacks remain authoritative without imposing compactness in that domain.
  Scene textured(SoMaterialBinding::OVERALL,true);CoinRenderAction action;action.getPimpl()->planOnly=true;action.setTransparencyType(CoinRenderAction::NONE);
  ok &= apply(action,textured,false);return ok;
}
bool observers() {
  bool ok=true;
  for(bool mutate:{false,true}) {
    Scene scene;CoinRenderAction action(SbViewportRegion(128,128));action.getPimpl()->planOnly=true;action.setTransparencyType(CoinRenderAction::NONE);
    ok &= apply(action,scene,true);
    // Register after the first capture: a cached replay must not hide the new
    // callbacks or their effective-state changes. Reference uses the same API.
    Observer expected(scene.cube),actual(scene.cube);expected.mutate=actual.mutate=mutate;actual.install(action,true);
    ok &= apply(action,scene,false,&expected,&actual);
    scene.cube->depth=2;ok &= apply(action,scene,false,&expected,&actual);
    scene.cube->depth=4;ok &= apply(action,scene,false,&expected,&actual);
    Observer expectedLate(scene.cube),actualLate(scene.cube);
    action.addTriangleCallback(SoCube::getClassTypeId(),Observer::triangle,&actualLate);
    // This observer is additional to the original set and must see both Cubes.
    const unsigned before=actualLate.triangles;
    ok &= apply(action,scene,false,&expected,&actual);
    ok &= check(actualLate.triangles-before==24,"late second triangle callback sees every triangle on replay");
  }
  Scene scene;SoPath *path=new SoPath(scene.root);path->ref();path->append(scene.occurrences[1]);path->append(scene.cube);
  CoinRenderAction action;action.getPimpl()->planOnly=true;action.setTransparencyType(CoinRenderAction::NONE);
  Observer expected(scene.cube),actual(scene.cube);actual.install(action,true,true);
  ok &= apply(action,scene,false,&expected,&actual,path);
  ok &= check(actual.pre==1&&actual.post==1&&actual.triangles==12,"tail path visits exactly one shared occurrence");
  path->unref();return ok;
}

class CustomCube:public SoCube {
  SO_NODE_HEADER(CustomCube);
public:
  CustomCube(){SO_NODE_CONSTRUCTOR(CustomCube);}
  static void initClass();unsigned generated=0;
protected:
  void generatePrimitives(SoAction *action)override{++generated;SoCube::generatePrimitives(action);}
};
// A legal C++ generator override need not register a distinct Coin SoType.
// Its side effect is observable even though getTypeId() remains SoCube.
class InheritedTypeCube:public SoCube {
public:
  unsigned generated=0;
protected:
  void generatePrimitives(SoAction *action)override{++generated;SoCube::generatePrimitives(action);}
};
SO_NODE_SOURCE(CustomCube);
void CustomCube::initClass(){SO_NODE_INIT_CLASS(CustomCube,SoCube,"Cube");}

// Extra primitives are deliberately outside the Cube's compactness domain.
// Compare only the consumed Cube ranges, never infer data from idle vertices.
bool cubeSubset(const CoinRenderFramePlan &frame,const Reference &reference,const SoCube *cube) {
  bool ok=true;size_t cursor=0;unsigned draws=0,vertices=0;
  for(const auto &draw:frame.draws)if(draw.sourceNodeId==cube->getNodeId()) {
    ++draws;vertices+=draw.geometry.vertexCount;
    ok &= check(draw.geometry.indexCount==36&&draw.geometry.vertexCount==24,"ordinary Cube stays compact beside another primitive family");
    if(draw.renderStateSlot>=frame.renderStates.size()){ok=false;continue;}
    for(unsigned i=0;i<draw.geometry.indexCount;++i,++cursor) {
      const size_t offset=size_t(draw.geometry.firstIndex)+i;
      if(offset>=frame.indices.size()||cursor>=reference.vertices.size()){ok=false;continue;}
      const size_t index=frame.indices[offset];
      if(index>=frame.vertices.size()||index<draw.geometry.firstVertex||index>=size_t(draw.geometry.firstVertex)+draw.geometry.vertexCount){ok=false;continue;}
      const auto &v=frame.vertices[index];const auto &expected=reference.vertices[cursor];
      ok &= check(SbVec3f(v.position)==expected.position&&SbVec3f(v.normal)==expected.normal&&SbVec2f(v.texcoord)==expected.uv&&
        frame.renderStates[draw.renderStateSlot].model==expected.model,"Cube attributes and transform survive intervening primitive/state-pop");
      if(v.materialSlot>=frame.materials.size()){ok=false;continue;}
      ok &= check(std::memcmp(frame.materials[v.materialSlot].diffuse,expected.diffuse.getValue(),sizeof(float)*4)==0,"intervening raster/line state cannot contaminate Cube diffuse/alpha");
    }
  }
  return check(draws==2&&vertices==48&&cursor==72&&reference.vertices.size()==72,"two compact Cube occurrences remain independently consumed")&&ok;
}
struct PrimitiveEvents {
  std::vector<SbVec3f> lines,points;
  static void line(void *data,SoCallbackAction*,const SoPrimitiveVertex *a,const SoPrimitiveVertex *b) {
    auto &self=*static_cast<PrimitiveEvents*>(data);self.lines.push_back(a->getPoint());self.lines.push_back(b->getPoint());
  }
  static void point(void *data,SoCallbackAction*,const SoPrimitiveVertex *a){static_cast<PrimitiveEvents*>(data)->points.push_back(a->getPoint());}
  void install(SoCallbackAction &action) {
    action.addLineSegmentCallback(SoNode::getClassTypeId(),line,this);
    action.addPointCallback(SoShape::getClassTypeId(),point,this);
  }
};
bool boundaries() {
  bool ok=true;
  // A distinct SoType and observable generator are valid native consumers.
  // Replaying its base class template would incorrectly suppress side effects.
  {
    auto *shape=new CustomCube;Scene scene(SoMaterialBinding::OVERALL,false,shape);
    CoinRenderAction action;action.getPimpl()->planOnly=true;action.setTransparencyType(CoinRenderAction::NONE);
    for(float width:{2.f,3.f,2.f}) {
      shape->width=width;const unsigned before=shape->generated;
      ok &= apply(action,scene,false);
      ok &= check(shape->generated==before+4,"subclass generates both actual occurrences on each native and captured traversal");
    }
  }
  {
    auto *shape=new InheritedTypeCube;Scene scene(SoMaterialBinding::OVERALL,false,shape);
    ok &= check(shape->getTypeId()==SoCube::getClassTypeId(),"C++ subclass deliberately retains native Cube SoType");
    CoinRenderAction action;action.getPimpl()->planOnly=true;action.setTransparencyType(CoinRenderAction::NONE);
    for(float depth:{4.f,2.f,4.f}) {
      shape->depth=depth;const unsigned before=shape->generated;
      ok &= apply(action,scene,false);
      ok &= check(shape->generated==before+4,"native-SoType C++ subclass keeps generator side effects for both occurrences and replay");
    }
  }
  {
    Scene scene;auto *branch=new SoSeparator;
    auto *coordinates=new SoCoordinate3;
    const SbVec3f points[]={SbVec3f(-1,-1,0),SbVec3f(1,-1,0),SbVec3f(-1,1,0),SbVec3f(1,1,0)};
    coordinates->point.setValues(0,4,points);branch->addChild(coordinates);
    auto *line=new SoLineSet;const int32_t lengths[]={2,2};line->numVertices.setValues(0,2,lengths);branch->addChild(line);
    auto *point=new SoPointSet;point->startIndex=2;point->numPoints=2;branch->addChild(point);
    scene.root->insertChild(branch,scene.root->getNumChildren()-1);
    CoinRenderAction action(SbViewportRegion(128,128));action.getPimpl()->planOnly=true;action.setTransparencyType(CoinRenderAction::NONE);
    PrimitiveEvents actual;actual.install(action);
    for(unsigned iteration=0;iteration<2;++iteration) {
      Reference reference;PrimitiveEvents expected;SoCallbackAction native;expected.install(native);
      native.addTriangleCallback(SoCube::getClassTypeId(),Reference::triangle,&reference);
      const auto stamp=scene.stamp();native.apply(scene.root);action.apply(scene.root);
      ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS&&scene.stamp()==stamp,"mixed primitive capture preserves source");
      ok &= check(expected.lines.size()==4&&expected.points.size()==2&&actual.lines==expected.lines&&actual.points==expected.points,"ancestor callbacks see every actual line endpoint and point in native order");
      // Human observers may make Cube compactness ineligible; preserve full
      // expanded attributes instead of imposing an optimization in that case.
      const auto &frame=action.getPimpl()->lastValidPlan;size_t cursor=0;unsigned cubeDraws=0;
      for(const auto &draw:frame.draws)if(draw.sourceNodeId==scene.cube->getNodeId()) {
        ++cubeDraws;
        for(unsigned i=0;i<draw.geometry.indexCount;++i,++cursor) {
          const size_t at=size_t(draw.geometry.firstIndex)+i;
          if(at>=frame.indices.size()||frame.indices[at]>=frame.vertices.size()||cursor>=reference.vertices.size()){ok=false;continue;}
          const auto &v=frame.vertices[frame.indices[at]];const auto &ref=reference.vertices[cursor];
          ok &= check(SbVec3f(v.position)==ref.position&&SbVec3f(v.normal)==ref.normal&&SbVec2f(v.texcoord)==ref.uv,"line/point observer registration preserves all Cube attributes");
          if(v.materialSlot>=frame.materials.size()){ok=false;continue;}
          ok &= check(std::memcmp(frame.materials[v.materialSlot].diffuse,ref.diffuse.getValue(),sizeof(float)*4)==0,"line/point observers preserve Cube material/alpha");
        }
      }
      ok &= check(cubeDraws==2&&cursor==72,"mixed primitive frame retains both full Cube occurrences");
      actual.lines.clear();actual.points.clear();
    }
  }
  {
    Scene scene;auto *branch=new SoSeparator;
    auto *move=new SoTranslation;move->translation.setValue(.5f,.5f,0);branch->addChild(move);
    auto *material=new SoMaterial;material->diffuseColor.setValue(.875f,.125f,.625f);branch->addChild(material);
    auto *texture=new SoTexture2;const unsigned char white[]={255,255,255,255};texture->image.setValue(SbVec2s(1,1),4,white);branch->addChild(texture);
    auto *texgen=new SoTextureCoordinatePlane;texgen->directionS.setValue(.5f,0,0);texgen->directionT.setValue(0,.25f,0);branch->addChild(texgen);
    auto *image=new SoImage;const unsigned char rgba[]={17,43,91,255,211,29,73,255,61,179,101,255,233,151,37,255};image->image.setValue(SbVec2s(2,2),4,rgba);branch->addChild(image);
    scene.root->insertChild(branch,scene.root->getNumChildren()-1);
    CoinRenderAction action(SbViewportRegion(128,128));action.getPimpl()->planOnly=true;action.setTransparencyType(CoinRenderAction::NONE);
    for(unsigned revision=0;revision<3;++revision) {
      if(revision==1)texgen->directionS.setValue(.25f,.5f,0);
      if(revision==2)texgen->directionS.setValue(.5f,0,0);
      Reference reference;SoCallbackAction native;native.addTriangleCallback(SoCube::getClassTypeId(),Reference::triangle,&reference);
      const auto stamp=scene.stamp();const auto imageId=image->getNodeId();native.apply(scene.root);action.apply(scene.root);
      ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS&&stamp==scene.stamp()&&image->getNodeId()==imageId,"raster/texgen capture and state-pop preserve source revisions");
      const auto &frame=action.getPimpl()->lastValidPlan;
      // rasterOccurrences is a shadow-caster inventory, populated only under
      // an eligible ShadowGroup. Here the actual main Image draw is authoritative.
      unsigned imageDraws=0,imageIndices=0;
      for(const auto &draw:frame.draws)if(draw.sourceNodeId==image->getNodeId()) {
        ++imageDraws;imageIndices+=draw.geometry.indexCount;
        ok &= check(draw.geometry.vertexCount==4&&draw.geometry.indexCount==6&&draw.renderStateSlot<frame.renderStates.size(),
          "Image consumes one real four-vertex/six-index pixel quad");
        if(draw.renderStateSlot>=frame.renderStates.size())continue;
        const auto &state=frame.renderStates[draw.renderStateSlot];
        ok &= check(state.model==SbMatrix::identity()&&state.view==SbMatrix::identity()&&state.projectionCoin==SbMatrix::identity()&&state.hasTexture,
          "Image draw consumes screen-space geometry and an actual image binding");
        if(state.textureImageSlot>=frame.textures.size()){ok=false;continue;}
        const auto &payload=frame.textures[state.textureImageSlot];
        ok &= check(payload.width==2&&payload.height==2&&payload.components==4&&payload.pixelsRgba.size()==sizeof(rgba)&&
          std::memcmp(payload.pixelsRgba.data(),rgba,sizeof(rgba))==0,"consumed Image payload retains every authored asymmetric RGBA pixel");
        double minX=2,maxX=-2,minY=2,maxY=-2;
        for(unsigned corner=0;corner<draw.geometry.indexCount;++corner) {
          const size_t at=size_t(draw.geometry.firstIndex)+corner;
          if(at>=frame.indices.size()||frame.indices[at]>=frame.vertices.size()){ok=false;continue;}
          const auto &v=frame.vertices[frame.indices[at]];
          minX=std::min(minX,double(v.position[0]));maxX=std::max(maxX,double(v.position[0]));
          minY=std::min(minY,double(v.position[1]));maxY=std::max(maxY,double(v.position[1]));
          ok &= check((v.texcoord[0]==0||v.texcoord[0]==1)&&(v.texcoord[1]==0||v.texcoord[1]==1),"Image quad carries full pixel-domain UV endpoints");
        }
        ok &= check(maxX-minX==4./128&&maxY-minY==4./128,"two authored image pixels keep their exact viewport footprint");
        ok &= check(state.rasterTextureSourceSlot<frame.renderStates.size(),"inherited texgen has a retained consumed source state");
      }
      ok &= check(imageDraws==1&&imageIndices==6,"exactly one authored Image draw survives internal post callbacks");
      ok &= cubeSubset(frame,reference,scene.cube);
    }
  }
  return ok;
}
} // namespace compact_cube
int main(int argc,char **argv) {
  SoDB::init();CoinRenderAction::initClass();compact_cube::CustomCube::initClass();std::string mode="compact";
  for(int i=1;i<argc;++i) {
    const std::string arg=argv[i];if(arg=="--compact")mode="compact";else if(arg=="--observers")mode="observers";else if(arg=="--boundaries")mode="boundaries";
    else {std::cerr<<"unknown argument "<<arg<<std::endl;return 2;}
  }
  return(mode=="observers"?compact_cube::observers():mode=="boundaries"?compact_cube::boundaries():compact_cube::compact())?0:1;
}
