// Investigation only: independent Vulkan MSAA4 resolve/copy/Xlib + EGL compatibility MSAA4.
// No Coin, BGFX or wgpu symbols. One acquire/submit/present per frame; no image retry renders.
#define VK_USE_PLATFORM_XLIB_KHR
#include <vulkan/vulkan.h>
#include <X11/Xutil.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>
#include "CoinRenderEglLifetimeFixture.h"
#include <algorithm>
#include <atomic>
#include <exception>
#include <mutex>
#include <sstream>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
using Clock=std::chrono::steady_clock;
constexpr uint32_t initialSide=96;
constexpr uint64_t timeoutNs=1000000000;
std::atomic<unsigned> validationErrors{0},teardownErrors{0};
uint64_t monotonicUs(){return uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch()).count());}
void diagnostic(const std::string &line){static std::mutex mutex;std::lock_guard<std::mutex> lock(mutex);std::cout<<line<<std::endl;}
struct JoinPresent {
  std::thread &worker;
  ~JoinPresent(){if(worker.joinable())worker.join();}
};
double ms(Clock::time_point t) {return std::chrono::duration<double,std::milli>(Clock::now()-t).count();}
template<class T> uint64_t handle(T value) {
  static_assert(sizeof(value)<=sizeof(uint64_t),"handle fits diagnostic");
  uint64_t result=0; std::memcpy(&result,&value,sizeof(value)); return result;
}
void check(VkResult result,const char * call) {
  if (result!=VK_SUCCESS) throw std::runtime_error(std::string(call)+" result="+std::to_string(result));
}
VKAPI_ATTR VkBool32 VKAPI_CALL validation(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
  VkDebugUtilsMessageTypeFlagsEXT,const VkDebugUtilsMessengerCallbackDataEXT * data,void *) {
  if (severity&VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ++validationErrors;
  std::cerr<<"msaa_wsi validation severity="<<severity<<" message="<<data->pMessage<<'\n';
  return VK_FALSE;
}
struct EglControl {
  egl_lifetime::Api api;
  Display *native=nullptr;egl_lifetime::Handle display=nullptr,config=nullptr,context=nullptr,surface=nullptr;
  GLuint multisample=0,resolve=0,color4=0,color1=0;unsigned side=0;bool initialized=false,closed=false;
  void require(bool ok,const char *stage){
    if(!ok)throw std::runtime_error(std::string("EGL control ")+stage+" error="+std::to_string(api.GetError?api.GetError():0));
  }
  void init(){
    require(api.ready(),"loader");native=XOpenDisplay(nullptr);require(native!=nullptr,"XOpenDisplay");
    std::cout<<"msaa_wsi EGL_get_display_begin native="<<handle(native)<<" fd="<<ConnectionNumber(native)<<'\n';
    display=api.GetDisplay(native);require(display!=nullptr,"GetDisplay");
    int major=0,minor=0;require(api.Initialize(display,&major,&minor),"Initialize");initialized=true;
    require(api.BindAPI(egl_lifetime::OPENGL_API),"BindAPI desktop");
    const int attrs[]={egl_lifetime::SURFACE_TYPE,egl_lifetime::PBUFFER_BIT,egl_lifetime::RENDERABLE_TYPE,egl_lifetime::OPENGL_BIT,
      egl_lifetime::RED_SIZE,8,egl_lifetime::GREEN_SIZE,8,egl_lifetime::BLUE_SIZE,8,0x3021,8,egl_lifetime::NONE};
    int count=0;require(api.ChooseConfig(display,attrs,&config,1,&count)&&count==1,"ChooseConfig RGBA8");
    const char *extensions=api.QueryString(display,egl_lifetime::EXTENSIONS);
    require(extensions&&std::strstr(extensions,"EGL_KHR_create_context"),"KHR_create_context compatibility capability");
    const int contextAttrs[]={0x3098,3,0x30fb,3,0x30fd,2,egl_lifetime::NONE};
    context=api.CreateContext(display,config,nullptr,contextAttrs);require(context!=nullptr,"CreateContext compatibility3.3");
    const int size[]={egl_lifetime::WIDTH,128,egl_lifetime::HEIGHT,128,egl_lifetime::NONE};
    surface=api.CreatePbufferSurface(display,config,size);require(surface!=nullptr,"CreatePbufferSurface");
    require(api.MakeCurrent(display,surface,surface,context),"initial MakeCurrent");
    const char *vendor=reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    const char *renderer=reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const char *version=reinterpret_cast<const char*>(glGetString(GL_VERSION));
    GLint profile=0,maxSamples=0;glGetIntegerv(GL_CONTEXT_PROFILE_MASK,&profile);glGetIntegerv(GL_MAX_SAMPLES,&maxSamples);
    std::cout<<"msaa_wsi EGL_created native="<<handle(native)<<" fd="<<ConnectionNumber(native)<<" display="<<handle(display)
      <<" context="<<handle(context)<<" surface="<<handle(surface)<<" version="<<major<<'.'<<minor
      <<" GL_vendor="<<(vendor?vendor:"")<<" GL_renderer="<<(renderer?renderer:"")<<" GL_version="<<(version?version:"")
      <<" profile="<<profile<<" max_samples="<<maxSamples<<'\n';
    require(vendor&&std::strstr(vendor,"NVIDIA")&&renderer&&std::strstr(renderer,"NVIDIA"),"actual NVIDIA GL identity");
    require((profile&GL_CONTEXT_COMPATIBILITY_PROFILE_BIT)&&maxSamples>=4,"compatibility and MSAA4 capability");
    require(glGetError()==GL_NO_ERROR,"initial GL state");
    require(api.MakeCurrent(display,nullptr,nullptr,nullptr),"initial unbind");
    require(!api.GetCurrentContext()&&!api.GetCurrentDisplay(),"initial caller context absent after unbind");
  }
  void buffers(unsigned n){
    if(side==n)return;
    if(multisample)glDeleteFramebuffers(1,&multisample);if(resolve)glDeleteFramebuffers(1,&resolve);
    if(color4)glDeleteRenderbuffers(1,&color4);if(color1)glDeleteRenderbuffers(1,&color1);
    side=n;glGenFramebuffers(1,&multisample);glBindFramebuffer(GL_FRAMEBUFFER,multisample);
    glGenRenderbuffers(1,&color4);glBindRenderbuffer(GL_RENDERBUFFER,color4);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER,4,GL_RGBA8,n,n);
    GLint actual=0;glGetRenderbufferParameteriv(GL_RENDERBUFFER,GL_RENDERBUFFER_SAMPLES,&actual);require(actual==4,"actual FBO samples4");
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,color4);
    require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"MSAA4 FBO complete");
    glGenFramebuffers(1,&resolve);glBindFramebuffer(GL_FRAMEBUFFER,resolve);
    glGenRenderbuffers(1,&color1);glBindRenderbuffer(GL_RENDERBUFFER,color1);glRenderbufferStorage(GL_RENDERBUFFER,GL_RGBA8,n,n);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,color1);
    require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"resolve FBO complete");
    std::cout<<"msaa_wsi EGL_buffers extent="<<n<<"x"<<n<<" framebuffer4="<<multisample<<" framebuffer1="<<resolve
      <<" color4="<<color4<<" color1="<<color1<<" actual_samples="<<actual<<'\n';
  }
  void between(unsigned afterFrame,unsigned n){
    if(!display)init();const auto start=Clock::now();
    require(!api.GetCurrentContext()&&!api.GetCurrentDisplay(),"before interleave caller context absent");
    require(api.MakeCurrent(display,surface,surface,context),"interleave MakeCurrent");buffers(n);
    std::array<uint8_t,4> expected{{uint8_t((afterFrame*61+17)%256),uint8_t((afterFrame*137+53)%256),uint8_t((afterFrame*211+97)%256),255}};
    glBindFramebuffer(GL_FRAMEBUFFER,multisample);glViewport(0,0,n,n);glDisable(GL_SCISSOR_TEST);glDisable(GL_FRAMEBUFFER_SRGB);glDisable(GL_DITHER);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glClearColor(expected[0]/255.f,expected[1]/255.f,expected[2]/255.f,1);glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,multisample);glReadBuffer(GL_COLOR_ATTACHMENT0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,resolve);glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glBlitFramebuffer(0,0,n,n,0,0,n,n,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,resolve);glReadBuffer(GL_COLOR_ATTACHMENT0);glPixelStorei(GL_PACK_ALIGNMENT,1);
    std::vector<uint8_t> pixels(size_t(n)*n*4,37);glReadPixels(0,0,n,n,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    bool matches=true;for(size_t b=0;b<pixels.size();b+=4)matches &= std::equal(expected.begin(),expected.end(),pixels.data()+b);
    const auto error=glGetError();glBindFramebuffer(GL_FRAMEBUFFER,0);
    require(api.MakeCurrent(display,nullptr,nullptr,nullptr),"interleave unbind");
    const bool unbound=!api.GetCurrentContext()&&!api.GetCurrentDisplay();
    std::cout<<"msaa_wsi EGL_between after_frame="<<afterFrame<<" extent="<<n<<"x"<<n<<" samples=4 readback="<<matches
      <<" GL_error="<<error<<" unbound="<<unbound<<" ms="<<ms(start)<<" rgb="<<unsigned(expected[0])<<','<<unsigned(expected[1])<<','<<unsigned(expected[2])<<'\n';
    require(matches&&error==GL_NO_ERROR&&unbound,"interleave readback/state");
  }
  void close(){
    if(closed)return;closed=true;
    if(context){require(api.MakeCurrent(display,surface,surface,context),"teardown bind");
      if(multisample)glDeleteFramebuffers(1,&multisample);if(resolve)glDeleteFramebuffers(1,&resolve);
      if(color4)glDeleteRenderbuffers(1,&color4);if(color1)glDeleteRenderbuffers(1,&color1);
      require(glGetError()==GL_NO_ERROR,"teardown GL deletion");require(api.MakeCurrent(display,nullptr,nullptr,nullptr),"teardown unbind");
      require(api.DestroyContext(display,context),"DestroyContext");context=nullptr;}
    if(surface){require(api.DestroySurface(display,surface),"DestroySurface");surface=nullptr;}
    if(initialized){require(api.Terminate(display),"Terminate");initialized=false;}
    if(display&&api.ReleaseThread)require(api.ReleaseThread(),"ReleaseThread");
    if(native){XCloseDisplay(native);native=nullptr;}
    std::cout<<"msaa_wsi EGL_destroyed normal=1\n";
  }
  ~EglControl(){try{close();}catch(const std::exception &e){++teardownErrors;std::cerr<<"msaa_wsi EGL_teardown_error "<<e.what()<<'\n';}}
};
struct Probe {
  Display * display=nullptr; Window window=0;
  VkInstance instance=VK_NULL_HANDLE; VkDebugUtilsMessengerEXT messenger=VK_NULL_HANDLE;
  VkSurfaceKHR surface=VK_NULL_HANDLE; VkPhysicalDevice physical=VK_NULL_HANDLE;
  VkDevice device=VK_NULL_HANDLE; VkQueue queue=VK_NULL_HANDLE; uint32_t family=0;
  VkSwapchainKHR swapchain=VK_NULL_HANDLE; VkFormat format=VK_FORMAT_UNDEFINED;
  VkPresentModeKHR presentMode=VK_PRESENT_MODE_FIFO_KHR;
  bool maintenanceRequested=false,maintenanceEnabled=false;
  uint32_t requestedImageCount=3;
  VkExtent2D extent{}; std::vector<VkImage> images; std::vector<bool> seen;
  std::vector<VkSemaphore> presented; VkSemaphore acquired=VK_NULL_HANDLE;
  VkFence submitted=VK_NULL_HANDLE; VkCommandPool pool=VK_NULL_HANDLE; VkCommandBuffer command=VK_NULL_HANDLE;
  VkBuffer readback=VK_NULL_HANDLE; VkDeviceMemory memory=VK_NULL_HANDLE; void * mapped=nullptr;
  uint64_t serial=0; uint32_t acquiredFrames=0;
  uint32_t side=initialSide,generation=0;
  VkImage color4=VK_NULL_HANDLE,color1=VK_NULL_HANDLE;
  VkDeviceMemory color4Memory=VK_NULL_HANDLE,color1Memory=VK_NULL_HANDLE;
  VkImageView color4View=VK_NULL_HANDLE,color1View=VK_NULL_HANDLE;
  VkRenderPass renderPass=VK_NULL_HANDLE;VkFramebuffer framebuffer=VK_NULL_HANDLE;
  bool closed=false;
  void releaseExtent(){
    if(mapped){vkUnmapMemory(device,memory);mapped=nullptr;}
    if(readback){vkDestroyBuffer(device,readback,nullptr);readback=VK_NULL_HANDLE;}
    if(memory){vkFreeMemory(device,memory,nullptr);memory=VK_NULL_HANDLE;}
    if(framebuffer){vkDestroyFramebuffer(device,framebuffer,nullptr);framebuffer=VK_NULL_HANDLE;}
    if(renderPass){vkDestroyRenderPass(device,renderPass,nullptr);renderPass=VK_NULL_HANDLE;}
    if(color4View){vkDestroyImageView(device,color4View,nullptr);color4View=VK_NULL_HANDLE;}
    if(color1View){vkDestroyImageView(device,color1View,nullptr);color1View=VK_NULL_HANDLE;}
    if(color4){vkDestroyImage(device,color4,nullptr);color4=VK_NULL_HANDLE;}
    if(color1){vkDestroyImage(device,color1,nullptr);color1=VK_NULL_HANDLE;}
    if(color4Memory){vkFreeMemory(device,color4Memory,nullptr);color4Memory=VK_NULL_HANDLE;}
    if(color1Memory){vkFreeMemory(device,color1Memory,nullptr);color1Memory=VK_NULL_HANDLE;}
    for(auto semaphore:presented)if(semaphore)vkDestroySemaphore(device,semaphore,nullptr);presented.clear();images.clear();seen.clear();
    if(swapchain){vkDestroySwapchainKHR(device,swapchain,nullptr);swapchain=VK_NULL_HANDLE;}
  }
  void close(){
    if(closed)return;closed=true;
    if(device){const auto start=Clock::now();const auto idle=vkDeviceWaitIdle(device);
      std::cout<<"msaa_wsi teardown_wait result="<<idle<<" ms="<<ms(start)<<'\n';check(idle,"teardown vkDeviceWaitIdle");
      releaseExtent();if(pool)vkDestroyCommandPool(device,pool,nullptr);if(submitted)vkDestroyFence(device,submitted,nullptr);
      if(acquired)vkDestroySemaphore(device,acquired,nullptr);vkDestroyDevice(device,nullptr);device=VK_NULL_HANDLE;}
    if(surface){vkDestroySurfaceKHR(instance,surface,nullptr);surface=VK_NULL_HANDLE;}
    if(messenger){auto destroy=reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance,"vkDestroyDebugUtilsMessengerEXT"));
      if(destroy)destroy(instance,messenger,nullptr);messenger=VK_NULL_HANDLE;}
    if(instance){vkDestroyInstance(instance,nullptr);instance=VK_NULL_HANDLE;}
    if(window){XDestroyWindow(display,window);window=0;}if(display){XCloseDisplay(display);display=nullptr;}
    std::cout<<"msaa_wsi Vulkan_destroyed normal=1 acquired_frames="<<acquiredFrames<<" serial="<<serial<<" validation_errors="<<validationErrors<<'\n';
  }
  ~Probe(){try{close();}catch(const std::exception &e){++teardownErrors;std::cerr<<"msaa_wsi Vulkan_teardown_error "<<e.what()<<'\n';}}
  void init(bool validate,bool managed,VkPresentModeKHR requestedMode,bool enableMaintenance,uint32_t imageCount) {
    presentMode=requestedMode;maintenanceRequested=enableMaintenance;requestedImageCount=imageCount;
    display=XOpenDisplay(nullptr); if (!display) throw std::runtime_error("XOpenDisplay failed");
    window=XCreateSimpleWindow(display,DefaultRootWindow(display),10,10,side,side,0,0,0);
    if (!managed) {XSetWindowAttributes a{};a.override_redirect=True;XChangeWindowAttributes(display,window,CWOverrideRedirect,&a);}
    XStoreName(display,window,"R30 independent Vulkan MSAA4 + EGL");
    XSelectInput(display,window,StructureNotifyMask);
    XMapWindow(display,window);XFlush(display);
    XEvent event{};
    do {XWindowEvent(display,window,StructureNotifyMask,&event);} while(event.type!=MapNotify);
    XSync(display,False);
    XWindowAttributes a{};
    if (!XGetWindowAttributes(display,window,&a)||a.map_state!=IsViewable||a.width!=int(side)||a.height!=int(side))
      throw std::runtime_error("window is not viewable at 96x96");
    std::cout<<"msaa_wsi environment display="<<(std::getenv("DISPLAY")?std::getenv("DISPLAY"):"")
      <<" session="<<(std::getenv("XDG_SESSION_TYPE")?std::getenv("XDG_SESSION_TYPE"):"")
      <<" xvendor="<<ServerVendor(display)<<" display_handle="<<handle(display)<<" window="<<window<<" managed="<<managed<<'\n';
    std::vector<const char*> extensions{VK_KHR_SURFACE_EXTENSION_NAME,VK_KHR_XLIB_SURFACE_EXTENSION_NAME};
    if(validate)extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if(maintenanceRequested){
      extensions.push_back(VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
      extensions.push_back(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
      uint32_t count=0;check(vkEnumerateInstanceExtensionProperties(nullptr,&count,nullptr),"instance extension count");
      std::vector<VkExtensionProperties> available(count);check(vkEnumerateInstanceExtensionProperties(nullptr,&count,available.data()),"instance extensions");
      for(const char *name:extensions){
        if(std::none_of(available.begin(),available.end(),[&](const VkExtensionProperties &e){return !std::strcmp(e.extensionName,name);}))
          throw std::runtime_error(std::string("required instance extension unavailable: ")+name);
        std::cout<<"msaa_wsi maintenance1_instance_extension enabled="<<name<<'\n';
      }
    }
    const char * layer="VK_LAYER_KHRONOS_validation";
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.pApplicationName="R30 Vulkan MSAA4 WSI";app.apiVersion=VK_API_VERSION_1_3;
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ci.pApplicationInfo=&app;ci.enabledExtensionCount=uint32_t(extensions.size());ci.ppEnabledExtensionNames=extensions.data();
    ci.enabledLayerCount=validate?1:0;ci.ppEnabledLayerNames=validate?&layer:nullptr;
    check(vkCreateInstance(&ci,nullptr,&instance),"vkCreateInstance");
    if (validate) {
      auto make=reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance,"vkCreateDebugUtilsMessengerEXT"));
      if (!make) throw std::runtime_error("debug utils unavailable");
      VkDebugUtilsMessengerCreateInfoEXT debug{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
      debug.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
      debug.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
      debug.pfnUserCallback=validation;check(make(instance,&debug,nullptr,&messenger),"debug messenger");
    }
    VkXlibSurfaceCreateInfoKHR native{VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR};native.dpy=display;native.window=window;
    check(vkCreateXlibSurfaceKHR(instance,&native,nullptr,&surface),"vkCreateXlibSurfaceKHR");
    uint32_t count=0;check(vkEnumeratePhysicalDevices(instance,&count,nullptr),"device count");
    std::vector<VkPhysicalDevice> devices(count);check(vkEnumeratePhysicalDevices(instance,&count,devices.data()),"devices");
    for (auto candidate:devices) {
      VkPhysicalDeviceProperties props{};vkGetPhysicalDeviceProperties(candidate,&props);
      if (props.vendorID!=0x10de||props.apiVersion<VK_API_VERSION_1_3||!(props.limits.framebufferColorSampleCounts&VK_SAMPLE_COUNT_4_BIT)) continue; // Never silently compare a different vendor.
      uint32_t n=0;vkGetPhysicalDeviceQueueFamilyProperties(candidate,&n,nullptr);
      std::vector<VkQueueFamilyProperties> families(n);vkGetPhysicalDeviceQueueFamilyProperties(candidate,&n,families.data());
      for (uint32_t i=0;i<n;++i) {
        VkBool32 present=VK_FALSE;check(vkGetPhysicalDeviceSurfaceSupportKHR(candidate,i,surface,&present),"present support");
        if ((families[i].queueFlags&VK_QUEUE_GRAPHICS_BIT)&&present) {physical=candidate;family=i;break;}
      }
      if (physical) {
        std::cout<<"msaa_wsi adapter="<<props.deviceName<<" vendor="<<props.vendorID<<" device="<<props.deviceID<<" driver="<<props.driverVersion<<" api="<<props.apiVersion<<" queue_family="<<family<<'\n';
        break;
      }
    }
    if (!physical) throw std::runtime_error("NVIDIA Vulkan graphics+present device unavailable");
    float priority=1;VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qi.queueFamilyIndex=family;qi.queueCount=1;qi.pQueuePriorities=&priority;
    std::vector<const char*> deviceExtensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT maintenanceFeature{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT};
    if(maintenanceRequested){
      deviceExtensions.push_back(VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME);
      uint32_t count=0;check(vkEnumerateDeviceExtensionProperties(physical,nullptr,&count,nullptr),"device extension count");
      std::vector<VkExtensionProperties> available(count);check(vkEnumerateDeviceExtensionProperties(physical,nullptr,&count,available.data()),"device extensions");
      for(const char *name:deviceExtensions){
        if(std::none_of(available.begin(),available.end(),[&](const VkExtensionProperties &e){return !std::strcmp(e.extensionName,name);}))
          throw std::runtime_error(std::string("required device extension unavailable: ")+name);
        std::cout<<"msaa_wsi maintenance1_device_extension enabled="<<name<<'\n';
      }
      VkPhysicalDeviceFeatures2 query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};query.pNext=&maintenanceFeature;
      vkGetPhysicalDeviceFeatures2(physical,&query);
      std::cout<<"msaa_wsi maintenance1_feature supported="<<maintenanceFeature.swapchainMaintenance1<<'\n';
      if(!maintenanceFeature.swapchainMaintenance1)throw std::runtime_error("swapchainMaintenance1 feature unavailable");
      maintenanceFeature.swapchainMaintenance1=VK_TRUE;
    }
    VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};di.queueCreateInfoCount=1;di.pQueueCreateInfos=&qi;
    di.enabledExtensionCount=uint32_t(deviceExtensions.size());di.ppEnabledExtensionNames=deviceExtensions.data();
    if(maintenanceRequested)di.pNext=&maintenanceFeature;
    check(vkCreateDevice(physical,&di,nullptr,&device),"vkCreateDevice");maintenanceEnabled=maintenanceRequested;vkGetDeviceQueue(device,family,0,&queue);
    if(maintenanceEnabled)std::cout<<"msaa_wsi maintenance1_feature enabled="<<maintenanceFeature.swapchainMaintenance1
      <<" device_pNext_sType="<<maintenanceFeature.sType<<'\n';
    VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};check(vkCreateSemaphore(device,&semaphore,nullptr,&acquired),"acquire semaphore");
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};fence.flags=VK_FENCE_CREATE_SIGNALED_BIT;check(vkCreateFence(device,&fence,nullptr,&submitted),"submit fence");
    VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pi.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;pi.queueFamilyIndex=family;
    check(vkCreateCommandPool(device,&pi,nullptr,&pool),"command pool");
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};alloc.commandPool=pool;alloc.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;alloc.commandBufferCount=1;
    check(vkAllocateCommandBuffers(device,&alloc,&command),"command buffer");
    setup(initialSide,false);
  }
  uint32_t memoryType(uint32_t mask,VkMemoryPropertyFlags required){
    VkPhysicalDeviceMemoryProperties properties{};vkGetPhysicalDeviceMemoryProperties(physical,&properties);
    for(uint32_t i=0;i<properties.memoryTypeCount;++i)if((mask&(1u<<i))&&(properties.memoryTypes[i].propertyFlags&required)==required)return i;
    throw std::runtime_error("required Vulkan memory type unavailable");
  }
  void attachment(VkSampleCountFlagBits samples,VkImageUsageFlags usage,VkImage &image,VkDeviceMemory &mem,VkImageView &view){
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ci.imageType=VK_IMAGE_TYPE_2D;ci.format=format;ci.extent={side,side,1};
    ci.mipLevels=ci.arrayLayers=1;ci.samples=samples;ci.tiling=VK_IMAGE_TILING_OPTIMAL;ci.usage=usage;ci.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateImage(device,&ci,nullptr,&image),"private image");VkMemoryRequirements requirements{};vkGetImageMemoryRequirements(device,image,&requirements);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.allocationSize=requirements.size;
    allocation.memoryTypeIndex=memoryType(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkAllocateMemory(device,&allocation,nullptr,&mem),"private image memory");check(vkBindImageMemory(device,image,mem,0),"bind private image");
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.image=image;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=format;
    vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};check(vkCreateImageView(device,&vi,nullptr,&view),"private image view");
  }
  void setup(uint32_t n,bool resizeWindow){
    if(resizeWindow){const auto start=Clock::now();check(vkDeviceWaitIdle(device),"resize queue completion");
      std::cout<<"msaa_wsi resize_wait ms="<<ms(start)<<" old_extent="<<side<<"x"<<side<<" new_extent="<<n<<"x"<<n<<'\n';
      releaseExtent();XResizeWindow(display,window,n,n);XSync(display,False);}
    side=n;++generation;XWindowAttributes attrs{};
    if(!XGetWindowAttributes(display,window,&attrs)||attrs.map_state!=IsViewable||attrs.width!=int(n)||attrs.height!=int(n))
      throw std::runtime_error("actual X window extent/mapstate mismatch");
    VkSurfaceCapabilitiesKHR caps{};check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical,surface,&caps),"surface capabilities");
    uint32_t modeCount=0;check(vkGetPhysicalDeviceSurfacePresentModesKHR(physical,surface,&modeCount,nullptr),"present mode count");
    std::vector<VkPresentModeKHR> modes(modeCount);
    check(vkGetPhysicalDeviceSurfacePresentModesKHR(physical,surface,&modeCount,modes.data()),"present modes");
    if(std::find(modes.begin(),modes.end(),presentMode)==modes.end())
      throw std::runtime_error("requested present mode unavailable: "+std::to_string(presentMode));
    std::cout<<"msaa_wsi present_mode requested="<<presentMode<<" name="
      <<(presentMode==VK_PRESENT_MODE_FIFO_KHR?"FIFO":"IMMEDIATE")<<" supported=";
    for(auto mode:modes)std::cout<<mode<<",";std::cout<<'\n';
    const VkPresentModeKHR declaredModes[]={VK_PRESENT_MODE_FIFO_KHR,VK_PRESENT_MODE_IMMEDIATE_KHR};
    if(maintenanceEnabled){
      for(auto required:declaredModes)if(std::find(modes.begin(),modes.end(),required)==modes.end())
        throw std::runtime_error("maintenance1 requires both FIFO and IMMEDIATE support");
      auto capabilities2=reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilities2KHR>(vkGetInstanceProcAddr(instance,"vkGetPhysicalDeviceSurfaceCapabilities2KHR"));
      if(!capabilities2)throw std::runtime_error("enabled capabilities2 entry point unavailable");
      VkSurfacePresentModeEXT modeQuery{VK_STRUCTURE_TYPE_SURFACE_PRESENT_MODE_EXT};modeQuery.presentMode=presentMode;
      VkPhysicalDeviceSurfaceInfo2KHR info{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR};info.surface=surface;info.pNext=&modeQuery;
      VkSurfacePresentModeCompatibilityEXT compatibility{VK_STRUCTURE_TYPE_SURFACE_PRESENT_MODE_COMPATIBILITY_EXT};
      VkSurfaceCapabilities2KHR output{VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR};output.pNext=&compatibility;
      check(capabilities2(physical,&info,&output),"maintenance1 compatible mode count");
      std::vector<VkPresentModeKHR> compatible(compatibility.presentModeCount);compatibility.pPresentModes=compatible.data();
      check(capabilities2(physical,&info,&output),"maintenance1 compatible modes");compatible.resize(compatibility.presentModeCount);
      std::cout<<"msaa_wsi maintenance1_compatibility selected="<<presentMode<<" compatible=";
      for(auto mode:compatible)std::cout<<mode<<",";std::cout<<'\n';
      for(auto required:declaredModes)if(std::find(compatible.begin(),compatible.end(),required)==compatible.end())
        throw std::runtime_error("maintenance1 FIFO/IMMEDIATE not compatible with selected mode");
      // The two advertised modes must share valid creation limits. In
      // particular minImageCount satisfies the maximum of their minima.
      caps=output.surfaceCapabilities;output.pNext=nullptr;
      for(auto mode:declaredModes){modeQuery.presentMode=mode;check(capabilities2(physical,&info,&output),"maintenance1 per-mode capabilities");
        const auto &other=output.surfaceCapabilities;
        caps.minImageCount=std::max(caps.minImageCount,other.minImageCount);
        if(other.maxImageCount)caps.maxImageCount=caps.maxImageCount?std::min(caps.maxImageCount,other.maxImageCount):other.maxImageCount;
        caps.supportedUsageFlags &= other.supportedUsageFlags;caps.supportedCompositeAlpha &= other.supportedCompositeAlpha;
        if(caps.currentExtent.width!=other.currentExtent.width||caps.currentExtent.height!=other.currentExtent.height||
          caps.currentTransform!=other.currentTransform)throw std::runtime_error("maintenance1 mode extents/transform differ");
      }
      if(caps.maxImageCount&&caps.minImageCount>caps.maxImageCount)throw std::runtime_error("maintenance1 advertised modes have incompatible image counts");
    }
    uint32_t count=0;check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical,surface,&count,nullptr),"format count");
    std::vector<VkSurfaceFormatKHR> formats(count);check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical,surface,&count,formats.data()),"formats");
    VkSurfaceFormatKHR chosen{};for(auto f:formats)if((f.format==VK_FORMAT_B8G8R8A8_UNORM||f.format==VK_FORMAT_R8G8B8A8_UNORM)&&
      f.colorSpace==VK_COLOR_SPACE_SRGB_NONLINEAR_KHR){chosen=f;break;}
    if(chosen.format==VK_FORMAT_UNDEFINED)throw std::runtime_error("UNORM RGBA8/BGRA8 surface unavailable");
    format=chosen.format;extent=caps.currentExtent;if(extent.width==UINT32_MAX)extent={side,side};
    if(extent.width!=side||extent.height!=side||caps.currentTransform!=VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
      throw std::runtime_error("surface extent/identity-transform contract unavailable");
    const VkImageUsageFlags usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if((caps.supportedUsageFlags&usage)!=usage)throw std::runtime_error("surface transfer src/dst unavailable");
    VkImageFormatProperties imageProperties{};check(vkGetPhysicalDeviceImageFormatProperties(physical,format,VK_IMAGE_TYPE_2D,
      VK_IMAGE_TILING_OPTIMAL,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,0,&imageProperties),"MSAA image format");
    if(!(imageProperties.sampleCounts&VK_SAMPLE_COUNT_4_BIT))throw std::runtime_error("format MSAA4 unsupported");
    const uint32_t requested=requestedImageCount;
    std::cout<<"msaa_wsi image_count_contract requested="<<requested<<" caps_min="<<caps.minImageCount<<" caps_max="<<caps.maxImageCount<<'\n';
    if(requested<caps.minImageCount||(caps.maxImageCount&&requested>caps.maxImageCount))
      throw std::runtime_error("requested swapchain image minimum unsupported; no clamp");
    VkCompositeAlphaFlagBitsKHR alpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if(!(caps.supportedCompositeAlpha&alpha))throw std::runtime_error("opaque composite unsupported");
    VkSwapchainCreateInfoKHR swap{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};swap.surface=surface;swap.minImageCount=requested;
    swap.imageFormat=format;swap.imageColorSpace=chosen.colorSpace;swap.imageExtent=extent;swap.imageArrayLayers=1;swap.imageUsage=usage;
    swap.imageSharingMode=VK_SHARING_MODE_EXCLUSIVE;swap.preTransform=caps.currentTransform;swap.compositeAlpha=alpha;
    swap.presentMode=presentMode;swap.clipped=VK_TRUE;
    VkSwapchainPresentModesCreateInfoEXT announced{VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODES_CREATE_INFO_EXT};
    if(maintenanceEnabled){announced.presentModeCount=2;announced.pPresentModes=declaredModes;swap.pNext=&announced;
      std::cout<<"msaa_wsi maintenance1_create_pNext sType="<<announced.sType<<" modes="<<declaredModes[0]<<","<<declaredModes[1]
        <<" initial_mode="<<swap.presentMode<<" image_count="<<requested<<'\n';}
    check(vkCreateSwapchainKHR(device,&swap,nullptr,&swapchain),"swapchain create");
    check(vkGetSwapchainImagesKHR(device,swapchain,&count,nullptr),"image count");images.resize(count);seen.assign(count,false);
    check(vkGetSwapchainImagesKHR(device,swapchain,&count,images.data()),"swapchain images");
    presented.assign(count,VK_NULL_HANDLE);VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for(auto &semaphore:presented)check(vkCreateSemaphore(device,&si,nullptr,&semaphore),"per-image present semaphore");
    attachment(VK_SAMPLE_COUNT_4_BIT,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,color4,color4Memory,color4View);
    attachment(VK_SAMPLE_COUNT_1_BIT,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,color1,color1Memory,color1View);
    VkAttachmentDescription attachments[2]{};
    for(unsigned i=0;i<2;++i){auto &a=attachments[i];a.format=format;a.samples=i?VK_SAMPLE_COUNT_1_BIT:VK_SAMPLE_COUNT_4_BIT;
      a.loadOp=i?VK_ATTACHMENT_LOAD_OP_DONT_CARE:VK_ATTACHMENT_LOAD_OP_CLEAR;a.storeOp=i?VK_ATTACHMENT_STORE_OP_STORE:VK_ATTACHMENT_STORE_OP_DONT_CARE;
      a.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;a.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
      a.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;a.finalLayout=i?VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;}
    VkAttachmentReference color{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},resolve{1,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;subpass.colorAttachmentCount=1;
    subpass.pColorAttachments=&color;subpass.pResolveAttachments=&resolve;
    VkSubpassDependency dependencies[2]{};dependencies[0].srcSubpass=VK_SUBPASS_EXTERNAL;dependencies[0].dstSubpass=0;
    dependencies[0].srcStageMask=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;dependencies[0].dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;dependencies[0].dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].srcSubpass=0;dependencies[1].dstSubpass=VK_SUBPASS_EXTERNAL;dependencies[1].srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].dstStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT;dependencies[1].srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};rp.attachmentCount=2;rp.pAttachments=attachments;
    rp.subpassCount=1;rp.pSubpasses=&subpass;rp.dependencyCount=2;rp.pDependencies=dependencies;
    check(vkCreateRenderPass(device,&rp,nullptr,&renderPass),"renderpass MSAA resolve");
    const VkImageView views[]={color4View,color1View};VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fb.renderPass=renderPass;fb.attachmentCount=2;fb.pAttachments=views;fb.width=side;fb.height=side;fb.layers=1;
    check(vkCreateFramebuffer(device,&fb,nullptr,&framebuffer),"private framebuffer");
    VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};buffer.size=VkDeviceSize(side)*side*4*2;
    buffer.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;buffer.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateBuffer(device,&buffer,nullptr,&readback),"dual readback buffer");VkMemoryRequirements requirements{};vkGetBufferMemoryRequirements(device,readback,&requirements);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.allocationSize=requirements.size;
    allocation.memoryTypeIndex=memoryType(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(vkAllocateMemory(device,&allocation,nullptr,&memory),"readback memory");check(vkBindBufferMemory(device,readback,memory,0),"bind readback");
    check(vkMapMemory(device,memory,0,VK_WHOLE_SIZE,0,&mapped),"map readback");
    std::cout<<"msaa_wsi created generation="<<generation<<" instance="<<handle(instance)<<" surface="<<handle(surface)
      <<" device="<<handle(device)<<" queue="<<handle(queue)<<" swapchain="<<handle(swapchain)<<" image_count="<<images.size()
      <<" requested_images="<<requested<<" actual_images="<<images.size()<<" format="<<format<<" present_mode="<<presentMode<<" mode="
      <<(presentMode==VK_PRESENT_MODE_FIFO_KHR?"FIFO":"IMMEDIATE")<<" extent="<<side<<"x"<<side
      <<" color4="<<handle(color4)<<" color1="<<handle(color1)<<" renderpass="<<handle(renderPass)<<" framebuffer="<<handle(framebuffer)
      <<" samples=4 timeout_ns="<<timeoutNs<<" acquire_semaphore="<<handle(acquired)<<" fence="<<handle(submitted)<<'\n';
    for(size_t i=0;i<images.size();++i)std::cout<<"msaa_wsi image generation="<<generation<<" index="<<i<<" image="<<handle(images[i])
      <<" present_semaphore="<<handle(presented[i])<<'\n';
  }
  void barrier(VkImage image,VkImageLayout oldLayout,VkImageLayout layout,VkAccessFlags src,VkAccessFlags dst,VkPipelineStageFlags before,VkPipelineStageFlags after) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.oldLayout=oldLayout;b.newLayout=layout;b.srcAccessMask=src;b.dstAccessMask=dst;
    b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.image=image;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    vkCmdPipelineBarrier(command,before,after,0,0,nullptr,0,nullptr,1,&b);
  }
  VkResult wait(const char * stage,uint32_t frame) {
    const auto t=Clock::now();const auto result=vkWaitForFences(device,1,&submitted,VK_TRUE,timeoutNs);
    std::cout<<"msaa_wsi stage="<<stage<<" frame="<<frame<<" serial="<<serial<<" fence="<<handle(submitted)<<" result="<<result<<" ms="<<ms(t)<<" timeout_ns="<<timeoutNs<<'\n';
    return result;
  }
  int frame(uint32_t n,EglControl *egl,bool asyncPresent,uint32_t repeatColor){
    check(wait("fence_before_acquire",n),"pre-acquire fence");
    uint32_t index=UINT32_MAX;const auto start=Clock::now();
    const auto acquiredResult=vkAcquireNextImageKHR(device,swapchain,timeoutNs,acquired,VK_NULL_HANDLE,&index);
    std::cout<<"msaa_wsi acquire frame="<<n<<" serial="<<serial<<" generation="<<generation<<" extent="<<side<<"x"<<side
      <<" swapchain="<<handle(swapchain)<<" semaphore="<<handle(acquired)<<" result="<<acquiredResult<<" ms="<<ms(start)
      <<" timeout_ns="<<timeoutNs<<" image_index="<<index<<'\n';
    if(acquiredResult!=VK_SUCCESS&&acquiredResult!=VK_SUBOPTIMAL_KHR)check(acquiredResult,"single acquire");
    if(index>=images.size())throw std::runtime_error("invalid acquired index");++acquiredFrames;
    check(vkResetCommandBuffer(command,0),"reset command");VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;check(vkBeginCommandBuffer(command,&begin),"begin command");
    // Repeated colors still acquire, resolve, copy, read back, submit and
    // present exactly once per frame. Only the authored RGB epoch changes.
    const uint32_t colorFrame=n/repeatColor;
    const std::array<uint8_t,4> expected{{uint8_t((colorFrame*73+31)%256),uint8_t((colorFrame*151+67)%256),uint8_t((colorFrame*199+113)%256),255}};
    VkClearValue clears[2]{};for(unsigned c=0;c<4;++c)clears[0].color.float32[c]=expected[c]/255.f;
    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};rp.renderPass=renderPass;rp.framebuffer=framebuffer;
    rp.renderArea.extent=extent;rp.clearValueCount=2;rp.pClearValues=clears;
    vkCmdBeginRenderPass(command,&rp,VK_SUBPASS_CONTENTS_INLINE);vkCmdEndRenderPass(command);
    // The layout transition is a write, including the first UNDEFINED
    // transition. Order it after the acquire semaphore's presentation read,
    // not merely the later transfer copy. The submission waits ALL_COMMANDS.
    barrier(images[index],seen[index]?VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      0,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkImageCopy imageCopy{};imageCopy.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};imageCopy.dstSubresource=imageCopy.srcSubresource;
    imageCopy.extent={side,side,1};vkCmdCopyImage(command,color1,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      images[index],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&imageCopy);
    barrier(images[index],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,
      VK_ACCESS_TRANSFER_READ_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={side,side,1};
    vkCmdCopyImageToBuffer(command,color1,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback,1,&copy);
    copy.bufferOffset=VkDeviceSize(side)*side*4;
    vkCmdCopyImageToBuffer(command,images[index],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback,1,&copy);
    VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    host.srcQueueFamilyIndex=host.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;host.buffer=readback;host.size=VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host,0,nullptr);
    barrier(images[index],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,VK_ACCESS_TRANSFER_READ_BIT,0,
      VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    check(vkEndCommandBuffer(command),"end command");check(vkResetFences(device,1,&submitted),"reset fence");
    VkPipelineStageFlags stage=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.waitSemaphoreCount=1;submit.pWaitSemaphores=&acquired;submit.pWaitDstStageMask=&stage;
    submit.commandBufferCount=1;submit.pCommandBuffers=&command;submit.signalSemaphoreCount=1;submit.pSignalSemaphores=&presented[index];
    const auto queued=vkQueueSubmit(queue,1,&submit,submitted);std::cout<<"msaa_wsi submit frame="<<n<<" result="<<queued
      <<" command="<<handle(command)<<" fence="<<handle(submitted)<<" wait_stage="<<stage<<" wait="<<handle(acquired)<<" signal="<<handle(presented[index])<<'\n';
    check(queued,"single queue submit");++serial;seen[index]=true;
    check(wait("fence_readback_before_present",n),"readback completion");const auto fenceStatus=vkGetFenceStatus(device,submitted);
    check(fenceStatus,"independent completed fence status");auto storage=expected;if(format==VK_FORMAT_B8G8R8A8_UNORM)std::swap(storage[0],storage[2]);
    const auto *pixels=static_cast<const uint8_t*>(mapped);const size_t bytes=size_t(side)*side*4;
    for(unsigned image=0;image<2;++image)for(size_t b=0;b<bytes;b+=4)if(!std::equal(storage.begin(),storage.end(),pixels+image*bytes+b))
      throw std::runtime_error(std::string(image?"swapchain":"private resolve")+" current readback mismatch frame="+std::to_string(n));
    std::cout<<"msaa_wsi readback_verified frame="<<n<<" serial="<<serial<<" color_epoch="<<colorFrame<<" repeat_color="<<repeatColor<<" private_resolve=1 swapchain=1 before_present=1 fence_status="<<fenceStatus<<'\n';
    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};present.waitSemaphoreCount=1;present.pWaitSemaphores=&presented[index];
    present.swapchainCount=1;present.pSwapchains=&swapchain;present.pImageIndices=&index;
    VkSwapchainPresentModeInfoEXT selectedMode{VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODE_INFO_EXT};
    if(maintenanceEnabled){selectedMode.swapchainCount=1;selectedMode.pPresentModes=&presentMode;present.pNext=&selectedMode;
      std::cout<<"msaa_wsi maintenance1_present_pNext frame="<<n<<" serial="<<serial<<" sType="<<selectedMode.sType
        <<" swapchain_count="<<selectedMode.swapchainCount<<" selected_mode="<<presentMode<<'\n';}
    // Present info, its pointed-to handles/index and worker state all outlive
    // the RAII join, including an exception from EGL/Xlib on the main thread.
    // No other Vulkan API is called while this worker can access the queue.
    std::atomic<bool> workerStarted{false},workerFinished{false};
    std::atomic<VkResult> workerResult{VK_NOT_READY};
    std::atomic<uint64_t> presentBeginUs{0},presentEndUs{0};
    std::exception_ptr workerException;
    std::thread worker;JoinPresent joinPresent{worker};
    if(asyncPresent){
      worker=std::thread([&]{
        try{
          const auto beginUs=monotonicUs();presentBeginUs.store(beginUs,std::memory_order_relaxed);
          workerStarted.store(true,std::memory_order_release);
          {std::ostringstream line;line<<"msaa_wsi present_begin frame="<<n<<" serial="<<serial<<" monotonic_us="<<beginUs
            <<" generation="<<generation<<" image_index="<<index<<" image="<<handle(images[index])
            <<" swapchain="<<handle(swapchain)<<" wait="<<handle(presented[index])<<" thread="<<std::this_thread::get_id();diagnostic(line.str());}
          const auto shown=vkQueuePresentKHR(queue,&present);const auto endUs=monotonicUs();
          workerResult.store(shown,std::memory_order_relaxed);presentEndUs.store(endUs,std::memory_order_relaxed);
          workerFinished.store(true,std::memory_order_release);
          {std::ostringstream line;line<<"msaa_wsi present_end frame="<<n<<" serial="<<serial<<" monotonic_us="<<endUs
            <<" result="<<shown<<" duration_us="<<endUs-beginUs<<" thread="<<std::this_thread::get_id();diagnostic(line.str());}
        }catch(...){workerException=std::current_exception();workerFinished.store(true,std::memory_order_release);}
      });
    }else{
      const auto presentStart=Clock::now();const auto shown=vkQueuePresentKHR(queue,&present);
      std::cout<<"msaa_wsi present frame="<<n<<" serial="<<serial<<" generation="<<generation<<" index="<<index
        <<" image="<<handle(images[index])<<" swapchain="<<handle(swapchain)<<" wait="<<handle(presented[index])<<" result="<<shown<<" ms="<<ms(presentStart)<<'\n';
      if(shown!=VK_SUCCESS&&shown!=VK_SUBOPTIMAL_KHR)check(shown,"single present");
    }
    // Match the manager fixture: native render/readback/present first, then
    // the independent EGL reference, then observe the same physical frame.
    // This never requests another Vulkan render, acquire, submit or present.
    if(egl)egl->between(n,side);
    bool observed=false;unsigned attempts=0;std::array<uint8_t,3> first{{0,0,0}},last{{0,0,0}};
    unsigned different=0;const auto observation=Clock::now();
    if(asyncPresent){std::ostringstream line;line<<"msaa_wsi observation_begin frame="<<n<<" serial="<<serial
      <<" monotonic_us="<<monotonicUs()<<" worker_started="<<workerStarted.load(std::memory_order_acquire)
      <<" worker_finished="<<workerFinished.load(std::memory_order_acquire)
      <<" worker_result="<<workerResult.load(std::memory_order_relaxed)<<" borrowed_display="<<handle(display)
      <<" observer_thread="<<std::this_thread::get_id();diagnostic(line.str());}
    do{++attempts;XSync(display,False);XImage *image=XGetImage(display,window,0,0,side,side,AllPlanes,ZPixmap);
      if(image){observed=true;different=0;
        auto channel=[](unsigned long value,unsigned long mask){if(!mask)return uint8_t(0);unsigned shift=0;
          while(!(mask&1)){mask>>=1;++shift;}return uint8_t((((value>>shift)&mask)*255+mask/2)/mask);};
        for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x){const auto value=XGetPixel(image,x,y);
          const std::array<uint8_t,3> rgb{{channel(value,image->red_mask),channel(value,image->green_mask),channel(value,image->blue_mask)}};
          if(!x&&!y){last=rgb;if(attempts==1)first=rgb;}
          bool same=true;for(unsigned c=0;c<3;++c)same &= rgb[c]==expected[c];different+=!same;observed &= same;}
        XDestroyImage(image);}
      if(!observed)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }while(!observed&&ms(observation)<5000);
    if(asyncPresent){
      {std::ostringstream line;line<<"msaa_wsi observation_end frame="<<n<<" serial="<<serial<<" monotonic_us="<<monotonicUs()
        <<" physical="<<observed<<" attempts="<<attempts<<" worker_started="<<workerStarted.load(std::memory_order_acquire)
        <<" worker_finished="<<workerFinished.load(std::memory_order_acquire)
        <<" worker_result="<<workerResult.load(std::memory_order_relaxed);diagnostic(line.str());}
      // Join even after a failed five-second observation; a stalled present
      // remains a stalled process for C's watchdog, never a synthetic PASS.
      worker.join();
      {std::ostringstream line;line<<"msaa_wsi present_joined frame="<<n<<" serial="<<serial<<" monotonic_us="<<monotonicUs()
        <<" worker_started="<<workerStarted.load()<<" worker_finished="<<workerFinished.load()
        <<" result="<<workerResult.load()<<" present_begin_us="<<presentBeginUs.load()<<" present_end_us="<<presentEndUs.load();diagnostic(line.str());}
      if(workerException)std::rethrow_exception(workerException);
      const auto shown=workerResult.load();if(shown!=VK_SUCCESS&&shown!=VK_SUBOPTIMAL_KHR)check(shown,"single async present");
    }
    XWindowAttributes attrs{};const bool valid=XGetWindowAttributes(display,window,&attrs);
    std::cout<<"msaa_wsi verified frame="<<n<<" serial="<<serial<<" samples=4 extent="<<side<<"x"<<side
      <<" physical="<<observed<<" attempts="<<attempts<<" changed_pixels="<<different<<" observation_ms="<<ms(observation)
      <<" window="<<window<<" native_valid="<<valid<<" map_state="<<(valid?attrs.map_state:-1)
      <<" native_extent="<<(valid?attrs.width:0)<<"x"<<(valid?attrs.height:0)<<" expected="<<unsigned(expected[0])<<','<<unsigned(expected[1])<<','<<unsigned(expected[2])
      <<" first_observed="<<unsigned(first[0])<<','<<unsigned(first[1])<<','<<unsigned(first[2])
      <<" last_observed="<<unsigned(last[0])<<','<<unsigned(last[1])<<','<<unsigned(last[2])<<'\n';
    return validationErrors?4:(!observed?3:0);
  }
};
}
int main(int argc,char **argv){
  std::cout.setf(std::ios::unitbuf);std::cerr.setf(std::ios::unitbuf);
  bool validationEnabled=false,interleave=true,managed=true,asyncPresent=false,maintenance=false;uint32_t frames=96,repeatColor=1,imageCount=3;
  VkPresentModeKHR presentMode=VK_PRESENT_MODE_FIFO_KHR;
  for(int i=1;i<argc;++i){if(!std::strcmp(argv[i],"--validation"))validationEnabled=true;
    else if(!std::strcmp(argv[i],"--no-egl"))interleave=false;
    else if(!std::strcmp(argv[i],"--async-present"))asyncPresent=true;
    else if(!std::strcmp(argv[i],"--swapchain-maintenance1"))maintenance=true;
    else if(!std::strcmp(argv[i],"--override-redirect"))managed=false;
    else if(!std::strcmp(argv[i],"--present-mode")&&i+1<argc){const char *mode=argv[++i];
      if(!std::strcmp(mode,"fifo"))presentMode=VK_PRESENT_MODE_FIFO_KHR;
      else if(!std::strcmp(mode,"immediate"))presentMode=VK_PRESENT_MODE_IMMEDIATE_KHR;
      else return 64;}
    else if(!std::strcmp(argv[i],"--images")&&i+1<argc){char *end=nullptr;const auto n=std::strtoul(argv[++i],&end,10);
      if(!end||*end||n<1||n>64)return 64;imageCount=uint32_t(n);}
    else if(!std::strcmp(argv[i],"--repeat-color")&&i+1<argc){char *end=nullptr;const auto n=std::strtoul(argv[++i],&end,10);
      if(!end||*end||n<1||n>256)return 64;repeatColor=uint32_t(n);}
    else if(!std::strcmp(argv[i],"--frames")&&i+1<argc){char *end=nullptr;const auto n=std::strtoul(argv[++i],&end,10);
      if(!end||*end||n<2||n>256)return 64;frames=uint32_t(n);}
    else{std::cerr<<"usage: probe [--validation] [--no-egl] [--async-present] [--swapchain-maintenance1] [--override-redirect] [--repeat-color 1..256] [--images 1..64] [--present-mode fifo|immediate] [--frames 2..256]\n";return 64;}}
  if(!XInitThreads()){std::cerr<<"msaa_wsi XInitThreads failed\n";return 1;}
  int status=0;try{Probe probe;EglControl egl;probe.init(validationEnabled,managed,presentMode,maintenance,imageCount);
    for(uint32_t frame=0;frame<frames;++frame){if(frame==frames/2)probe.setup(128,true);
      if((status=probe.frame(frame,interleave?&egl:nullptr,asyncPresent,repeatColor)))break;}
    egl.close();probe.close();
  }catch(const std::exception &error){std::cerr<<"msaa_wsi ERROR "<<error.what()<<'\n';status=1;}
  if(validationErrors||teardownErrors)status=4;
  std::cout<<"msaa_wsi result="<<status<<" requested_frames="<<frames<<" interleave_EGL="<<interleave
    <<" async_present="<<asyncPresent<<" repeat_color="<<repeatColor<<" present_mode="<<presentMode<<" swapchain_maintenance1="<<maintenance<<" requested_images="<<imageCount<<" validation_errors="<<validationErrors<<" teardown_errors="<<teardownErrors<<'\n';return status;
}
