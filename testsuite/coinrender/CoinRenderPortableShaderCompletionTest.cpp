// Reuse the existing public scene/publication audit; only the finite completion
// cases below run in this executable. Existing suites remain separate controls.
#define main coin_portable_original_main
#include "CoinRenderPortableShaderModuleTest.cpp"
#undef main
namespace portable_module_test {
Color expand(const std::vector<uint8_t> &bytes,unsigned components,size_t pixel,bool srgb) {
  const size_t i=pixel*components;
  Color value{{bytes[i]/255.f,0,0,1}};
  value[1]=components<3?value[0]:bytes[i+1]/255.f;
  value[2]=components<3?value[0]:bytes[i+2]/255.f;
  if(components==2)value[3]=bytes[i+1]/255.f;
  if(components==4)value[3]=bytes[i+3]/255.f;
  if(srgb)for(unsigned c=0;c<3;++c) {
    const double v=value[c];value[c]=float(v<=.04045?v/12.92:std::pow((v+.055)/1.055,2.4));
  }
  return value;
}
Color rectangleSample(const std::vector<uint8_t> &bytes,unsigned components,unsigned width,unsigned height,
 double s,double t,bool linear,bool srgb) {
  s-=std::floor(s);t-=std::floor(t);
  if(!linear)return expand(bytes,components,size_t(std::floor(t*height))*width+size_t(std::floor(s*width)),srgb);
  const double xx=s*width-.5,yy=t*height-.5;const int x=int(std::floor(xx)),y=int(std::floor(yy));
  const double fx=xx-x,fy=yy-y;Color result{{0,0,0,0}};
  auto wrap=[](int index,unsigned size){return (index%int(size)+int(size))%int(size);};
  for(int j=0;j<2;++j)for(int i=0;i<2;++i) {
    const auto value=expand(bytes,components,size_t(wrap(y+j,height))*width+wrap(x+i,width),srgb);
    for(unsigned c=0;c<4;++c)result[c]+=float(value[c]*(i?fx:1-fx)*(j?fy:1-fy));
  }
  return result;
}
Module sampleOnly(unsigned unit) {
  Module m;auto uv=operation(Op::TEXCOORD,Type::FLOAT4);uv.textureUnit=unit;
  auto sample=operation(Op::SAMPLE_2D,Type::FLOAT4,0);sample.textureUnit=unit;
  m.expressions={uv,sample};m.output=1;return m;
}
bool completionTextures(Run &run,bool srgb) {
  run.scene.attach(true);TextureInputs inputs(run);
  const std::vector<uint8_t> white={255,255,255,255};inputs.add(0,white,1);inputs.add(7,white,1);
  const unsigned widths[]={2,3},heights[]={4,5};
  // Four source layouts, POT and NPOT asymmetric extents, finite filter list.
  // Constant coordinates make mip level zero an independent exact oracle.
  const float quality[]={.1f,.3f,.6f,.8f};
  const CoinRenderTextureFilter filters[]={CoinRenderTextureFilter::NEAREST,CoinRenderTextureFilter::LINEAR,
    CoinRenderTextureFilter::NEAREST_MIPMAP_LINEAR,CoinRenderTextureFilter::LINEAR_MIPMAP_LINEAR};
  bool ok=true;Frame first,changed,back;
  for(unsigned components=1;components<=4;++components)for(unsigned shape=0;shape<2;++shape) {
    const unsigned width=widths[shape],height=heights[shape],unit=components%2?0:7;
    std::vector<uint8_t> bytes(size_t(width)*height*components);
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)for(unsigned c=0;c<components;++c)
      bytes[(size_t(y)*width+x)*components+c]=uint8_t(23+(x*53+y*29+c*47)%207);
    inputs.images[unit]->image.setValue(SbVec2s(width,height),components,bytes.data());
    SbMatrix matrix=SbMatrix::identity();matrix[0][0]=1.25f;matrix[1][1]=.75f;
    matrix[3][0]=.03125f;matrix[3][1]=.0625f;inputs.matrices[unit]->matrix=matrix;
    inputs.setCoords(unit,SbVec2f(.25f,.375f));
    ok &= run.bind(sampleOnly(unit));
    for(unsigned filter=0;filter<4;++filter) {
      inputs.quality->textureQuality=quality[filter];inputs.images[unit]->image.touch();
      Color expected=rectangleSample(bytes,components,width,height,.34375,.34375,filter!=0,srgb);
      ok &= run.draw(expected,first,"L/LA/RGB/RGBA SAMPLE owns RGBA, asymmetric POT/NPOT/filter");
      bool delivered=false;
      for(const auto &draw:run.audit->plan.draws)if(draw.sourceNodeId==run.scene.mesh->getNodeId()) {
        const auto layer=coin_render_texture_unit(run.audit->plan.renderStates[draw.renderStateSlot],unit);
        if(!check(layer.enabled && layer.imageSlot<run.audit->plan.textures.size() && layer.samplerSlot<run.audit->plan.samplers.size(),"actual consumed logical texture binding")){ok=false;continue;}
        const auto &image=run.audit->plan.textures[layer.imageSlot];
        ok &= check(image.width==width && image.height==height && image.components==4 &&
          image.format==(srgb?CoinRenderTextureFormat::RGBA8_SRGB:CoinRenderTextureFormat::RGBA8_LINEAR) &&
          run.audit->plan.samplers[layer.samplerSlot].filter==filters[filter] && image.mipmapped==(filter>=2),
          "capture preserves extent/filter/explicit RGB color space");
        std::vector<uint8_t> rgba;
        for(size_t p=0;p<size_t(width)*height;++p){auto v=expand(bytes,components,p,false);for(float c:v)rgba.push_back(uint8_t(std::lround(c*255)));}
        ok &= check(image.pixelsRgba==rgba,"authored source components independently expand to actual captured RGBA");
        delivered=true;
      }
      SbVec2s authoredSize;int authoredComponents=0;
      inputs.images[unit]->image.getValue(authoredSize,authoredComponents);
      ok &= check(delivered && authoredComponents==int(components) && authoredSize==SbVec2s(width,height),"real SoTexture2 source component count remains authored");
    }
    // Same-target current bytes, current coordinate matrix, primary and params
    // are tested separately so a stale cache cannot hide behind multiplication.
    inputs.quality->textureQuality=.1f;
    // SoTexture2 stores filter quality with its image upload. Refresh that
    // upload before comparing nearest A/B/A; every draw still applies once.
    inputs.images[unit]->image.touch();
    const auto expected=rectangleSample(bytes,components,width,height,.34375,.34375,false,srgb);
    ok &= run.draw(expected,first,"texture A before same-revision-sensitive mutations");
    auto changedBytes=bytes;for(auto &v:changedBytes)v=uint8_t(255-v);
    inputs.images[unit]->image.setValue(SbVec2s(width,height),components,changedBytes.data());
    ok &= run.draw(rectangleSample(changedBytes,components,width,height,.34375,.34375,false,srgb),changed,"texture byte B");
    inputs.images[unit]->image.setValue(SbVec2s(width,height),components,bytes.data());
    ok &= run.draw(expected,back,"texture byte A restoration");
    ok &= check(first.rgba==back.rgba && first.depth==back.depth,"texture bytes A/B/A exact same-target restoration");
    matrix[3][0]=.5f;inputs.matrices[unit]->matrix=matrix;
    ok &= run.draw(rectangleSample(bytes,components,width,height,.8125,.34375,false,srgb),changed,"independent matrix B changes sampled texel");
    matrix[3][0]=.03125f;inputs.matrices[unit]->matrix=matrix;
    ok &= run.draw(expected,back,"matrix A restoration");
    run.scene.material->diffuseColor.setValue(.1f,.8f,.25f);
    ok &= run.draw(expected,back,"SAMPLE does not implicitly multiply current PRIMARY");
    run.scene.material->diffuseColor.setValue(.6f,.3f,.8f);
    Module m=sampleOnly(unit);m.expressions.push_back(operation(Op::PRIMARY_COLOR,Type::FLOAT4));
    m.expressions.push_back(operation(Op::MULTIPLY,Type::FLOAT4,1,2));m.output=3;
    ok &= run.bind(m);Color product=expected;product[0]*=.6f;product[1]*=.3f;product[2]*=.8f;
    ok &= run.draw(product,changed,"PRIMARY applies exactly when the authored AST requests it");
    ok &= run.bind(sampleOnly(unit));ok &= run.draw(expected,back,"sample-only A/B/A restores without hidden fixed model");
    run.scene.camera->position.setValue(0,0,2);
    ok &= run.render(changed,"camera B keeps sampled RGBA and updates geometric depth") && ownerImage(changed,expected,false,false,1.9/9.9);
    run.scene.camera->position.setValue(0,0,3);ok &= run.draw(expected,back,"camera A restoration on same target");
    Module oversized=literal({{.9f,.1f,.2f,1}});const std::string huge(65536,'n');declare(oversized,huge.c_str(),Type::FLOAT1);
    ok &= check(!run.action.setPortableShaderModule(run.scene.program,oversized),"oversized public setter explicitly refuses replacement");
    ok &= run.draw(expected,back,"oversized setter preserves observable previous SAMPLE association");
  }
  return ok;
}
Color over(Color front,Color rear) {
  Color result;for(unsigned c=0;c<3;++c)result[c]=front[c]*front[3]+rear[c]*rear[3]*(1-front[3]);
  result[3]=front[3]+rear[3]*(1-front[3]);return result;
}
bool completionComposition(Run &run,CoinRenderTransparencyMode strategy) {
  Scene front,rear;front.attach(true);rear.attach(true);
  const Color a{{.8f,.15f,.35f,.25f}},b{{.1f,.65f,.4f,.6f}};
  auto *value=front.parameter4("rgba",a);rear.parameter4("rgba",b);
  bool ok=check(run.action.setPortableShaderModule(front.program,presentationModule()) &&
    run.action.setPortableShaderModule(rear.program,presentationModule()),"two independently bound variable-alpha modules");
  for(auto *s:{&front,&rear})for(int i=0;i<s->root->getNumChildren();++i)
    if(s->root->getChild(i)->isOfType(SoDepthBuffer::getClassTypeId()))static_cast<SoDepthBuffer*>(s->root->getChild(i))->write=FALSE;
  for(int i=0;i<4;++i){auto p=front.positions->point[i];p[2]=.2f;front.positions->point.set1Value(i,p);p=rear.positions->point[i];p[2]=-.2f;rear.positions->point.set1Value(i,p);}
  run.scene.root->removeChild(run.scene.mesh);run.scene.root->removeChild(run.scene.positions);
  run.scene.root->addChild(front.root);run.scene.root->addChild(rear.root);
  Frame first,changed,back;
  const CoinRenderAction::TransparencyType modes[]={CoinRenderAction::BLEND,CoinRenderAction::DELAYED_BLEND,
    CoinRenderAction::SORTED_OBJECT_BLEND,CoinRenderAction::ADD};
  for(unsigned mode=0;mode<4;++mode) {
    if(strategy!=COIN_RENDER_TRANSPARENCY_COIN && mode!=2)continue;
    run.action.setTransparencyType(modes[mode]);
    auto expected=[&](Color f) {
      if(strategy==COIN_RENDER_TRANSPARENCY_WEIGHTED_OIT) {
        // P10's declared bounded alpha/depth weighting, evaluated in double.
        auto weight=[](double alpha,double z){return std::min(8.,alpha*8+.01)*std::min(16.,std::pow(1-z,3)*16+.1);};
        const double wf=weight(f[3],2.7/9.9)*f[3],wr=weight(b[3],3.1/9.9)*b[3];
        Color result;const double alpha=1-(1-f[3])*(1-b[3]);
        for(unsigned c=0;c<3;++c)result[c]=float((f[c]*wf+b[c]*wr)/(wf+wr)*alpha);
        result[3]=float(alpha);return result;
      }
      if(mode==3){Color r;for(unsigned c=0;c<3;++c)r[c]=f[c]*f[3]+b[c]*b[3];r[3]=f[3]*f[3]+b[3]*b[3];return r;}
      return mode==2?over(f,b):over(b,f);
    };
    auto draw=[&](Color color,Frame &frame,const char *label){return run.render(frame,label)&&ownerImage(frame,expected(color),false,false,1);};
    ok &= draw(a,first,"module alpha composition and observable traversal/depth order");
    const Color edit{{.35f,.7f,.2f,.75f}};value->value.setValue(edit[0],edit[1],edit[2],edit[3]);
    ok &= draw(edit,changed,"current module alpha/color B reaches selected composition");
    ok &= check(run.lastCompiles.empty(),"module alpha/color value edit reuses compiled variant");
    value->value.setValue(a[0],a[1],a[2],a[3]);ok &= draw(a,back,"module alpha/color A restoration");
    ok &= check(first.rgba==back.rgba && first.depth==back.depth,"composition A/B/A exact same-target recovery");
    run.scene.root->removeChild(front.root);run.scene.root->addChild(front.root);
    const auto expectedReversed=(strategy!=COIN_RENDER_TRANSPARENCY_COIN||mode==2||mode==3)?expected(a):over(a,b);
    ok &= run.render(back,"reverse traversal exposes scheduling policy") && ownerImage(back,expectedReversed,false,false,1);
    run.scene.root->removeChild(front.root);run.scene.root->insertChild(front.root,run.scene.root->findChild(rear.root));
  }
  return ok;
}
bool completionScreen(Run &run) {
  run.scene.attach(true);run.scene.material->transparency=.5f;
  auto *value=run.scene.parameter4("rgba",{{.2f,.6f,.8f,.25f}});
  bool ok=run.bind(presentationModule());run.action.setTransparencyType(CoinRenderAction::SCREEN_DOOR);
  Frame first,changed,back;
  auto draw=[&](float alpha,Frame &frame,const char *label) {
    if(!run.render(frame,label))return false;
    unsigned kept=0,empty=0,bad=0;
    // Exactly one complete 32x32 Bayer tile, wholly inside the authored quad.
    for(unsigned y=16;y<48;++y)for(unsigned x=16;x<48;++x) {
      const size_t p=size_t(y)*64+x;
      if(frame.depth[p]==1){++empty;for(unsigned c=0;c<4;++c)bad+=frame.rgba[p*4+c]!=0;}
      else {++kept;bad+=std::abs(frame.depth[p]-2.9/9.9)>2e-5;
        const Color expected{{.2f,.6f,.8f,alpha}};
        for(unsigned c=0;c<4;++c)bad+=std::abs(frame.rgba[p*4+c]-255.*expected[c])>(c==3?1:3);
      }
    }
    return check(kept==512&&empty==512&&!bad,"SCREEN_DOOR material stipple is independent of module alpha; retained RGBA/depth is current");
  };
  ok &= draw(.25f,first,"screen-door module alpha A");value->value.setValue(.2f,.6f,.8f,.75f);
  ok &= draw(.75f,changed,"screen-door variable module alpha B");
  value->value.setValue(.2f,.6f,.8f,.25f);ok &= draw(.25f,back,"screen-door alpha A restored");
  return check(first.rgba==back.rgba&&first.depth==back.depth,"screen-door A/B/A exact")&&ok;
}
bool completionRttSrgb(Run &run,CoinRenderSceneTextureMode route) {
  Scene source;source.attach(true);
  const SbVec3f full[]={SbVec3f(-1.2f,-1.2f,0),SbVec3f(1.2f,-1.2f,0),SbVec3f(1.2f,1.2f,0),SbVec3f(-1.2f,1.2f,0)};
  source.positions->point.setValues(0,4,full);
  auto *stored=new SoTexture2;stored->model=SoTexture2::MODULATE;
  const uint8_t bytes[]={64,128,192,255};stored->image.setValue(SbVec2s(1,1),4,bytes);
  source.root->insertChild(stored,source.root->findChild(source.positions));
  Module producer;producer.expressions={constant({{.5f,.5f,0,1}}),operation(Op::SAMPLE_2D,Type::FLOAT4,0)};producer.output=1;
  bool ok=check(run.action.setPortableShaderModule(source.program,producer),"stored-sRGB module producer association");
  auto *texture=new SoSceneTexture2;texture->scene=source.root;texture->size.setValue(32,16);texture->type=SoSceneTexture2::RGBA8;
  texture->transparencyFunction=SoSceneTexture2::NONE;
  run.scene.root->insertChild(texture,run.scene.root->findChild(run.scene.positions));
  run.scene.attach(true);ok &= run.bind(producer);
  const std::vector<uint8_t> authored(bytes,bytes+4);Color expected=expand(authored,4,0,true);
  Frame first,changed,back;
  ok &= run.draw(expected,first,"sRGB stored producer decoded once, asymmetric linear RTT consumer");
  unsigned linearRtt=0;for(const auto &image:run.audit->plan.textures)if(image.width==32&&image.height==16) {
    ++linearRtt;ok &= check(image.format==CoinRenderTextureFormat::RGBA8_LINEAR,"RTT stays linear despite target stored-sRGB option");
    ok &= check(route==COIN_RENDER_SCENE_TEXTURE_DIRECT?bool(image.gpuToken):!image.pixelsRgba.empty(),"requested RTT route delivers actual resource");
  }
  ok &= check(linearRtt==1,"one actual asymmetric RTT binding");
  const uint8_t edit[]={191,95,31,255};stored->image.setValue(SbVec2s(1,1),4,edit);
  ok &= run.draw(expand(std::vector<uint8_t>(edit,edit+4),4,0,true),changed,"sRGB source byte B propagates through linear RTT");
  stored->image.setValue(SbVec2s(1,1),4,bytes);ok &= run.draw(expected,back,"stored-sRGB RTT A restoration");
  ok &= check(first.rgba==back.rgba&&first.depth==back.depth,"RTT A/B/A exact current publication");
  return ok;
}
CoinRenderShaderInstance budgetInstance(size_t length) {
  CoinRenderShaderInstance s;s.module=literal({{.2f,.5f,.7f,1}});
  const std::string name(length,'z');declare(s.module,name.c_str(),Type::FLOAT1);
  std::string diagnostic;check(coin_render_shader_ir(s.module,s.ir,diagnostic),"proxy fixture lowers bounded module");s.parameters={.5f};return s;
}
class CompletionBudgetAudit:public Audit {
public:
  using Audit::Audit;
  bool inject=false;
  mutable unsigned injected=0;
  CoinRenderSubmitResult preflightRtt(const CoinRenderRttPlan &g,const CoinRenderFramePlan &f,const SbVec2i32 &size)const override {
    if(!inject)return Audit::preflightRtt(g,f,size);
    ++injected;
    if(g.producers.empty())return {CoinRenderBackendStatus::BACKEND_ERROR,"budget fixture needs actual producer"};
    // Each frame is individually below the real threshold. The graph exceeds
    // it by exactly one byte, retaining every module, parameter and signature.
    auto root=f;auto graph=g;const auto unit=budgetInstance(65364);
    root.portableShaders.assign(256,unit);
    graph.producers[0].plan.portableShaders.assign(256,unit);
    graph.producers[0].plan.portableShaders.back()=budgetInstance(65365);
    return real->preflightRtt(graph,root,size);
  }
};
bool completionBudget(Run &run) {
  auto &backend=run.target->getPimpl()->backend;
  auto *audit=new CompletionBudgetAudit(std::move(run.audit->real));backend.reset(audit);run.audit=audit;
  Frame stable,recovered;bool ok=run.draw({{.6f,.3f,.8f,1}},stable,"ordinary publication before graph budget refusal");
  CoinRenderReadbackTicket ticket{};
  if(run.samples==1) {
    run.action.applyAsync(run.scene.root,ticket);
    ok &= check(run.action.getLastStatus()==CoinRenderAction::SUCCESS&&ticket.token,"actual prior independently owned ticket");
    ok &= run.draw({{.6f,.3f,.8f,1}},stable,"synchronous publication retained beside old ticket");
  }
  Scene source;auto *texture=new SoSceneTexture2;texture->scene=source.root;texture->size.setValue(8,4);
  texture->type=SoSceneTexture2::RGBA8;texture->transparencyFunction=SoSceneTexture2::NONE;
  run.scene.root->insertChild(texture,run.scene.root->findChild(run.scene.positions));
  audit->inject=true;
  const auto entries=audit->entries,producers=audit->producers;
  startup::Environment tracing("COIN_RENDER_TRACE_PHASES","1");startup::Trace trace;
  ok &= check(trace.start(),"passive budget compilation observation");
  run.action.apply(run.scene.root);const std::string log=trace.stop();const auto after=run.read();
  std::string error=run.action.getLastError().getString();for(auto &c:error)c=char(std::tolower(static_cast<unsigned char>(c)));
  ok &= check(audit->injected && run.action.getLastStatus()!=CoinRenderAction::SUCCESS &&
    (error.find("budget")!=std::string::npos||error.find("mib")!=std::string::npos||error.find("limit")!=std::string::npos) &&
    after.serial==stable.serial&&after.rgba==stable.rgba&&after.depth==stable.depth&&
    audit->entries==entries&&audit->producers==producers&&
    log.find("COIN_RENDER_PHASE portable_shader_compile ")==std::string::npos,
    "real action/RTT graph preflight rejects summed budget before producers/compilation and preserves publication");
  audit->inject=false;
  run.scene.root->removeChild(texture);
  ok &= run.draw({{.6f,.3f,.8f,1}},recovered,"same-target recovery after graph budget refusal");
  // Direct executor ingress must independently enforce the table limit too.
  auto oversized=audit->plan;oversized.portableShaders.assign(512,budgetInstance(65364));
  oversized.portableShaders.back()=budgetInstance(65365);
  const auto previous=run.read();const auto result=audit->real->submit(oversized,run.target->getPimpl().get());const auto directAfter=run.read();
  ok &= check(result.status!=CoinRenderBackendStatus::SUCCESS&&!result.diagnostic.empty()&&
    previous.serial==directAfter.serial&&previous.rgba==directAfter.rgba&&previous.depth==directAfter.depth,
    "direct executor rejects real above-budget table transactionally");
  if(ticket.token) {
    std::vector<uint8_t> rgba;std::vector<float> depth;SbString diagnostic;
    auto status=CoinRenderTarget::READBACK_NOT_READY;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(status==CoinRenderTarget::READBACK_NOT_READY&&std::chrono::steady_clock::now()<deadline) {
      status=CoinRenderTarget::pollReadback(ticket,rgba,depth,&diagnostic);
      if(status==CoinRenderTarget::READBACK_NOT_READY)run.target->pollDevice();
    }
    Frame old;old.rgba=std::move(rgba);old.depth=std::move(depth);
    ok &= check(status==CoinRenderTarget::READBACK_READY,"budget refusal preserves prior ticket");
    if(status==CoinRenderTarget::READBACK_READY)ok &= image(old,{{.6f,.3f,.8f,1}});
    else if(ticket.token)CoinRenderTarget::cancelReadback(ticket);
  }
  return ok;
}
} // namespace portable_module_test
int main(int argc,char **argv) {
  using namespace portable_module_test;
  std::string mode="textures";bool portable=false,srgb=false;unsigned samples=1;
  CoinRenderSceneTextureMode route=COIN_RENDER_SCENE_TEXTURE_STAGED;
  CoinRenderTransparencyMode strategy=COIN_RENDER_TRANSPARENCY_COIN;
  for(int i=1;i<argc;++i) {
    const std::string a=argv[i];
    if(a=="--mode"&&i+1<argc)mode=argv[++i];
    else if(a=="--portable-sampling")portable=true;
    else if(a=="--srgb")srgb=true;
    else if(a=="--direct")route=COIN_RENDER_SCENE_TEXTURE_DIRECT;
    else if(a=="--staged")route=COIN_RENDER_SCENE_TEXTURE_STAGED;
    else if(a=="--weighted")strategy=COIN_RENDER_TRANSPARENCY_WEIGHTED_OIT;
    else if(a=="--peeling")strategy=COIN_RENDER_TRANSPARENCY_PEELING;
    else if(a=="--samples"&&i+1<argc){const std::string n=argv[++i];if(n!="1"&&n!="2"&&n!="4")return 2;samples=unsigned(n[0]-'0');}
    else return 2;
  }
  if(mode!="textures"&&mode!="composition"&&mode!="screen"&&mode!="rtt-srgb"&&mode!="budget")return 2;
  SoDB::init();CoinRenderAction::initClass();CoinRenderCapabilities caps{};
  const auto status=coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,&caps,sizeof(caps));
  if(status)return 1;if(!caps.gpu_available)return 77;
  Run run(caps,portable,samples,false,route,srgb,strategy);if(!run.target)return 1;
  const bool ok=mode=="textures"?completionTextures(run,srgb):mode=="composition"?completionComposition(run,strategy):
    mode=="screen"?completionScreen(run):mode=="budget"?completionBudget(run):completionRttSrgb(run,route);
  return ok?0:1;
}
