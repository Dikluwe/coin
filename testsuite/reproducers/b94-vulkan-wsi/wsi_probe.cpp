// Independent Xlib/Vulkan control: no Coin, BGFX, wgpu, or OpenGL dependency.
#define VK_USE_PLATFORM_XLIB_KHR
#include <vulkan/vulkan.h>
#include <X11/Xutil.h>
#ifdef B94_WSI_HAVE_GLX
#include <GL/gl.h>
#include <GL/glx.h>
#endif
#include <algorithm>
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
constexpr uint32_t side=96;
constexpr uint64_t timeoutNs=1000000000;
unsigned validationErrors=0;
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
  std::cerr<<"wsi_probe validation severity="<<severity<<" message="<<data->pMessage<<'\n';
  return VK_FALSE;
}
struct Probe {
  Display * display=nullptr; Window window=0;
  VkInstance instance=VK_NULL_HANDLE; VkDebugUtilsMessengerEXT messenger=VK_NULL_HANDLE;
  VkSurfaceKHR surface=VK_NULL_HANDLE; VkPhysicalDevice physical=VK_NULL_HANDLE;
  VkDevice device=VK_NULL_HANDLE; VkQueue queue=VK_NULL_HANDLE; uint32_t family=0;
  VkSwapchainKHR swapchain=VK_NULL_HANDLE; VkFormat format=VK_FORMAT_UNDEFINED;
  VkExtent2D extent{}; std::vector<VkImage> images; std::vector<bool> seen;
  std::vector<VkSemaphore> presented; VkSemaphore acquired=VK_NULL_HANDLE;
  VkFence submitted=VK_NULL_HANDLE; VkCommandPool pool=VK_NULL_HANDLE; VkCommandBuffer command=VK_NULL_HANDLE;
  VkBuffer readback=VK_NULL_HANDLE; VkDeviceMemory memory=VK_NULL_HANDLE; void * mapped=nullptr;
  uint64_t serial=0; uint32_t acquiredFrames=0;
#ifdef B94_WSI_HAVE_GLX
  GLXContext glContext=nullptr; GLXPixmap glDrawable=0; Pixmap glPixmap=0;
#endif
  ~Probe() {
    if (device) {
      const auto t=Clock::now(); const auto idle=vkDeviceWaitIdle(device);
      std::cout<<"wsi_probe stage=teardown_wait result="<<idle<<" ms="<<ms(t)<<'\n';
      if (mapped) vkUnmapMemory(device,memory);
      if (readback) vkDestroyBuffer(device,readback,nullptr);
      if (memory) vkFreeMemory(device,memory,nullptr);
      if (pool) vkDestroyCommandPool(device,pool,nullptr);
      if (submitted) vkDestroyFence(device,submitted,nullptr);
      if (acquired) vkDestroySemaphore(device,acquired,nullptr);
      for (auto s:presented) if (s) vkDestroySemaphore(device,s,nullptr);
      if (swapchain) vkDestroySwapchainKHR(device,swapchain,nullptr);
      vkDestroyDevice(device,nullptr);
    }
    if (surface) vkDestroySurfaceKHR(instance,surface,nullptr);
    if (messenger) {
      auto destroy=reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance,"vkDestroyDebugUtilsMessengerEXT"));
      if (destroy) destroy(instance,messenger,nullptr);
    }
    if (instance) vkDestroyInstance(instance,nullptr);
#ifdef B94_WSI_HAVE_GLX
    if (glContext) {
      glXMakeContextCurrent(display,None,None,nullptr);
      glXDestroyContext(display,glContext);
    }
    if (glDrawable) glXDestroyPixmap(display,glDrawable);
    if (glPixmap) XFreePixmap(display,glPixmap);
#endif
    if (window) XDestroyWindow(display,window);
    if (display) XCloseDisplay(display);
    std::cout<<"wsi_probe stage=destroyed acquired_frames="<<acquiredFrames<<" serial="<<serial<<" validation_errors="<<validationErrors<<'\n';
  }
  void initGlx() {
#ifdef B94_WSI_HAVE_GLX
    const int attributes[]={GLX_X_RENDERABLE,True,GLX_DRAWABLE_TYPE,GLX_PIXMAP_BIT,
      GLX_RENDER_TYPE,GLX_RGBA_BIT,GLX_X_VISUAL_TYPE,GLX_TRUE_COLOR,
      GLX_RED_SIZE,8,GLX_GREEN_SIZE,8,GLX_BLUE_SIZE,8,GLX_ALPHA_SIZE,8,
      GLX_DOUBLEBUFFER,False,None};
    int count=0;
    GLXFBConfig * configs=glXChooseFBConfig(display,DefaultScreen(display),attributes,&count);
    if (!configs||count<1) {
      if (configs) XFree(configs);
      throw std::runtime_error("independent GLX pixmap RGBA8 configuration unavailable");
    }
    const GLXFBConfig config=configs[0]; XFree(configs);
    XVisualInfo * visual=glXGetVisualFromFBConfig(display,config);
    if (!visual) throw std::runtime_error("GLX configuration has no X visual");
    glPixmap=XCreatePixmap(display,RootWindow(display,visual->screen),side,side,unsigned(visual->depth));
    XFree(visual);
    if (!glPixmap) throw std::runtime_error("XCreatePixmap for GLX failed");
    glDrawable=glXCreatePixmap(display,config,glPixmap,nullptr);
    if (!glDrawable) throw std::runtime_error("glXCreatePixmap failed");
    glContext=glXCreateNewContext(display,config,GLX_RGBA_TYPE,nullptr,True);
    if (!glContext||!glXMakeContextCurrent(display,glDrawable,glDrawable,glContext))
      throw std::runtime_error("GLX independent context creation/bind failed");
    const auto * vendor=glGetString(GL_VENDOR); const auto * renderer=glGetString(GL_RENDERER);
    std::cout<<"wsi_probe stage=glx_created context="<<handle(glContext)<<" pixmap="<<glPixmap<<" drawable="<<glDrawable
      <<" extent=96x96 direct="<<glXIsDirect(display,glContext)<<" vendor="<<(vendor?reinterpret_cast<const char *>(vendor):"")
      <<" renderer="<<(renderer?reinterpret_cast<const char *>(renderer):"")<<'\n';
    if (!glXMakeContextCurrent(display,None,None,nullptr)) throw std::runtime_error("GLX initial release failed");
#else
    throw std::runtime_error("--glx-between requires build option B94_WSI_ENABLE_GLX=ON");
#endif
  }
  void glxBetween(uint32_t afterFrame) {
#ifdef B94_WSI_HAVE_GLX
    const auto start=Clock::now();
    if (!glXMakeContextCurrent(display,glDrawable,glDrawable,glContext)) throw std::runtime_error("GLX interleave bind failed");
    glViewport(0,0,side,side); glDrawBuffer(GL_FRONT); glReadBuffer(GL_FRONT); glPixelStorei(GL_PACK_ALIGNMENT,1);
    std::array<uint8_t,4> expected{{0,0,0,255}}; expected[(afterFrame+1)%3]=255;
    glClearColor(expected[0]/255.f,expected[1]/255.f,expected[2]/255.f,1);glClear(GL_COLOR_BUFFER_BIT);
    std::vector<uint8_t> pixels(size_t(side)*side*4,37);
    glReadPixels(0,0,side,side,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    const GLenum error=glGetError();
    bool matches=true;
    for(size_t b=0;b<pixels.size();b+=4)if(!std::equal(expected.begin(),expected.end(),pixels.data()+b)){matches=false;break;}
    if (!glXMakeContextCurrent(display,None,None,nullptr)) throw std::runtime_error("GLX interleave release failed");
    std::cout<<"wsi_probe stage=glx_between after_frame="<<afterFrame<<" error="<<error<<" readback="<<matches<<" ms="<<ms(start)
      <<" rgb="<<unsigned(expected[0])<<','<<unsigned(expected[1])<<','<<unsigned(expected[2])<<'\n';
    if(error!=GL_NO_ERROR||!matches)throw std::runtime_error("independent GLX clear/readback failed");
#else
    (void)afterFrame;
    throw std::runtime_error("GLX interleave unavailable in this build");
#endif
  }
  void init(bool validate,bool managed) {
    display=XOpenDisplay(nullptr); if (!display) throw std::runtime_error("XOpenDisplay failed");
    window=XCreateSimpleWindow(display,DefaultRootWindow(display),10,10,side,side,0,0,0);
    if (!managed) {XSetWindowAttributes a{};a.override_redirect=True;XChangeWindowAttributes(display,window,CWOverrideRedirect,&a);}
    XStoreName(display,window,"B94 independent Vulkan WSI");
    XSelectInput(display,window,StructureNotifyMask);
    XMapWindow(display,window);XFlush(display);
    XEvent event{};
    do {XWindowEvent(display,window,StructureNotifyMask,&event);} while(event.type!=MapNotify);
    XSync(display,False);
    XWindowAttributes a{};
    if (!XGetWindowAttributes(display,window,&a)||a.map_state!=IsViewable||a.width!=int(side)||a.height!=int(side))
      throw std::runtime_error("window is not viewable at 96x96");
    std::cout<<"wsi_probe environment display="<<(std::getenv("DISPLAY")?std::getenv("DISPLAY"):"")
      <<" session="<<(std::getenv("XDG_SESSION_TYPE")?std::getenv("XDG_SESSION_TYPE"):"")
      <<" xvendor="<<ServerVendor(display)<<" display_handle="<<handle(display)<<" window="<<window<<" managed="<<managed<<'\n';
    const char * extensions[]={VK_KHR_SURFACE_EXTENSION_NAME,VK_KHR_XLIB_SURFACE_EXTENSION_NAME,VK_EXT_DEBUG_UTILS_EXTENSION_NAME};
    const char * layer="VK_LAYER_KHRONOS_validation";
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.pApplicationName="B94 Vulkan WSI";app.apiVersion=VK_API_VERSION_1_0;
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ci.pApplicationInfo=&app;ci.enabledExtensionCount=validate?3:2;ci.ppEnabledExtensionNames=extensions;
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
      if (props.vendorID!=0x1002) continue; // Never silently compare a different vendor.
      uint32_t n=0;vkGetPhysicalDeviceQueueFamilyProperties(candidate,&n,nullptr);
      std::vector<VkQueueFamilyProperties> families(n);vkGetPhysicalDeviceQueueFamilyProperties(candidate,&n,families.data());
      for (uint32_t i=0;i<n;++i) {
        VkBool32 present=VK_FALSE;check(vkGetPhysicalDeviceSurfaceSupportKHR(candidate,i,surface,&present),"present support");
        if ((families[i].queueFlags&VK_QUEUE_GRAPHICS_BIT)&&present) {physical=candidate;family=i;break;}
      }
      if (physical) {
        std::cout<<"wsi_probe adapter="<<props.deviceName<<" vendor="<<props.vendorID<<" device="<<props.deviceID<<" driver="<<props.driverVersion<<" api="<<props.apiVersion<<" queue_family="<<family<<'\n';
        break;
      }
    }
    if (!physical) throw std::runtime_error("AMD Vulkan graphics+present device unavailable");
    float priority=1;VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qi.queueFamilyIndex=family;qi.queueCount=1;qi.pQueuePriorities=&priority;
    const char * extension=VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};di.queueCreateInfoCount=1;di.pQueueCreateInfos=&qi;di.enabledExtensionCount=1;di.ppEnabledExtensionNames=&extension;
    check(vkCreateDevice(physical,&di,nullptr,&device),"vkCreateDevice");vkGetDeviceQueue(device,family,0,&queue);
    VkSurfaceCapabilitiesKHR caps{};check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical,surface,&caps),"surface capabilities");
    check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical,surface,&count,nullptr),"format count");
    std::vector<VkSurfaceFormatKHR> formats(count);check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical,surface,&count,formats.data()),"formats");
    VkSurfaceFormatKHR chosen{};
    for (auto f:formats) if ((f.format==VK_FORMAT_B8G8R8A8_UNORM||f.format==VK_FORMAT_R8G8B8A8_UNORM)&&f.colorSpace==VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {chosen=f;break;}
    if (chosen.format==VK_FORMAT_UNDEFINED) throw std::runtime_error("RGBA8/BGRA8 UNORM surface unavailable");
    format=chosen.format;extent=caps.currentExtent;if (extent.width==UINT32_MAX) extent={side,side};
    if (extent.width!=side||extent.height!=side) throw std::runtime_error("surface extent differs from 96x96");
    const VkImageUsageFlags usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if ((caps.supportedUsageFlags&usage)!=usage) throw std::runtime_error("surface transfer clear/readback unsupported");
    uint32_t requested=std::max(3u,caps.minImageCount);if(caps.maxImageCount)requested=std::min(requested,caps.maxImageCount);
    VkCompositeAlphaFlagBitsKHR alpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(caps.supportedCompositeAlpha&alpha)) for (auto x:{VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR}) if(caps.supportedCompositeAlpha&x){alpha=x;break;}
    VkSwapchainCreateInfoKHR swap{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};swap.surface=surface;swap.minImageCount=requested;
    swap.imageFormat=format;swap.imageColorSpace=chosen.colorSpace;swap.imageExtent=extent;swap.imageArrayLayers=1;swap.imageUsage=usage;
    swap.imageSharingMode=VK_SHARING_MODE_EXCLUSIVE;swap.preTransform=caps.currentTransform;swap.compositeAlpha=alpha;swap.presentMode=VK_PRESENT_MODE_FIFO_KHR;swap.clipped=VK_TRUE;
    check(vkCreateSwapchainKHR(device,&swap,nullptr,&swapchain),"vkCreateSwapchainKHR");
    check(vkGetSwapchainImagesKHR(device,swapchain,&count,nullptr),"image count");images.resize(count);seen.resize(count,false);
    check(vkGetSwapchainImagesKHR(device,swapchain,&count,images.data()),"images");
    VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};check(vkCreateSemaphore(device,&semaphore,nullptr,&acquired),"acquire semaphore");
    presented.resize(count,VK_NULL_HANDLE);for(auto &s:presented)check(vkCreateSemaphore(device,&semaphore,nullptr,&s),"present semaphore");
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};fence.flags=VK_FENCE_CREATE_SIGNALED_BIT;check(vkCreateFence(device,&fence,nullptr,&submitted),"submit fence");
    VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pi.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;pi.queueFamilyIndex=family;
    check(vkCreateCommandPool(device,&pi,nullptr,&pool),"command pool");
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};alloc.commandPool=pool;alloc.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;alloc.commandBufferCount=1;
    check(vkAllocateCommandBuffers(device,&alloc,&command),"command buffer");
    VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};buffer.size=side*side*4;buffer.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;buffer.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateBuffer(device,&buffer,nullptr,&readback),"readback buffer");
    VkMemoryRequirements requirements{};vkGetBufferMemoryRequirements(device,readback,&requirements);
    VkPhysicalDeviceMemoryProperties memories{};vkGetPhysicalDeviceMemoryProperties(physical,&memories);
    uint32_t type=UINT32_MAX;const auto flags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for(uint32_t i=0;i<memories.memoryTypeCount;++i)if((requirements.memoryTypeBits&(1u<<i))&&(memories.memoryTypes[i].propertyFlags&flags)==flags){type=i;break;}
    if(type==UINT32_MAX)throw std::runtime_error("coherent readback memory unavailable");
    VkMemoryAllocateInfo mem{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};mem.allocationSize=requirements.size;mem.memoryTypeIndex=type;
    check(vkAllocateMemory(device,&mem,nullptr,&memory),"readback memory");check(vkBindBufferMemory(device,readback,memory,0),"bind readback");
    check(vkMapMemory(device,memory,0,VK_WHOLE_SIZE,0,&mapped),"map readback");
    std::cout<<"wsi_probe stage=created generation=1 instance="<<handle(instance)<<" surface="<<handle(surface)<<" device="<<handle(device)<<" queue="<<handle(queue)<<" swapchain="<<handle(swapchain)
      <<" image_count="<<images.size()<<" requested_images="<<requested<<" format="<<format<<" mode=FIFO extent=96x96 timeout_ns="<<timeoutNs<<" acquire_semaphore="<<handle(acquired)<<" submit_fence="<<handle(submitted)<<'\n';
    for(size_t i=0;i<images.size();++i)std::cout<<"wsi_probe stage=image index="<<i<<" handle="<<handle(images[i])<<" present_semaphore="<<handle(presented[i])<<'\n';
  }
  void barrier(VkImage image,VkImageLayout oldLayout,VkImageLayout layout,VkAccessFlags src,VkAccessFlags dst,VkPipelineStageFlags before,VkPipelineStageFlags after) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.oldLayout=oldLayout;b.newLayout=layout;b.srcAccessMask=src;b.dstAccessMask=dst;
    b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.image=image;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    vkCmdPipelineBarrier(command,before,after,0,0,nullptr,0,nullptr,1,&b);
  }
  VkResult wait(const char * stage,uint32_t frame) {
    const auto t=Clock::now();const auto result=vkWaitForFences(device,1,&submitted,VK_TRUE,timeoutNs);
    std::cout<<"wsi_probe stage="<<stage<<" frame="<<frame<<" serial="<<serial<<" fence="<<handle(submitted)<<" result="<<result<<" ms="<<ms(t)<<" timeout_ns="<<timeoutNs<<'\n';
    return result;
  }
  int frame(uint32_t n,bool observePhysical,bool readbackBeforePresent) {
    // Complete the previous semaphore consumer before reusing the acquire
    // semaphore. Each present semaphore belongs to an image, and is reused
    // only after that image is acquired again.
    const auto ready=wait("fence_before_acquire",n);
    if(ready==VK_TIMEOUT){std::cout<<"wsi_probe NOT_READY origin=fence WSI_called=0 frame="<<n<<'\n';return 2;}
    check(ready,"pre-acquire fence");
    uint32_t index=UINT32_MAX;const auto t=Clock::now();
    const auto result=vkAcquireNextImageKHR(device,swapchain,timeoutNs,acquired,VK_NULL_HANDLE,&index);
    std::cout<<"wsi_probe stage=vkAcquireNextImageKHR frame="<<n<<" generation=1 serial="<<serial<<" swapchain="<<handle(swapchain)<<" semaphore="<<handle(acquired)
      <<" result="<<result<<" ms="<<ms(t)<<" timeout_ns="<<timeoutNs<<" image_index="<<index<<" WSI_called=1\n";
    if(result==VK_TIMEOUT||result==VK_NOT_READY){std::cout<<"wsi_probe NOT_READY origin=WSI frame="<<n<<'\n';return 2;}
    if(result!=VK_SUCCESS&&result!=VK_SUBOPTIMAL_KHR)check(result,"acquire");
    if(index>=images.size())throw std::runtime_error("acquired image outside swapchain");
    ++acquiredFrames;
    check(vkResetCommandBuffer(command,0),"reset command");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(command,&begin),"begin command");
    barrier(images[index],seen[index]?VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,0,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
    std::array<uint8_t,4> expected{{0,0,0,255}};expected[n%3]=255;
    VkClearColorValue color{};color.float32[n%3]=1;color.float32[3]=1;
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    vkCmdClearColorImage(command,images[index],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&color,1,&range);
    barrier(images[index],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={side,side,1};
    vkCmdCopyImageToBuffer(command,images[index],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback,1,&copy);
    VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    host.srcQueueFamilyIndex=host.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;host.buffer=readback;host.size=VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host,0,nullptr);
    barrier(images[index],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,VK_ACCESS_TRANSFER_READ_BIT,0,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    check(vkEndCommandBuffer(command),"end command");check(vkResetFences(device,1,&submitted),"reset fence");
    VkPipelineStageFlags stage=VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.waitSemaphoreCount=1;submit.pWaitSemaphores=&acquired;submit.pWaitDstStageMask=&stage;
    submit.commandBufferCount=1;submit.pCommandBuffers=&command;submit.signalSemaphoreCount=1;submit.pSignalSemaphores=&presented[index];
    check(vkQueueSubmit(queue,1,&submit,submitted),"vkQueueSubmit");++serial;seen[index]=true;
    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};present.waitSemaphoreCount=1;present.pWaitSemaphores=&presented[index];present.swapchainCount=1;present.pSwapchains=&swapchain;present.pImageIndices=&index;
    const auto presentImage=[&]() {
      const auto p=Clock::now();const auto shown=vkQueuePresentKHR(queue,&present);
      std::cout<<"wsi_probe stage=vkQueuePresentKHR frame="<<n<<" serial="<<serial<<" image_index="<<index<<" semaphore="<<handle(presented[index])<<" result="<<shown<<" ms="<<ms(p)<<'\n';
      if(shown!=VK_SUCCESS&&shown!=VK_SUBOPTIMAL_KHR)check(shown,"present");
    };
    if(!readbackBeforePresent)presentImage();
    check(wait("fence_readback",n),"readback fence");
    const auto * pixels=static_cast<const uint8_t *>(mapped);auto storage=expected;
    if(format==VK_FORMAT_B8G8R8A8_UNORM)std::swap(storage[0],storage[2]);
    for(size_t b=0;b<size_t(side)*side*4;b+=4)if(!std::equal(storage.begin(),storage.end(),pixels+b))throw std::runtime_error("current GPU readback mismatch");
    std::cout<<"wsi_probe stage=readback_verified frame="<<n<<" serial="<<serial<<" before_present="<<readbackBeforePresent<<'\n';
    if(readbackBeforePresent)presentImage();
    // Observation may lag present. Poll pixels only, without another GPU
    // submission or acquisition, and never turn unobserved presentation into
    // success. This observation window does not change the WSI timeout.
    bool observed=false;const auto observation=Clock::now();
    if(observePhysical) do {
      XSync(display,False);XImage * image=XGetImage(display,window,0,0,side,side,AllPlanes,ZPixmap);
      if(image){observed=true;for(unsigned y=0;y<side&&observed;++y)for(unsigned x=0;x<side;++x){const auto v=XGetPixel(image,x,y);
        if((v&image->red_mask)!=(expected[0]?image->red_mask:0)||(v&image->green_mask)!=(expected[1]?image->green_mask:0)||(v&image->blue_mask)!=(expected[2]?image->blue_mask:0)){observed=false;break;}}
        XDestroyImage(image);}
      if(!observed)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }while(!observed&&ms(observation)<2000);
    XWindowAttributes attrs{};const bool valid=XGetWindowAttributes(display,window,&attrs);
    std::cout<<"wsi_probe stage=verified frame="<<n<<" serial="<<serial<<" readback=1 physical="<<(observePhysical?(observed?"observed":"unobserved"):"skipped")<<" observation_ms="<<(observePhysical?ms(observation):0)
      <<" native_valid="<<valid<<" map_state="<<(valid?attrs.map_state:-1)<<" native_extent="<<(valid?attrs.width:0)<<','<<(valid?attrs.height:0)
      <<" rgb="<<unsigned(expected[0])<<','<<unsigned(expected[1])<<','<<unsigned(expected[2])<<'\n';
    return observePhysical&&!observed?3:(validationErrors?4:0);
  }
};
}
int main(int argc,char ** argv) {
  std::cout.setf(std::ios::unitbuf);std::cerr.setf(std::ios::unitbuf);
  uint32_t frames=12;bool validate=false,managed=true,observePhysical=false,readbackBeforePresent=false,glx=false;
  for(int i=1;i<argc;++i){
    if(!std::strcmp(argv[i],"--validation"))validate=true;
    else if(!std::strcmp(argv[i],"--override-redirect"))managed=false;
    else if(!std::strcmp(argv[i],"--observe-physical"))observePhysical=true;
    else if(!std::strcmp(argv[i],"--readback-before-present"))readbackBeforePresent=true;
    else if(!std::strcmp(argv[i],"--glx-between"))glx=true;
    else if(!std::strcmp(argv[i],"--frames")&&i+1<argc){char *end=nullptr;const auto n=std::strtoul(argv[++i],&end,10);if(!end||*end||n==0||n>100000)return 64;frames=uint32_t(n);}
    else{std::cerr<<"usage: B94VulkanWsiProbe [--validation] [--override-redirect] [--observe-physical] [--readback-before-present] [--glx-between] [--frames N]\n";return 64;}
  }
  int status=0;
  try{
    Probe probe;probe.init(validate,managed);if(glx)probe.initGlx();
    for(uint32_t i=0;i<frames;++i){
      if((status=probe.frame(i,observePhysical,readbackBeforePresent)))break;
      if(glx&&i+1<frames)probe.glxBetween(i);
    }
  }
  catch(const std::exception&e){std::cerr<<"wsi_probe ERROR "<<e.what()<<'\n';return 1;}
  if(validationErrors)status=4;
  std::cout<<"wsi_probe result="<<status<<" requested_frames="<<frames<<'\n';return status;
}
