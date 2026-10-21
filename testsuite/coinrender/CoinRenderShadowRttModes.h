#ifndef COIN_RENDER_SHADOW_RTT_MODES_H
#define COIN_RENDER_SHADOW_RTT_MODES_H
#include "rendering/coinrender/CoinRenderComposition.h"
namespace {
bool c036ObjectDepthModes(WindowPair&h){
  if(!check(h.off->getOptions().transparency==COIN_RENDER_TRANSPARENCY_OBJECT,"OBJECT mode sweep requires explicit OBJECT target"))return false;
  const auto proxies=c036Install(h);C036Scene s(false);auto*camera=static_cast<SoOrthographicCamera*>(s.root->getChild(0));
  camera->position.setValue(0,0,4);camera->nearDistance=1;camera->farDistance=17;s.depth->write=TRUE;
  const bool direct=h.off->getOptions().sceneTexture==COIN_RENDER_SCENE_TEXTURE_DIRECT;bool ok=true;
  const int modes[]={SoGLRenderAction::BLEND,SoGLRenderAction::ADD,SoGLRenderAction::DELAYED_BLEND,SoGLRenderAction::DELAYED_ADD,
    SoGLRenderAction::SORTED_OBJECT_BLEND,SoGLRenderAction::SORTED_OBJECT_ADD,SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND,SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_ADD};
  for(int m=0;m<8;++m){s.mode->value=modes[m];const bool additive=m%2;Frame a,b,c;
    const auto render=[&](Frame&f,const char*phase){if(!h.frame(s,f,phase,true,false,2))return false;
      bool valid=c036Delivery(s,*proxies.first,direct)&&c036Delivery(s,*proxies.second,direct);unsigned receiverCount=0;
      for(const auto*proxy:{proxies.first,proxies.second}){CoinWgpuFfiFrame ffi;std::string why;if(!ffi.prepare(proxy->plan,side,side,why,false)){valid=false;continue;}
        std::vector<CoinRenderCompositionItem>order;if(!coin_render_composition_order(proxy->plan,order,why)){valid=false;continue;}
        for(const auto&item:order){const auto&draw=proxy->plan.draws[item.drawIndex];if(draw.shadowLightSlot||draw.sourceNodeId!=s.mesh->getNodeId())continue;
          ++receiverCount;const auto&state=proxy->plan.renderStates[draw.renderStateSlot];const auto&packed=ffi.getView().states[draw.renderStateSlot];
          valid &= state.transparencyType==modes[m]&&item.blend&&item.additive==additive&&item.deferred==(m>=2)&&state.depthWrite&&packed.depth_write&&
            packed.depth_range[0]==s.depth->range.getValue()[0]&&packed.depth_range[1]==s.depth->range.getValue()[1];}}
      const double alpha=.75*.75*.5;const double source[]={.75*.75*.5,.875*.875*.75,.875};
      const auto range=s.depth->range.getValue();const double depth=double(range[0])+(double(range[1])-range[0])*.25;unsigned wrong=0;
      for(int y=side/4;y<3*side/4;++y)for(int x=side/4;x<3*side/4;++x){const size_t p=size_t(y*side+x);
        for(int channel=0;channel<3;++channel){const int expected=int(std::lround(255*source[channel]*alpha+alphaBackground[channel]*(additive?1.:1.-alpha)));
          if(std::abs(int(f.rgba[4*p+channel])-expected)>3&&wrong==0)std::cout<<"c036_modes mode="<<modes[m]<<" channel="<<channel<<" expected="<<expected<<" actual="<<int(f.rgba[4*p+channel])<<'\n';
          wrong+=std::abs(int(f.rgba[4*p+channel])-expected)>3;wrong+=std::abs(int(h.lastShown[4*p+channel])-expected)>3;wrong+=std::abs(int(h.lastReadback[4*p+channel])-expected)>3;}
        wrong+=std::abs(int(f.rgba[4*p+3])-255)>1;wrong+=!std::isfinite(f.depth[p])||std::abs(double(f.depth[p])-depth)>2./16777215.;}
      return check(valid&&receiverCount>0&&wrong==0,"eight OBJECT modes retain scheduling, two-producer RGBA equation and geometric custom depth-write");};
    s.depth->range=SbVec2f(0,1);const bool first=render(a,"OBJECT mode default range A");ok &= first;
    s.depth->range=SbVec2f(.2f,.8f);ok &= render(b,"OBJECT mode ascending range B");
    s.depth->range=SbVec2f(.8f,.2f);ok &= render(b,"OBJECT mode inverted range C");
    s.depth->range=SbVec2f(0,1);const bool restored=render(c,"OBJECT mode default range A restored");ok &= restored;if(first&&restored)ok &= exact(a,c);
  }
  return ok;
}
}
#endif
