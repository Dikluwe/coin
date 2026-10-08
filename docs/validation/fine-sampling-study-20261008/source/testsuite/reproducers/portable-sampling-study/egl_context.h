#pragma once
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <iostream>
struct EglContext {
  EGLDisplay display=EGL_NO_DISPLAY;
  EGLSurface surface=EGL_NO_SURFACE;
  EGLContext context=EGL_NO_CONTEXT;
  bool initialized=false;
  bool init() {
    display=eglGetDisplay(EGL_DEFAULT_DISPLAY);EGLint major=0,minor=0;
    if(!eglInitialize(display,&major,&minor))return false;
    initialized=true;
    std::cout<<"EGL vendor="<<eglQueryString(display,EGL_VENDOR)<<" version="<<major<<'.'<<minor<<'\n';
    if(!eglBindAPI(EGL_OPENGL_API))return false;
    const EGLint configAttributes[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,
      EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_DEPTH_SIZE,24,
      EGL_SAMPLE_BUFFERS,0,EGL_SAMPLES,0,EGL_NONE};
    EGLConfig config;EGLint count=0;
    if(!eglChooseConfig(display,configAttributes,&config,1,&count)||!count)return false;
    const EGLint contextAttributes[]={EGL_CONTEXT_MAJOR_VERSION_KHR,4,EGL_CONTEXT_MINOR_VERSION_KHR,5,
      EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT_KHR,EGL_NONE};
    context=eglCreateContext(display,config,EGL_NO_CONTEXT,contextAttributes);
    const EGLint surfaceAttributes[]={EGL_WIDTH,64,EGL_HEIGHT,64,EGL_NONE};
    surface=eglCreatePbufferSurface(display,config,surfaceAttributes);
    return context!=EGL_NO_CONTEXT && surface!=EGL_NO_SURFACE && eglMakeCurrent(display,surface,surface,context);
  }
  ~EglContext() {
    if(!initialized)return;
    eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    if(surface!=EGL_NO_SURFACE)eglDestroySurface(display,surface);
    if(context!=EGL_NO_CONTEXT)eglDestroyContext(display,context);
    eglTerminate(display);
  }
};
