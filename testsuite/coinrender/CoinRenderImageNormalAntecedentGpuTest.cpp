#define main coin_render_normal_antecedent_cpu_main
#include "CoinRenderImageNormalAntecedentTest.cpp"
#undef main
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/nodes/SoCallback.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/system/gl.h>
namespace defined_history {
struct NativeQuery {
  std::array<double,3>expected{{0,0,1}};unsigned reads=0;bool matches=true;
  static void reset(void*,SoAction*a){if(a->isOfType(SoGLRenderAction::getClassTypeId()))glNormal3f(0,0,1);}
  static void read(void*d,SoAction*a) {
    if(!a->isOfType(SoGLRenderAction::getClassTypeId()))return;auto&q=*static_cast<NativeQuery*>(d);++q.reads;
    GLfloat n[3]={};glGetFloatv(GL_CURRENT_NORMAL,n);const GLenum error=glGetError();
    // Called only when the native source has proved preservation or an
    // explicit OVERALL emission. Never called after a consumed normal array.
    q.matches &= check(error==GL_NO_ERROR&&n[0]==q.expected[0]&&n[1]==q.expected[1]&&n[2]==q.expected[2],
      "strict native defined current normal expected="+std::to_string(q.expected[0])+","+std::to_string(q.expected[1])+","+std::to_string(q.expected[2])+" actual="+std::to_string(n[0])+","+std::to_string(n[1])+","+std::to_string(n[2]));
  }
};
std::array<int,3> color(const Scene&s,const std::array<double,3>&normal) {
  const int unit=s.units.back();const auto raw=coordinate(s.kinds[unit],s.q[unit],false,normal);
  const int x=std::max(0,std::min(3,int(std::floor(4*(.25*raw[0]/raw[3]+.125)))));
  const int y=std::max(0,std::min(3,int(std::floor(4*(.25*raw[1]/raw[3]+.125)))));
  return {{20+50*x,30+40*y,40+15*(x+y)}};
}
class Render {
public:
  bool native;
  std::unique_ptr<SoOffscreenRenderer>gl;
  std::unique_ptr<CoinRenderTarget>target;
  CoinRenderAction action{SbViewportRegion(side,side)};
  NativeQuery query;SoSwitch *reset=nullptr;
  Render(bool n,const CoinRenderOptions&options):native(n) {
    if(native){gl.reset(new SoOffscreenRenderer(SbViewportRegion(side,side)));gl->setComponents(SoOffscreenRenderer::RGB);}
    else{target.reset(CoinRenderTarget::createOffscreen(SbVec2i32(side,side),options));if(target){target->setDepthReadbackEnabled(TRUE);action.setRenderTarget(target.get());}}
  }
  ~Render(){action.setRenderTarget(nullptr);}
  void callbacks(Scene&s) {
    if(!native)return;reset=new SoSwitch;reset->whichChild=SO_SWITCH_ALL;auto*r=new SoCallback;r->setCallback(NativeQuery::reset);reset->addChild(r);s.root->insertChild(reset,0);
    auto*q=new SoCallback;q->setCallback(NativeQuery::read,&query);s.root->insertChild(q,s.root->getNumChildren()-1);
  }
  void mode(int mode){if(native)gl->getGLRenderAction()->setTransparencyType(static_cast<SoGLRenderAction::TransparencyType>(mode));else action.setTransparencyType(static_cast<CoinRenderAction::TransparencyType>(mode));}
  bool frame(Scene&s,const std::array<double,3>&normal,const std::string&label,std::vector<uint8_t>&pixels) {
    const auto expected=color(s,normal);bool ok=true;int channels;
    if(native) {
      query.expected=normal;query.reads=0;query.matches=true;
      if(!check(gl->render(s.root)&&gl->getBuffer(),label+" native first render"))return false;
      pixels.assign(gl->getBuffer(),gl->getBuffer()+side*side*3);channels=3;
      ok &= check(query.reads==1&&query.matches,label+" one native normal query on opaque traversal");
    } else {
      if(!check(bool(target),label+" GPU target"))return false;
      const uint64_t serial=target->getLastSubmissionSerial();action.apply(s.root);target->readbackRGBA(pixels);std::vector<float>depth;target->readbackDepth(depth);channels=4;
      if(!check(action.getLastStatus()==CoinRenderAction::SUCCESS&&target->getLastSubmissionSerial()>serial&&pixels.size()==size_t(side*side*4)&&depth.size()==size_t(side*side),label+" first apply RGBA/depth publication: "+action.getLastError().getString()))return false;
      ok &= delivery(s,action.getPimpl()->lastValidPlan,false,normal);
      for(int y:{26,32,38})for(int x:{26,32,38})ok &= check(pixels[4*(y*side+x)+3]==255&&std::abs(double(depth[y*side+x])-9./29.)<=2./16777215.,label+" image alpha and authored depth");
    }
    for(int y:{26,32,38})for(int x:{26,32,38})for(int c=0;c<3;++c)
      ok &= check(std::abs(int(pixels[channels*(y*side+x)+c])-expected[c])<=3,label+" analytic texgen pixel channel="+std::to_string(c)+" actual="+std::to_string(pixels[channels*(y*side+x)+c])+" expected="+std::to_string(expected[c]));
    return ok;
  }
};
bool campaign(bool native,const CoinRenderOptions&options) {
  bool ok=true;
  // Representative defined VP semantics, not a repeat of all normal/GPU domains.
  for(int generator=0;generator<3;++generator)for(int kind=0;kind<3;++kind)for(bool phong:{false,true}) {
    Scene scene(generator,0,true,false,true);antecedents::Predecessor previous(kind,phong?1:2,phong);previous.toggle->whichChild=SO_SWITCH_ALL;
    scene.root->insertChild(previous.toggle,scene.root->findChild(scene.model));Render render(native,options);render.callbacks(scene);
    std::vector<uint8_t>a,b,c;const std::array<double,3> expected=phong?std::array<double,3>{{0,1,0}}:std::array<double,3>{{0,0,1}};
    ok &= render.frame(scene,expected,"VP defined first frame",a);
    if(phong){previous.property->normal.setValue(1,0,0);ok &= render.frame(scene,{{1,0,0}},"VP defined normal edit",b);previous.property->normal.setValue(0,1,0);}
    else {previous.property->normal.set1Value(35,SbVec3f(0,0,-1));ok &= render.frame(scene,expected,"idle VP array edit preserves normal",b);}
    ok &= render.frame(scene,expected,"VP defined restoration",c);ok &= check(a==c,"defined VP A/B/A exact pixels");
  }
  for(int mode:{SoGLRenderAction::BLEND,SoGLRenderAction::DELAYED_BLEND,SoGLRenderAction::SORTED_OBJECT_BLEND}) {
    Scene scene(2,0,true,false,true);auto*reset=pointHistory(false);reset->whichChild=SO_SWITCH_ALL;
    auto*previous=antecedents::scheduledPoint(false);previous->whichChild=SO_SWITCH_ALL;
    scene.root->insertChild(reset,scene.root->findChild(scene.model));scene.root->insertChild(previous,scene.root->findChild(scene.model));
    Render render(native,options);render.callbacks(scene);render.mode(mode);
    std::vector<uint8_t>a,b,c;const bool deferred=mode!=SoGLRenderAction::BLEND;
    ok &= render.frame(scene,deferred?std::array<double,3>{{0,0,1}}:std::array<double,3>{{0,1,0}},"defined transparent execution order",a);
    reset->whichChild=SO_SWITCH_NONE;previous->whichChild=SO_SWITCH_NONE;if(native)render.reset->whichChild=SO_SWITCH_NONE;
    ok &= render.frame(scene,{{0,1,0}},"deferred completion persists to next frame",b);
    reset->whichChild=SO_SWITCH_ALL;if(native)render.reset->whichChild=SO_SWITCH_ALL;
    ok &= render.frame(scene,{{0,0,1}},"defined OVERALL restoration",c);
    if(deferred)ok &= check(a==c,"deferred Image first frame and restored frame match exactly");
  }
  // Native culling/caching is queried only for no-emission BASE_COLOR.
  // PHONG outside-frustum ambiguity remains exclusively an Unknown oracle.
  Scene scene(2,0,true,false,true);auto*reset=pointHistory(false);reset->whichChild=SO_SWITCH_ALL;scene.root->insertChild(reset,scene.root->findChild(scene.model));antecedents::CulledPredecessor previous(false);scene.root->insertChild(previous.toggle,scene.root->findChild(scene.model));Render render(native,options);render.callbacks(scene);
  std::vector<uint8_t>a,b,c;ok &= render.frame(scene,{{0,0,1}},"cache no-emission visible A",a);previous.move->translation.setValue(80,0,0);ok &= render.frame(scene,{{0,0,1}},"cache no-emission outside B",b);previous.move->translation.setValue(-20,0,0);ok &= render.frame(scene,{{0,0,1}},"cache no-emission visible A restored",c);ok &= check(a==c,"functional cache A/B/A exact restoration");
  return ok;
}
}
int main(int argc,char**argv) {
  bool native=false;CoinRenderOptions options;
  for(int i=1;i<argc;++i){const std::string arg=argv[i];if(arg=="--native")native=true;else if(arg=="--portable-sampling")options.textureSamplingPolicy=COIN_RENDER_SAMPLING_PORTABLE;else if(arg!="--gpu")return 2;}
  SoDB::init();CoinRenderAction::initClass();
  if(!native){CoinRenderCapabilities caps{};if(coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,&caps,sizeof(caps))||!caps.gpu_available)return 77;options.renderer=static_cast<CoinRenderRenderer>(caps.renderer);}
  return defined_history::campaign(native,options)?0:1;
}
