#ifndef COIN_RENDER_SHADOW_RTT_DEPTH_CONTROLS_H
#define COIN_RENDER_SHADOW_RTT_DEPTH_CONTROLS_H
namespace {
bool c036Depth(WindowPair&h,bool diagnostic=false){
  const auto proxies=c036Install(h);C036Scene s(false);const bool weighted=h.off->getOptions().transparency==COIN_RENDER_TRANSPARENCY_WEIGHTED_OIT;
  if(h.off->getOptions().transparency!=COIN_RENDER_TRANSPARENCY_OBJECT)s.mode->value=SoGLRenderAction::SORTED_LAYERS_BLEND;
  auto*camera=static_cast<SoOrthographicCamera*>(s.root->getChild(0));camera->position.setValue(0,0,4);camera->nearDistance=1;camera->farDistance=17;
  auto*offset=new SoPolygonOffset;offset->styles=SoPolygonOffset::FILLED;offset->factor=0;offset->units=16;offset->on=FALSE;s.receiver->insertChild(offset,3);
  // Identical mesh/state gives a coplanar opaque half-plane. Only its final
  // RGBA REPLACE, explicit depth, clip and transparency mode are different.
  auto*cover=new SoSeparator;for(int child=0;child<s.receiver->getNumChildren()-1;++child)cover->addChild(s.receiver->getChild(child));
  auto*coverMode=new SoTransparencyType;coverMode->value=SoGLRenderAction::NONE;cover->addChild(coverMode);
  auto*coverDepth=new SoDepthBuffer;coverDepth->write=TRUE;coverDepth->function=SoDepthBuffer::ALWAYS;cover->addChild(coverDepth);
  auto*coverOffset=new SoPolygonOffset;coverOffset->on=FALSE;cover->addChild(coverOffset);
  auto*clip=new SoClipPlane;clip->plane=SbPlane(SbVec3f(-1,0,0),.25f);cover->addChild(clip);
  auto*solid=new SoTexture2;const uint8_t green[]={64,128,32,255};solid->image.setValue(SbVec2s(1,1),4,green);solid->model=SoTexture2::REPLACE;cover->addChild(solid);cover->addChild(s.mesh);s.group->addChild(cover);
  auto*shift=new SoTranslation;s.receiver->insertChild(shift,s.receiver->getNumChildren()-1);
  const bool direct=h.off->getOptions().sceneTexture==COIN_RENDER_SCENE_TEXTURE_DIRECT;bool ok=true;
  const auto frame=[&](Frame&f,const std::string&phase){return h.frame(s,f,phase,true,false,2)&&c036Delivery(s,*proxies.first,direct)&&c036Delivery(s,*proxies.second,direct);};
  const auto delivery=[&](){bool valid=true;unsigned receivers=0;
    for(const auto*proxy:{proxies.first,proxies.second}){CoinWgpuFfiFrame ffi;std::string why;if(!ffi.prepare(proxy->plan,side,side,why,false)){valid=false;continue;}
      for(const auto&draw:proxy->plan.draws){if(draw.shadowLightSlot||draw.sourceNodeId!=s.mesh->getNodeId())continue;const auto&state=proxy->plan.renderStates[draw.renderStateSlot];
        const auto&packed=ffi.getView().states[draw.renderStateSlot];
        if(diagnostic){std::cout<<"c036_depth_delivery mode="<<state.transparencyType<<" authored_mode="<<s.mode->value.getValue()
          <<" core_range="<<std::hexfloat<<state.depthRange[0]<<','<<state.depthRange[1]<<" ffi_range="<<packed.depth_range[0]<<','<<packed.depth_range[1]
          <<" ffi_depth_function="<<packed.depth_function<<" write="<<state.depthWrite<<" polygon_offset="<<state.polygonOffsetEnabled<<" units="<<state.polygonOffsetUnits
          <<" z_authored="<<s.positions->point[0][2]<<" shift_z="<<shift->translation.getValue()[2]<<" camera_z="<<camera->position.getValue()[2]
          <<" near="<<camera->nearDistance.getValue()<<" far="<<camera->farDistance.getValue()<<" mvp=";
          for(int i=0;i<16;++i)std::cout<<packed.model_view_projection[i]<<',';
          const float clipZ=packed.model_view_projection[14]-packed.model_view_projection[10];
          const float clipW=packed.model_view_projection[15]-packed.model_view_projection[11];
          const float glNormalized=.5f*(clipZ/clipW)+.5f;
          const float mappedFloat=packed.depth_range[0]+(packed.depth_range[1]-packed.depth_range[0])*glNormalized;
          std::cout<<" plane_clip_z="<<clipZ<<" plane_clip_w="<<clipW<<" gl_normalized_float="<<glNormalized<<" mapped_float_gl="<<mappedFloat<<std::defaultfloat<<'\n';}
        if(state.transparencyType!=s.mode->value.getValue())continue;++receivers;
        valid &= state.depthRange[0]==s.depth->range.getValue()[0]&&state.depthRange[1]==s.depth->range.getValue()[1]&&state.depthWrite==bool(s.depth->write.getValue())&&
          state.polygonOffsetEnabled==bool(offset->on.getValue())&&packed.depth_range[0]==state.depthRange[0]&&packed.depth_range[1]==state.depthRange[1]&&
          bool(packed.polygon_offset_enabled)==state.polygonOffsetEnabled&&(!offset->on.getValue()||(state.polygonOffsetUnits==offset->units.getValue()&&packed.polygon_offset_units==state.polygonOffsetUnits));}}
    return check(valid&&receivers>0,"C03.6 transparent range/write/offset are delivered unchanged in Core and FFI");};
  const auto math=[&](const Frame&f,const SbVec2f&range,bool leftPass,bool writes){
    constexpr double formatBound=2./16777215.;const double d=double(range[0])+(double(range[1])-range[0])*.25;
    const double alpha=.75*.75*.5;const double source[]={.75*.75*.5,.875*.875*.75,1.*1.*.875};unsigned bad=0,checked=0,badDepth=0,badRgb=0;
    for(int y=side/4;y<3*side/4;++y)for(int x=side/4;x<3*side/4;++x){if(x>side*3/8&&x<side*5/8)continue;const size_t p=size_t(y*side+x);const bool covered=x<=side*3/8;const bool pass=!covered||leftPass;++checked;
      for(int c=0;c<3;++c){const double background=covered?green[c]:alphaBackground[c];const int expected=int(std::lround(pass?255*source[c]*alpha+background*(1-alpha):background));
        if(std::abs(int(f.rgba[4*p+c])-expected)>3&&badRgb++==0)std::cout<<"c036_depth first_rgb x="<<x<<" y="<<y<<" c="<<c<<" expected="<<expected<<" actual="<<int(f.rgba[4*p+c])
          <<" physical="<<int(h.lastShown[4*p+c])<<" readback="<<int(h.lastReadback[4*p+c])<<" covered="<<covered<<" pass="<<pass
          <<" opaque_depth_expected="<<std::hexfloat<<d<<" attachment_depth="<<f.depth[p]<<" range="<<range[0]<<','<<range[1]
          <<" raw_plane_depth="<<.25<<" shift_z="<<shift->translation.getValue()[2]<<std::defaultfloat<<'\n';
        bad+=std::abs(int(f.rgba[4*p+c])-expected)>3;bad+=std::abs(int(h.lastShown[4*p+c])-expected)>3;bad+=std::abs(int(h.lastReadback[4*p+c])-expected)>3;}
      const double expectedDepth=covered||(writes&&!weighted)?d:1.;const bool depthWrong=!std::isfinite(f.depth[p])||std::abs(double(f.depth[p])-expectedDepth)>formatBound;
      if(depthWrong&&badDepth++==0)std::cout<<"c036_depth first_depth x="<<x<<" y="<<y<<" expected="<<std::hexfloat<<expectedDepth<<" actual="<<f.depth[p]<<" format_bound="<<formatBound<<std::defaultfloat<<'\n';
      bad+=depthWrong;bad+=std::abs(int(f.rgba[4*p+3])-255)>1;}
    if(diagnostic)for(const int x:{side/4,3*side/4}){const int y=side/4;const size_t p=size_t(y*side+x);if(p>=f.depth.size()||4*p+3>=f.rgba.size())continue;
      uint32_t bits=0;std::memcpy(&bits,&f.depth[p],sizeof(bits));
      std::cout<<"c036_depth_sample x="<<x<<" y="<<y<<" role="<<(x<side/2?"covered":"open")
        <<" rgba="<<int(f.rgba[4*p])<<','<<int(f.rgba[4*p+1])<<','<<int(f.rgba[4*p+2])<<','<<int(f.rgba[4*p+3])
        <<" depth_hex="<<std::hexfloat<<f.depth[p]<<" depth_bits="<<std::hex<<bits<<std::dec<<std::defaultfloat
        <<" function="<<s.depth->function.getValue()<<" write="<<s.depth->write.getValue()<<" requested_writes="<<writes
        <<" range="<<std::hexfloat<<range[0]<<','<<range[1]<<" shift="<<shift->translation.getValue()[2]
        <<" offset_enabled="<<offset->on.getValue()<<" offset_units="<<offset->units.getValue()<<std::defaultfloat
        <<" left_pass_expected="<<leftPass<<'\n';
    }
    return check(checked>1000&&bad==0,"C03.6 geometry/range, coplanar comparison and compositor depth-write obey B49 bounds");};
  Frame initial,final;
  for(const auto range:{SbVec2f(0,1),SbVec2f(.2f,.8f),SbVec2f(.8f,.2f)}){
    s.depth->range=range;coverDepth->range=range;s.depth->write=FALSE;offset->on=FALSE;s.depth->function=SoDepthBuffer::ALWAYS;
    Frame a,b,restored;const bool first=frame(a,"transparent custom range A");ok &= first;if(!first)continue;if(initial.rgba.empty())initial=a;
    ok &= delivery()&&math(a,range,true,false);
    s.depth->function=SoDepthBuffer::LEQUAL;const bool coplanar=frame(b,"transparent coplanar LEQUAL");ok &= coplanar;if(coplanar)ok &= delivery()&&math(b,range,true,false);
    if(diagnostic){
      // Main and occluder retain the exact same authored plane/mesh. Only the
      // main receiver's model transform moves by a representable world delta.
      constexpr float delta=1.f/4096.f;
      shift->translation.setValue(0,0,delta);const bool front=frame(b,"diagnostic geometric +Z neighbour");ok &= front;
      if(front)ok &= delivery()&&math(b,range,range[1]>range[0],false);
      shift->translation.setValue(0,0,-delta);const bool backNeighbour=frame(b,"diagnostic geometric -Z neighbour");ok &= backNeighbour;
      if(backNeighbour)ok &= delivery()&&math(b,range,range[1]<range[0],false);
      shift->translation.setValue(0,0,0);const bool plane=frame(b,"diagnostic original coplanar LEQUAL restored");ok &= plane;
      if(plane)ok &= delivery()&&math(b,range,true,false);
    }
    offset->on=TRUE;offset->units=16;const bool farther=frame(b,"positive polygon offset rejects coplanar left");ok &= farther;if(farther)ok &= delivery()&&math(b,range,false,false)&&check(alphaChanges(a,b)>0,"effective positive bias changes the coplanar translucent contribution");
    offset->units=-16;const bool closer=frame(b,"negative polygon offset passes coplanar left");ok &= closer;if(closer)ok &= delivery()&&math(b,range,true,false);
    offset->on=FALSE;const bool back=frame(restored,"polygon offset A restored");ok &= back;if(back)ok &= exact(a,restored);
    s.depth->write=TRUE;const bool wrote=frame(b,"transparent explicit depth-write requested");ok &= wrote;if(wrote)ok &= delivery()&&math(b,range,true,true);
    s.depth->write=FALSE;const bool unwrote=frame(restored,"transparent depth-write A restored");ok &= unwrote;if(unwrote)ok &= exact(a,restored);
  }
  s.depth->range=SbVec2f(0,1);coverDepth->range=SbVec2f(0,1);s.depth->function=SoDepthBuffer::ALWAYS;s.depth->write=FALSE;offset->on=FALSE;
  const bool restored=frame(final,"default range A restored");ok &= restored;if(restored&&!initial.rgba.empty())ok &= exact(initial,final);return ok;
}
}
#endif
