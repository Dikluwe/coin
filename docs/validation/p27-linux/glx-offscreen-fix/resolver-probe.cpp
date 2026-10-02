#include <GL/glx.h>
#include <EGL/egl.h>
#include <cstdio>
int main(){Display*d=XOpenDisplay(nullptr);int a[]={GLX_RENDER_TYPE,GLX_RGBA_BIT,GLX_RED_SIZE,8,GLX_GREEN_SIZE,8,GLX_BLUE_SIZE,8,GLX_ALPHA_SIZE,8,GLX_DEPTH_SIZE,24,GLX_STENCIL_SIZE,1,GLX_DRAWABLE_TYPE,GLX_PBUFFER_BIT,GLX_DOUBLEBUFFER,True,None};
using Choose=GLXFBConfig*(*)(Display*,int,const int*,int*);
Choose funcs[]={glXChooseFBConfig,(Choose)eglGetProcAddress("glXChooseFBConfig"),(Choose)glXGetProcAddress((const GLubyte*)"glXChooseFBConfig")};
const char*names[]={"linked GLX","EGL resolver","GLX resolver"};for(int i=0;i<3;i++){int n=0;auto*f=funcs[i]?funcs[i](d,DefaultScreen(d),a,&n):nullptr;printf("%s count=%d\n",names[i],n);if(f)XFree(f);}XCloseDisplay(d);}
