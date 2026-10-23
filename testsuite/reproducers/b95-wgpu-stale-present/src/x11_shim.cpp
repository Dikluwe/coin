// Xlib ownership/readback only. No Vulkan, EGL, Coin or renderer calls.
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <new>
struct B95Attrs {
  int valid,x,y,width,height,depth,map_state,override_redirect,bits_per_pixel,bytes_per_line;
  unsigned long window,red_mask,green_mask,blue_mask;
};
struct B95Window {Display *display;Window window;int screen;};
static std::atomic<unsigned> errors{0};
static int onError(Display *display,XErrorEvent *event){
  ++errors;char text[256]={};XGetErrorText(display,event->error_code,text,sizeof(text));
  std::fprintf(stderr,"b95_wgpu XERROR code=%u request=%u minor=%u resource=%lu text=%s\n",
    event->error_code,event->request_code,event->minor_code,event->resourceid,text);return 0;
}
extern "C" {
unsigned b95_x11_errors(){return errors.load();}
B95Window *b95_x11_create(unsigned side){
  if(!XInitThreads())return nullptr;
  XSetErrorHandler(onError);
  Display *display=XOpenDisplay(nullptr);if(!display)return nullptr;
  const int screen=DefaultScreen(display);
  Window window=XCreateSimpleWindow(display,RootWindow(display,screen),32,32,side,side,0,0,0);
  if(!window){XCloseDisplay(display);return nullptr;}
  XSetWindowAttributes attrs{};attrs.override_redirect=True;XChangeWindowAttributes(display,window,CWOverrideRedirect,&attrs);
  XStoreName(display,window,"B95 stock wgpu stale presentation");XMapRaised(display,window);XSync(display,False);
  auto *owner=new(std::nothrow) B95Window{display,window,screen};
  if(!owner){XDestroyWindow(display,window);XCloseDisplay(display);}return owner;
}
void *b95_x11_display(B95Window *owner){return owner->display;}
unsigned long b95_x11_window(B95Window *owner){return owner->window;}
int b95_x11_screen(B95Window *owner){return owner->screen;}
int b95_x11_attributes(B95Window *owner,B95Attrs *out){
  *out={};out->window=owner->window;XWindowAttributes attrs{};
  if(!XGetWindowAttributes(owner->display,owner->window,&attrs))return 0;
  out->valid=1;out->x=attrs.x;out->y=attrs.y;out->width=attrs.width;out->height=attrs.height;
  out->depth=attrs.depth;out->map_state=attrs.map_state;out->override_redirect=attrs.override_redirect;return 1;
}
int b95_x11_resize(B95Window *owner,unsigned side){
  XResizeWindow(owner->display,owner->window,side,side);XSync(owner->display,False);
  B95Attrs attrs{};return b95_x11_attributes(owner,&attrs)&&attrs.width==int(side)&&attrs.height==int(side)&&attrs.map_state==IsViewable;
}
int b95_x11_capture(B95Window *owner,unsigned side,unsigned char *rgba,size_t bytes,B95Attrs *attrs){
  XSync(owner->display,False);
  if(bytes!=size_t(side)*side*4||!b95_x11_attributes(owner,attrs)||attrs->width!=int(side)||
    attrs->height!=int(side)||attrs->map_state!=IsViewable)return 0;
  XImage *image=XGetImage(owner->display,owner->window,0,0,side,side,AllPlanes,ZPixmap);if(!image)return 0;
  attrs->red_mask=image->red_mask;attrs->green_mask=image->green_mask;attrs->blue_mask=image->blue_mask;
  attrs->bits_per_pixel=image->bits_per_pixel;attrs->bytes_per_line=image->bytes_per_line;
  auto channel=[](unsigned long value,unsigned long mask){if(!mask)return uint8_t(0);unsigned shift=0;
    while(!(mask&1)){mask>>=1;++shift;}return uint8_t((((value>>shift)&mask)*255+mask/2)/mask);};
  for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x){const auto pixel=XGetPixel(image,x,y);const size_t at=4*(size_t(y)*side+x);
    rgba[at]=channel(pixel,image->red_mask);rgba[at+1]=channel(pixel,image->green_mask);
    rgba[at+2]=channel(pixel,image->blue_mask);rgba[at+3]=255;}
  XDestroyImage(image);return 1;
}
int b95_x11_close(B95Window *owner){
  if(!owner)return 0;
  XDestroyWindow(owner->display,owner->window);XSync(owner->display,False);
  XCloseDisplay(owner->display);delete owner;std::fprintf(stdout,"b95_wgpu X11_destroyed normal=1\n");return 1;
}
}
