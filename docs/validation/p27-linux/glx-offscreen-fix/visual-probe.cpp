#include <GL/glx.h>
#include <cstdio>
int main() {
 Display *d=XOpenDisplay(nullptr);
 for(int dbl=0;dbl<2;++dbl) {
  int a[]={GLX_RGBA,GLX_DEPTH_SIZE,1,GLX_RED_SIZE,4,GLX_GREEN_SIZE,4,GLX_BLUE_SIZE,4,GLX_ALPHA_SIZE,4,GLX_STENCIL_SIZE,1,0,0};
  if(dbl) a[13]=GLX_DOUBLEBUFFER;
  XVisualInfo *v=glXChooseVisual(d,DefaultScreen(d),a);
  printf("double=%d visual=%p id=%lx\n",dbl,(void*)v,v?v->visualid:0);
  if(v) XFree(v);
 }
 XCloseDisplay(d);
}
