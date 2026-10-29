// Isolated renderer for the opt-in FreeCAD viewport connector. FreeCAD's Coin
// ABI never crosses this process boundary. There is one retained scene/action.
#include <Inventor/SoDB.h>
#include <Inventor/SoInput.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/nodes/SoRotation.h>
#include <Inventor/nodes/SoSwitch.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/rendering/SoPBRMaterial.h>
#include <Inventor/rendering/SoPBRMaterialData.h>
#include <Inventor/rendering/CoinRenderRetainedRenderer.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/rendering/CoinRenderNativeSurface.h>
#if defined(__linux__) && !defined(__ANDROID__)
#include <X11/Xlib.h>
#undef Status
#endif
#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinbgfx/CoinBgfxBackend.h"
#include "rendering/coinrender/CoinRenderSelectionCore.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
// The X connection belongs to the worker, the window to the Qt host. The host
// fences surface destruction until this target and connection have retired.
struct NativeDisplay {
  void * display=nullptr;
  ~NativeDisplay() {
#if defined(__linux__) && !defined(__ANDROID__)
    if(display)XCloseDisplay(static_cast<Display *>(display));
#endif
  }
};
void require(bool ok, const char * message) { if (!ok) throw std::runtime_error(message); }
std::string quoted(const std::string & text) {
  std::string result="\"";
  for (unsigned char c:text) {
    if(c=='"'||c=='\\') {result+='\\';result+=char(c);}
    else if(c>=32)result+=char(c);
  }
  return result+'"';
}
template<class T> T * named(const std::string & name) {
  SoNode * node=SoNode::getByName(name.c_str());
  require(node && node->isOfType(T::getClassTypeId()),"Missing typed viewport node");
  return static_cast<T *>(node);
}
struct PartState { float position[3], rotation[4]; int visible, selected, hover; };
struct MaterialState { SoPBRMaterialData value; };
struct Frame {
  uint64_t id; int width,height, perspective;
  float position[3],rotation[4],nearPlane,farPlane,focal,extent;
  float environmentRotation,environmentIntensity,exposure;
  std::vector<PartState> parts;
  std::vector<MaterialState> materials;
  bool anisotropyProvided=false;
  CoinRenderPbrEffects effects;
};
void readFloat(std::istream & stream,float & value) {
  require(bool(stream>>value)&&std::isfinite(value),"Invalid finite frame field");
}
void readRotation(std::istream & stream,float (&rotation)[4]) {
  float norm=0;for(float & v:rotation){readFloat(stream,v);norm+=v*v;}
  require(std::abs(norm-1)<.001f,"Non-unit frame rotation");
}
Frame readFrame(const std::string & line,size_t count) {
  std::istringstream stream(line);Frame frame{};std::string tag;
  require(bool(stream>>tag>>frame.id>>frame.width>>frame.height>>frame.perspective)&&tag=="FRAME","Invalid frame header");
  require(frame.width>0&&frame.height>0&&frame.width<=4096&&frame.height<=4096,"Invalid viewport dimensions");
  require(frame.perspective==0||frame.perspective==1,"Invalid camera kind");
  for(float & v:frame.position)readFloat(stream,v);
  readRotation(stream,frame.rotation);
  for(float * v:{&frame.nearPlane,&frame.farPlane,&frame.focal,&frame.extent,
                 &frame.environmentRotation,&frame.environmentIntensity,&frame.exposure})readFloat(stream,*v);
  require((!frame.perspective||frame.nearPlane>0)&&frame.farPlane>frame.nearPlane&&frame.focal>0&&frame.extent>0,"Invalid camera range");
  require(!frame.perspective||frame.extent<3.14159f,"Invalid perspective angle");
  require(frame.environmentIntensity>=0&&frame.environmentIntensity<=64&&frame.exposure>=-16&&frame.exposure<=16,"Invalid output parameters");
  frame.parts.resize(count);
  for(auto & part:frame.parts) {
    for(float & v:part.position)readFloat(stream,v);
    readRotation(stream,part.rotation);
    require(bool(stream>>part.visible>>part.selected>>part.hover),"Incomplete part state");
    for(int value:{part.visible,part.selected,part.hover})require(value==0||value==1,"Invalid part flag");
  }
  require(bool(stream>>tag),"Incomplete viewport frame");
  if(tag=="FX") {
    int shadows,ao,background;
    require(bool(stream>>shadows>>ao>>background),"Incomplete effect flags");
    for(int value:{shadows,ao,background})require(value==0||value==1,"Invalid effect flag");
    frame.effects.shadows=shadows;frame.effects.ambientOcclusion=ao;frame.effects.background=background;
    readFloat(stream,frame.effects.occlusionRadius);readFloat(stream,frame.effects.occlusionStrength);
    require(bool(stream>>frame.effects.shadowMapSize),"Invalid shadow size");
    readFloat(stream,frame.effects.shadowBias);
    require(bool(stream>>frame.effects.resourceBudgetBytes),"Invalid effects budget");
    float top[3],bottom[3];for(float & v:top)readFloat(stream,v);for(float & v:bottom)readFloat(stream,v);
    frame.effects.backgroundTop=SbColor(top);frame.effects.backgroundBottom=SbColor(bottom);
    std::string error;if(!coin_render_valid_pbr_effects(frame.effects,error))throw std::runtime_error(error);
    require(bool(stream>>tag),"Incomplete effects frame");
  }
  if(tag=="PBR") {
    frame.materials.resize(count);
    for(auto & entry:frame.materials) {
      auto & m=entry.value;float color[3],emissive[3];
      for(float & v:color)readFloat(stream,v);for(float & v:emissive)readFloat(stream,v);
      m.baseColor=SbColor(color);m.emissive=SbColor(emissive);
      for(float * v:{&m.metallic,&m.roughness,&m.clearcoat,&m.clearcoatRoughness,&m.normalScale})readFloat(stream,*v);
      require(bool(stream>>m.normalYFlip)&&so_valid_pbr_material(m),"Invalid PBR material packet");
    }
    for(size_t i=0;i<count;++i) {
      const auto & m=frame.materials[i].value;
      const SbColor highlight=frame.parts[i].selected?SbColor(.55f,.28f,0):frame.parts[i].hover?SbColor(0,.3f,.45f):SbColor(0,0,0);
      for(int c=0;c<3;++c)require(m.emissive[c]+highlight[c]<=65504.f,"PBR highlighted emission exceeds HDR range");
    }
    require(bool(stream>>tag),"Incomplete PBR packet");
  }
  if(tag=="ANIS") {
    require(!frame.materials.empty(),"Anisotropy requires a PBR material packet");
    for(auto & entry:frame.materials) {
      readFloat(stream,entry.value.anisotropy);readFloat(stream,entry.value.anisotropyRotation);
      require(so_valid_pbr_material(entry.value),"Invalid anisotropy packet");
    }
    frame.anisotropyProvided=true;
    require(bool(stream>>tag),"Incomplete anisotropy packet");
  }
  if(tag=="REFL") {
    int enabled;float normal[3],distance,tolerance;uint32_t scale;uint64_t budget;
    require(bool(stream>>enabled)&&(enabled==0||enabled==1),"Invalid planar reflection flag");
    for(float & v:normal)readFloat(stream,v);readFloat(stream,distance);
    require(bool(stream>>scale>>budget),"Invalid planar reflection scale/budget");readFloat(stream,tolerance);
    require(std::abs(SbVec3f(normal).sqrLength()-1.f)<.0001f,"Reflection plane requires a unit normal");
    frame.effects.planarReflection=enabled;frame.effects.reflectionPlane=SbPlane(SbVec3f(normal),distance);
    frame.effects.reflectionScale=scale;frame.effects.reflectionBudgetBytes=budget;frame.effects.reflectionPlaneTolerance=tolerance;
    std::string error;require(coin_render_valid_pbr_effects(frame.effects,error),error.c_str());
    require(bool(stream>>tag),"Incomplete planar reflection packet");
  }
  if(tag=="ROUGH") {
    int enabled;require(bool(stream>>enabled)&&(enabled==0||enabled==1),"Invalid rough reflection flag");
    frame.effects.roughPlanarReflection=enabled;
    require(bool(stream>>tag),"Incomplete rough reflection packet");
  }
  require(tag=="END","Incomplete viewport frame");
  require(!(stream>>tag),"Trailing frame data");return frame;
}
template<class Field,class Value> void assign(Field & field,const Value & value) {
  if(field.getValue()!=value)field.setValue(value);
}
void save(const std::string & path,const void * data,size_t size) {
  const auto temporary=path+".tmp";
  {std::ofstream out(temporary,std::ios::binary);require(bool(out),"Cannot create frame file");
   out.write(static_cast<const char *>(data),size);require(bool(out),"Cannot write frame file");}
  require(std::rename(temporary.c_str(),path.c_str())==0,"Cannot publish frame file");
}
}
int main(int argc,char **argv) {
  try {
    require(argc>=7,"Usage: coin_render_freecad_viewport SCENE.iv ENV.cibl OUTPUT_DIRECTORY PART_COUNT WIDTH HEIGHT [--current] [--window XLIB_XID]");
    bool current=false,metrics=false;uint64_t window=0;
    for(int i=7;i<argc;++i) {
      const std::string option=argv[i];
      if(option=="--current")current=true;
      else if(option=="--metrics")metrics=true;
      else if(option=="--window"&&i+1<argc) {window=std::stoull(argv[++i]);require(window!=0,"Invalid native window");}
      else require(false,"Invalid worker option");
    }
    const int count=std::stoi(argv[4]),width=std::stoi(argv[5]),height=std::stoi(argv[6]);
    require(count>0&&count<=10000&&width>0&&height>0&&width<=4096&&height<=4096,"Invalid initial viewport");
    SoDB::init();CoinRenderAction::initClass();SoInput input;
    require(input.openFile(argv[1]),"Cannot open viewport scene");
    SoSeparator * root=SoDB::readAll(&input);require(root,"Cannot read viewport scene");root->ref();
    // DEF lookup uses Coin's name table; no discovery traversal is necessary.
    auto * camera=named<SoCamera>("ViewportCamera");
    std::vector<SoTranslation *> placements;std::vector<SoRotation *> orientations;
    std::vector<SoPBRMaterial *> materials;std::vector<SoSwitch *> visibility;
    for(int i=0;i<count;++i) {
      placements.push_back(named<SoTranslation>("Placement_"+std::to_string(i)));
      orientations.push_back(named<SoRotation>("Orientation_"+std::to_string(i)));
      materials.push_back(named<SoPBRMaterial>("Material_"+std::to_string(i)));
      visibility.push_back(named<SoSwitch>("Visible_"+std::to_string(i)));
    }
    CoinRenderOptions options;options.renderer=COIN_RENDER_RENDERER_VULKAN;options.pbrEnvironmentBundle=argv[2];
    NativeDisplay connection;CoinRenderNativeSurfaceDescriptor surface{};
    if(window) {
#if defined(__linux__) && !defined(__ANDROID__)
      require(XInitThreads()!=0,"Cannot initialize Xlib threading");
      connection.display=XOpenDisplay(nullptr);require(connection.display,"Cannot open the host X11 display");
      surface.abiVersion=COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;surface.structSize=sizeof(surface);
      surface.type=COIN_RENDER_SURFACE_XLIB;surface.native.xlib.display=connection.display;surface.native.xlib.window=window;
#else
      require(false,"Native FreeCAD viewport worker currently requires Xlib");
#endif
    }
    std::unique_ptr<CoinRenderTarget> target(window
      ? CoinRenderTarget::createWindow(surface,SbVec2i32(width,height),options)
      : CoinRenderTarget::createOffscreen(SbVec2i32(width,height),options));
    require(bool(target),"Cannot create viewport target");
    if(window)target->setDepthReadbackEnabled(FALSE);
    CoinRenderRetainedRenderer retained{SbViewportRegion(width,height)};
    CoinRenderAction control{SbViewportRegion(width,height)};
    if(current)control.setRenderTarget(target.get());
    int lastWidth=width,lastHeight=height;
    std::string line;
    while(std::getline(std::cin,line)) {
      if(line=="QUIT")break;
      try {
        const Frame frame=readFrame(line,count);
        require(frame.perspective==int(camera->isOfType(SoPerspectiveCamera::getClassTypeId())),"Camera kind changed; reconnect viewport");
        if(frame.width!=lastWidth||frame.height!=lastHeight) {
          require(target->resize(SbVec2i32(frame.width,frame.height)),"Viewport resize failed");
          const SbViewportRegion vp(frame.width,frame.height);retained.setViewportRegion(vp);control.setViewportRegion(vp);
          lastWidth=frame.width;lastHeight=frame.height;
        }
        assign(camera->position,SbVec3f(frame.position));assign(camera->orientation,SbRotation(frame.rotation));
        assign(camera->nearDistance,frame.nearPlane);assign(camera->farDistance,frame.farPlane);assign(camera->focalDistance,frame.focal);
        if(frame.perspective)assign(static_cast<SoPerspectiveCamera *>(camera)->heightAngle,frame.extent);
        else assign(static_cast<SoOrthographicCamera *>(camera)->height,frame.extent);
        for(int i=0;i<count;++i) {
          const auto & part=frame.parts[i];assign(placements[i]->translation,SbVec3f(part.position));
          assign(orientations[i]->rotation,SbRotation(part.rotation));
          assign(visibility[i]->whichChild,part.visible?SO_SWITCH_ALL:SO_SWITCH_NONE);
          // Interaction is logical material state, captured with the same scene.
          SbColor baseEmission(0,0,0);
          if(!frame.materials.empty()) {
            const auto & m=frame.materials[i].value;
            assign(materials[i]->baseColor,m.baseColor);assign(materials[i]->metallic,m.metallic);
            assign(materials[i]->roughness,m.roughness);assign(materials[i]->clearcoat,m.clearcoat);
            assign(materials[i]->clearcoatRoughness,m.clearcoatRoughness);assign(materials[i]->normalScale,m.normalScale);
            if(frame.anisotropyProvided) {
              assign(materials[i]->anisotropy,m.anisotropy);assign(materials[i]->anisotropyRotation,m.anisotropyRotation);
            }
            assign(materials[i]->normalYFlip,SbBool(m.normalYFlip));baseEmission=m.emissive;
          }
          const SbColor highlight=part.selected?SbColor(.55f,.28f,0):part.hover?SbColor(0,.3f,.45f):SbColor(0,0,0);
          const SbColor emission(baseEmission[0]+highlight[0],baseEmission[1]+highlight[1],baseEmission[2]+highlight[2]);
          assign(materials[i]->emissive,emission);
        }
        require(target->setPbrEnvironmentParameters(frame.environmentRotation,frame.environmentIntensity)&&
                target->setPbrExposureStops(frame.exposure)&&target->setPbrEffects(frame.effects),"Invalid environment parameters");
        const auto renderBegin=std::chrono::steady_clock::now();
        if(current) {
          control.apply(root);require(control.getLastStatus()==CoinRenderAction::SUCCESS,control.getLastError().getString());
        }else {
          const auto status=retained.render(root,*target);
          require(status==CoinRenderAction::SUCCESS,retained.getLastError().getString());
        }
        const auto renderEnd=std::chrono::steady_clock::now();
        if(window) {
          const auto & state=target->getPimpl();
          require(!state->windowReadbackRequested&&state->colorBuffer.empty()&&state->depthBuffer.empty(),
                  "Native presentation unexpectedly allocated CPU pixel storage");
        }else {
          std::vector<uint8_t> rgba;std::vector<float> depth;target->readbackRGBA(rgba);target->readbackDepth(depth);
          require(rgba.size()==size_t(frame.width)*frame.height*4&&depth.size()==size_t(frame.width)*frame.height,"Incomplete viewport publication");
          const auto prefix=std::string(argv[3])+"/frame-"+std::to_string(frame.id);
          save(prefix+".rgba",rgba.data(),rgba.size());save(prefix+".depth",depth.data(),depth.size()*sizeof(float));
        }
        const auto stats=retained.getStatistics();
        const auto backend=static_cast<CoinBgfxBackend *>(target->getPimpl()->backend.get());
        const auto uploads=backend->getPbrEnvironmentUploadCount();
        std::cout<<"{";
        if(metrics) {
          const auto * gpu=bgfx::getStats();
          std::cout<<"\"render_ms\":"<<std::chrono::duration<double,std::milli>(renderEnd-renderBegin).count()
            <<",\"effect_attachment_bytes\":"<<backend->getPbrEffectAttachmentBytes()
            <<",\"texture_bytes\":"<<gpu->textureMemoryUsed<<",\"render_target_bytes\":"<<gpu->rtMemoryUsed
            <<",\"renderer\":"<<quoted(bgfx::getRendererName(bgfx::getRendererType()))
            <<",\"gpu_vendor_id\":"<<bgfx::getCaps()->vendorId<<",\"gpu_device_id\":"<<bgfx::getCaps()->deviceId
            <<",\"gpu_memory_used_bytes\":"<<gpu->gpuMemoryUsed<<",";
        }
        std::cout<<"\"status\":\"ok\",\"id\":"<<frame.id<<",\"width\":"<<frame.width<<",\"height\":"<<frame.height
          <<",\"serial\":"<<target->getLastSubmissionSerial()<<",\"captures\":"<<stats.captures<<",\"plans\":"<<stats.planBuilds
          <<",\"incremental_updates\":"<<stats.incrementalUpdates<<",\"transport_builds\":"<<stats.transportBuilds
          <<",\"copied_vertices\":"<<stats.copiedVertices<<",\"environment_uploads\":"<<uploads
          <<",\"anisotropy_preparations\":"<<backend->getPbrAnisotropyPreparationCount()
          <<",\"anisotropy_product_bytes\":"<<backend->getPbrAnisotropyProductBytes()
          <<",\"environment_gpu_bytes\":"<<backend->getPbrEnvironmentGpuBytes()
          <<",\"reflection_passes\":"<<backend->getPbrReflectionRenderCount()<<",\"reflection_attachment_bytes\":"<<backend->getPbrReflectionAttachmentBytes()<<",\"rough_reflection_enabled\":"<<(backend->getPbrRoughReflectionEnabled()?"true":"false")<<",\"rough_proxy_height\":"<<backend->getPbrRoughProxyHeight()
          <<",\"static_reuses\":"<<backend->getPbrStaticPresentationReuseCount()<<",\"shaded_frames\":"<<backend->getPbrShadedFrameCount()
          <<",\"shadow_passes\":"<<backend->getPbrShadowPassCount()<<",\"ao_depth_passes\":"<<backend->getPbrOcclusionDepthPassCount()
          <<",\"native_window\":"<<window<<",\"pixel_readbacks\":"<<(window?0:2)
          <<",\"cpu_pixel_bytes\":"<<(window?0:uint64_t(frame.width)*frame.height*8)<<"}"<<std::endl;
      }catch(const std::exception & error) {
        std::cout<<"{\"status\":\"error\",\"serial\":"<<target->getLastSubmissionSerial()<<",\"error\":"<<quoted(error.what())<<"}"<<std::endl;
      }
    }
    control.setRenderTarget(nullptr);root->unref();return 0;
  }catch(const std::exception & error){std::cerr<<error.what()<<'\n';return 1;}
}
