#ifndef COIN_RENDER_MULTIPASS_BUMP_CASES_H
#define COIN_RENDER_MULTIPASS_BUMP_CASES_H
bool bumpImage(const Image &image,const Scene &scene,int n,bool changed,const char *label){
  if(!check(image.rgba.size()==side*side*4&&image.depth.size()==side*side,std::string(label)+" full bump result"))return false;
  unsigned bad=0,depthBad=0,partial=0,bright=0;const double diffuse=.8*(changed?1./255:1.);
  for(int y=16;y<48;++y)for(int x=0;x<side;++x){unsigned covered=0;bool last=false;for(const auto &j:samples(n)){const double px=x+1-j[0];last=px>8&&px<scene.edge;covered+=last;}
    const size_t p=size_t(y*side+x);const auto color=scene.material->diffuseColor[0];for(unsigned c=0;c<3;++c)bad+=std::abs(double(image.rgba[4*p+c])-255*color[c]*diffuse*covered/n)>3;
    bad+=std::abs(double(image.rgba[4*p+3])-255.*covered/n)>1;depthBad+=!std::isfinite(image.depth[p])||std::abs(image.depth[p]-(last?.5f:1.f))>1e-5;
    partial+=covered>0&&covered<unsigned(n);bright+=covered==unsigned(n)&&x>=12&&x<24;
  }
  std::cout<<"multipass_bump "<<label<<" N="<<n<<" tilted="<<changed<<" bad="<<bad<<" depth_bad="<<depthBad<<" partial="<<partial<<" full="<<bright<<'\n';
  return check(!bad&&!depthBad&&bright>300&&(n==1||partial>=32),std::string(label)+" constant authored normal-map DOT3 then linear average, full brightness and LAST depth");
}
bool bumpPath(const CoinRenderOptions &options,int requested){
  Scene scene;static_cast<SoLightModel*>(scene.root->getChild(2))->model=SoLightModel::PHONG;
  scene.material->ambientColor.setValue(0,0,0);scene.material->emissiveColor.setValue(0,0,0);scene.material->specularColor.setValue(0,0,0);
  auto *light=new SoDirectionalLight;light->direction.setValue(0,0,-1);light->intensity=.8f;scene.root->insertChild(light,3);
  // FaceSet's default binding consumes one normal per vertex. Supply the
  // complete authored array rather than relying on an incidental allocation
  // after a single overall-looking value.
  auto *normal=new SoNormal;const SbVec3f normals[]={SbVec3f(0,0,1),SbVec3f(0,0,1),SbVec3f(0,0,1),SbVec3f(0,0,1)};normal->vector.setValues(0,4,normals);scene.root->insertChild(normal,scene.root->getNumChildren()-1);
  auto *uv=new SoTextureCoordinate2;const SbVec2f points[]={SbVec2f(0,0),SbVec2f(1,0),SbVec2f(1,1),SbVec2f(0,1)};uv->point.setValues(0,4,points);scene.root->insertChild(uv,scene.root->getNumChildren()-1);
  auto *bump=new SoBumpMap;const unsigned char flat[]={128,128,255},tilted[]={255,128,128};bump->image.setValue(SbVec2s(1,1),3,flat);scene.root->insertChild(bump,scene.root->getNumChildren()-1);
  std::unique_ptr<CoinRenderTarget>target(CoinRenderTarget::createOffscreen(SbVec2i32(side,side),options));auto *audit=prepare(target.get(),true);if(!audit)return false;
  CoinRenderAction action(SbViewportRegion(side,side));action.setRenderTarget(target.get());action.setTransparencyType(CoinRenderAction::NONE);action.setBackgroundColor(SbColor4f(0,0,0,0));
  auto draw=[&](int n,bool changed,const char *label){action.setNumPasses(n);const auto serial=target->getLastSubmissionSerial(),source=scene.root->getNodeId();const unsigned captures=scene.captures;action.apply(scene.root);const bool success=check(action.getLastStatus()==CoinRenderAction::SUCCESS&&target->getLastSubmissionSerial()>serial,std::string(label)+" current bump publication: "+action.getLastError().getString());check(scene.captures==captures+1&&source==scene.root->getNodeId(),"bump source captured once without mutations");if(success){bumpImage(read(target.get()),scene,n,changed,label);bool normalsValid=!audit->last.vertices.empty();for(const auto &v:audit->last.vertices)normalsValid&=v.normal[0]==0&&v.normal[1]==0&&v.normal[2]==1;check(normalsValid,"every consumed bump vertex retains its explicit finite +Z normal");check(audit->last.bumpImages.size()==1,"actual bump payload retained");CoinWgpuFfiFrame ffi;std::string error;const bool packed=ffi.prepareBumpFrame(audit->last,side,side,error);check(packed,"real bump FFI: "+error);if(packed)check(ffi.getView().bump_image_count==1&&ffi.getView().bump_vertex_count>0,"active bump vertices and image delivered, no declaration-only admission");}};
  const int n=requested?requested:4;draw(1,false,"bump predecessor");draw(n,false,"bump multipass");bump->image.setValue(SbVec2s(1,1),3,tilted);draw(n,true,"tilted live normal map");bump->image.setValue(SbVec2s(1,1),3,flat);draw(n,false,"bump authorial restoration");
  action.setRenderTarget(nullptr);return good;
}
#endif
