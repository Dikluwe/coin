// Query driver offerings directly, without Coin's visual-selection logic.
#include <GL/glx.h>
#include <X11/Xlib.h>
#include <iostream>
int main() {
  Display *display=XOpenDisplay(nullptr);
  if(!display)return 77;
  int single[]={GLX_RGBA,GLX_DEPTH_SIZE,1,None};
  int dual[]={GLX_RGBA,GLX_DEPTH_SIZE,1,GLX_DOUBLEBUFFER,None};
  XVisualInfo *a=glXChooseVisual(display,DefaultScreen(display),single);
  XVisualInfo *b=glXChooseVisual(display,DefaultScreen(display),dual);
  std::cout<<"client="<<glXGetClientString(display,GLX_VENDOR)
           <<" single_rgba_depth_visual="<<(a!=nullptr)<<" double_rgba_depth_visual="<<(b!=nullptr)<<'\n';
  if(a)XFree(a);if(b)XFree(b);XCloseDisplay(display);return 0;
}
