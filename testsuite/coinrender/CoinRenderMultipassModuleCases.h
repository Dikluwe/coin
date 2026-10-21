#ifndef COIN_RENDER_MULTIPASS_MODULE_CASES_H
#define COIN_RENDER_MULTIPASS_MODULE_CASES_H
CoinRenderShaderModule multipassModule() {
  CoinRenderShaderModule module;CoinRenderShaderUniform uniform;
  uniform.name="AA surface";uniform.type=CoinRenderShaderValueType::FLOAT4;module.uniforms.push_back(uniform);
  CoinRenderShaderExpression parameter;parameter.op=CoinRenderShaderOp::UNIFORM;parameter.type=CoinRenderShaderValueType::FLOAT4;parameter.uniformName=uniform.name;
  // Existing public structural-opacity proof supports equal CLAMP bounds;
  // it intentionally does not infer unknown-uniform times zero. All authored
  // RGB parameters below are within[0,1], so this preserves their exact RGB.
  CoinRenderShaderExpression low;low.value={{0,0,0,1}};
  CoinRenderShaderExpression high;high.value={{1,1,1,1}};
  CoinRenderShaderExpression clamp;clamp.op=CoinRenderShaderOp::CLAMP;clamp.inputs={{0,1,2}};
  module.expressions={parameter,low,high,clamp};module.output=3;module.alphaMayVary=FALSE;return module;
}
bool moduleImage(const Image &image,const SbVec4f &value,int fogMode,int n,const char *label) {
  if(!check(image.rgba.size()==side*side*4&&image.depth.size()==side*side,std::string(label)+" complete module result"))return false;
  const double fog[]={.75,.25,.125},factor=multipassFogFactor(fogMode);
  unsigned bad=0,depthBad=0,partial=0,full=0;
  for(int y=16;y<48;++y)for(int x=0;x<side;++x) {
    unsigned covered=0;bool last=false;
    for(const auto &j:samples(n)){const double px=x+1-j[0];last=px>24.32&&px<32.33;covered+=last;}
    const double fraction=double(covered)/n;const size_t p=size_t(y*side+x);
    for(unsigned c=0;c<3;++c)bad+=std::abs(double(image.rgba[p*4+c])-255*(value[c]*factor+fog[c]*(1-factor))*fraction)>3;
    bad+=std::abs(double(image.rgba[p*4+3])-255*fraction)>1;
    depthBad+=!std::isfinite(image.depth[p])||std::abs(image.depth[p]-(last?.5f:1.f))>1e-5;
    partial+=covered>0&&covered<unsigned(n);full+=covered==unsigned(n);
  }
  std::cout<<"multipass_module "<<label<<" N="<<n<<" bad="<<bad<<" depth_bad="<<depthBad<<" partial="<<partial<<" full="<<full<<'\n';
  return check(!bad&&!depthBad&&full>128&&partial>=32,std::string(label)+" module RGBA before fog/clip, analytic average and LAST depth");
}
bool moduleDelivery(const CoinRenderFramePlan &frame,const SbVec4f &value,SoNode *shape) {
  bool ok=true;unsigned indices=0;std::set<uint32_t> slots;
  for(const auto &draw:frame.draws)if(draw.sourceNodeId==shape->getNodeId()&&draw.isSceneLayer()) {
    if(!check(draw.renderStateSlot<frame.renderStates.size(),"module draw has real state")){ok=false;continue;}
    const auto slot=frame.renderStates[draw.renderStateSlot].portableShaderSlot;
    ok&=check(slot>0&&slot<=frame.portableShaders.size(),"every actual surface occurrence has its module");
    if(slot&&slot<=frame.portableShaders.size()) {
      const auto &instance=frame.portableShaders[slot-1];slots.insert(slot);
      ok&=check(instance.parameters.size()==4&&std::memcmp(instance.parameters.data(),value.getValue(),4*sizeof(float))==0&&!instance.module.alphaMayVary,"exact logical parameters and structural opaque alpha preserved per occurrence");
    }
    indices+=draw.geometry.indexCount;
  }
  ok&=check(indices==6&&slots.size()==1&&frame.portableShaders.size()==1,"complete six-index module coverage with one shared semantic payload");
  CoinWgpuFfiFrame ffi;std::string error;const bool packed=ffi.prepare(frame,side,side,error,false);ok&=check(packed,"real module FFI transport: "+error);
  if(packed) {
    const auto &view=ffi.getView();
    ok&=check(view.portable_module_count==1&&view.portable_modules&&view.portable_modules[0].parameter_count==4&&view.portable_modules[0].parameters&&std::memcmp(view.portable_modules[0].parameters,value.getValue(),4*sizeof(float))==0,"exact actual FFI module parameter bytes");
  }
  return ok;
}
bool modulePath(const CoinRenderOptions &options,int requested) {
  Scene scene;
  auto *environment=new SoEnvironment;environment->fogType=SoEnvironment::HAZE;environment->fogVisibility=8;environment->fogColor.setValue(.75f,.25f,.125f);scene.root->insertChild(environment,1);
  auto *clip=new SoClipPlane;clip->plane=SbPlane(SbVec3f(1,0,0),-.24f);scene.root->insertChild(clip,scene.root->getNumChildren()-2);
  auto *program=new SoShaderProgram;program->ref();auto *fragment=new SoFragmentShader;
  fragment->sourceType=SoShaderObject::GLSL_PROGRAM;fragment->sourceProgram="void main(){gl_FragColor=vec4(1.0,0.0,0.0,1.0);}";fragment->isActive=TRUE;
  auto *parameter=new SoShaderParameter4f;parameter->name="AA surface";parameter->value.setValue(.75f,.125f,.5f,0);fragment->parameter.set1Value(0,parameter);program->shaderObject.set1Value(0,fragment);
  std::unique_ptr<CoinRenderTarget>target(CoinRenderTarget::createOffscreen(SbVec2i32(side,side),options));Audit *audit=prepare(target.get(),true);if(!audit){program->unref();return false;}
  CoinRenderAction action(SbViewportRegion(side,side));action.setRenderTarget(target.get());action.setTransparencyType(CoinRenderAction::NONE);action.setBackgroundColor(SbColor4f(0,0,0,0));
  const auto module=multipassModule();SbString validation;const bool validModule=coin_render_validate_shader_module(module,validation);if(!validModule)std::cerr<<"multipass_module_validation error="<<validation.getString()<<'\n';check(validModule,"independently authored opaque module validates: "+std::string(validation.getString()));
  const int n=requested?requested:7;action.setNumPasses(n);
  auto draw=[&](bool active,bool cacheHit,const char *label) {
    const auto serial=target->getLastSubmissionSerial(),source=scene.root->getNodeId();const unsigned callbacks=scene.captures;
    RttTrace trace;check(trace.start(),"passive module compile observation");action.apply(scene.root);const auto events=trace.stop();
    const bool success=check(action.getLastStatus()==CoinRenderAction::SUCCESS&&target->getLastSubmissionSerial()>serial,std::string(label)+" current module publication: "+action.getLastError().getString());
    check(callbacks+1==scene.captures&&source==scene.root->getNodeId(),"module multipass captures parameter state once without source mutations");
    if(cacheHit)check(events.find("COIN_RENDER_PHASE portable_shader_compile ")==std::string::npos,"parametric-only edits reuse compiled module/variant under the same N/profile");
    if(success){if(active){moduleDelivery(audit->last,parameter->value.getValue(),scene.root->getChild(scene.root->getNumChildren()-1));moduleImage(read(target.get()),parameter->value.getValue(),environment->fogType.getValue(),n,label);}else fogClipImage(read(target.get()),environment->fogType.getValue(),n,true,label);}
  };
  draw(false,false,"ordinary fog/clip predecessor");
  check(action.setPortableShaderModule(program,module),"public portable module association");scene.root->insertChild(program,scene.root->findChild(scene.coordinates));draw(true,false,"module multipass introduction");
  parameter->value.setValue(.125f,.75f,.375f,0);draw(true,true,"module parameter mutation without recompilation");
  parameter->value.setValue(.75f,.125f,.5f,0);draw(true,true,"module authorial restoration");
  const auto stable=read(target.get());const auto serial=target->getLastSubmissionSerial();const auto commands=audit->commands;
  parameter->value.setValue(std::numeric_limits<float>::quiet_NaN(),.125f,.5f,0);action.apply(scene.root);
  check(action.getLastStatus()==CoinRenderAction::UNSUPPORTED&&action.getLastError().getString()&&std::string(action.getLastError().getString()).find("nonfinite")!=std::string::npos&&audit->commands==commands&&target->getLastSubmissionSerial()==serial&&exact(stable,read(target.get())),"nonfinite active module parameter refuses before commands with exact prior publication");
  parameter->value.setValue(.75f,.125f,.5f,0);draw(true,true,"module same-target recovery");
  scene.root->removeChild(program);action.clearPortableShaderModule(program);draw(false,false,"fixed pipeline restoration");
  action.setRenderTarget(nullptr);program->unref();return good;
}
#endif
