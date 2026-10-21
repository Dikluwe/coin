#ifndef COIN_RENDER_SHADOW_RTT_PEER_H
#define COIN_RENDER_SHADOW_RTT_PEER_H
namespace {
struct C036Peer {
  C036Producer scene{{{.125f,.25f,.75f}},1.f,32,32};
  std::unique_ptr<CoinRenderTarget>target;CoinRenderAction action{SbViewportRegion(32,32)};CountedProxy*proxy=nullptr;Frame known;bool ready=false;
  explicit C036Peer(WindowPair&h){target.reset(CoinRenderTarget::createOffscreen(SbVec2i32(32,32),h.off->getOptions()));if(!target)return;
    target->setDepthReadbackEnabled(TRUE);auto&state=target->getPimpl().get();const auto status=state.prepareBackend();
    if(status!=CoinRenderBackendStatus::SUCCESS||!state.backend||!state.backend->isGpuBackend())return;
    proxy=new CountedProxy(std::move(state.backend));state.backend.reset(proxy);action.setRenderTarget(target.get());
    action.setBackgroundColor(SbColor4f(1,0,0,1));ready=true;
  }
  ~C036Peer(){action.setRenderTarget(nullptr);}
  Frame read(){Frame f;target->readbackRGBA(f.rgba);target->readbackDepth(f.depth);f.serial=target->getLastSubmissionSerial();return f;}
  bool math(const Frame&f){if(f.rgba.size()!=32*32*4||f.depth.size()!=32*32)return false;unsigned bad=0;const int color[]={32,64,191,255};
    for(int y=4;y<28;++y)for(int x=4;x<28;++x){const size_t p=size_t(y*32+x);for(int c=0;c<4;++c)bad+=std::abs(int(f.rgba[4*p+c])-color[c])>1;
      bad+=!std::isfinite(f.depth[p])||std::abs(double(f.depth[p])-.5)>2./16777215.;}
    return bad==0;
  }
  bool publish(WindowPair&h){const auto before=target->getLastSubmissionSerial();const unsigned calls=proxy->submits;action.apply(scene.root);Frame f=read();
    const uint64_t expected=h.caps.backend==COIN_RENDER_EXPERIMENTAL_RUST?h.latest+1:before+1;
    bool ok=check(action.getLastStatus()==CoinRenderAction::SUCCESS&&f.serial==expected&&proxy->submits==calls+1&&math(f),"independent peer publishes its own analytic blue RGB/opaque alpha and depth0.5");
    if(!known.rgba.empty())ok &= check(f.rgba==known.rgba&&f.depth==known.depth,"independent peer resubmission preserves its distinct known scene");
    known=f;if(h.caps.backend==COIN_RENDER_EXPERIMENTAL_RUST)h.latest=f.serial;return ok;
  }
  bool preserved(){const Frame f=read();return check(f.serial==known.serial&&f.rgba==known.rgba&&f.depth==known.depth&&math(f),"independent peer color/depth/serial survive another target's admission or refusal");}
};
}
#endif
