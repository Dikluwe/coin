#ifndef COIN_RENDER_SHADOW_RTT_GROUPS_H
#define COIN_RENDER_SHADOW_RTT_GROUPS_H
namespace {
struct C036Groups {
  AlphaScene left{1,8,false,1.f},right{1,8,true,.2f};
  std::shared_ptr<SoSeparator>root{new SoSeparator,[](SoSeparator*p){p->unref();}};
  SoSwitch*leftVisible=new SoSwitch,*rightVisible=new SoSwitch;
  C036Groups(){root->ref();auto*camera=new SoOrthographicCamera;camera->position.setValue(0,0,6);camera->height=4;camera->nearDistance=1;camera->farDistance=20;root->addChild(camera);
    for(AlphaScene*scene:{&left,&right}){scene->receiverStyle->style=SoShadowStyle::SHADOWED;scene->lighting->model=SoLightModel::PHONG;
      scene->receiverMaterial->diffuseColor.setValue(1,1,1);scene->receiverMaterial->transparency=.125f;
      scene->group->precision=scene==&left?.0625f:.125f;
      for(int unit:scene->units){const uint8_t white[]={255,255,255,255};scene->images[unit]->image.setValue(SbVec2s(1,1),4,white);}
      for(int i=0;i<9;++i){auto p=scene->positions->point[i];p[0]=p[0]*.5f+(scene==&left?-.9f:.9f);scene->positions->point.set1Value(i,p);}}
    leftVisible->whichChild=rightVisible->whichChild=SO_SWITCH_ALL;leftVisible->addChild(left.group);rightVisible->addChild(right.group);root->addChild(leftVisible);root->addChild(rightVisible);
    left.casterX(0,-.8f);right.casterX(0,.8f);
  }
  void mode(int mode){left.mode->value=right.mode->value=mode;}
};
void c036GroupGraphDiagnostic(const CoinRenderRttPlan&graph,const CoinRenderFramePlan&root,const std::string&phase){
  std::cout<<"c036_groups_graph phase="<<phase<<" producer_count="<<graph.producers.size()<<" charged_bytes="<<graph.chargedBytes<<'\n';
  const auto references=[&](const CoinRenderFramePlan&plan,const std::string&label){
    std::vector<uint64_t>dependencies;for(const auto&image:plan.textures)if(image.producerId&&std::find(dependencies.begin(),dependencies.end(),image.producerId)==dependencies.end())dependencies.push_back(image.producerId);
    std::cout<<"c036_groups_dependencies phase="<<phase<<" frame="<<label<<" deps=";for(auto id:dependencies)std::cout<<id<<',';std::cout<<'\n';
    for(size_t index=0;index<plan.draws.size();++index){const auto&draw=plan.draws[index];if(draw.renderStateSlot>=plan.renderStates.size())continue;
      const auto&state=plan.renderStates[draw.renderStateSlot];std::cout<<"c036_groups_refs phase="<<phase<<" frame="<<label<<" draw="<<index<<" source_node="<<draw.sourceNodeId<<" stable_node="<<draw.stableNodeId<<" state="<<draw.renderStateSlot<<" shadow_light="<<draw.shadowLightSlot;
      for(unsigned unit=0;unit<8;++unit){const auto t=coin_render_texture_unit(state,unit);if(!t.enabled)continue;
        std::cout<<" unit"<<unit<<"_image="<<t.imageSlot<<"_sampler="<<t.samplerSlot;
        if(t.imageSlot<plan.textures.size()){const auto&image=plan.textures[t.imageSlot];std::cout<<"_producer="<<image.producerId<<"_token="<<image.gpuToken<<"_extent="<<image.width<<'x'<<image.height;}}
      std::cout<<'\n';}
  };
  for(size_t i=0;i<graph.producers.size();++i){const auto&p=graph.producers[i];std::cout<<"c036_groups_producer phase="<<phase<<" id="<<i+1<<" source_revision="<<p.sourceRevision<<" size="<<p.size[0]<<'x'<<p.size[1]<<" format="<<int(p.format)<<'\n';references(p.plan,"producer"+std::to_string(i+1));}
  references(root,"root");
}
bool c036GroupsDelivery(const C036Proxy&spy){
  bool ok=check(spy.graph.producers.size()==2,"C03.6 grouped RTT retains exactly two producers");if(spy.graph.producers.size()!=2)return false;
  const auto inspect=[&](const CoinRenderFramePlan&frame,const char*phase){CoinRenderShadowPlan maps;std::string why;const bool planned=coin_render_plan_shadows(frame,maps,why);
    bool small=false,large=false,spot=false,directional=false;for(const auto&pass:maps.passes)if(pass.visible){small|=pass.mapSize==128;large|=pass.mapSize==256;
      if(pass.lightSlot<frame.shadowLights.size()){spot|=frame.shadowLights[pass.lightSlot].type==CoinRenderLightType::SPOT;directional|=frame.shadowLights[pass.lightSlot].type==CoinRenderLightType::DIRECTIONAL;}}
    std::cout<<"c036_groups phase="<<phase<<" maps="<<maps.passes.size()<<" bytes="<<maps.attachmentBytes<<" diagnostic="<<why<<'\n';
    return planned&&maps.passes.size()==2&&small&&large&&spot&&directional;};
  ok &= check(inspect(spy.root,"consumer")&&inspect(spy.graph.producers[0].plan,"producer"),
    "C03.6 both consumer and producer coexist with real128/256 spot/directional groups");
  return ok;
}
bool c036RttGroups(WindowPair&h){
  const auto proxies=c036Install(h);C036Groups source,consumer;C036Producer second{{{.5f,.75f,.875f}},.5f,48,32};
  std::shared_ptr<SoSceneTexture2>first(new SoSceneTexture2,[](SoSceneTexture2*p){p->unref();});first->ref();first->scene=source.root.get();first->size.setValue(64,64);
  first->type=SoSceneTexture2::RGBA8;first->model=SoSceneTexture2::MODULATE;first->transparencyFunction=SoSceneTexture2::ALPHA_BLEND;first->backgroundColor.setValue(0,0,0,0);
  first->wrapS=SoSceneTexture2::REPEAT;first->wrapT=SoSceneTexture2::CLAMP;
  const int mode=h.off->getOptions().transparency==COIN_RENDER_TRANSPARENCY_OBJECT?SoGLRenderAction::SORTED_OBJECT_BLEND:SoGLRenderAction::SORTED_LAYERS_BLEND;
  source.mode(mode);consumer.mode(mode);
  // Place source shadow witnesses away from the consumer's own shadow footprint.
  source.left.casterX(0,-1.3f);source.right.casterX(0,.4f);
  for(AlphaScene*scene:{&consumer.left,&consumer.right}){scene->receiverMaterial->transparency=0;
    scene->stages[0]->insertChild(first.get(),2);scene->stages[3]->insertChild(first.get(),2);scene->stages[7]->insertChild(second.texture,2);}
  const bool direct=h.off->getOptions().sceneTexture==COIN_RENDER_SCENE_TEXTURE_DIRECT;
  const auto frame=[&](Frame&f,const std::string&phase){const unsigned off=proxies.first->producers,window=proxies.second->producers;
    CoinRenderAction capture(SbViewportRegion(side,side));capture.setSortedLayersNumPasses(4);capture.getPimpl()->planOnly=true;
    capture.getPimpl()->executionOptions=h.off->getOptions();auto graph=std::make_shared<CoinRenderRttPlan>(h.off->getOptions().sceneTexture);capture.getPimpl()->sceneTexturePlan=graph;
    capture.apply(consumer.root.get());std::cout<<"c036_groups_authorship phase="<<phase<<" first_texture_node="<<first->getNodeId()<<" second_texture_node="<<second.texture->getNodeId()
      <<" source_left_mesh="<<source.left.mesh->getNodeId()<<" source_right_mesh="<<source.right.mesh->getNodeId()<<" consumer_left_mesh="<<consumer.left.mesh->getNodeId()<<" consumer_right_mesh="<<consumer.right.mesh->getNodeId()
      <<" plan_status="<<int(capture.getLastStatus())<<" error="<<capture.getLastError().getString()<<'\n';
    c036GroupGraphDiagnostic(*graph,capture.getPimpl()->lastValidPlan,phase+" planOnly");
    const bool published=h.frame(consumer.left,f,phase,true,false,2,consumer.root.get());
    c036GroupGraphDiagnostic(proxies.first->graph,proxies.first->root,phase+" offscreenActual");
    c036GroupGraphDiagnostic(proxies.second->graph,proxies.second->root,phase+" windowActual");
    return published&&check(proxies.first->producers==off+(direct?2:0)&&proxies.second->producers==window+(direct?2:0),"grouped RTT distinct producers execute once per DIRECT root");};
  Frame left,right,both,b,c;consumer.rightVisible->whichChild=SO_SWITCH_NONE;bool ok=frame(left,"grouped RTT isolated consumer left");
  consumer.leftVisible->whichChild=SO_SWITCH_NONE;consumer.rightVisible->whichChild=SO_SWITCH_ALL;ok &= frame(right,"grouped RTT isolated consumer right");
  consumer.leftVisible->whichChild=SO_SWITCH_ALL;const bool combined=frame(both,"grouped RTT combined consumer/producer A");ok &= combined;
  if(combined){ok &= c036GroupsDelivery(*proxies.first)&&c036GroupsDelivery(*proxies.second);unsigned bad=0,checked=0;
    for(int y=side/4;y<3*side/4;++y)for(int x=side/8;x<7*side/8;++x){if(std::abs(x-side/2)<4)continue;const auto&reference=x<side/2?left:right;const size_t p=size_t(y*side+x);++checked;
      if(reference.rgba.size()!=both.rgba.size()||reference.depth.size()!=both.depth.size()){++bad;continue;}
      for(int ch=0;ch<4;++ch)bad+=std::abs(int(both.rgba[4*p+ch])-int(reference.rgba[4*p+ch]))>3;bad+=both.depth[p]!=reference.depth[p];}
    ok &= check(checked>1000&&bad==0,"grouped RTT each consumer region matches its own local isolated reference");}
  if(combined)for(int location=0;location<4;++location){AlphaScene*scene=location==0?&consumer.left:location==1?&consumer.right:location==2?&source.left:&source.right;
    const auto position=scene->casterPositions[0]->translation.getValue();scene->casterX(0,position[0]<0?1.4f:-1.4f);
    const bool changed=frame(b,"independent consumer/producer map B "+std::to_string(location));ok &= changed;
    if(changed)ok &= check(alphaChanges(both,b)>0&&both.depth==b.depth,"each grouped consumer/producer map changes color independently and preserves depth");
    scene->casterPositions[0]->translation=position;const bool restored=frame(c,"grouped RTT map A restored");ok &= restored;if(restored)ok &= exact(both,c);
  }
  return ok;
}
}
#endif
