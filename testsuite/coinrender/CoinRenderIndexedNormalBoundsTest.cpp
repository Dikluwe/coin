#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "CoinRenderBumpProfileFixture.h"
#include "CoinRenderTexture2dStartupTrace.h"
#include <Inventor/nodes/SoSceneTexture2.h>
#include <Inventor/nodes/SoComplexity.h>
#include "rendering/coinrender/CoinRenderIndexedGeometryCore.h"
#include "actions/CoinRenderActionP.h"
#include <Inventor/SoPrimitiveVertex.h>
#include <Inventor/elements/SoNormalElement.h>
#include <Inventor/SoInteraction.h>
#include <Inventor/SoPath.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/actions/SoLineHighlightRenderAction.h>
#include <Inventor/nodes/SoSelection.h>
#include <Inventor/nodes/SoSubNode.h>
#include <Inventor/nodes/SoMaterialBinding.h>
#include <Inventor/rendering/CoinRenderManagerAdapter.h>
#include <Inventor/rendering/CoinRenderSceneManager.h>
#include <Inventor/nodes/SoIndexedLineSet.h>
#include <Inventor/nodes/SoDrawStyle.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/nodes/SoVertexProperty.h>
#include <cctype>
#include <set>
namespace normal_bounds {
bool check(bool value,const std::string &message){if(!value)std::cerr<<"normal_bounds FAIL "<<message<<std::endl;return value;}
bool normalDiagnostic(const char *message) {
  std::string value=message;for(auto &c:value)c=char(std::tolower(static_cast<unsigned char>(c)));
  return value.find("normal")!=std::string::npos &&
    (value.find("index")!=std::string::npos||value.find("bound")!=std::string::npos||value.find("binding")!=std::string::npos||value.find("array")!=std::string::npos||value.find("count")!=std::string::npos);
}
// Runtime type and valid semantics stay native. On malformed baseline inputs
// this observer suppresses only the unsafe native body, after recording entry.
// Therefore failure is deterministic and never relies on reading invalid data.
template<class Base> struct Guarded:Base {
  unsigned nativeEntries=0;bool suppressNative=false;
  void callback(SoCallbackAction *action)override {
    if(action->shouldGeneratePrimitives(this)) {
      ++nativeEntries;if(suppressNative)return;
    }
    Base::callback(action);
  }
};
using Face=Guarded<SoIndexedFaceSet>;
using Line=Guarded<SoIndexedLineSet>;
class TypedFace:public SoIndexedFaceSet {
  SO_NODE_HEADER(TypedFace);
public:
  TypedFace();static void initClass();unsigned nativeEntries=0;bool suppressNative=false;
  void callback(SoCallbackAction *action)override {
    if(action->shouldGeneratePrimitives(this)){++nativeEntries;if(suppressNative)return;}
    SoIndexedFaceSet::callback(action);
  }
};
SO_NODE_SOURCE(TypedFace);
TypedFace::TypedFace(){SO_NODE_CONSTRUCTOR(TypedFace);}
void TypedFace::initClass(){SO_NODE_INIT_CLASS(TypedFace,SoIndexedFaceSet,"IndexedFaceSet");}
class TypedLine:public SoIndexedLineSet {
  SO_NODE_HEADER(TypedLine);
public:
  TypedLine();static void initClass();unsigned nativeEntries=0;bool suppressNative=false;
  void callback(SoCallbackAction *action)override {
    if(action->shouldGeneratePrimitives(this)){++nativeEntries;if(suppressNative)return;}
    SoIndexedLineSet::callback(action);
  }
};
SO_NODE_SOURCE(TypedLine);
TypedLine::TypedLine(){SO_NODE_CONSTRUCTOR(TypedLine);}
void TypedLine::initClass(){SO_NODE_INIT_CLASS(TypedLine,SoIndexedLineSet,"IndexedLineSet");}
const SbVec3f positions[]={{-.6f,-.6f,0},{.6f,-.6f,0},{-.6f,.6f,0},{.6f,.6f,0},{0,.8f,0}};
const SbVec3f normals[]={{0,0,1},{.6f,0,.8f},{0,.6f,.8f},{-.6f,0,.8f},{0,-.6f,.8f},{0,0,-1}};
const int bindings[]={SoNormalBinding::OVERALL,SoNormalBinding::PER_PART,SoNormalBinding::PER_PART_INDEXED,
  SoNormalBinding::PER_FACE,SoNormalBinding::PER_FACE_INDEXED,SoNormalBinding::PER_VERTEX,SoNormalBinding::PER_VERTEX_INDEXED};
CoinRenderDirectGeometryView view(const int32_t *indices,size_t count) {
  CoinRenderDirectGeometryView v;v.positions={positions,5};v.coordIndex={indices,count};return v;
}
bool core() {
  bool ok=true;unsigned positive=0,negative=0;
  const int32_t faces[]={0,1,2,-1,1,3,2,-1};
  const int32_t lines[]={0,1,2,-1,2,3,-1};
  const int32_t faceVertexIndices[]={2,1,0,-1,3,2,1,-1};
  const int32_t lineVertexIndices[]={3,2,1,-1,0,2,-1};
  const int32_t pair[]={1,0},segments[]={2,1,0};
  CoinRenderIndexedGeometryOptions options;options.hasTraversalState=true;options.materialCount=1;options.baseColorLighting=false;
  for(bool line:{false,true})for(int binding:bindings) {
    auto v=view(line?lines:faces,line?7:8);v.normals={normals,6};v.normalBinding=static_cast<SoNormalBindingElement::Binding>(binding);
    if(binding==SoNormalBinding::PER_VERTEX_INDEXED)v.normalIndex=line?CoinRenderSpan<int32_t>(lineVertexIndices,7):CoinRenderSpan<int32_t>(faceVertexIndices,8);
    else if(binding==SoNormalBinding::PER_FACE_INDEXED)v.normalIndex={pair,2};
    else if(binding==SoNormalBinding::PER_PART_INDEXED)v.normalIndex=line?CoinRenderSpan<int32_t>(segments,3):CoinRenderSpan<int32_t>(pair,2);
    const auto result=line?CoinRenderIndexedGeometryCore::buildLines(v,options):CoinRenderIndexedGeometryCore::buildFaces(v,options);
    ok &= check(result.status==CoinRenderFastPathResult::SUCCESS_PRUNE&&result.indices.size()==6,"valid binding must yield six deterministic expanded indices");
    const unsigned faceCoordinate[]={0,1,2,1,3,2},lineCoordinate[]={0,1,1,2,2,3};
    const unsigned vertexExplicitFace[]={2,1,0,3,2,1},vertexExplicitLine[]={3,2,2,1,0,2};
    const unsigned partLine[]={0,0,1,1,2,2},partIndexedLine[]={2,2,1,1,0,0};
    const unsigned lineVertexOrdinal[]={0,1,1,2,3,4};
    if(result.indices.size()==6)for(unsigned corner=0;corner<6;++corner) {
      unsigned slot=0;
      switch(binding) {
      case SoNormalBinding::PER_FACE:slot=line?(corner<4?0:1):corner/3;break;
      case SoNormalBinding::PER_FACE_INDEXED:slot=line?(corner<4?1:0):1-corner/3;break;
      case SoNormalBinding::PER_PART:slot=line?partLine[corner]:corner/3;break;
      case SoNormalBinding::PER_PART_INDEXED:slot=line?partIndexedLine[corner]:1-corner/3;break;
      case SoNormalBinding::PER_VERTEX:slot=line?lineVertexOrdinal[corner]:corner;break;
      case SoNormalBinding::PER_VERTEX_INDEXED:slot=line?vertexExplicitLine[corner]:vertexExplicitFace[corner];break;
      }
      const auto index=result.indices[corner];
      if(index>=result.vertices.size()){ok=false;continue;}
      const auto &vertex=result.vertices[index].vertex;
      ok &= check(SbVec3f(vertex.normal)==normals[slot]&&SbVec3f(vertex.position)==positions[line?lineCoordinate[corner]:faceCoordinate[corner]],
                  "normal binding preserves authorial occurrence/index orientation");
    }
    ++positive;
  }
  for(bool line:{false,true}) {
    auto v=view(line?lines:faces,line?7:8);v.normalBinding=SoNormalBindingElement::PER_VERTEX_INDEXED;
    auto result=line?CoinRenderIndexedGeometryCore::buildLines(v,options):CoinRenderIndexedGeometryCore::buildFaces(v,options);
    ok &= check(result.status==CoinRenderFastPathResult::SUCCESS_PRUNE&&!result.indices.empty(),"absent normal arrays remain valid automatic/default normals");++positive;
    for(int binding:{SoNormalBinding::OVERALL,SoNormalBinding::PER_VERTEX}) {
      const int32_t idle[]={999,-7,999};v.normals={normals,6};v.normalBinding=static_cast<SoNormalBindingElement::Binding>(binding);v.normalIndex={idle,3};
      result=line?CoinRenderIndexedGeometryCore::buildLines(v,options):CoinRenderIndexedGeometryCore::buildFaces(v,options);
      ok &= check(result.status==CoinRenderFastPathResult::SUCCESS_PRUNE,"unconsumed normalIndex is not an arbitrary rejection");++positive;
    }
    for(int binding:{SoNormalBinding::PER_VERTEX,SoNormalBinding::PER_VERTEX_INDEXED,SoNormalBinding::PER_FACE,SoNormalBinding::PER_PART}) {
      v=view(line?lines:faces,line?7:8);v.normals={normals,1};v.normalBinding=static_cast<SoNormalBindingElement::Binding>(binding);
      result=line?CoinRenderIndexedGeometryCore::buildLines(v,options):CoinRenderIndexedGeometryCore::buildFaces(v,options);
      ok &= check(result.status!=CoinRenderFastPathResult::SUCCESS_PRUNE&&result.vertices.empty()&&result.indices.empty(),"insufficient consumed span cannot produce a partial valid payload");++negative;
    }
    const int32_t badVertex[]={0,1,8,-1,0,1,2,-1},badFace[]={0,8};
    for(int binding:{SoNormalBinding::PER_VERTEX_INDEXED,SoNormalBinding::PER_FACE_INDEXED,SoNormalBinding::PER_PART_INDEXED}) {
      v=view(line?lines:faces,line?7:8);v.normals={normals,6};v.normalBinding=static_cast<SoNormalBindingElement::Binding>(binding);
      v.normalIndex=binding==SoNormalBinding::PER_VERTEX_INDEXED?CoinRenderSpan<int32_t>(badVertex,8):CoinRenderSpan<int32_t>(badFace,2);
      result=line?CoinRenderIndexedGeometryCore::buildLines(v,options):CoinRenderIndexedGeometryCore::buildFaces(v,options);
      ok &= check(result.status!=CoinRenderFastPathResult::SUCCESS_PRUNE&&result.vertices.empty()&&result.indices.empty(),"consumed out-of-range normal indices leave Core output atomic");++negative;
    }
    const int32_t empty[]={-1};v=view(empty,1);v.normals={normals,1};v.normalBinding=SoNormalBindingElement::PER_VERTEX_INDEXED;
    result=line?CoinRenderIndexedGeometryCore::buildLines(v,options):CoinRenderIndexedGeometryCore::buildFaces(v,options);
    ok &= check(result.status==CoinRenderFastPathResult::SUCCESS_PRUNE&&result.indices.empty(),"empty geometry consumes no normal slot");++positive;
  }
  std::cout<<"normal_bounds_core positive="<<positive<<" negative="<<negative<<std::endl;return ok;
}
template<class Shape> struct Scene {
  SoSeparator *root=new SoSeparator,*subjectBranch=new SoSeparator;
  Shape *subject=new Shape;
  SoNormal *normal=new SoNormal;
  SoNormalBinding *binding=new SoNormalBinding;
  SoCoordinate3 *coordinates=new SoCoordinate3;
  bool line;
  explicit Scene(bool isLine):line(isLine) {
    root->ref();auto *camera=new SoOrthographicCamera;camera->position.setValue(0,0,4);camera->height=4;camera->nearDistance=1;camera->farDistance=9;root->addChild(camera);
    auto *model=new SoLightModel;model->model=SoLightModel::BASE_COLOR;root->addChild(model);
    auto *move=new SoTranslation;move->translation.setValue(-.8f,0,0);subjectBranch->addChild(move);
    auto *material=new SoMaterial;material->diffuseColor.setValue(.8f,.4f,.2f);material->transparency=0;subjectBranch->addChild(material);
    subjectBranch->addChild(normal);subjectBranch->addChild(binding);subjectBranch->addChild(coordinates);
    if(line){auto *style=new SoDrawStyle;style->lineWidth=3;subjectBranch->addChild(style);}
    subjectBranch->addChild(subject);root->addChild(subjectBranch);good();
    auto *anchor=new SoSeparator;auto *shift=new SoTranslation;shift->translation.setValue(.8f,0,0);anchor->addChild(shift);
    auto *color=new SoMaterial;color->diffuseColor.setValue(.2f,.6f,.8f);anchor->addChild(color);
    auto *aNormal=new SoNormal;const SbVec3f n[]={SbVec3f(0,0,1),SbVec3f(0,0,1),SbVec3f(0,0,1)};aNormal->vector.setValues(0,3,n);anchor->addChild(aNormal);
    auto *aBinding=new SoNormalBinding;aBinding->value=SoNormalBinding::PER_VERTEX_INDEXED;anchor->addChild(aBinding);
    auto *aCoord=new SoCoordinate3;aCoord->point.setValues(0,3,positions);anchor->addChild(aCoord);
    auto *face=new SoIndexedFaceSet;const int32_t tri[]={0,1,2,-1};face->coordIndex.setValues(0,4,tri);anchor->addChild(face);root->addChild(anchor);
  }
  ~Scene(){root->unref();}
  void good(unsigned count=3) {
    subject->vertexProperty=nullptr;subject->suppressNative=false;
    coordinates->point.setValues(0,5,positions);
    const int32_t tri[]={0,1,2,-1};subject->coordIndex.setValues(0,4,tri);
    subject->normalIndex.setValue(-1);binding->value=SoNormalBinding::PER_VERTEX_INDEXED;
    normal->vector.setNum(int(count));for(unsigned i=0;i<count;++i)normal->vector.set1Value(int(i),SbVec3f(0,0,1));
  }
  void twoParts() {
    const int32_t faceIndices[]={0,1,2,-1,1,3,2,-1},lineIndices[]={0,1,-1,1,2,-1};
    subject->coordIndex.setValues(0,line?6:8,line?lineIndices:faceIndices);
  }
  void pentagon() {
    const SbVec3f p[]={SbVec3f(-.6f,-.5f,0),SbVec3f(.4f,-.6f,0),SbVec3f(.6f,0,0),SbVec3f(0,.6f,0),SbVec3f(-.6f,.4f,0)};
    coordinates->point.setValues(0,5,p);const int32_t index[]={0,1,2,3,4,-1};subject->coordIndex.setValues(0,6,index);
  }
  std::vector<SbUniqueId> stamp()const{return {root->getNodeId(),normal->getNodeId(),binding->getNodeId(),coordinates->getNodeId(),subject->getNodeId()};}
};
struct Events {
  const SoNode *subject;unsigned subjectTriangles=0,subjectLines=0;std::vector<int> order;
  explicit Events(const SoNode *s):subject(s){}
  static SoCallbackAction::Response pre(void *data,SoCallbackAction*,const SoNode *node){auto &s=*static_cast<Events*>(data);s.order.push_back(node==s.subject?1:4);return SoCallbackAction::CONTINUE;}
  static SoCallbackAction::Response post(void *data,SoCallbackAction*,const SoNode *node){auto &s=*static_cast<Events*>(data);s.order.push_back(node==s.subject?3:6);return SoCallbackAction::CONTINUE;}
  static void triangle(void *data,SoCallbackAction *action,const SoPrimitiveVertex*,const SoPrimitiveVertex*,const SoPrimitiveVertex*) {auto &s=*static_cast<Events*>(data);const bool subject=action->getCurPath()->getTail()==s.subject;if(subject)++s.subjectTriangles;s.order.push_back(subject?2:5);}
  static void segment(void *data,SoCallbackAction *action,const SoPrimitiveVertex*,const SoPrimitiveVertex*) {auto &s=*static_cast<Events*>(data);const bool subject=action->getCurPath()->getTail()==s.subject;if(subject)++s.subjectLines;s.order.push_back(subject?2:5);}
  void install(CoinRenderAction &action) {
    action.addPreCallback(SoShape::getClassTypeId(),pre,this);action.addPostCallback(SoShape::getClassTypeId(),post,this);
    action.addTriangleCallback(SoShape::getClassTypeId(),triangle,this);action.addLineSegmentCallback(SoShape::getClassTypeId(),segment,this);
  }
};
void configure(CoinRenderAction &action,int route) {
  action.getPimpl()->planOnly=true;
  if(route==1)action.setFastPathEnabled(FALSE);
  if(route==3)action.setDrawMode(CoinRenderAction::DrawMode::WIREFRAME);
}
template<class Shape> bool capture(bool line) {
  bool ok=true;unsigned positives=0,negatives=0;
  for(int route=0;route<4;++route) {
    for(int binding:bindings) {
      Scene<Shape> s(line);s.good(5);s.binding->value=binding;
      CoinRenderAction action;configure(action,route);Events events(s.subject);if(route==2)events.install(action);
      const auto stamp=s.stamp();action.apply(s.root);const auto &frame=action.getPimpl()->lastValidPlan;
      bool correct=action.getLastStatus()==CoinRenderAction::SUCCESS&&frame.isValid()&&s.stamp()==stamp;
      unsigned indices=0;
      for(const auto &draw:frame.draws)if(draw.sourceNodeId==s.subject->getNodeId()) {
        indices+=draw.geometry.indexCount;
        for(unsigned i=0;i<draw.geometry.indexCount;++i) {
          const size_t offset=size_t(draw.geometry.firstIndex)+i;
          if(offset>=frame.indices.size()||frame.indices[offset]>=frame.vertices.size()){correct=false;continue;}
          const auto &vertex=frame.vertices[frame.indices[offset]];
          correct &= vertex.normal[0]==0&&vertex.normal[1]==0&&vertex.normal[2]==1;
        }
      }
      correct &= indices>0;
      if(route==2) {
        const std::vector<int> expected=line?std::vector<int>{1,2,2,3,4,5,6}:std::vector<int>{1,2,3,4,5,6};
        correct &= events.order==expected&&s.subject->nativeEntries==1;
      }
      std::cout<<"normal_bounds_valid line="<<line<<" binding="<<binding<<" route="<<route<<" status="<<action.getLastStatus()<<" native="<<s.subject->nativeEntries<<" indices="<<indices<<" error="<<action.getLastError().getString()<<std::endl;
      ok &= check(correct,"valid public binding/callback path preserves explicit normals and source");++positives;
    }
    for(unsigned kind=0;kind<8;++kind) {
      if(line&&kind==7)continue;
      Scene<Shape> s(line);s.good();
      if(kind==0)s.normal->vector.setNum(1);
      else if(kind==1){s.binding->value=SoNormalBinding::PER_VERTEX;s.normal->vector.setNum(1);}
      else if(kind==2){s.twoParts();s.binding->value=SoNormalBinding::PER_FACE;s.normal->vector.setNum(1);}
      else if(kind==3){s.twoParts();s.binding->value=SoNormalBinding::PER_PART;s.normal->vector.setNum(1);}
      else if(kind==4){const int32_t bad[]={0,1,9,-1};s.subject->normalIndex.setValues(0,4,bad);}
      else if(kind==5){s.twoParts();s.binding->value=SoNormalBinding::PER_FACE_INDEXED;const int32_t bad[]={0,9};s.subject->normalIndex.setValues(0,2,bad);}
      else if(kind==6){auto *vp=new SoVertexProperty;vp->vertex.setValues(0,3,positions);vp->normal.setValue(SbVec3f(0,0,1));vp->normalBinding=SoVertexProperty::PER_VERTEX_INDEXED;s.subject->vertexProperty=vp;}
      else {s.pentagon();s.normal->vector.setNum(1);}
      s.subject->suppressNative=true;
      CoinRenderAction action;configure(action,route);Events events(s.subject);if(route==2)events.install(action);
      const auto stamp=s.stamp();action.apply(s.root);
      std::cout<<"normal_bounds_invalid line="<<line<<" kind="<<kind<<" route="<<route<<" status="<<action.getLastStatus()<<" native="<<s.subject->nativeEntries<<" error="<<action.getLastError().getString()<<std::endl;
      ok &= check(action.getLastStatus()==CoinRenderAction::INVALID_SCENE&&normalDiagnostic(action.getLastError().getString())&&
        s.subject->nativeEntries==0&&events.subjectTriangles==0&&events.subjectLines==0&&s.stamp()==stamp,
        "malformed public normals rejected before the observed native primitive-generation boundary");++negatives;
    }
    for(unsigned variant=0;variant<4;++variant) {
      Scene<Shape> s(line);s.good(5);
      if(variant==0)s.normal->vector.setNum(0);
      else if(variant==1){s.binding->value=SoNormalBinding::OVERALL;s.normal->vector.setNum(1);const int32_t idle[]={999,-7};s.subject->normalIndex.setValues(0,2,idle);}
      else if(variant==2){auto *vp=new SoVertexProperty;vp->vertex.setValues(0,3,positions);vp->normal.setValue(SbVec3f(0,0,1));vp->normalBinding=SoVertexProperty::OVERALL;s.subject->vertexProperty=vp;}
      else s.subject->coordIndex.setNum(0);
      CoinRenderAction action;configure(action,route);Events events(s.subject);if(route==2)events.install(action);
      action.apply(s.root);
      ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS&&action.getPimpl()->lastValidPlan.isValid(),"automatic/OVERALL/vertex-property/empty valid domains remain admitted");++positives;
    }
  }
  std::cout<<"normal_bounds_capture line="<<line<<" positive="<<positives<<" negative="<<negatives<<std::endl;return ok;
}
template<class Shape> bool typedAndOverrides(bool line) {
  bool ok=true;Scene<Shape> scene(line);CoinRenderAction action;configure(action,2);
  Events events(scene.subject);events.install(action);action.apply(scene.root);
  ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS&&scene.subject->nativeEntries==1&&
    (line?events.subjectLines==2:events.subjectTriangles==1),"own SoType subclass preserves valid delegated native callbacks");
  scene.normal->vector.setNum(1);scene.subject->suppressNative=true;scene.subject->nativeEntries=0;
  action.apply(scene.root);
  ok &= check(action.getLastStatus()==CoinRenderAction::INVALID_SCENE&&scene.subject->nativeEntries==0,
              "own SoType subclass rejects malformed normals before its native callback");
  scene.good();events.order.clear();action.apply(scene.root);
  ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS,"own SoType valid recovery remains admitted");
  auto *overrideBinding=new SoNormalBinding;overrideBinding->value=SoNormalBinding::OVERALL;overrideBinding->setOverride(TRUE);
  scene.root->insertChild(overrideBinding,2);scene.normal->vector.setNum(1);
  action.apply(scene.root);
  ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS,"inherited OVERALL override consumes only one normal");
  overrideBinding->value=SoNormalBinding::PER_VERTEX_INDEXED;scene.binding->value=SoNormalBinding::OVERALL;
  scene.subject->suppressNative=true;scene.subject->nativeEntries=0;action.apply(scene.root);
  ok &= check(action.getLastStatus()==CoinRenderAction::INVALID_SCENE&&scene.subject->nativeEntries==0,
              "effective inherited vertex binding defeats ignored local OVERALL and rejects short normals");
  auto *overrideNormal=new SoNormal;const SbVec3f unit[]={SbVec3f(0,0,1),SbVec3f(0,0,1),SbVec3f(0,0,1)};
  overrideNormal->vector.setValues(0,3,unit);overrideNormal->setOverride(TRUE);scene.root->insertChild(overrideNormal,2);
  scene.subject->suppressNative=false;action.apply(scene.root);
  ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS,"inherited normal override supplies valid normals despite short ignored child");
  return ok;
}
bool asymmetricAttributes() {
  Scene<Face> scene(false);
  const SbColor colors[]={SbColor(.8f,.2f,.1f),SbColor(.1f,.7f,.3f),SbColor(.3f,.2f,.9f)};
  auto *material=static_cast<SoMaterial*>(scene.subjectBranch->getChild(1));material->diffuseColor.setValues(0,3,colors);
  auto *binding=new SoMaterialBinding;binding->value=SoMaterialBinding::PER_VERTEX_INDEXED;
  scene.subjectBranch->insertChild(binding,scene.subjectBranch->getNumChildren()-1);
  auto *texture=new SoTexture2;const uint8_t white[]={255,255,255,255};texture->image.setValue(SbVec2s(1,1),4,white);
  scene.subjectBranch->insertChild(texture,scene.subjectBranch->getNumChildren()-1);
  auto *uv=new SoTextureCoordinate2;const SbVec2f coords[]={SbVec2f(.125f,.25f),SbVec2f(.75f,.125f),SbVec2f(.25f,.875f)};
  uv->point.setValues(0,3,coords);scene.subjectBranch->insertChild(uv,scene.subjectBranch->getNumChildren()-1);
  bool ok=true;
  for(bool fast:{true,false}) {
    CoinRenderAction action;action.getPimpl()->planOnly=true;action.setFastPathEnabled(fast);action.apply(scene.root);
    const auto &frame=action.getPimpl()->lastValidPlan;unsigned corners=0;
    ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS,"asymmetric material and UV valid capture");
    for(const auto &draw:frame.draws)if(draw.sourceNodeId==scene.subject->getNodeId()) {
      for(unsigned i=0;i<draw.geometry.indexCount;++i) {
        const size_t offset=size_t(draw.geometry.firstIndex)+i;
        if(offset>=frame.indices.size()||frame.indices[offset]>=frame.vertices.size()){ok=false;continue;}
        const auto &vertex=frame.vertices[frame.indices[offset]];const unsigned corner=corners++;
        if(corner>=3||vertex.materialSlot>=frame.materials.size()){ok=false;continue;}
        ok &= check(SbVec3f(vertex.position)==positions[corner]&&SbVec2f(vertex.texcoord)==coords[corner]&&
          vertex.normal[0]==0&&vertex.normal[1]==0&&vertex.normal[2]==1,"normal validation preserves authored vertex and UV order");
        const auto &packed=frame.materials[vertex.materialSlot];
        for(unsigned c=0;c<3;++c)ok &= check(packed.diffuse[c]==colors[corner][c],"normal validation preserves per-corner diffuse material");
      }
    }
    ok &= check(corners==3,"complete asymmetric triangle is captured once");
  }
  return ok;
}
class Inspection:public CoinRenderBackend {
public:
  unsigned roots=0;CoinRenderFramePlan frame;std::string diagnostic="CPU capture stops before commands";
  bool isGpuBackend()const override{return true;}
  CoinRenderBackendStatus getStatus()const override{return CoinRenderBackendStatus::SUCCESS;}
  CoinRenderBackendStatus prepare(CoinRenderTargetP&)override{return CoinRenderBackendStatus::SUCCESS;}
  CoinRenderSubmitResult submit(const CoinRenderFramePlan &f,CoinRenderTargetP&)override{++roots;frame=f;return {CoinRenderBackendStatus::UNSUPPORTED,diagnostic};}
  void poll()override{}const std::string &getLastError()const override{return diagnostic;}
};
struct NativeLine:SoLineHighlightRenderAction {
  unsigned applies=0;
  void apply(SoNode *n)override{++applies;SoLineHighlightRenderAction::apply(n);}
  void apply(SoPath *p)override{++applies;SoLineHighlightRenderAction::apply(p);}
  void apply(const SoPathList &p,SbBool obey=FALSE)override{++applies;SoLineHighlightRenderAction::apply(p,obey);}
};
bool pathReplay() {
  Scene<Face> scene(false);scene.subjectBranch->ref();scene.root->removeChild(scene.subjectBranch);
  auto *selection=new SoSelection;selection->addChild(scene.subjectBranch);scene.subjectBranch->unref();scene.root->insertChild(selection,2);
  auto *path=new SoPath(selection);path->ref();path->append(0);path->append(scene.subjectBranch->getNumChildren()-1);selection->select(path);
  NativeLine highlight;highlight.setVisible(TRUE);highlight.setLineWidth(3);highlight.setTransparencyType(SoGLRenderAction::NONE);
  SoRenderManager manager;manager.setViewportRegion(SbViewportRegion(96,96));manager.setAutoClipping(SoRenderManager::NO_AUTO_CLIPPING);
  manager.setSceneGraph(scene.root);manager.setGLRenderAction(&highlight);
  CoinRenderManagerAdapter adapter(manager,SbVec2i32(96,96));auto *target=adapter.getSceneManager()->getRenderTarget();
  auto *inspection=new Inspection;target->getPimpl().get().backend.reset(inspection);
  const auto valid=adapter.render();bool ok=check(valid==CoinRenderAction::UNSUPPORTED&&inspection->roots==1&&highlight.applies==0,
    "selected public LINE path reaches capture without GL or GPU");
  unsigned base=0,stroke=0;for(const auto &draw:inspection->frame.draws) {
    if(draw.sourceNodeId==scene.subject->getNodeId()&&draw.isSceneLayer())base+=draw.geometry.indexCount;
    if(draw.sourceNodeId==scene.subject->getNodeId()&&!draw.isSceneLayer())stroke+=draw.geometry.indexCount;
  }
  ok &= check(base==3&&stroke>0,"real selected path retains base and replay geometry");
  scene.normal->vector.setNum(1);scene.subject->suppressNative=true;scene.subject->nativeEntries=0;
  const auto roots=inspection->roots;const auto invalid=adapter.render();
  ok &= check(invalid==CoinRenderAction::INVALID_SCENE&&normalDiagnostic(adapter.getLastError().getString())&&
    inspection->roots==roots&&scene.subject->nativeEntries==0&&highlight.applies==0,
    "malformed selected shape refuses before base/native replay/submission");
  scene.good();ok &= check(adapter.render()==CoinRenderAction::UNSUPPORTED&&inspection->roots==roots+1,"valid selected path recovers on same manager target");
  path->unref();manager.setGLRenderAction(nullptr);manager.setSceneGraph(nullptr);return ok;
}
struct EffectiveNormals {
  const SoNode *subject;unsigned calls=0;int count;SoNormalBindingElement::Binding binding;
  SbVec3f backing[3]={SbVec3f(0,0,1),SbVec3f(0,0,1),SbVec3f(0,0,1)};
  EffectiveNormals(const SoNode *node,int n,SoNormalBindingElement::Binding b):subject(node),count(n),binding(b){}
  static SoCallbackAction::Response before(void *data,SoCallbackAction *action,const SoNode *node) {
    auto &s=*static_cast<EffectiveNormals*>(data);if(node!=s.subject)return SoCallbackAction::CONTINUE;
    ++s.calls;
    SoNormalElement::set(action->getState(),const_cast<SoNode*>(node),s.count,s.backing,TRUE);
    SoNormalBindingElement::set(action->getState(),s.binding);
    return SoCallbackAction::CONTINUE;
  }
};
template<class Shape> bool callbackState(bool line) {
  bool ok=true;
  for(unsigned variant=0;variant<4;++variant) {
    Scene<Shape> scene(line);
    if(variant!=0)scene.normal->vector.setNum(1);
    if(variant==1)scene.binding->value=SoNormalBinding::OVERALL;
    const auto binding=variant==3?SoNormalBindingElement::OVERALL:SoNormalBindingElement::PER_VERTEX_INDEXED;
    EffectiveNormals modifier(scene.subject,variant==2?3:1,binding);
    const bool invalid=variant<2;scene.subject->suppressNative=invalid;
    CoinRenderAction action;action.getPimpl()->planOnly=true;
    action.addPreCallback(line?SoIndexedLineSet::getClassTypeId():SoIndexedFaceSet::getClassTypeId(),EffectiveNormals::before,&modifier);
    const auto stamp=scene.stamp();action.apply(scene.root);
    std::cout<<"normal_bounds_effective_callback line="<<line<<" variant="<<variant<<" called="<<modifier.calls
      <<" status="<<action.getLastStatus()<<" native="<<scene.subject->nativeEntries<<" error="<<action.getLastError().getString()<<std::endl;
    bool correct=modifier.calls==1&&scene.stamp()==stamp;
    if(invalid)correct &= action.getLastStatus()==CoinRenderAction::INVALID_SCENE&&normalDiagnostic(action.getLastError().getString())&&scene.subject->nativeEntries==0;
    else correct &= action.getLastStatus()==CoinRenderAction::SUCCESS&&scene.subject->nativeEntries==1&&action.getPimpl()->lastValidPlan.isValid();
    ok &= check(correct,invalid?"pre-callback invalid effective normals/binding rejected before native generation":
                               "pre-callback valid repair honored before native generation without source mutation");
  }
  return ok;
}
struct VrmlNativeAttributes {
  const SoNode *subject;unsigned consumed,count=0,seen=0;const SbColor *colors;const SbVec2f *uv;bool checkUV,valid=true;
  VrmlNativeAttributes(const SoNode *n,unsigned c,const SbColor *m,const SbVec2f *t,bool texture):subject(n),consumed(c),colors(m),uv(t),checkUV(texture){}
  void vertex(SoCallbackAction *action,const SoPrimitiveVertex *v) {
    if(action->getCurPathTail()!=subject)return;
    unsigned coordinate=consumed;
    for(unsigned c=0;c<consumed;++c)if(v->getPoint()==positions[c]){coordinate=c;break;}
    ++count;if(coordinate==consumed){valid=false;return;}seen|=1u<<coordinate;
    valid &= v->getNormal()==normals[coordinate];
    SbColor ambient,diffuse,specular,emission;float shininess,transparency;
    action->getMaterial(ambient,diffuse,specular,emission,shininess,transparency,v->getMaterialIndex());
    valid &= diffuse==colors[coordinate];
    if(checkUV)valid &= v->getTextureCoords()[0]==uv[coordinate][0]&&v->getTextureCoords()[1]==uv[coordinate][1];
  }
  static void triangle(void *data,SoCallbackAction *a,const SoPrimitiveVertex *x,const SoPrimitiveVertex *y,const SoPrimitiveVertex *z) {
    auto &self=*static_cast<VrmlNativeAttributes*>(data);self.vertex(a,x);self.vertex(a,y);self.vertex(a,z);
  }
  static void segment(void *data,SoCallbackAction *a,const SoPrimitiveVertex *x,const SoPrimitiveVertex *y) {
    auto &self=*static_cast<VrmlNativeAttributes*>(data);self.vertex(a,x);self.vertex(a,y);
  }
};
// VRML1 PER_VERTEX addresses the coordinate domain, not the expanded
// occurrence domain. Native IFS/ILS replace normalIndex with coordIndex here.
// The two-part fixtures share coordinates: six/four occurrences consume only
// four/three normals respectively, even with deliberately irrelevant indices.
template<class Shape> bool vrml1Capture(bool line) {
  bool ok=true;
  for(int route=0;route<4;++route)for(bool explicitIdle:{false,true}) {
    Scene<Shape> scene(line);scene.twoParts();
    scene.subject->setNodeType(SoNode::VRML1);scene.binding->value=SoNormalBinding::PER_VERTEX;
    const unsigned consumed=line?3:4;scene.normal->vector.setValues(0,int(consumed),normals);
    scene.normal->vector.setNum(int(consumed));
    // VRML1 PER_VERTEX material binding has the same coordinate-domain rule.
    // Explicit materialIndex is deliberately irrelevant, just like normalIndex.
    const SbColor colors[]={SbColor(.75f,.25f,.125f),SbColor(.125f,.75f,.375f),SbColor(.375f,.25f,.875f),SbColor(.625f,.5f,.25f)};
    auto *material=static_cast<SoMaterial*>(scene.subjectBranch->getChild(1));
    material->diffuseColor.setValues(0,int(consumed),colors);
    auto *materialBinding=new SoMaterialBinding;materialBinding->value=SoMaterialBinding::PER_VERTEX;
    scene.subjectBranch->insertChild(materialBinding,scene.subjectBranch->getNumChildren()-1);
    const int32_t idleMaterials[]={999,-7,999,-7,999,-7,999,-7};
    scene.subject->materialIndex.setValues(0,8,idleMaterials);
    const SbVec2f authoredUV[]={SbVec2f(.125f,.25f),SbVec2f(.75f,.125f),SbVec2f(.25f,.875f),SbVec2f(.625f,.75f)};
    // Filled IFS consumes ordinary per-coordinate UVs; LINE replay has its own
    // stroke attributes and is covered here for normal/material restoration.
    if(!line&&route!=3) {
      auto *texture=new SoTexture2;const uint8_t white[]={255,255,255,255};
      texture->image.setValue(SbVec2s(1,1),4,white);texture->model=SoTexture2::MODULATE;
      scene.subjectBranch->insertChild(texture,scene.subjectBranch->getNumChildren()-1);
      auto *uv=new SoTextureCoordinate2;uv->point.setValues(0,int(consumed),authoredUV);
      scene.subjectBranch->insertChild(uv,scene.subjectBranch->getNumChildren()-1);
      scene.subject->textureCoordIndex.setValue(-1);
    }
    if(explicitIdle) {
      const int32_t idle[]={999,-7,999,-7,999,-7,999,-7};
      scene.subject->normalIndex.setValues(0,8,idle);
    }
    // Independent native primitive traversal witnesses original object-space
    // normal/material/UV values before CoinRender expands lines into NDC quads.
    // It does not register callbacks on or disable the tested fastpath action.
    VrmlNativeAttributes native(scene.subject,consumed,colors,authoredUV,!line&&route!=3);
    SoCallbackAction reference;reference.addTriangleCallback(SoShape::getClassTypeId(),VrmlNativeAttributes::triangle,&native);
    reference.addLineSegmentCallback(SoShape::getClassTypeId(),VrmlNativeAttributes::segment,&native);
    const auto referenceStamp=scene.stamp();reference.apply(scene.root);
    ok &= check(native.valid&&native.count==(line?4u:6u)&&native.seen==((1u<<consumed)-1)&&scene.stamp()==referenceStamp,
      "native VRML1 primitive attributes match independent coordinate-domain authoring");
    scene.subject->nativeEntries=0;
    CoinRenderAction action(SbViewportRegion(128,128));configure(action,route);Events events(scene.subject);if(route==2)events.install(action);
    auto stamp=scene.stamp();action.apply(scene.root);
    const auto &frame=action.getPimpl()->lastValidPlan;unsigned corners=0,seen=0;
    bool valid=action.getLastStatus()==CoinRenderAction::SUCCESS&&frame.isValid()&&scene.stamp()==stamp;
    for(const auto &draw:frame.draws)if(draw.sourceNodeId==scene.subject->getNodeId()) {
      for(unsigned i=0;i<draw.geometry.indexCount;++i) {
        const size_t at=size_t(draw.geometry.firstIndex)+i;
        if(at>=frame.indices.size()||frame.indices[at]>=frame.vertices.size()){valid=false;continue;}
        const auto &vertex=frame.vertices[frame.indices[at]];unsigned coordinate=consumed;
        if(line||route==3) {
          // Solid strokes still interpolate at raster-cell boundaries; those
          // boundaries need not retain an exact endpoint material. Orthographic
          // authoring in a square 128-pixel viewport gives world = 2*NDC and
          // removes aspect ambiguity. Width offsets affect only the minor axis,
          // so the major coordinate independently determines interpolation t.
          ++corners;
          if(vertex.materialSlot>=frame.materials.size()){valid=false;continue;}
          const auto &material=frame.materials[vertex.materialSlot];
          const double x=2.*vertex.position[0]+.8,y=2.*vertex.position[1];
          const unsigned faceEdges[][2]={{0,1},{1,2},{2,0},{1,3},{3,2}};
          const unsigned lineEdges[][2]={{0,1},{1,2}};
          bool matched=false;
          for(unsigned edge=0;edge<(line?2u:5u);++edge) {
            const unsigned first=line?lineEdges[edge][0]:faceEdges[edge][0];
            const unsigned last=line?lineEdges[edge][1]:faceEdges[edge][1];
            const double dx=positions[last][0]-positions[first][0],dy=positions[last][1]-positions[first][1];
            const bool xMajor=std::abs(dx)>=std::abs(dy);
            const double rawT=xMajor?(x-positions[first][0])/dx:(y-positions[first][1])/dy;
            const double t=std::max(0.,std::min(1.,rawT));
            const double px=positions[first][0]+t*dx,py=positions[first][1]+t*dy;
            // Half-width <=1.5 pixels and endpoint cell extension <=1 pixel
            // give distance <=hypot(1.5,1), below the existing margin2.1 even
            // after the 1/256-pixel grid shift/rounding. No extra band is added.
            if(std::hypot(x-px,y-py)>2.1*4./128)continue;
            bool same=true;
            for(unsigned lane=0;lane<3;++lane) {
              const double expected=colors[first][lane]*(1.-t)+colors[last][lane]*t;
              // Grid shift plus rounding is <=1.5/256 pixel. A 1.2-world
              // major span covers38.4 pixels, so even full channel contrast
              // changes by <255*(1.5/256)/38.4 = .039 RGB levels. Float
              // arithmetic is smaller still; this uses the existing RGB3 gate.
              same &= std::abs(255.*(material.diffuse[lane]-expected))<=3.;
            }
            if(same) {
              matched=true;
              if(t<.25)seen|=1u<<first;
              if(t>.75)seen|=1u<<last;
            }
          }
          valid &= matched;
          continue;
        }
        for(unsigned c=0;c<consumed;++c)if(SbVec3f(vertex.position)==positions[c]){coordinate=c;break;}
        if(coordinate==consumed){valid=false;continue;}
        seen|=1u<<coordinate;++corners;
        valid &= SbVec3f(vertex.normal)==normals[coordinate];
        if(vertex.materialSlot>=frame.materials.size())valid=false;
        else for(unsigned channel=0;channel<3;++channel)
          valid &= frame.materials[vertex.materialSlot].diffuse[channel]==colors[coordinate][channel];
        if(!line&&route!=3)valid &= SbVec2f(vertex.texcoord)==authoredUV[coordinate];
      }
    }
    valid &= seen==((1u<<consumed)-1)&&corners>0;
    if(!line&&route!=3)valid &= corners==6;
    if(route==2)valid &= scene.subject->nativeEntries==1&&
      (line?events.subjectLines==2:events.subjectTriangles==2);
    std::cout<<"normal_bounds_vrml1 line="<<line<<" route="<<route<<" idle_indices="<<explicitIdle
      <<" status="<<action.getLastStatus()<<" corners="<<corners<<" coordinate_mask="<<seen
      <<" error="<<action.getLastError().getString()<<std::endl;
    ok &= check(valid,"VRML1 coordinate-domain normals/materials ignore idle authored indices and preserve every expanded attribute including filled IFS UV");
    // The highest consumed coordinate still needs its own normal. Guard the
    // native body on the old baseline so the negative never reads beyond it.
    scene.normal->vector.setNum(int(consumed-1));scene.subject->suppressNative=true;scene.subject->nativeEntries=0;
    const unsigned primitiveCount=events.subjectLines+events.subjectTriangles;stamp=scene.stamp();action.apply(scene.root);
    ok &= check(action.getLastStatus()==CoinRenderAction::INVALID_SCENE&&normalDiagnostic(action.getLastError().getString())&&
      scene.subject->nativeEntries==0&&events.subjectLines+events.subjectTriangles==primitiveCount&&scene.stamp()==stamp,
      "VRML1 insufficient consumed coordinate normal is rejected before native generation");
    scene.normal->vector.setValues(0,int(consumed),normals);scene.subject->suppressNative=false;
    stamp=scene.stamp();action.apply(scene.root);
    ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS&&scene.stamp()==stamp,
      "VRML1 valid coordinate-domain normals recover on the same action");
  }
  return ok;
}
bool extendedCapture() {
  bool ok=typedAndOverrides<TypedFace>(false);ok &= typedAndOverrides<TypedLine>(true);
  ok &= asymmetricAttributes();ok &= pathReplay();ok &= callbackState<Face>(false);ok &= callbackState<Line>(true);
  ok &= vrml1Capture<Face>(false);ok &= vrml1Capture<Line>(true);return ok;
}
class Audit:public bump_profile::Proxy {
public:
  using Proxy::Proxy;unsigned roots=0;
  CoinRenderSubmitResult submit(const CoinRenderFramePlan &f,CoinRenderTargetP &t)override{++roots;return Proxy::submit(f,t);}
  CoinRenderSubmitResult submit(const CoinRenderFramePlan &f,CoinRenderTargetP &t,const CoinRenderFrameReuseDecision &r)override{++roots;return Proxy::submit(f,t,r);}
};
double segment(double x,double y,double ax,double ay,double bx,double by) {
  const double dx=bx-ax,dy=by-ay,t=std::max(0.,std::min(1.,((x-ax)*dx+(y-ay)*dy)/(dx*dx+dy*dy)));return std::hypot(x-ax-t*dx,y-ay-t*dy);
}
bool pixels(const bump_profile::Frame &frame,bool line) {
  if(frame.rgba.size()!=96*96*4||frame.depth.size()!=96*96)return false;
  unsigned subject=0,anchor=0,outside=0,bad=0,depthBad=0;
  const double pixel=4./96,margin=2.1*pixel;
  for(unsigned y=0;y<96;++y)for(unsigned x=0;x<96;++x) {
    const size_t p=size_t(y)*96+x;const double wx=(x+.5)*pixel-2,wy=2-(y+.5)*pixel;
    const double left=wx+.8,right=wx-.8;
    const double a=std::min({right+.6,wy+.6,(-right-wy)/std::sqrt(2.)});
    const double b=std::min({left+.6,wy+.6,(-left-wy)/std::sqrt(2.)});
    const double edge=std::min(segment(left,wy,-.6,-.6,.6,-.6),segment(left,wy,.6,-.6,-.6,.6));
    const bool inAnchor=a>margin,inSubject=!line&&b>margin;
    if(line) {
      bool solid=true;const double expected[]={204,102,51};
      for(unsigned c=0;c<3;++c)solid &= std::abs(frame.rgba[4*p+c]-expected[c])<=3;
      if(solid){++subject;bad+=edge>(1.5+2.1)*pixel;depthBad+=std::abs(frame.depth[p]-.375)>2e-5;}
    }
    if(inAnchor||inSubject) {
      const double expectedAnchor[]={51,153,204},expectedSubject[]={204,102,51};
      if(inAnchor)++anchor;else ++subject;
      for(unsigned c=0;c<3;++c)bad+=std::abs(frame.rgba[4*p+c]-(inAnchor?expectedAnchor[c]:expectedSubject[c]))>3;
      bad+=frame.rgba[4*p+3]!=255;depthBad+=std::abs(frame.depth[p]-.375)>2e-5;
    }
    if(a < -margin && (line?edge>(1.5+2.1)*pixel:b < -margin)) {
      ++outside;const double clear[]={.03*255,.05*255,.07*255};
      for(unsigned c=0;c<3;++c)bad+=std::abs(frame.rgba[4*p+c]-clear[c])>3;
      bad+=frame.rgba[4*p+3]!=255;depthBad+=frame.depth[p]!=1;
    }
  }
  std::cout<<"normal_bounds_image line="<<line<<" subject="<<subject<<" anchor="<<anchor<<" outside="<<outside<<" bad="<<bad<<" depth_bad="<<depthBad<<std::endl;
  return check(subject>(line?10u:50u)&&anchor>50&&outside>500&&!bad&&!depthBad,"independent valid subject/anchor/clear RGB and depth");
}
template<class Shape> bool gpu(bool line,CoinRenderRenderer renderer) {
  Scene<Shape> scene(line);auto options=samplingTestOptions();options.renderer=renderer;
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(96,96),options));if(!target)return false;
  auto &p=target->getPimpl().get();if(p.prepareBackend()!=CoinRenderBackendStatus::SUCCESS||!p.backend||!p.backend->isGpuBackend())return false;
  auto *audit=new Audit(std::move(p.backend));p.backend.reset(audit);target->setDepthReadbackEnabled(TRUE);
  CoinRenderAction action(SbViewportRegion(96,96));action.setRenderTarget(target.get());action.setBackgroundColor(SbColor4f(.03f,.05f,.07f,1));action.setTransparencyType(CoinRenderAction::NONE);
  bump_profile::Frame stable;
  auto render=[&](const char *label) {
    const auto roots=audit->roots;const auto stamp=scene.stamp();const uint64_t before=target->getLastSubmissionSerial();
    action.apply(scene.root);bump_profile::Frame current;target->readbackRGBA(current.rgba);target->readbackDepth(current.depth);current.serial=target->getLastSubmissionSerial();
    std::cout<<"normal_bounds_gpu "<<label<<" status="<<action.getLastStatus()<<" serial="<<before<<"->"<<current.serial<<" error="<<action.getLastError().getString()<<std::endl;
    const bool good=check(action.getLastStatus()==CoinRenderAction::SUCCESS&&audit->roots==roots+1&&current.serial>before&&scene.stamp()==stamp,"single valid GPU publication/source preserved")&&pixels(current,line);
    if(good)stable=std::move(current);return good;
  };
  bool ok=render("explicit normals predecessor");
  for(unsigned kind=0;ok&&kind<3;++kind) {
    scene.good();scene.subject->nativeEntries=0;scene.subject->suppressNative=true;
    if(kind==0)scene.normal->vector.setNum(1);
    else if(kind==1){const int32_t invalid[]={0,1,9,-1};scene.subject->normalIndex.setValues(0,4,invalid);action.setFastPathEnabled(FALSE);}
    else {auto *vp=new SoVertexProperty;vp->vertex.setValues(0,3,positions);vp->normal.setValue(SbVec3f(0,0,1));vp->normalBinding=SoVertexProperty::PER_VERTEX_INDEXED;scene.subject->vertexProperty=vp;}
    const auto roots=audit->roots,producers=audit->producers;const auto stamp=scene.stamp();
    action.apply(scene.root);bump_profile::Frame current;target->readbackRGBA(current.rgba);target->readbackDepth(current.depth);current.serial=target->getLastSubmissionSerial();
    std::cout<<"normal_bounds_gpu_refuse kind="<<kind<<" status="<<action.getLastStatus()<<" native="<<scene.subject->nativeEntries<<" error="<<action.getLastError().getString()<<std::endl;
    ok &= check(action.getLastStatus()==CoinRenderAction::INVALID_SCENE&&normalDiagnostic(action.getLastError().getString())&&
      audit->roots==roots&&audit->producers==producers&&scene.subject->nativeEntries==0&&scene.stamp()==stamp&&
      current.serial==stable.serial&&current.rgba==stable.rgba&&current.depth==stable.depth,"deterministic pre-generation GPU refusal with exact rollback and no work");
    scene.good();action.setFastPathEnabled(TRUE);if(ok)ok=render("same target valid recovery");
  }
  if(ok){scene.normal->vector.setNum(0);ok=render("automatic/default normals remain valid");}
  action.setRenderTarget(nullptr);return ok;
}
// The full-UV consumer makes every source ROI observable. Its own plane writes
// depth everywhere, including where the source texture contains its clear color.
bool rttPixels(const bump_profile::Frame &frame) {
  if(frame.rgba.size()!=96*96*4||frame.depth.size()!=96*96)return false;
  unsigned subject=0,anchor=0,outside=0,bad=0,depthBad=0;
  const double pixel=4./96,margin=2.1*4./64;
  for(unsigned y=0;y<96;++y)for(unsigned x=0;x<96;++x) {
    const size_t p=size_t(y)*96+x;const double wx=(x+.5)*pixel-2,wy=2-(y+.5)*pixel;
    const double left=wx+.8,right=wx-.8;
    const double a=std::min({right+.6,wy+.6,(-right-wy)/std::sqrt(2.)});
    const double b=std::min({left+.6,wy+.6,(-left-wy)/std::sqrt(2.)});
    const bool inAnchor=a>margin,inSubject=b>margin,empty=a < -margin&&b < -margin;
    if(!inAnchor&&!inSubject&&!empty)continue;
    const double ca[]={51,153,204},cs[]={204,102,51},clear[]={.03*255,.05*255,.07*255};
    if(inAnchor)++anchor;else if(inSubject)++subject;else ++outside;
    for(unsigned c=0;c<3;++c)bad+=std::abs(frame.rgba[4*p+c]-(inAnchor?ca[c]:inSubject?cs[c]:clear[c]))>3;
    bad+=frame.rgba[4*p+3]!=255;depthBad+=std::abs(frame.depth[p]-.375)>2e-5;
  }
  std::cout<<"normal_bounds_rtt_image subject="<<subject<<" anchor="<<anchor<<" outside="<<outside<<" bad="<<bad<<" depth_bad="<<depthBad<<std::endl;
  return check(subject>50&&anchor>50&&outside>500&&!bad&&!depthBad,"independent source-color ROIs and consumer-plane depth");
}
unsigned completed(const std::string &trace) {
#ifdef COIN_NORMAL_BOUNDS_BGFX
  const std::string marker="COIN_RENDER_PHASE bgfx lower_ms=";
#else
  const std::string marker="COIN_RENDER_PHASE bridge pack_ms=";
#endif
  size_t pos=0;unsigned n=0;while((pos=trace.find(marker,pos))!=std::string::npos){++n;pos+=marker.size();}return n;
}
bool gpuRtt(bool direct,CoinRenderRenderer renderer) {
  Scene<Face> source(false);
  SoSeparator *root=new SoSeparator;root->ref();
  auto *camera=new SoOrthographicCamera;camera->position.setValue(0,0,4);camera->height=4;camera->nearDistance=1;camera->farDistance=9;root->addChild(camera);
  auto *model=new SoLightModel;model->model=SoLightModel::BASE_COLOR;root->addChild(model);
  auto *quality=new SoComplexity;quality->textureQuality=.1f;root->addChild(quality);
  auto *material=new SoMaterial;material->diffuseColor.setValue(1,1,1);root->addChild(material);
  auto *texture=new SoSceneTexture2;texture->scene=source.root;texture->size.setValue(64,64);
  texture->type=SoSceneTexture2::RGBA8;texture->transparencyFunction=SoSceneTexture2::NONE;
  texture->backgroundColor.setValue(.03f,.05f,.07f,1);texture->model=SoSceneTexture2::MODULATE;
  texture->wrapS=texture->wrapT=SoSceneTexture2::CLAMP;root->addChild(texture);
  auto *uv=new SoTextureCoordinate2;const SbVec2f texcoords[]={SbVec2f(0,0),SbVec2f(1,0),SbVec2f(1,1),SbVec2f(0,1)};uv->point.setValues(0,4,texcoords);root->addChild(uv);
  auto *coordinates=new SoCoordinate3;const SbVec3f corners[]={SbVec3f(-2,-2,0),SbVec3f(2,-2,0),SbVec3f(2,2,0),SbVec3f(-2,2,0)};coordinates->point.setValues(0,4,corners);root->addChild(coordinates);
  auto *shape=new SoIndexedFaceSet;const int32_t quad[]={0,1,2,-1,0,2,3,-1};shape->coordIndex.setValues(0,8,quad);shape->textureCoordIndex.setValues(0,8,quad);root->addChild(shape);
  auto options=samplingTestOptions();options.renderer=renderer;options.sceneTexture=direct?COIN_RENDER_SCENE_TEXTURE_DIRECT:COIN_RENDER_SCENE_TEXTURE_STAGED;
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(96,96),options));
  if(!target){root->unref();return false;}
  auto &p=target->getPimpl().get();if(p.prepareBackend()!=CoinRenderBackendStatus::SUCCESS||!p.backend||!p.backend->isGpuBackend()){root->unref();return false;}
  auto *audit=new Audit(std::move(p.backend));p.backend.reset(audit);target->setDepthReadbackEnabled(TRUE);
  CoinRenderAction action(SbViewportRegion(96,96));action.setRenderTarget(target.get());action.setTransparencyType(CoinRenderAction::NONE);
  startup::Environment tracing("COIN_RENDER_TRACE_PHASES","1");bump_profile::Frame stable;
  auto read=[&](){bump_profile::Frame f;target->readbackRGBA(f.rgba);target->readbackDepth(f.depth);f.serial=target->getLastSubmissionSerial();return f;};
  auto apply=[&](const char *label,bool valid) {
    const unsigned roots=audit->roots,producers=audit->producers;const auto stamp=source.stamp();
    const SbUniqueId rootId=root->getNodeId(),textureId=texture->getNodeId();const uint64_t before=target->getLastSubmissionSerial();
    startup::Trace trace;if(!check(trace.start(),"passive RTT completion capture"))return false;
    action.apply(root);const std::string events=trace.stop();const auto current=read();const unsigned phases=completed(events);
    std::cout<<"normal_bounds_rtt label="<<label<<" direct="<<direct<<" status="<<action.getLastStatus()<<" serial="<<before<<"->"<<current.serial<<" roots="<<audit->roots-roots<<" direct_producers="<<audit->producers-producers<<" completed="<<phases<<" native="<<source.subject->nativeEntries<<" error="<<action.getLastError().getString()<<std::endl;
    bool ok=check(source.stamp()==stamp&&root->getNodeId()==rootId&&texture->getNodeId()==textureId,"RTT source and consumer unchanged during apply");
    if(valid) {
#ifdef COIN_NORMAL_BOUNDS_BGFX
      const unsigned expectedPhases=direct?1:2;
#else
      const unsigned expectedPhases=2;
#endif
      ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS&&current.serial>before&&audit->roots==roots+1&&
        audit->producers==producers+(direct?1:0)&&audit->declaredProducers==1&&phases==expectedPhases,"one real source and root execution through requested route");
      ok &= rttPixels(current);if(ok)stable=current;
    } else {
      ok &= check(action.getLastStatus()==CoinRenderAction::INVALID_SCENE&&normalDiagnostic(action.getLastError().getString())&&source.subject->nativeEntries==0&&
        audit->roots==roots&&audit->producers==producers&&phases==0&&current.serial==stable.serial&&current.rgba==stable.rgba&&current.depth==stable.depth,
        "malformed source refused before native generation or completed producer work with exact parent rollback");
    }
    return ok;
  };
  bool ok=apply("valid predecessor",true);
  for(unsigned kind=0;ok&&kind<2;++kind) {
    source.good();source.subject->nativeEntries=0;source.subject->suppressNative=true;
    if(kind==0)source.normal->vector.setNum(1);
    else {const int32_t invalid[]={0,1,9,-1};source.subject->normalIndex.setValues(0,4,invalid);action.setFastPathEnabled(FALSE);}
    ok &= apply("invalid source",false);source.good();action.setFastPathEnabled(TRUE);if(ok)ok &= apply("same target recovery",true);
  }
  action.setRenderTarget(nullptr);target.reset();root->unref();return ok;
}
} // namespace normal_bounds
int main(int argc,char **argv) {
  using namespace normal_bounds;std::string mode="core";bool line=false,rtt=false,direct=false;
  for(int i=1;i<argc;++i) {
    const std::string arg=argv[i];if(arg=="--core")mode="core";else if(arg=="--capture")mode="capture";else if(arg=="--extended")mode="extended";else if(arg=="--gpu")mode="gpu";else if(arg=="--lines")line=true;else if(arg=="--rtt")rtt=true;else if(arg=="--direct")direct=true;else if(arg=="--staged")direct=false;
    else {std::cerr<<"unknown option "<<arg<<std::endl;return 2;}
  }
  SoDB::init();SoInteraction::init();CoinRenderAction::initClass();TypedFace::initClass();TypedLine::initClass();
  if(mode=="core")return core()?0:1;
  if(mode=="extended")return extendedCapture()?0:1;
  if(mode=="capture") {const bool faces=capture<Face>(false),lines=capture<Line>(true);return faces&&lines?0:1;}
  CoinRenderCapabilities caps{};const int result=coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,&caps,sizeof(caps));
  std::cout<<"normal_bounds_environment query="<<result<<" gpu="<<caps.gpu_available<<" backend="<<caps.backend<<" renderer="<<caps.renderer<<" adapter="<<caps.adapter_name<<" diagnostic="<<caps.diagnostic<<std::endl;
  if(result!=0)return 1;if(!caps.gpu_available)return 77;
  if(rtt)return gpuRtt(direct,static_cast<CoinRenderRenderer>(caps.renderer))?0:1;
  return(line?gpu<Line>(true,static_cast<CoinRenderRenderer>(caps.renderer)):gpu<Face>(false,static_cast<CoinRenderRenderer>(caps.renderer)))?0:1;
}
