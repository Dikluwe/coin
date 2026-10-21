// Reuse only the established image comparison and sampling helpers. The new
// scenes, authored index maps, ordering equations and lifecycle are below.
#define main coin_render_legacy_concave_alpha_main
#include "CoinRenderConcaveAlphaTest.cpp"
#undef main
#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/SoPrimitiveVertex.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoNormal.h>
#include <Inventor/nodes/SoNormalBinding.h>
#include <Inventor/nodes/SoSubNode.h>
#include <Inventor/nodes/SoTextureCoordinateBinding.h>
#include "actions/CoinRenderActionP.h"

class ObservedAlphaIndexedFaceSet:public SoIndexedFaceSet {
  SO_NODE_HEADER(ObservedAlphaIndexedFaceSet);
public:
  ObservedAlphaIndexedFaceSet(){SO_NODE_CONSTRUCTOR(ObservedAlphaIndexedFaceSet);}
  static void initClass(){SO_NODE_INIT_CLASS(ObservedAlphaIndexedFaceSet,SoIndexedFaceSet,"IndexedFaceSet");}
  unsigned generated=0;
protected:
  void generatePrimitives(SoAction *a)override{++generated;SoIndexedFaceSet::generatePrimitives(a);}
};
SO_NODE_SOURCE(ObservedAlphaIndexedFaceSet);
namespace indexed_alpha {
const int ci[8]={3,6,1,7,0,5,2,4};
const int mi[8]={5,2,7,1,6,0,4,3};
const int ni[8]={7,5,3,1,0,2,4,6};
const int ti[8]={2,4,6,0,1,3,5,7};
const unsigned char indexedTexels[16]={128,224,64,192, 240,64,192,128, 48,192,240,255, 224,160,80,64};
const SbColor background(.0625f,.125f,.25f);
const SbColor colors[2]={SbColor(.8f,.1f,.2f),SbColor(.1f,.7f,.3f)};
const float opacity[2]={.25f,.625f};
const SbVec3f normals[8]={SbVec3f(0,0,1),SbVec3f(0,.6f,.8f),SbVec3f(.6f,0,.8f),SbVec3f(-.6f,0,.8f),SbVec3f(0,-.6f,.8f),SbVec3f(.8f,.6f,0),SbVec3f(-.8f,.6f,0),SbVec3f(0,.8f,.6f)};
bool near(float a,float b){return std::abs(a-b)<1e-5f;}
struct Scene {
  SoSeparator *root=new SoSeparator,*group[2];
  SoCoordinate3 *coords[2];SoMaterial *material[2];SoNormal *normal[2];
  SoTextureCoordinate2 *uv[2];SoMaterialBinding *mb[2];SoNormalBinding *nb[2];
  SoDrawStyle *style[2];SoClipPlane *clip[2];SoTexture2 *texture[2];SoPolygonOffset *offset[2];
  SoShape *shape=nullptr;ObservedAlphaIndexedFaceSet *derived=nullptr;
  int kind,binding=0;bool variable=false,invalid=false,reversed=false,textured=false;
  float z[2]={.2f,-.2f};float slope=0;
  explicit Scene(int k):kind(k){
    root->ref();auto *camera=new SoOrthographicCamera;camera->position.setValue(0,0,3);camera->height=2;camera->nearDistance=.1f;camera->farDistance=10;root->addChild(camera);
    auto *base=new SoSeparator;auto *lm=new SoLightModel;lm->model=SoLightModel::BASE_COLOR;base->addChild(lm);auto *m=new SoMaterial;m->diffuseColor=background;base->addChild(m);
    auto *p=new SoCoordinate3;const SbVec3f quad[]={SbVec3f(-1,-1,-.6f),SbVec3f(1,-1,-.6f),SbVec3f(1,1,-.6f),SbVec3f(-1,1,-.6f)};p->point.setValues(0,4,quad);base->addChild(p);auto *face=new SoFaceSet;face->numVertices=4;base->addChild(face);root->addChild(base);
    if(k==0){auto *f=new SoFaceSet;f->numVertices=8;shape=f;}
    else {auto *f=k==2?static_cast<SoIndexedFaceSet*>(derived=new ObservedAlphaIndexedFaceSet):new SoIndexedFaceSet;
      int32_t indices[9];for(int i=0;i<8;++i)indices[i]=ci[i];indices[8]=-1;f->coordIndex.setValues(0,9,indices);
      for(int i=0;i<8;++i)indices[i]=mi[i];f->materialIndex.setValues(0,9,indices);
      for(int i=0;i<8;++i)indices[i]=ni[i];f->normalIndex.setValues(0,9,indices);
      for(int i=0;i<8;++i)indices[i]=ti[i];f->textureCoordIndex.setValues(0,9,indices);shape=f;
    }
    for(int o=0;o<2;++o){group[o]=new SoSeparator;group[o]->ref();
      auto *light=new SoLightModel;light->model=SoLightModel::BASE_COLOR;group[o]->addChild(light);
      auto *h=new SoShapeHints;h->faceType=SoShapeHints::UNKNOWN_FACE_TYPE;h->vertexOrdering=SoShapeHints::UNKNOWN_ORDERING;group[o]->addChild(h);
      material[o]=new SoMaterial;group[o]->addChild(material[o]);mb[o]=new SoMaterialBinding;group[o]->addChild(mb[o]);normal[o]=new SoNormal;group[o]->addChild(normal[o]);nb[o]=new SoNormalBinding;group[o]->addChild(nb[o]);
      style[o]=new SoDrawStyle;style[o]->lineWidth=3;style[o]->pointSize=9;group[o]->addChild(style[o]);
      offset[o]=new SoPolygonOffset;offset[o]->on=FALSE;offset[o]->styles=SoPolygonOffset::LINES|SoPolygonOffset::POINTS;offset[o]->units=0;group[o]->addChild(offset[o]);
      clip[o]=new SoClipPlane;clip[o]->plane=SbPlane(SbVec3f(0,1,0),0);clip[o]->on=TRUE;group[o]->addChild(clip[o]);
      auto *quality=new SoComplexity;quality->textureQuality=.1f;group[o]->addChild(quality);
      texture[o]=new SoTexture2;const unsigned char white[]={255,255,255,255};texture[o]->image.setValue(SbVec2s(1,1),4,white);texture[o]->model=SoTexture2::MODULATE;group[o]->addChild(texture[o]);
      uv[o]=new SoTextureCoordinate2;group[o]->addChild(uv[o]);auto *tb=new SoTextureCoordinateBinding;tb->value=SoTextureCoordinateBinding::PER_VERTEX_INDEXED;group[o]->addChild(tb);
      coords[o]=new SoCoordinate3;group[o]->addChild(coords[o]);group[o]->addChild(shape);root->addChild(group[o]);
    }attributes(0,false);positions();
  }
  ~Scene(){root->unref();group[0]->unref();group[1]->unref();}
  int materialIndex(int corner)const{return binding==0?0:(kind&&binding==2?mi[corner]:corner);}
  int normalIndex(int corner)const{return binding==0?0:(kind&&binding==2?ni[corner]:corner);}
  int uvIndex(int corner)const{return kind?ti[corner]:corner;}
  void attributes(int b,bool varied){binding=b;variable=varied;
    for(int o=0;o<2;++o){mb[o]->value=b==0?SoMaterialBinding::OVERALL:(b==1?SoMaterialBinding::PER_VERTEX:SoMaterialBinding::PER_VERTEX_INDEXED);nb[o]->value=b==0?SoNormalBinding::OVERALL:(b==1?SoNormalBinding::PER_VERTEX:SoNormalBinding::PER_VERTEX_INDEXED);
      for(int j=0;j<8;++j){material[o]->diffuseColor.set1Value(j,varied?SbColor(.125f+.0625f*j,.625f-.03125f*j,.1875f+.03125f*o):colors[o]);material[o]->transparency.set1Value(j,varied?.125f+.0625f*j:1-opacity[o]);normal[o]->vector.set1Value(j,normals[j]);uv[o]->point.set1Value(j,SbVec2f(.125f+.09f*j,.2f+.07f*j));}
    }
  }
  void useTexture(){textured=true;for(int o=0;o<2;++o)texture[o]->image.setValue(SbVec2s(2,2),4,indexedTexels);}
  void positions(){for(int o=0;o<2;++o){for(int j=0;j<8;++j){SbVec3f p=outline[j];p[0]+=o?.125f:-.125f;p[2]=z[o]+slope*outline[j][0];coords[o]->point.set1Value(kind?ci[j]:j,p);}coords[o]->point.set1Value(8,SbVec3f(8,8,8));}}
  void order(bool reverse){root->removeChild(group[0]);root->removeChild(group[1]);root->addChild(group[reverse?1:0]);root->addChild(group[reverse?0:1]);reversed=reverse;}
  void drawstyle(int value,bool clipping=true){for(int o=0;o<2;++o){style[o]->style=value;clip[o]->on=clipping;}}
  int corner(int o,const SbVec3f &p)const{for(int j=0;j<8;++j)if((p-coords[o]->point[kind?ci[j]:j]).length()<1e-5f)return j;return -1;}
};
struct Observer {
  Scene &scene;unsigned before=0,after=0,triangles=0;bool good=true;
  explicit Observer(Scene&s):scene(s){}
  void reset(){before=after=triangles=0;good=true;}
  static SoCallbackAction::Response pre(void *d,SoCallbackAction*,const SoNode*n){auto &o=*static_cast<Observer*>(d);if(n==o.scene.shape)++o.before;return SoCallbackAction::CONTINUE;}
  static SoCallbackAction::Response post(void *d,SoCallbackAction*,const SoNode*n){auto &o=*static_cast<Observer*>(d);if(n==o.scene.shape)++o.after;return SoCallbackAction::CONTINUE;}
  static void triangle(void*d,SoCallbackAction*a,const SoPrimitiveVertex*x,const SoPrimitiveVertex*y,const SoPrimitiveVertex*z){auto &o=*static_cast<Observer*>(d);if(a->getCurPath()->getTail()!=o.scene.shape)return;++o.triangles;if(o.scene.invalid)return;
    const int branch=a->getCurPath()->getNodeFromTail(1)==o.scene.group[0]?0:1;
    for(const auto *v:{x,y,z}){const int c=o.scene.corner(branch,v->getPoint());if(!check(c>=0,"callback keeps authored indexed corner")){o.good=false;continue;}
      const int m=o.scene.materialIndex(c),n=o.scene.normalIndex(c),t=o.scene.uvIndex(c);
      SbColor amb,diff,spec,emission;float shine,transparency;a->getMaterial(amb,diff,spec,emission,shine,transparency,v->getMaterialIndex());
      const auto expectedUv=o.scene.uv[branch]->point[t];const auto actualUv=v->getTextureCoords();
      o.good &= check((diff-o.scene.material[branch]->diffuseColor[m]).length()<1e-5f&&near(transparency,o.scene.material[branch]->transparency[m])&&(v->getNormal()-o.scene.normal[branch]->vector[n]).length()<1e-5f&&near(actualUv[0],expectedUv[0])&&near(actualUv[1],expectedUv[1]),"callback selects independent material/normal/UV indices for the current occurrence");
    }
  }
  void install(CoinRenderAction &a){a.addPreCallback(SoShape::getClassTypeId(),pre,this);a.addPostCallback(SoShape::getClassTypeId(),post,this);a.addTriangleCallback(SoShape::getClassTypeId(),triangle,this);}
};
struct Harness {
  std::unique_ptr<CoinRenderTarget> target;CoinRenderAction action{SbViewportRegion(side,side)};
  explicit Harness(bool gpu){auto opts=samplingTestOptions();opts.rasterizationPolicy=canonical?COIN_RENDER_RASTER_CANONICAL_8BIT:COIN_RENDER_RASTER_NATIVE;target.reset(CoinRenderTarget::createOffscreen(SbVec2i32(side,side),opts));if(target){if(!gpu)target->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);target->setDepthReadbackEnabled(TRUE);action.setRenderTarget(target.get());action.setBackgroundColor(SbColor4f(0,0,0,1));}}
  ~Harness(){action.setRenderTarget(nullptr);}
  bool take(Scene&s,Observer &o,Image &out,const std::string &label){if(!target)return check(false,label+" target");o.reset();const unsigned generated=s.derived?s.derived->generated:0;const auto serial=target->getLastSubmissionSerial();action.apply(s.root);target->readbackRGBA(out.rgba);target->readbackDepth(out.depth);out.serial=target->getLastSubmissionSerial();
    bool ok=check(action.getLastStatus()==CoinRenderAction::SUCCESS&&out.serial>serial&&out.rgba.size()==size_t(side*side*4)&&out.depth.size()==size_t(side*side),label+" first apply publication: "+action.getLastError().getString());
    ok &= check(o.before==2&&o.after==2&&o.triangles==12&&o.good,label+" both shared occurrences preserve callbacks/triangulation");if(s.derived)ok &= check(s.derived->generated==generated+2,label+" subclass override executed for each occurrence");return ok;
  }
};
bool additive(int mode){return mode==SoGLRenderAction::ADD||mode==SoGLRenderAction::SORTED_OBJECT_ADD;}
bool immediate(int mode){return mode==SoGLRenderAction::ADD||mode==SoGLRenderAction::BLEND;}
bool sorted(int mode){return mode==SoGLRenderAction::SORTED_OBJECT_BLEND||mode==SoGLRenderAction::SORTED_OBJECT_ADD||mode==SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND;}
std::array<double,4> source(const Scene&s,int o,int style,int x){
  const int a=style==SoDrawStyle::POINTS?(o==0?1:0):0,b=style==SoDrawStyle::POINTS?a:1;
  const double t=style==SoDrawStyle::POINTS?0:(((x+.5)/48.-1)-(outline[0][0]+(o?.125:-.125)))/.35;
  const auto ca=s.material[o]->diffuseColor[s.materialIndex(a)],cb=s.material[o]->diffuseColor[s.materialIndex(b)];
  const auto ua=s.uv[o]->point[s.uvIndex(a)],ub=s.uv[o]->point[s.uvIndex(b)];
  std::array<double,4> out;
  for(int c=0;c<3;++c)out[c]=(1-t)*ca[c]+t*cb[c];
  out[3]=1-((1-t)*s.material[o]->transparency[s.materialIndex(a)]+t*s.material[o]->transparency[s.materialIndex(b)]);
  if(s.textured){const double u=(1-t)*ua[0]+t*ub[0],v=(1-t)*ua[1]+t*ub[1];const int tx=std::min(1,int(std::floor((u-std::floor(u))*2))),ty=std::min(1,int(std::floor((v-std::floor(v))*2)));for(int c=0;c<4;++c)out[c]*=indexedTexels[4*(2*ty+tx)+c]/255.;}
  return out;
}
bool witness(const Image &image,const Scene&s,int mode,int style,bool oneShape=false){
  bool ok=true;const int xs[4]={48,style==SoDrawStyle::POINTS?20:10,style==SoDrawStyle::POINTS?15:28,18};const unsigned masks[4]={0,1,2,3};
  for(int w=0;w<4;++w){double rgb[3]={background[0],background[1],background[2]};double depth=(3+.6-.1)/9.9;
    int order[2]={s.reversed?1:0,s.reversed?0:1};if(sorted(mode)&&(!oneShape||mode==SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND)){order[0]=s.z[0]<s.z[1]?0:1;order[1]=1-order[0];}
    for(int k=0;k<2;++k){const int o=order[k];if(!(masks[w]&(1u<<o)))continue;const double fragment=(3-s.z[o]-.1)/9.9;if(fragment>=depth)continue;
      const auto src=source(s,o,style,xs[w]);const double alpha=src[3];for(int c=0;c<3;++c)rgb[c]=alpha*src[c]+(additive(mode)?1:1-alpha)*rgb[c];if(immediate(mode))depth=fragment;
    }
    const size_t p=pixel(xs[w],style==SoDrawStyle::FILLED?72:86);for(int c=0;c<3;++c){const int expected=int(std::lround(255*std::min(1.,rgb[c])));ok &= check(std::abs(int(image.rgba[p+c])-expected)<=5,"analytic zero/one/two contribution RGB x="+std::to_string(xs[w])+" mode="+std::to_string(mode)+" channel="+std::to_string(c)+" actual="+std::to_string(image.rgba[p+c])+" expected="+std::to_string(expected));}
    ok &= check(image.rgba[p+3]==255&&std::abs(image.depth[p/4]-depth)<=2e-5,"opaque destination alpha and authored immediate/deferred depth");
  }
  // Entire central cut gap and lower clipped area must remain the base.
  unsigned left=0,right=0;
  for(int y=4;y<92;++y)for(int x=4;x<92;++x){const size_t p=pixel(x,y);bool changed=false;for(int c=0;c<3;++c)changed |= std::abs(int(image.rgba[p+c])-int(std::lround(255*background[c])))>5;
    if((x>=41&&x<=55&&y>=45&&y<=56)||y<35)ok &= check(!changed,"clipped components have no bridge and no lower remnant");
    if(changed&&y>52){left+=x<39;right+=x>57;}
  }
  return check(left>10&&right>10,"both disconnected components are present")&&ok;
}
bool planAttributes(Harness &h,const Scene&s){bool ok=true;size_t count=0;const auto &p=h.action.getPimpl()->lastValidPlan;
  for(const auto &d:p.draws)if(d.sourceNodeId==s.shape->getNodeId())for(unsigned j=0;j<d.geometry.indexCount;++j){const auto &v=p.vertices[p.indices[d.geometry.firstIndex+j]];const int o=near(v.position[2],s.z[0])?0:1;const int c=s.corner(o,SbVec3f(v.position));if(!check(c>=0,"filled prepared payload preserves authored corner"))return false;const auto &m=p.materials[v.materialSlot];const auto rgb=s.material[o]->diffuseColor[s.materialIndex(c)];const auto n=s.normal[o]->vector[s.normalIndex(c)];const auto uv=s.uv[o]->point[s.uvIndex(c)];
    ok &= check(near(m.diffuse[0],rgb[0])&&near(m.diffuse[1],rgb[1])&&near(m.diffuse[2],rgb[2])&&near(m.diffuse[3],1-s.material[o]->transparency[s.materialIndex(c)])&&(SbVec3f(v.normal)-n).length()<1e-5f&&near(v.texcoord[0],uv[0])&&near(v.texcoord[1],uv[1]),"prepared material alpha, normal and UV match independent authored maps");++count;
  }return check(count==36,"two indexed six-triangle faces in captured payload")&&ok;
}
bool attributes(){bool ok=true;
  for(int kind=0;kind<3;++kind){Scene s(kind);Harness h(false);Observer o(s);o.install(h.action);h.action.setTransparencyType(CoinRenderAction::BLEND);
    for(int binding=0;binding<3;++binding){s.attributes(binding,true);s.drawstyle(SoDrawStyle::FILLED,false);Image original,changed,restored;ok &= h.take(s,o,original,"indexed attributes initial");ok &= planAttributes(h,s);
      const int slot=s.materialIndex(1);const auto saved=s.material[1]->transparency[slot];s.material[1]->transparency.set1Value(slot,.9375f);ok &= h.take(s,o,changed,"indexed alpha edit");ok &= planAttributes(h,s);s.material[1]->transparency.set1Value(slot,saved);ok &= h.take(s,o,restored,"indexed alpha restore");ok &= planAttributes(h,s);ok &= check(equalImage(original,restored),"indexed authored A/B/A image recovery");
    }
  }return ok;
}
bool renderCases(bool gpu){bool ok=true;
  const int policies[]={SoGLRenderAction::BLEND,SoGLRenderAction::ADD,SoGLRenderAction::DELAYED_BLEND,SoGLRenderAction::SORTED_OBJECT_BLEND,SoGLRenderAction::SORTED_OBJECT_ADD,SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND};
  for(int kind=0;kind<3;++kind)for(int style:{SoDrawStyle::LINES,SoDrawStyle::POINTS}){
    Scene s(kind);s.drawstyle(style);Harness h(gpu),cpu(false);Observer o(s),co(s);o.install(h.action);co.install(cpu.action);
    for(int mode:policies){h.action.setTransparencyType(static_cast<CoinRenderAction::TransparencyType>(mode));cpu.action.setTransparencyType(static_cast<CoinRenderAction::TransparencyType>(mode));Image first,second;
      for(unsigned phase=0;phase<3;++phase){s.order(phase==1);Image current,reference;const std::string label="kind="+std::to_string(kind)+" style="+std::to_string(style)+" mode="+std::to_string(mode)+" phase="+std::to_string(phase);bool got=h.take(s,o,current,label);ok &= got;if(got)ok &= witness(current,s,mode,style);if(gpu){const bool ref=cpu.take(s,co,reference,label+" CPU");ok &= ref;if(got&&ref)ok &= closeImages(current,reference,label);}
        if(!phase)first=current;else if(phase==1)second=current;else ok &= check(equalImage(first,current),"restored traversal order returns exact RGBA/depth");
      }
      ok &= check(sorted(mode)?equalImage(first,second):differing(first,second)>4,"object sorting or literal traversal produces the authored order effect");
    }
  }return ok;
}
bool indexedStrokes(bool gpu){bool ok=true;
  for(int kind:{1,2})for(int binding:{1,2})for(int style:{SoDrawStyle::LINES,SoDrawStyle::POINTS}){
    Scene s(kind);s.attributes(binding,true);s.useTexture();s.drawstyle(style);Harness h(gpu),cpu(false);Observer o(s),co(s);o.install(h.action);co.install(cpu.action);h.action.setTransparencyType(CoinRenderAction::SORTED_OBJECT_BLEND);cpu.action.setTransparencyType(CoinRenderAction::SORTED_OBJECT_BLEND);Image first,changed;
    for(int phase=0;phase<3;++phase){s.attributes(binding,true);if(phase==1){s.material[0]->transparency.set1Value(s.materialIndex(1),.9375f);s.uv[0]->point.set1Value(s.uvIndex(1),SbVec2f(.85f,.85f));}Image current,reference;
      const bool got=h.take(s,o,current,"indexed textured alpha A/B/A");ok &= got;if(got)ok &= witness(current,s,SoGLRenderAction::SORTED_OBJECT_BLEND,style);if(gpu){const bool ref=cpu.take(s,co,reference,"indexed textured alpha CPU");ok &= ref;if(got&&ref)ok &= closeImages(current,reference,"indexed textured alpha");}
      if(!phase)first=current;else if(phase==1)changed=current;else ok &= check(equalImage(first,current),"indexed alpha/UV restored image exact");
    }ok &= check(differing(first,changed)>4,"indexed alpha/UV edit has visible contribution");
  }return ok;
}
#include "CoinRenderConcaveIndexedAlphaStates.h"
bool refusal(bool gpu){Scene s(2);s.attributes(2,true);s.drawstyle(SoDrawStyle::LINES);Harness h(gpu);Observer o(s);o.install(h.action);h.action.setTransparencyType(CoinRenderAction::SORTED_OBJECT_BLEND);Image original,recovered;bool ok=h.take(s,o,original,"transaction initial");
  const float nan=std::numeric_limits<float>::quiet_NaN();s.coords[1]->point.set1Value(8,SbVec3f(nan,0,0));ok &= h.take(s,o,recovered,"unused indexed coordinate stays unconsumed");ok &= check(equalImage(original,recovered),"idle coordinate does not alter image");
  for(int bad=0;bad<4;++bad){const auto serial=h.target->getLastSubmissionSerial();const auto stable=recovered;s.invalid=true;
    if(bad==0)s.coords[1]->point.set1Value(ci[0],SbVec3f(nan,.8f,s.z[1]));if(bad==1)s.normal[1]->vector.set1Value(ni[0],SbVec3f(nan,0,1));if(bad==2)s.material[1]->transparency.set1Value(mi[0],nan);if(bad==3)s.uv[1]->point.set1Value(ti[0],SbVec2f(nan,.25f));
    h.action.apply(s.root);Image failed;h.target->readbackRGBA(failed.rgba);h.target->readbackDepth(failed.depth);failed.serial=h.target->getLastSubmissionSerial();ok &= check(h.action.getLastStatus()!=CoinRenderAction::SUCCESS&&failed.serial==serial&&equalImage(stable,failed),"consumed invalid indexed input refuses transactionally");
    s.positions();s.attributes(2,true);s.invalid=false;ok &= h.take(s,o,recovered,"first valid apply after rejection");ok &= check(equalImage(original,recovered),"same-target recovery restores complete image");
  }return ok;
}

// A transparent destination exposes rounding in all four channels. These byte
// tables are authored from a single uniform dyadic source and one stored RGBA8
// destination, not from a rendered reference or the production blend helper.
bool transparentDestination() {
  bool ok=true;
  const unsigned char oneHalfBlend[4]={64,32,16,128},twoHalfBlend[4]={96,48,24,192};
  const unsigned char oneHalfAdd[4]={64,32,16,64},twoHalfAdd[4]={128,64,32,128};
  const unsigned char oneQuarterBlend[4]={32,16,8,64},twoQuarterBlend[4]={56,28,14,112};
  const unsigned char oneQuarterAdd[4]={32,16,8,16},twoQuarterAdd[4]={64,32,16,32};
  for(int kind=0;kind<3;++kind)for(int style:{SoDrawStyle::FILLED,SoDrawStyle::LINES,SoDrawStyle::POINTS})
    for(int mode:{SoGLRenderAction::SORTED_OBJECT_BLEND,SoGLRenderAction::SORTED_OBJECT_ADD}) {
      Scene s(kind);s.root->removeChild(1); // Remove the opaque destination, retain camera and both occurrences.
      s.drawstyle(style);for(int o=0;o<2;++o)for(int j=0;j<8;++j)s.material[o]->diffuseColor.set1Value(j,SbColor(.5f,.25f,.125f));
      Harness h(false);Observer observer(s);observer.install(h.action);
      h.action.setBackgroundColor(SbColor4f(0,0,0,0));h.action.setTransparencyType(static_cast<CoinRenderAction::TransparencyType>(mode));
      Image first;
      for(int phase=0;phase<3;++phase) {
        const bool quarter=phase==1,add=mode==SoGLRenderAction::SORTED_OBJECT_ADD;
        for(int o=0;o<2;++o)for(int j=0;j<8;++j)s.material[o]->transparency.set1Value(j,quarter?.75f:.5f);
        Image image;if(!h.take(s,observer,image,"transparent destination dyadic A/B/A"))return false;
        const unsigned char *one=quarter?(add?oneQuarterAdd:oneQuarterBlend):(add?oneHalfAdd:oneHalfBlend);
        const unsigned char *two=quarter?(add?twoQuarterAdd:twoQuarterBlend):(add?twoHalfAdd:twoHalfBlend);
        const int xs[]={48,style==SoDrawStyle::POINTS?20:10,18};
        for(int w=0;w<3;++w) {
          const size_t at=pixel(xs[w],style==SoDrawStyle::FILLED?72:86);
          for(int channel=0;channel<4;++channel) {
            const int expected=w==0?0:(w==1?one[channel]:two[channel]);
            ok &= check(image.rgba[at+channel]==expected,"transparent dyadic RGBA8 exact kind="+std::to_string(kind)+" style="+std::to_string(style)+" mode="+std::to_string(mode)+" phase="+std::to_string(phase)+" contributions="+std::to_string(w)+" channel="+std::to_string(channel)+" actual="+std::to_string(image.rgba[at+channel])+" expected="+std::to_string(expected));
          }
          ok &= check(image.depth[at/4]==1.f,"deferred alpha leaves initially clear depth untouched");
        }
        if(phase==0)first=image;else if(phase==2)ok &= check(equalImage(first,image),"transparent destination exact A/B/A restoration");
      }
    }
  return ok;
}

}
int main(int argc,char **argv){configureSamplingTest(argc,argv);bool gpu=false,attr=false,stateCases=false,transparent=false;
  for(int i=1;i<argc;++i){const std::string arg=argv[i];if(arg=="--gpu")gpu=true;else if(arg=="--attributes")attr=true;else if(arg=="--states")stateCases=true;else if(arg=="--transparent-destination")transparent=true;else if(arg=="--canonical-raster")canonical=true;else if(arg!="--cpu"&&arg!="--portable-sampling")return 2;}
  SoDB::init();CoinRenderAction::initClass();ObservedAlphaIndexedFaceSet::initClass();
  if(attr)return indexed_alpha::attributes()?0:1;
  if(transparent)return indexed_alpha::transparentDestination()?0:1;
  if(gpu){CoinRenderCapabilities caps{};if(coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,&caps,sizeof(caps))||!caps.gpu_available)return 77;std::cout<<"indexed_alpha backend="<<caps.backend<<" renderer="<<caps.renderer<<" adapter="<<caps.adapter_name<<'\n';}
  if(stateCases)return indexed_alpha::stateCases(gpu)?0:1;
  bool ok=indexed_alpha::renderCases(gpu);ok &= indexed_alpha::indexedStrokes(gpu);ok &= indexed_alpha::refusal(gpu);return ok?0:1;
}
