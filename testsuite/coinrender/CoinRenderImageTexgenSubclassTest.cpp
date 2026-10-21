#define COIN_RENDER_IMAGE_NORMAL_TEXGEN_FIXTURE_ONLY
#include "CoinRenderImageNormalTexgenTest.cpp"
#include <Inventor/nodes/SoTextureCoordinatePlane.h>
#include <Inventor/elements/SoMultiTextureCoordinateElement.h>
#include <Inventor/elements/SoTextureUnitElement.h>
namespace {
const SbVec4f&customCoordinates(void*,const SbVec3f&,const SbVec3f&){static const SbVec4f value(.125f,.875f,.25f,1);return value;}
void customCallback(SoCallbackAction*action,SoNode*node){SoMultiTextureCoordinateElement::setFunction(action->getState(),node,SoTextureUnitElement::get(action->getState()),customCoordinates,nullptr);}
// These legal subclasses intentionally provide different callback behavior.
// GLRender is explicitly empty: native texgen remains the preceding Object
// planes. Treating the class as its parent would invent different GL state.
#define UNKNOWN_TEXGEN(Name,Base,ParentName) \
class Name:public Base {SO_NODE_HEADER(Name);public:static void initClass(){SO_NODE_INIT_CLASS(Name,Base,ParentName);}Name(){SO_NODE_CONSTRUCTOR(Name);} \
  void callback(SoCallbackAction*action)override{customCallback(action,this);}void GLRender(SoGLRenderAction*)override{} }; \
SO_NODE_SOURCE(Name)
UNKNOWN_TEXGEN(UnknownObject,SoTextureCoordinateObject,"TextureCoordinateObject");
UNKNOWN_TEXGEN(UnknownPlane,SoTextureCoordinatePlane,"TextureCoordinatePlane");
UNKNOWN_TEXGEN(UnknownEnvironment,SoTextureCoordinateEnvironment,"TextureCoordinateEnvironment");
UNKNOWN_TEXGEN(UnknownReflection,SoTextureCoordinateReflectionMap,"TextureCoordinateReflectionMap");
#undef UNKNOWN_TEXGEN
bool subclasses(){
  UnknownObject::initClass();UnknownPlane::initClass();UnknownEnvironment::initClass();UnknownReflection::initClass();
  std::unique_ptr<CoinRenderTarget>target(CoinRenderTarget::createOffscreen(SbVec2i32(side,side)));if(!check(bool(target),"subclass CPU target"))return false;target->setDepthReadbackEnabled(TRUE);
  auto*record=new Capture;target->getPimpl()->backend.reset(record);CoinRenderAction action(SbViewportRegion(side,side));action.setRenderTarget(target.get());bool ok=true;
  Scene scene(2,7,true,false,true);
  for(int i=scene.root->getNumChildren()-1;i>=0;--i)if(scene.root->getChild(i)->isOfType(SoTextureCoordinateNormalMap::getClassTypeId()))scene.root->removeChild(i);
  scene.prefixes[7]->factorS.setValue(0,0,0,.25f);scene.prefixes[7]->factorT.setValue(0,0,0,.75f);scene.prefixes[7]->factorR.setValue(0,0,0,.5f);
  action.apply(scene.root);if(!check(action.getLastStatus()==CoinRenderAction::SUCCESS,"known Object prefix anchor succeeds"))return false;
  std::vector<uint8_t>rgba,after;std::vector<float>depth,afterDepth;target->readbackRGBA(rgba);target->readbackDepth(depth);
  for(int kind=0;kind<4;++kind){SoNode*node=kind==0?static_cast<SoNode*>(new UnknownObject):kind==1?static_cast<SoNode*>(new UnknownPlane):kind==2?static_cast<SoNode*>(new UnknownEnvironment):static_cast<SoNode*>(new UnknownReflection);
    scene.root->insertChild(node,scene.root->getNumChildren()-1);const auto serial=target->getLastSubmissionSerial();const unsigned calls=record->calls;action.apply(scene.root);target->readbackRGBA(after);target->readbackDepth(afterDepth);
    bool correct=false;
    if(action.getLastStatus()==CoinRenderAction::UNSUPPORTED){correct=record->calls==calls&&target->getLastSubmissionSerial()==serial&&after==rgba&&afterDepth==depth&&!std::string(action.getLastError().getString()).empty();}
    else if(action.getLastStatus()==CoinRenderAction::SUCCESS){
      // An executor may support this subclass only by preserving its actual
      // no-op GLRender semantics, never by reusing parent generator formulae.
      correct=record->calls==calls+1&&target->getLastSubmissionSerial()==serial+1;unsigned images=0;const SbVec4f native(.25f,.75f,.5f,2);
      for(const auto&draw:record->plan.draws)if(draw.sourceNodeId==scene.image->getNodeId()){++images;const auto&state=record->plan.renderStates[draw.renderStateSlot];correct &= state.rasterTextureCoordinates[7]==native;}
      correct &= images>0&&after==rgba&&afterDepth==depth;
    }
    std::cout<<"image_texgen_subclass kind="<<kind<<" status="<<int(action.getLastStatus())<<" error="<<action.getLastError().getString()<<'\n';
    ok &= check(correct,"unknown subclass is explicitly unsupported or executes true native state; parent/callback coordinates cannot be fabricated");
    scene.root->removeChild(node);action.apply(scene.root);target->readbackRGBA(after);target->readbackDepth(afterDepth);ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS&&after==rgba&&afterDepth==depth,"subclass refusal has exact same-target known-generator recovery");
  }
  action.setRenderTarget(nullptr);return ok;
}
}
int main(){SoDB::init();CoinRenderAction::initClass();return subclasses()?0:1;}
