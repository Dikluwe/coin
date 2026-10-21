#ifndef COIN_RENDER_SHADOW_SPATIAL_FIXTURE_H
#define COIN_RENDER_SHADOW_SPATIAL_FIXTURE_H
#ifndef COIN_RENDER_SHADOW_MULTITEXTURE_FIXTURE_ONLY
#define COIN_RENDER_SHADOW_MULTITEXTURE_FIXTURE_ONLY
#include "CoinRenderShadowMultitextureTest.cpp"
#endif
#include "CoinRenderVolumeOracle.h"
#include <Inventor/nodes/SoTexture3.h>
#include <Inventor/nodes/SoTextureCubeMap.h>
#include <Inventor/nodes/SoTextureCoordinate3.h>
#include <Inventor/nodes/SoTransparencyType.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoPolygonOffset.h>
namespace {
struct SpatialScene:Scene {
  SoTexture3*volume[8]{};SoTextureCubeMap*cube[8]{};SoTextureCoordinate3*uvw[8]{};
  unsigned kinds[8]{};bool whiteImages=true;uint8_t sourceAlpha=255;
  SoComplexity*sampling=nullptr;
  explicit SpatialScene(int maps=2,unsigned mask=255,bool dir=false,float shading=1.f,int forcedKind=-1):Scene(maps,8,shading,dir){
    materialBinding->value=SoMaterialBinding::OVERALL;normalBinding->value=SoNormalBinding::OVERALL;
    receiverMaterial->diffuseColor.setValue(.72f,.75f,.78f);normalData->vector.setValue(0,0,1);
    for(int i=0;i<receiver->getNumChildren();++i)if(receiver->getChild(i)->isOfType(SoComplexity::getClassTypeId()))sampling=static_cast<SoComplexity*>(receiver->getChild(i));
    sampling->textureQuality=.1f;units.clear();
    for(int u=0;u<8;++u){stages[u]->removeAllChildren();stages[u]->whichChild=(mask&(1u<<u))?SO_SWITCH_ALL:SO_SWITCH_NONE;if(!(mask&(1u<<u)))continue;units.push_back(u);kinds[u]=forcedKind<0?unsigned(u%3):unsigned(forcedKind);
      auto*selected=new SoTextureUnit;selected->unit=u;stages[u]->addChild(selected);
      if(kinds[u]==0){volume[u]=new SoTexture3;volume[u]->wrapS=SoTexture3::CLAMP;volume[u]->wrapT=SoTexture3::CLAMP;volume[u]->wrapR=SoTexture3::CLAMP;stages[u]->addChild(volume[u]);}
      else if(kinds[u]==1){cube[u]=new SoTextureCubeMap;cube[u]->wrapS=SoTextureCubeMap::CLAMP;cube[u]->wrapT=SoTextureCubeMap::CLAMP;stages[u]->addChild(cube[u]);}
      else{images[u]=new SoTexture2;images[u]->wrapS=SoTexture2::CLAMP;images[u]->wrapT=SoTexture2::CLAMP;stages[u]->addChild(images[u]);}
      uvw[u]=new SoTextureCoordinate3;stages[u]->addChild(uvw[u]);matrices[u]=new SoTextureMatrixTransform;stages[u]->addChild(matrices[u]);
      combines[u]=new SoSwitch;combines[u]->whichChild=SO_SWITCH_NONE;combine[u]=new SoTextureCombine;combines[u]->addChild(combine[u]);stages[u]->addChild(combines[u]);
      raw(u,kinds[u]==0?SbVec3f(.5f/3,.5f/5,6.5f/7):kinds[u]==1?SbVec3f(0,0,1):SbVec3f(.5f,.5f,0));
    }paint(true);
  }
  static std::array<uint8_t,4> voxel(int u,unsigned x,unsigned y,unsigned z,uint8_t alpha=255){return {{uint8_t(165+(u*7+x*13+y*5+z*11)%85),uint8_t(170+(u*11+x*3+y*17+z*7)%80),uint8_t(160+(u*13+x*19+y*7+z*5)%90),alpha}};}
  static std::array<uint8_t,4> faceColor(int u,unsigned face,uint8_t alpha=255){return {{uint8_t(165+(u*9+face*13)%85),uint8_t(170+(u*5+face*19)%80),uint8_t(160+(u*17+face*7)%90),alpha}};}
  void raw(int u,const SbVec3f&v){for(int i=0;i<9;++i)uvw[u]->point.set1Value(i,v);}
  void model(int value){for(int u:units){if(volume[u]){volume[u]->model=value;volume[u]->blendColor.setValue(.2f,.4f,.6f);}else if(cube[u]){cube[u]->model=value;cube[u]->blendColor.setValue(.2f,.4f,.6f);}else{images[u]->model=value;images[u]->blendColor.setValue(.2f,.4f,.6f);}combines[u]->whichChild=SO_SWITCH_NONE;}}
  void replace(int u){combines[u]->whichChild=SO_SWITCH_ALL;combine[u]->rgbOperation=SoTextureCombine::REPLACE;combine[u]->rgbSource.set1Value(0,SoTextureCombine::TEXTURE);combine[u]->alphaOperation=SoTextureCombine::REPLACE;combine[u]->alphaSource.set1Value(0,SoTextureCombine::PREVIOUS);}
  void paint(bool white,uint8_t alpha=255){whiteImages=white;sourceAlpha=alpha;model(SoTexture2::MODULATE);
    for(int u:units){if(volume[u]){std::vector<uint8_t> pixels(3*5*7*4);for(unsigned z=0;z<7;++z)for(unsigned y=0;y<5;++y)for(unsigned x=0;x<3;++x){auto rgb=voxel(u,x,y,z,alpha);for(unsigned c=0;c<4;++c)pixels[((z*5+y)*3+x)*4+c]=white&&c<3?255:rgb[c];}volume[u]->images.setValue(SbVec3s(3,5,7),4,pixels.data());}
      else if(cube[u]){SoSFImage*faces[]={&cube[u]->imagePosX,&cube[u]->imageNegX,&cube[u]->imagePosY,&cube[u]->imageNegY,&cube[u]->imagePosZ,&cube[u]->imageNegZ};for(unsigned f=0;f<6;++f){auto color=faceColor(u,f,alpha);std::vector<uint8_t> bytes(2*2*4);for(unsigned p=0;p<4;++p)for(unsigned c=0;c<4;++c)bytes[p*4+c]=white&&c<3?255:color[c];faces[f]->setValue(SbVec2s(2,2),4,bytes.data());}}
      else{auto color=faceColor(u,0,alpha);if(white)color[0]=color[1]=color[2]=255;images[u]->image.setValue(SbVec2s(1,1),4,color.data());}}
  }
  std::array<double,3> sampled(int u)const{auto rgba=kinds[u]==0?voxel(u,0,0,6):faceColor(u,kinds[u]==1?4:0);return {{double(rgba[0]),double(rgba[1]),double(rgba[2])}};}
};
struct SpatialRttScene {
  SpatialScene consumer;SpatialScene producerScene;SoSceneTexture2*texture=new SoSceneTexture2;int producerUnit=7;
  explicit SpatialRttScene(int kind=1):consumer(2,255),producerScene(2,128,false,1.f,kind){
    producerScene.paint(true);producerScene.replace(producerUnit);
    texture->scene=producerScene.root;texture->size.setValue(64,64);texture->type=SoSceneTexture2::RGBA8;texture->model=SoSceneTexture2::MODULATE;texture->transparencyFunction=SoSceneTexture2::NONE;texture->backgroundColor.setValue(1,1,1,1);texture->wrapS=SoSceneTexture2::CLAMP;texture->wrapT=SoSceneTexture2::CLAMP;
    consumer.stages[5]->insertChild(texture,2);
  }
  void colored(){consumer.paint(false);producerScene.paint(false);producerScene.replace(producerUnit);}
  void white(){consumer.paint(true);producerScene.paint(true);producerScene.replace(producerUnit);}
  std::array<double,3> produced()const{return producerScene.sampled(producerUnit);}
};

struct SpatialHarness {
  std::unique_ptr<CoinRenderTarget>target;CoinRenderAction action{SbViewportRegion(side,side)};Proxy*proxy=nullptr;
  SpatialHarness(const CoinRenderCapabilities&caps,unsigned samples=1,CoinRenderTransparencyMode mechanism=COIN_RENDER_TRANSPARENCY_COIN,CoinRenderSceneTextureMode route=COIN_RENDER_SCENE_TEXTURE_STAGED,bool srgb=false){auto options=samplingTestOptions();if(srgb)options.storedTextureColorSpace=COIN_RENDER_TEXTURE_SRGB;options.renderer=static_cast<CoinRenderRenderer>(caps.renderer);options.sampleCount=samples;options.transparency=mechanism;options.sceneTexture=route;target.reset(samplingTestOffscreen(SbVec2i32(side,side),options));if(target){auto&p=target->getPimpl().get();if(p.prepareBackend()!=CoinRenderBackendStatus::SUCCESS||!p.backend||!p.backend->isGpuBackend()){target.reset();return;}proxy=new Proxy(std::move(p.backend));p.backend.reset(proxy);target->setDepthReadbackEnabled(TRUE);action.setRenderTarget(target.get());}action.setBackgroundColor(SbColor4f(0,0,0,1));}
  ~SpatialHarness(){action.setRenderTarget(nullptr);}
  bool render(SoNode*root,Frame&f,const std::string&phase){if(!target)return check(false,"spatial GPU target available");auto before=target->getLastSubmissionSerial();action.apply(root);target->readbackRGBA(f.rgba);target->readbackDepth(f.depth);f.serial=target->getLastSubmissionSerial();bool pass=action.getLastStatus()==CoinRenderAction::SUCCESS&&f.serial>before&&f.rgba.size()==size_t(side*side*4)&&f.depth.size()==size_t(side*side);std::cout<<"spatial_shadow "<<phase<<" status="<<action.getLastStatus()<<" serial="<<before<<','<<f.serial<<" error="<<action.getLastError().getString()<<'\n';return check(pass,phase);}
  bool reject(SoNode*root,const Frame&stable,const std::string&phase){auto before=target->getLastSubmissionSerial();unsigned producers=proxy->producers;action.apply(root);Frame f;target->readbackRGBA(f.rgba);target->readbackDepth(f.depth);return check(action.getLastStatus()!=CoinRenderAction::SUCCESS&&target->getLastSubmissionSerial()==before&&proxy->producers==producers&&f.rgba==stable.rgba&&f.depth==stable.depth,phase+" rejects before producer/publication");}
};
bool spatialColor(const Frame&f,const std::array<double,3>&rgb,const std::string&name,unsigned extent=side,double alpha=1){if(!check(f.rgba.size()==size_t(extent)*extent*4,"spatial oracle complete buffer"))return false;unsigned count=0,bad=0;int maximum=0;for(unsigned y=extent/4;y<extent*3/4;++y)for(unsigned x=extent/4;x<extent*3/4;++x){size_t p=(size_t(y)*extent+x)*4;for(unsigned c=0;c<3;++c){int delta=std::abs(int(f.rgba[p+c])-int(std::lround(rgb[c]*alpha)));maximum=std::max(maximum,delta);bad+=delta>5;}++count;}std::cout<<"spatial_color "<<name<<" expected="<<rgb[0]<<','<<rgb[1]<<','<<rgb[2]<<" tested="<<count<<" bad="<<bad<<" max="<<maximum<<'\n';return check(count>0&&bad==0,name);}
bool spatialComposition(const SpatialScene&s,const Frame&white,const Frame&colored,int model=SoTexture2::MODULATE,unsigned extent=side,const std::array<double,3>*rttColor=nullptr){
  if(!check(white.rgba.size()==size_t(extent*extent*4)&&colored.rgba.size()==white.rgba.size()&&white.depth.size()==size_t(extent*extent)&&colored.depth.size()==white.depth.size(),"spatial composition complete RGBA/depth"))return false;
  unsigned count=0,rgbBad=0,depthBad=0,alphaBad=0;int maximum=0;bool first=true;
  for(unsigned y=extent/6;y<extent*5/6;++y)for(unsigned x=extent/6;x<extent*5/6;++x){size_t p=size_t(y*extent+x);if(white.rgba[p*4+3]!=255)continue;for(unsigned c=0;c<3;++c){double v=white.rgba[p*4+c]/255.;for(int u:s.units){double tex=(rttColor&&u==5?(*rttColor)[c]:s.sampled(u)[c])/255.;if(model==SoTexture2::MODULATE)v*=tex;else if(model==SoTexture2::DECAL)v=tex;else v=v*(1-tex)+std::array<double,3>{{.2,.4,.6}}[c]*tex;}int expected=int(std::lround(v*255)),delta=std::abs(int(colored.rgba[p*4+c])-expected);maximum=std::max(maximum,delta);rgbBad+=delta>5;if(first&&delta>5){std::cerr<<"spatial_composition_first xy="<<x<<','<<y<<" channel="<<c<<" white="<<unsigned(white.rgba[p*4+c])<<" expected="<<expected<<" actual="<<unsigned(colored.rgba[p*4+c])<<" model="<<model<<" units="<<s.units.size()<<'\n';first=false;}}depthBad+=white.depth[p]!=colored.depth[p];alphaBad+=colored.rgba[p*4+3]!=255;++count;}
  std::cout<<"spatial_composition model="<<model<<" modulate_enum="<<SoTexture2::MODULATE<<" units="<<s.units.size()<<" tested="<<count<<" rgb_bad="<<rgbBad<<" depth_bad="<<depthBad<<" alpha_bad="<<alphaBad<<" max_rgb="<<maximum<<'\n';
  return check(count>extent*extent/8&&rgbBad==0&&depthBad==0&&alphaBad==0,"independent spatial cascade arithmetic on shadowed white-equivalent primary");
}
bool spatialDelivery(const SpatialScene&s,const CoinRenderFramePlan&frame){CoinWgpuFfiFrame ffi;std::string error;if(!check(ffi.prepare(frame,side,side,error,false),"spatial Core/FFI prepare "+error))return false;const auto&view=ffi.getView();bool ok=check(view.abi_version==COIN_WGPU_ABI_VERSION&&frame.shadowLights.size()==size_t(s.slots),"spatial retains current ABI and all real maps");unsigned witnesses=0;for(const auto&draw:frame.draws){if(draw.shadowLightSlot||draw.sourceNodeId!=s.mesh->getNodeId())continue;++witnesses;const auto&state=frame.renderStates[draw.renderStateSlot];const auto&gpu=view.states[draw.renderStateSlot];std::set<uint32_t>slots;for(int u:s.units){auto layer=coin_render_texture_unit(state,u);ok &=check(layer.enabled&&layer.imageSlot<frame.textures.size()&&layer.samplerSlot<frame.samplers.size(),"each spatial unit keeps resource/sampler");if(!layer.enabled||layer.imageSlot>=frame.textures.size())continue;slots.insert(layer.imageSlot);const auto&image=frame.textures[layer.imageSlot];const auto&texture=view.textures[layer.imageSlot];ok &=check(texture.format==(s.kinds[u]==0?11u:s.kinds[u]==1?12u:0u),"volume/cube/2D format remains per unit");if(s.kinds[u]==0)ok &=check(image.volumeWidth==3&&image.volumeHeight==5&&image.volumeDepth==7&&texture.reserved==0x060402,"NPOT exact XYZ dimensions survive shadow transport");if(s.kinds[u]==1)ok &=check(image.cubeFaceSize==2,"cube face extent survives shadow transport");const auto&sampler=frame.samplers[layer.samplerSlot];ok &=check(view.samplers[layer.samplerSlot].filter==uint32_t(sampler.filter)&&view.samplers[layer.samplerSlot].wrap_s==uint32_t(sampler.wrapS)&&view.samplers[layer.samplerSlot].wrap_t==uint32_t(sampler.wrapT),"per-unit sampler survives transport");
      const auto raw=s.uvw[u]->point[0];unsigned coreChecked=0;for(uint32_t vi=draw.geometry.firstVertex;vi<draw.geometry.firstVertex+draw.geometry.vertexCount;++vi){if(vi>=frame.vertices.size()){ok=false;break;}const auto&v=frame.vertices[vi];const float*st=u?v.extraTexcoords[u-1]:v.texcoord;SbVec4f transformed;layer.matrix.multVecMatrix(SbVec4f(st[0],st[1],v.textureR[u],v.textureQ[u]),transformed);SbVec4f authored;s.matrices[u]->matrix.getValue().multVecMatrix(SbVec4f(raw[0],raw[1],raw[2],1),authored);for(unsigned c=0;c<4;++c)ok &=check(std::abs(transformed[c]-authored[c])<2e-5f,"each actual receiver vertex preserves transformed STRQ");++coreChecked;}ok &=check(coreChecked>0,"receiver geometry owns spatial coordinate witnesses");
      const float*matrix=u?gpu.extra_textures[u-1].matrix:gpu.texture_matrix;SbMatrix authored=s.matrices[u]->matrix.getValue();for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)ok &=check(matrix[r*4+c]==layer.matrix[r][c],"FFI keeps full independent STRQ matrix");
      const uint32_t gpuEnabled=u?gpu.extra_textures[u-1].enabled:gpu.has_texture;const uint32_t gpuImage=u?gpu.extra_textures[u-1].texture_slot:gpu.texture_slot;const uint32_t gpuSampler=u?gpu.extra_textures[u-1].sampler_slot:gpu.sampler_slot;ok &=check(gpuEnabled&&gpuImage==layer.imageSlot&&gpuSampler==layer.samplerSlot,"FFI binds each independent image/sampler to the actual unit");
      unsigned ffiChecked=0;for(uint32_t di=0;di<view.draw_count;++di){const auto&d=view.draws[di];if(d.render_state_slot!=draw.renderStateSlot)continue;for(uint32_t vi=d.first_vertex;vi<d.first_vertex+d.vertex_count;++vi){if(vi>=view.vertex_count){ok=false;break;}const auto*v=u?view.vertices[vi].extra_texcoords[u-1]:view.vertices[vi].texcoord;SbVec4f transformed;layer.matrix.multVecMatrix(SbVec4f(v[0],v[1],v[2],v[3]),transformed);SbVec4f expected;authored.multVecMatrix(SbVec4f(raw[0],raw[1],raw[2],1),expected);for(unsigned c=0;c<4;++c)ok &=check(std::abs(transformed[c]-expected[c])<2e-5f,"every actual FFI receiver vertex keeps complete transformed STRQ");++ffiChecked;}}ok &=check(ffiChecked>0,"FFI receiver coordinate witnesses belong to receiver draw");
    }if(!s.whiteImages)ok &=check(slots.size()==s.units.size(),"differently authored unit resources never alias");ok &=check(!state.transparentTexture||s.sourceAlpha<255,"zero atlas padding never implies source transparency");}return check(witnesses>0,"actual spatial shadow receiver captured")&&ok;}
}
#endif
