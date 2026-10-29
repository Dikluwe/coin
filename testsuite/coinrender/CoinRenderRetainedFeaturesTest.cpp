#include "CoinRenderRetainedTestRenderer.h"
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/rendering/CoinRenderRetainedRenderer.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoSphere.h>
#include <Inventor/nodes/SoCone.h>
#include <Inventor/nodes/SoCylinder.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoTriangleStripSet.h>
#include <Inventor/nodes/SoIndexedTriangleStripSet.h>
#include <Inventor/nodes/SoQuadMesh.h>
#include <Inventor/nodes/SoLineSet.h>
#include <Inventor/nodes/SoIndexedLineSet.h>
#include <Inventor/nodes/SoPointSet.h>
#include <Inventor/nodes/SoNormal.h>
#include <Inventor/nodes/SoNormalBinding.h>
#include <Inventor/nodes/SoMaterialBinding.h>
#include <Inventor/nodes/SoVertexProperty.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoPointLight.h>
#include <Inventor/nodes/SoSpotLight.h>
#include <Inventor/nodes/SoEnvironment.h>
#include <Inventor/nodes/SoPolygonOffset.h>
#include <Inventor/nodes/SoSwitch.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoAlphaTest.h>
#include <Inventor/nodes/SoClipPlane.h>
#include <Inventor/nodes/SoDrawStyle.h>
#include <Inventor/nodes/SoTexture2Transform.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoTextureCoordinateBinding.h>
#include "rendering/coinrender/CoinRenderComposition.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {
int failures=0;
void check(bool ok,const std::string & message){if(!ok){++failures;std::cerr<<"FAIL: "<<message<<'\n';}}
const SbViewportRegion viewport(96,96);
void compare(const std::string & name, SoSeparator * scene, CoinRenderTarget & currentTarget,
             CoinRenderTarget & retainedTarget, bool nativeStrokes=false, CoinRenderAction::TransparencyType transparency=CoinRenderAction::SORTED_OBJECT_BLEND, SoMaterial * editedMaterial=nullptr, bool worldStateMove=false) {
  std::cout << "fixture=" << name << std::endl;
  scene->ref();
  auto * translation = new SoTranslation;
  scene->insertChild(translation, worldStateMove ? (name=="translated camera scope"?0:2) : scene->getNumChildren()-1);
  CoinRenderAction current(viewport);current.setRenderTarget(&currentTarget);current.setTransparencyType(transparency);
  CoinRenderRetainedRenderer retained(viewport);retained.setTransparencyType(transparency);
  const auto classifications=coin_render_composition_classification_count_for_tests();
  auto status=retained.render(scene,retainedTarget);
  check(status==CoinRenderAction::SUCCESS,name+": retained render: "+retained.getLastError().getString());
  check(coin_render_composition_classification_count_for_tests()==classifications,name+": no second planner");
  if(status==CoinRenderAction::SUCCESS){
    const auto first=retained.getStatistics();
    check(first.nativeCommands>0,name+": native capture emitted geometry");
    check(retained.render(scene,retainedTarget)==CoinRenderAction::SUCCESS,name+": static reuse");
    const auto second=retained.getStatistics();
    check(second.captures==1 && second.planBuilds==1 && second.transportBuilds==1 && second.transportReuses==1,
          name+": one capture, one plan, one transport");
    current.apply(scene);
    check(current.getLastStatus()==CoinRenderAction::SUCCESS,name+": current render");
    auto compareReadback = [&]() {
    std::vector<uint8_t>a,b;retainedTarget.readbackRGBA(a);currentTarget.readbackRGBA(b);
    check(a.size()==96*96*4 && a.size()==b.size(),name+": readback size");
    if(a.size()==b.size() && !a.empty()){
      size_t bad=0,foregroundA=0,foregroundB=0;int maxDelta=0;
      for(size_t p=0;p<a.size();p+=4){bool different=false;for(int c=0;c<3;++c){int delta=std::abs(int(a[p+c])-int(b[p+c]));maxDelta=std::max(maxDelta,delta);different=different || delta>4;}
        bad+=different;foregroundA+=(a[p]+a[p+1]+a[p+2])>16;foregroundB+=(b[p]+b[p+1]+b[p+2])>16;}
      // Native 1px lines/points and expanded strokes can disagree at edges.
      const size_t limit=nativeStrokes?192:48;
      if(bad>limit)std::cerr<<name<<" mismatched="<<bad<<" max_delta="<<maxDelta<<" foreground="<<foregroundA<<','<<foregroundB<<'\n';
      check(bad<=limit,name+": pixel equivalence");
#if defined(HAVE_COIN_BGFX) || defined(HAVE_COIN_WGPU_RUST_BRIDGE)
      check(foregroundA>0 && foregroundB>0,name+": both paths draw visible geometry");
#endif
    }
    std::vector<float> depthA,depthB;retainedTarget.readbackDepth(depthA);currentTarget.readbackDepth(depthB);
    check(depthA.size()==96*96 && depthA.size()==depthB.size(),name+": depth readback size");
    if(depthA.size()==depthB.size()) {
      size_t bad=0;for(size_t i=0;i<depthA.size();++i)bad+=!std::isfinite(depthA[i]) || !std::isfinite(depthB[i]) || std::abs(depthA[i]-depthB[i])>0.000003f;
      if(bad>(nativeStrokes?192u:48u))std::cerr<<name<<" mismatched_depth="<<bad<<'\n';
      check(bad<=(nativeStrokes?192u:48u),name+": depth equivalence");
    }
    };
    compareReadback();
    translation->translation.setValue(.12f,.08f,name=="sorted material transparency"?-2.0f:0.0f);
    check(retained.render(scene,retainedTarget)==CoinRenderAction::SUCCESS,name+": incremental translation");
    current.setViewportRegion(viewport);current.apply(scene);
    check(current.getLastStatus()==CoinRenderAction::SUCCESS,name+": current translated frame");
    const auto translated=retained.getStatistics();
    check(translated.captures==(worldStateMove?2u:1u) && translated.transportBuilds==((nativeStrokes || editedMaterial || worldStateMove)?2u:1u) && translated.incrementalUpdates==(worldStateMove?0u:1u),
          name+": new profile preserves incremental capture/transport reuse");
    compareReadback();
    if(editedMaterial) {
      editedMaterial->diffuseColor.set1Value(1,SbColor(.2f,.8f,.6f));
      check(retained.render(scene,retainedTarget)==CoinRenderAction::SUCCESS,name+": per-vertex material edit");
      current.setViewportRegion(viewport);current.apply(scene);
      check(retained.getCaptureCount()==2,name+": colored material edit regenerates primitive colors once");
      compareReadback();
    }
  }
  scene->unref();
}
SoSeparator * base(bool phong=false){
  auto * scene=new SoSeparator;
  auto * camera=new SoOrthographicCamera;camera->position.setValue(0,0,6);camera->nearDistance=1;camera->farDistance=20;camera->height=5;scene->addChild(camera);
  auto * depth=new SoDepthBuffer;depth->function=SoDepthBuffer::LESS;scene->addChild(depth);
  auto * model=new SoLightModel;model->model=phong?SoLightModel::PHONG:SoLightModel::BASE_COLOR;scene->addChild(model);
  auto * material=new SoMaterial;material->diffuseColor.setValue(.7f,.35f,.2f);material->specularColor.setValue(.1f,.1f,.1f);scene->addChild(material);
  auto * complexity=new SoComplexity;complexity->value=.35f;scene->addChild(complexity);
  return scene;
}
void coordinates(SoSeparator * scene){auto * points=new SoCoordinate3;const SbVec3f p[]={{-1,-1,0},{1,-1,0},{-1,1,0},{1,1,0}};points->point.setValues(0,4,p);scene->addChild(points);}
}
int main(int argc, char ** argv){
 SoDB::init();CoinRenderAction::initClass();CoinRenderOptions options;
#if defined(HAVE_COIN_BGFX) || defined(HAVE_COIN_WGPU_RUST_BRIDGE)
 options.renderer=COIN_RENDER_RENDERER_VULKAN;
#endif
 CoinRenderAction::TransparencyType profileTransparency=CoinRenderAction::SORTED_OBJECT_BLEND;
 const std::string profile=argc==2?argv[1]:"";
 if(profile=="--msaa4")options.sampleCount=4;
 else if(profile=="--portable"){options.textureSamplingPolicy=COIN_RENDER_SAMPLING_PORTABLE;options.maxTextureAnisotropy=1;}
 else if(profile=="--canonical")options.rasterizationPolicy=COIN_RENDER_RASTER_CANONICAL_8BIT;
 else if(profile=="--srgb")options.storedTextureColorSpace=COIN_RENDER_TEXTURE_SRGB;
 else if(profile=="--weighted")options.transparency=COIN_RENDER_TRANSPARENCY_WEIGHTED_OIT;
 else if(profile=="--peeling")options.transparency=COIN_RENDER_TRANSPARENCY_PEELING;
 else if(profile=="--none")profileTransparency=CoinRenderAction::NONE;
 else if(profile=="--screen-door")profileTransparency=CoinRenderAction::SCREEN_DOOR;
 else if(profile=="--blend")profileTransparency=CoinRenderAction::BLEND;
 else if(profile=="--add")profileTransparency=CoinRenderAction::ADD;
 else if(profile=="--delayed-blend")profileTransparency=CoinRenderAction::DELAYED_BLEND;
 else if(profile=="--delayed-add")profileTransparency=CoinRenderAction::DELAYED_ADD;
 else if(profile=="--layers")profileTransparency=CoinRenderAction::SORTED_LAYERS_BLEND;
 else if(argc!=1)return 1;
 if(!coinRetainedTestRenderer(options))return 1;
 CoinRenderCapabilities capabilities{};
 coin_render_query_capabilities_for_renderer(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,options.renderer,&capabilities,sizeof(capabilities));
 if(capabilities.gpu_available && !(capabilities.runtime_features & COIN_RENDER_FEATURE_TEXTURE_ANISOTROPY)) {
   options.maxTextureAnisotropy=1;
   std::cout<<"profile maxTextureAnisotropy=1: selected adapter does not expose anisotropy"<<std::endl;
 }
 std::unique_ptr<CoinRenderTarget> a(CoinRenderTarget::createOffscreen(SbVec2i32(96,96),options)),b(CoinRenderTarget::createOffscreen(SbVec2i32(96,96),options));
 if(!a || !b){std::cerr<<"Selected adapter could not create matching targets, samples="<<options.sampleCount<<'\n';return 1;}
 if(!a->setDepthReadbackEnabled(TRUE) || !b->setDepthReadbackEnabled(TRUE))return 1;
 auto run=[&](const char * name,SoSeparator * root,bool strokes=false,CoinRenderAction::TransparencyType transparency=CoinRenderAction::SORTED_OBJECT_BLEND){compare(name,root,*a,*b,strokes,profileTransparency==CoinRenderAction::SORTED_OBJECT_BLEND?transparency:profileTransparency);};
 if(!profile.empty()) {
   {auto*s=base();s->addChild(new SoCube);run((profile+" opaque cube").c_str(),s);}
   {auto*s=base(true);auto*l=new SoPointLight;l->location.setValue(1,1,4);s->addChild(l);s->addChild(new SoSphere);run((profile+" PHONG").c_str(),s);}
   {auto*s=base();auto*m=new SoMaterial;m->transparency=.4f;s->addChild(m);s->addChild(new SoCube);run((profile+" blend").c_str(),s);}
   {auto*s=base();auto*t=new SoTexture2;const unsigned char pixels[]={255,64,32,32,255,64,64,32,255,180,160,140};t->image.setValue(SbVec2s(2,2),3,pixels);s->addChild(t);s->addChild(new SoCube);run((profile+" texture").c_str(),s);}
   {auto*s=base();auto*m=new SoMaterial;m->diffuseColor.setValue(0,0,1);m->transparency=.5f;s->addChild(m);s->addChild(new SoCube);auto*x=new SoTranslation;x->translation.setValue(0,0,-.5f);s->addChild(x);auto*n=new SoMaterial;n->diffuseColor.setValue(1,0,0);n->transparency=0;s->addChild(n);s->addChild(new SoCube);run((profile+" mixed opaque/transparent order").c_str(),s);}
   {auto*s=base();auto*binding=new SoMaterialBinding;binding->value=SoMaterialBinding::PER_FACE;s->addChild(binding);auto*m=new SoMaterial;const SbColor colors[]={{1,0,0},{0,1,0},{0,0,1},{1,1,0},{1,0,1},{0,1,1}};const float alpha[]={.3f,.6f,.4f,.7f,.1f,.5f};m->diffuseColor.setValues(0,6,colors);m->transparency.setValues(0,6,alpha);s->addChild(m);s->addChild(new SoCube);run((profile+" per-face transparency").c_str(),s);}
   return failures?1:0;
 }
 {auto*s=base();s->addChild(new SoSphere);run("sphere",s);}
 {auto*s=base();s->addChild(new SoCone);run("cone",s);}
 {auto*s=base();s->addChild(new SoCylinder);run("cylinder",s);}
 {auto*s=base();coordinates(s);auto*f=new SoIndexedFaceSet;const int32_t i[]={0,1,3,2,-1};f->coordIndex.setValues(0,5,i);s->addChild(f);run("indexed faces",s);}
 {auto*s=base();coordinates(s);auto*f=new SoTriangleStripSet;f->numVertices=4;s->addChild(f);run("triangle strip",s);}
 {auto*s=base();coordinates(s);auto*f=new SoIndexedTriangleStripSet;const int32_t i[]={0,1,2,3,-1};f->coordIndex.setValues(0,5,i);s->addChild(f);run("indexed triangle strip",s);}
 {auto*s=base();coordinates(s);auto*f=new SoQuadMesh;f->verticesPerColumn=2;f->verticesPerRow=2;s->addChild(f);run("quad mesh",s);}
 {auto*s=base();coordinates(s);auto*f=new SoLineSet;f->numVertices=4;s->addChild(f);run("lines",s,true);}
 {auto*s=base();coordinates(s);auto*f=new SoIndexedLineSet;const int32_t i[]={0,1,-1,2,3,-1};f->coordIndex.setValues(0,6,i);s->addChild(f);run("indexed lines",s,true);}
 {auto*s=base();coordinates(s);auto*f=new SoPointSet;f->numPoints=4;s->addChild(f);run("points",s,true);}
 {auto*s=base(true);auto*l=new SoDirectionalLight;l->direction.setValue(.2f,-.3f,-1);s->addChild(l);s->addChild(new SoSphere);run("directional sphere",s);}
 {auto*s=base(true);auto*l=new SoPointLight;l->location.setValue(1,1,4);s->addChild(l);s->addChild(new SoSphere);run("point light",s);}
 {auto*s=base(true);auto*l=new SoSpotLight;l->location.setValue(0,0,4);l->direction.setValue(0,0,-1);l->dropOffRate=.2f;l->cutOffAngle=.65f;s->addChild(l);s->addChild(new SoSphere);run("spot light",s);}
 {auto*s=base(true);auto*e=new SoEnvironment;e->ambientIntensity=.4f;e->ambientColor.setValue(.2f,.4f,.8f);e->attenuation.setValue(.01f,.02f,1);s->addChild(e);auto*l=new SoPointLight;l->location.setValue(1,1,4);s->addChild(l);s->addChild(new SoSphere);run("environment attenuation",s);}
 {auto*s=base();auto*offset=new SoPolygonOffset;offset->factor=1;offset->units=2;s->addChild(offset);s->addChild(new SoCube);run("polygon offset",s);}
 {auto*s=base();auto*sw=new SoSwitch;sw->whichChild=1;sw->addChild(new SoSphere);sw->addChild(new SoCube);s->addChild(sw);run("switch",s);}
 {auto*s=base(true);coordinates(s);auto*n=new SoNormal;n->vector.setValue(0,0,1);s->addChild(n);auto*binding=new SoNormalBinding;binding->value=SoNormalBinding::OVERALL;s->addChild(binding);s->addChild(new SoDirectionalLight);auto*f=new SoTriangleStripSet;f->numVertices=4;s->addChild(f);run("explicit normals",s);}
 {auto*s=base();auto*binding=new SoMaterialBinding;binding->value=SoMaterialBinding::PER_FACE;s->addChild(binding);auto*m=new SoMaterial;const SbColor colors[]={{1,0,0},{0,1,0},{0,0,1},{1,1,0},{1,0,1},{0,1,1}};m->diffuseColor.setValues(0,6,colors);s->addChild(m);s->addChild(new SoCube);run("per-face materials",s);}
 for(int mode : {SoEnvironment::HAZE,SoEnvironment::FOG,SoEnvironment::SMOKE}){
   auto*s=base();auto*e=new SoEnvironment;e->fogType=mode;e->fogColor.setValue(.1f,.3f,.5f);e->fogVisibility=12;s->addChild(e);s->addChild(new SoCube);run(("fog "+std::to_string(mode)).c_str(),s);
 }
 {auto*s=base(true);auto*e=new SoEnvironment;e->attenuation.setValue(.01f,.02f,1);s->addChild(e);auto*l=new SoPointLight;l->location.setValue(1,1,4);s->addChild(l);auto*later=new SoEnvironment;later->attenuation.setValue(.5f,.5f,1);s->addChild(later);s->addChild(new SoSphere);run("attenuation captured at light",s);}
 for(int components : {1,2,3,4}) for(int model : {SoTexture2::MODULATE,SoTexture2::REPLACE,SoTexture2::DECAL,SoTexture2::BLEND}) {
   // Coin does not define DECAL for luminance images.
   if(model==SoTexture2::DECAL && components<3)continue;
   auto*s=base();auto*t=new SoTexture2;t->model=model;
   std::vector<unsigned char> pixels(4*4*components);
   for(int i=0;i<16;++i) for(int c=0;c<components;++c) pixels[i*components+c]=(components==2 || components==4) && c==components-1?255:static_cast<unsigned char>(50+((i+c)%4)*60);
   t->image.setValue(SbVec2s(4,4),components,pixels.data());s->addChild(t);s->addChild(new SoCube);
   run(("texture "+std::to_string(components)+" model "+std::to_string(model)).c_str(),s);
 }
 for(float quality : {0.0f,.1f,.3f,.6f,.82f,.95f}) {
   auto*s=base();auto*c=new SoComplexity;c->textureQuality=quality;s->addChild(c);
   auto*t=new SoTexture2;unsigned char pixels[4*4*3];for(int i=0;i<48;++i)pixels[i]=50+(i%4)*60;t->image.setValue(SbVec2s(4,4),3,pixels);s->addChild(t);
   auto*x=new SoTexture2Transform;x->translation.setValue(.15f,.2f);x->scaleFactor.setValue(1.4f,.8f);s->addChild(x);s->addChild(new SoCube);
   run(("texture quality/matrix "+std::to_string(quality)).c_str(),s);
 }
 {auto*s=base();auto*m=new SoMaterial;m->diffuseColor.setValue(0,0,1);m->transparency=.4f;s->addChild(m);s->addChild(new SoCube);auto*t=new SoTranslation;t->translation.setValue(.3f,0,1);s->addChild(t);auto*n=new SoMaterial;n->diffuseColor.setValue(1,0,0);n->transparency=.6f;s->addChild(n);s->addChild(new SoSphere);run("sorted material transparency",s);}
 for(auto type : {CoinRenderAction::SORTED_OBJECT_BLEND,CoinRenderAction::SORTED_OBJECT_ADD}) {
   auto*s=base();auto*m=new SoMaterial;m->diffuseColor.setValue(0,1,0);m->transparency=.35f;s->addChild(m);s->addChild(new SoCube);
   run(type==CoinRenderAction::SORTED_OBJECT_BLEND?"blend":"add",s,false,type);
 }
 {auto*s=base();auto*t=new SoTexture2;unsigned char pixels[4*4*4];for(int i=0;i<16;++i){pixels[i*4]=200;pixels[i*4+1]=60;pixels[i*4+2]=20;pixels[i*4+3]=i%2?255:80;}t->image.setValue(SbVec2s(4,4),4,pixels);s->addChild(t);auto*alpha=new SoAlphaTest;alpha->function=SoAlphaTest::GREATER;alpha->value=.5f;s->addChild(alpha);s->addChild(new SoCube);run("textured alpha test",s);}
 {auto*s=base();auto*m=new SoMaterial;m->transparency=.0005f;s->addChild(m);s->addChild(new SoCube);run("small nonzero transparency",s);}
 {auto*s=base();auto*style=new SoDrawStyle;style->lineWidth=4;s->addChild(style);coordinates(s);auto*f=new SoLineSet;f->numVertices=4;s->addChild(f);run("wide lines",s,true);}
 {auto*s=base();auto*style=new SoDrawStyle;style->pointSize=5;s->addChild(style);coordinates(s);auto*f=new SoPointSet;f->numPoints=4;s->addChild(f);run("wide points",s,true);}
 {auto*s=base();auto*p=new SoClipPlane;p->plane=SbPlane(SbVec3f(1,0,0),0);s->addChild(p);s->addChild(new SoCube);run("world clipping plane",s);}
 {auto*s=base();coordinates(s);auto*t=new SoTexture2;const unsigned char pixels[]={255,0,0,0,255,0,0,0,255,255,255,0};t->image.setValue(SbVec2s(2,2),3,pixels);s->addChild(t);auto*uv=new SoTextureCoordinate2;const SbVec2f coords[]={{0,0},{1,0},{0,1},{1,1}};uv->point.setValues(0,4,coords);s->addChild(uv);auto*binding=new SoTextureCoordinateBinding;binding->value=SoTextureCoordinateBinding::PER_VERTEX_INDEXED;s->addChild(binding);auto*f=new SoIndexedFaceSet;const int32_t indices[]={0,1,3,2,-1};f->coordIndex.setValues(0,5,indices);s->addChild(f);run("explicit indexed texture coordinates",s);}
 {auto*s=base();coordinates(s);auto*m=new SoMaterial;const SbColor colors[]={{1,0,0},{0,1,0},{0,0,1},{1,1,0}};m->diffuseColor.setValues(0,4,colors);s->addChild(m);auto*binding=new SoMaterialBinding;binding->value=SoMaterialBinding::PER_VERTEX;s->addChild(binding);auto*f=new SoTriangleStripSet;f->numVertices=4;s->addChild(f);compare("per-vertex materials",s,*a,*b,false,CoinRenderAction::SORTED_OBJECT_BLEND,m);}
 {auto*s=base();auto*p=new SoVertexProperty;const SbVec3f coords[]={{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}};p->vertex.setValues(0,4,coords);auto*f=new SoIndexedFaceSet;f->vertexProperty=p;const int32_t indices[]={0,1,2,3,-1};f->coordIndex.setValues(0,5,indices);s->addChild(f);run("vertex property geometry",s);}
 {auto*s=base();s->ref();coordinates(s);auto*f=new SoIndexedFaceSet;const int32_t indices[]={0,1,99,-1};f->coordIndex.setValues(0,4,indices);s->addChild(f);CoinRenderRetainedRenderer renderer(viewport);const auto serial=b->getLastSubmissionSerial();check(renderer.render(s,*b)==CoinRenderAction::UNSUPPORTED,"invalid coordinate index rejected before primitive generation");check(renderer.getCaptureCount()==1 && renderer.getPlanBuildCount()==0 && b->getLastSubmissionSerial()==serial,"invalid indexed input preserves planning/publication");s->unref();}
 {auto*s=base(true);auto*l=new SoPointLight;l->location.setValue(1,1,4);l->intensity=2.5f;s->addChild(l);s->addChild(new SoSphere);run("high intensity light",s);}
 {auto*s=base(true);auto*e=new SoEnvironment;e->ambientIntensity=2;e->ambientColor.setValue(.2f,.5f,1);s->addChild(e);s->addChild(new SoCube);run("high intensity ambient",s);}
 {auto*s=base(true);auto*l=new SoSpotLight;l->location.setValue(0,0,4);l->direction.setValue(0,0,-1);l->cutOffAngle=1.57079632679f;s->addChild(l);s->addChild(new SoSphere);run("spot half sphere cutoff",s);}
 for(int mode : {SoEnvironment::HAZE,SoEnvironment::FOG,SoEnvironment::SMOKE}){auto*s=base();auto*e=new SoEnvironment;e->fogType=mode;e->fogVisibility=0;e->fogColor.setValue(.1f,.3f,.6f);s->addChild(e);s->addChild(new SoCube);run(("camera far fog "+std::to_string(mode)).c_str(),s);}
 {auto*s=base(true);auto*l=new SoPointLight;l->location.setValue(1,1,4);s->addChild(l);s->addChild(new SoSphere);compare("translated light scope",s,*a,*b,false,CoinRenderAction::SORTED_OBJECT_BLEND,nullptr,true);}
 {auto*s=base();auto*p=new SoClipPlane;p->plane=SbPlane(SbVec3f(1,0,0),0);s->addChild(p);s->addChild(new SoCube);compare("translated clipping scope",s,*a,*b,false,CoinRenderAction::SORTED_OBJECT_BLEND,nullptr,true);}
 {auto*s=base();s->addChild(new SoCube);compare("translated camera scope",s,*a,*b,false,CoinRenderAction::SORTED_OBJECT_BLEND,nullptr,true);}
 {auto*s=base();auto*binding=new SoMaterialBinding;binding->value=SoMaterialBinding::PER_FACE;s->addChild(binding);auto*m=new SoMaterial;const SbColor colors[]={{1,0,0},{0,1,0},{0,0,1},{1,1,0},{1,0,1},{0,1,1}};const float alpha[]={.3f,.6f,.4f,.7f,.1f,.5f};m->diffuseColor.setValues(0,6,colors);m->transparency.setValues(0,6,alpha);s->addChild(m);s->addChild(new SoCube);run("per-face object transparency",s);}
 {auto*s=base();s->ref();auto*sw=new SoSwitch;sw->whichChild=0;sw->addChild(new SoCube);s->addChild(sw);CoinRenderRetainedRenderer renderer(viewport);CoinRenderAction current(viewport);current.setRenderTarget(a.get());
   for(int which : {0,SO_SWITCH_NONE,0}) {sw->whichChild=which;check(renderer.render(s,*b)==CoinRenderAction::SUCCESS,"switch visibility update");current.setViewportRegion(viewport);current.apply(s);std::vector<uint8_t>x,y;a->readbackRGBA(x);b->readbackRGBA(y);check(!x.empty() && x==y,"switch visibility matches current image");}
   check(renderer.getCaptureCount()==1 && renderer.getStatistics().incrementalUpdates==2,"stable state-isolated switch retains one capture");s->unref();}
 {auto*s=base();s->ref();auto*m=new SoMaterial;s->addChild(m);auto*sw=new SoSwitch;sw->whichChild=0;sw->addChild(new SoCube);s->addChild(sw);CoinRenderRetainedRenderer renderer(viewport);CoinRenderAction current(viewport);current.setRenderTarget(a.get());check(renderer.render(s,*b)==CoinRenderAction::SUCCESS,"hidden-tail initial render");sw->whichChild=SO_SWITCH_NONE;check(renderer.render(s,*b)==CoinRenderAction::SUCCESS,"hidden-tail hide");m->diffuseColor.setValue(.2f,.8f,.6f);check(renderer.render(s,*b)==CoinRenderAction::SUCCESS,"hidden-tail material update");sw->whichChild=0;check(renderer.render(s,*b)==CoinRenderAction::SUCCESS,"hidden-tail show");current.apply(s);std::vector<uint8_t>x,y;a->readbackRGBA(x);b->readbackRGBA(y);check(!x.empty() && x==y,"hidden material edit reaches the restored image");check(renderer.getCaptureCount()<=3,"hidden-tail updates use at most one recapture per change");s->unref();}
 {auto*s=base();s->ref();auto*group=new SoGroup;auto*m=new SoMaterial;m->diffuseColor.setValue(1,0,0);group->addChild(m);group->addChild(new SoCube);auto*sw=new SoSwitch;sw->whichChild=0;sw->addChild(group);s->addChild(sw);s->addChild(new SoCube);CoinRenderRetainedRenderer renderer(viewport);CoinRenderAction current(viewport);current.setRenderTarget(a.get());for(int which : {0,SO_SWITCH_NONE,0}){sw->whichChild=which;check(renderer.render(s,*b)==CoinRenderAction::SUCCESS,"state-leaking switch render");current.setViewportRegion(viewport);current.apply(s);std::vector<uint8_t>x,y;a->readbackRGBA(x);b->readbackRGBA(y);check(!x.empty() && x==y,"switch preserves inherited state outside its branch");}check(renderer.getCaptureCount()==3,"state-leaking switch recaptures conservatively");s->unref();}
 {auto*s=base();s->removeChild(1);auto*m=new SoMaterial;m->diffuseColor.setValue(1,0,0);s->addChild(m);s->addChild(new SoCube);auto*n=new SoMaterial;n->diffuseColor.setValue(0,1,0);s->addChild(n);s->addChild(new SoCube);run("implicit depth with coplanar geometry",s);}
 {auto*s=base();s->ref();auto*q=new SoComplexity;q->textureQuality=.1f;s->addChild(q);auto*t=new SoTexture2;unsigned char pixels[48];for(int i=0;i<48;++i)pixels[i]=40+(i%4)*60;t->image.setValue(SbVec2s(4,4),3,pixels);s->addChild(t);s->addChild(new SoCube);CoinRenderRetainedRenderer renderer(viewport);CoinRenderAction current(viewport);current.setRenderTarget(a.get());for(float quality : {.1f,.8f,0.0f,.3f}){q->textureQuality=quality;check(renderer.render(s,*b)==CoinRenderAction::SUCCESS,"retained image upload quality update");current.setViewportRegion(viewport);current.apply(s);std::vector<uint8_t>x,y;a->readbackRGBA(x);b->readbackRGBA(y);check(!x.empty() && x==y,"retained image upload quality matches current after quality changes");}s->unref();}
 for(int copies : {1,40}) {auto*s=base(true);s->ref();auto*camera=static_cast<SoOrthographicCamera *>(s->getChild(0));s->addChild(new SoDirectionalLight);
   for(int side=0;side<2;++side){auto*part=new SoSeparator;auto*shift=new SoTranslation;shift->translation.setValue(side?1.1f:-1.1f,0,0);part->addChild(shift);auto*environment=new SoEnvironment;environment->fogType=SoEnvironment::HAZE;environment->fogColor.setValue(.1f,.3f,.5f);environment->fogVisibility=side?20.0f:0.0f;part->addChild(environment);auto*cube=new SoCube;cube->width=1;for(int copy=0;copy<copies;++copy)part->addChild(cube);s->addChild(part);}
   CoinRenderRetainedRenderer renderer(viewport);CoinRenderAction current(viewport);current.setRenderTarget(a.get());current.setTransparencyType(CoinRenderAction::SORTED_OBJECT_BLEND);
   uint64_t vertices=0,indices=0;
   for(int step=0;step<4;++step){if(step==1)camera->height=4.5f;if(step==2)camera->farDistance=24;if(step==3)camera->focalDistance=7;
     const auto classifications=coin_render_composition_classification_count_for_tests();
     check(renderer.render(s,*b)==CoinRenderAction::SUCCESS,"camera projection patch render");
     check(coin_render_composition_classification_count_for_tests()==classifications,"projection patch does not invoke another planner");
     current.setViewportRegion(viewport);current.apply(s);check(current.getLastStatus()==CoinRenderAction::SUCCESS,"projection reference render");
     std::vector<uint8_t>x,y;std::vector<float>dx,dy;a->readbackRGBA(x);b->readbackRGBA(y);a->readbackDepth(dx);b->readbackDepth(dy);
     check(!x.empty() && x==y && dx==dy,"projection patch equals full reference including explicit/automatic fog");
     const auto stats=renderer.getStatistics();if(!step){vertices=stats.copiedVertices;indices=stats.copiedIndices;}
     check(stats.captures==1 && stats.planBuilds==1 && stats.transportBuilds==1 && stats.copiedVertices==vertices && stats.copiedIndices==indices,"projection update reuses native capture/plan/transport without geometry copies");
   }s->unref();}
 return failures?1:0;
}
