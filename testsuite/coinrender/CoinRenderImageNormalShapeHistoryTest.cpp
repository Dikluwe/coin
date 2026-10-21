#define COIN_RENDER_IMAGE_NORMAL_TEXGEN_FIXTURE_ONLY
#include "CoinRenderImageNormalTexgenTest.cpp"
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/nodes/SoCallback.h>
#include <Inventor/nodes/SoCone.h>
#include <Inventor/nodes/SoCylinder.h>
#include <Inventor/nodes/SoSphere.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoNormalBinding.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoIndexedTriangleStripSet.h>
#include <Inventor/system/gl.h>
namespace {
struct NativeNormal {
  std::array<double,3>normal{{0,0,1}};bool observed=false,normalize=false;unsigned error=0;
  static void reset(void*,SoAction*action){if(!action->isOfType(SoGLRenderAction::getClassTypeId()))return;while(glGetError()!=GL_NO_ERROR){}glNormal3f(0,0,1);}
  static void read(void*data,SoAction*action){if(!action->isOfType(SoGLRenderAction::getClassTypeId()))return;auto&self=*static_cast<NativeNormal*>(data);GLfloat n[3]={};glGetFloatv(GL_CURRENT_NORMAL,n);self.normalize=glIsEnabled(GL_NORMALIZE)!=0;self.normal={{n[0],n[1],n[2]}};self.observed=true;self.error=glGetError();
    // RasterPos uses identity MODELVIEW. RESCALE_NORMAL then has unit scale;
    // NORMALIZE, if enabled, normalizes the queried current attribute.
    if(self.normalize){const double length=std::sqrt(self.normal[0]*self.normal[0]+self.normal[1]*self.normal[1]+self.normal[2]*self.normal[2]);if(length>0)for(auto&v:self.normal)v/=length;}
  }
};
SoNode*shape(int kind){
  if(kind<3){auto*cone=new SoCone;cone->parts=kind==0?SoCone::SIDES:kind==1?SoCone::BOTTOM:SoCone::ALL;return cone;}
  if(kind<7){auto*cylinder=new SoCylinder;cylinder->parts=kind==3?SoCylinder::SIDES:kind==4?SoCylinder::TOP:kind==5?SoCylinder::BOTTOM:SoCylinder::ALL;return cylinder;}
  if(kind<9)return new SoSphere;
  auto*branch=new SoSeparator;auto*normal=new SoNormal;normal->vector.setValue(0,1,0);branch->addChild(normal);auto*binding=new SoNormalBinding;binding->value=SoNormalBinding::OVERALL;branch->addChild(binding);
  auto*points=new SoCoordinate3;const SbVec3f p[]={SbVec3f(-1,-1,0),SbVec3f(1,-1,0),SbVec3f(1,1,0),SbVec3f(-1,1,0)};points->point.setValues(0,4,p);branch->addChild(points);
  if(kind==9){auto*face=new SoIndexedFaceSet;const int32_t indices[]={0,1,2,3,-1};face->coordIndex.setValues(0,5,indices);branch->addChild(face);}
  else{auto*strip=new SoIndexedTriangleStripSet;const int32_t indices[]={0,1,3,2,-1};strip->coordIndex.setValues(0,5,indices);branch->addChild(strip);}
  return branch;
}
bool campaign(){
  // One live native renderer; no retry/recreation is used to choose a normal.
  SoOffscreenRenderer native(SbViewportRegion(side,side));native.setComponents(SoOffscreenRenderer::RGB);bool ok=true;
  for(int kind=0;kind<11;++kind){Scene scene(2,7,true,false,true);NativeNormal observed;
    auto*reset=new SoCallback;reset->setCallback(NativeNormal::reset);scene.root->insertChild(reset,0);
    auto*predecessor=new SoSeparator;auto*phong=new SoLightModel;phong->model=SoLightModel::PHONG;predecessor->addChild(phong);
    auto*complexity=new SoComplexity;complexity->type=SoComplexity::OBJECT_SPACE;complexity->value=kind==8?.65f:.2f;predecessor->addChild(complexity);
    auto*move=new SoTransform;move->translation.setValue(-20,0,0);predecessor->addChild(move);predecessor->addChild(shape(kind));scene.root->insertChild(predecessor,scene.root->findChild(scene.model));
    auto*query=new SoCallback;query->setCallback(NativeNormal::read,&observed);scene.root->insertChild(query,scene.root->getNumChildren()-1);
    const bool rendered=native.render(scene.root)&&observed.observed&&observed.error==GL_NO_ERROR;
    if(!check(rendered,"native immediate/OVERALL shape normal query succeeds without GL error")){ok=false;continue;}
    std::cout<<"native_shape_history kind="<<kind<<" complexity="<<complexity->value.getValue()<<" normal="<<std::hexfloat<<observed.normal[0]<<','<<observed.normal[1]<<','<<observed.normal[2]<<std::defaultfloat<<" normalize="<<observed.normalize<<" error="<<observed.error<<'\n';
    // Cone/Cylinder/Sphere use sogl_render_* glBegin/glEnd normal emission.
    // Indexed OVERALL issues glNormal before rendering, and normal arrays are
    // disabled even if vertex arrays are used. No last-array-value inference.
    if(kind==1||kind==5)ok &= check(observed.normal[0]==0&&observed.normal[1]==-1&&observed.normal[2]==0,"bottom-only native normal has analytic-Y direction");
    if(kind==4||kind>=9)ok &= check(observed.normal[0]==0&&observed.normal[1]==1&&observed.normal[2]==0,"top/OVERALL native normal has analytic+Y direction");
    std::unique_ptr<CoinRenderTarget>target(CoinRenderTarget::createOffscreen(SbVec2i32(side,side)));if(!check(bool(target),"shape history CPU target")){ok=false;continue;}
    auto*record=new Capture;target->getPimpl()->backend.reset(record);CoinRenderAction action(SbViewportRegion(side,side));action.setRenderTarget(target.get());action.apply(scene.root);
    const bool captured=check(action.getLastStatus()==CoinRenderAction::SUCCESS,"audited defined shape history is admitted before normal-dependent Image");ok &= captured;if(captured)ok &= delivery(scene,record->plan,false,observed.normal);action.setRenderTarget(nullptr);
  }
  return ok;
}
}
int main(){SoDB::init();CoinRenderAction::initClass();return campaign()?0:1;}
