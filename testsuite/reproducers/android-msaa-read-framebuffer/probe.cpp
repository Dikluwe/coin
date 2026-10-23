#include <android/native_activity.h>
#include <android/log.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl31.h>
#include <initializer_list>
#define LOG(...) __android_log_print(ANDROID_LOG_INFO,"CoinMsaaProbe",__VA_ARGS__)
static bool proc_storage = false;
static GLuint texture(int samples, GLenum format) {
  GLuint t; glGenTextures(1,&t); GLenum target=samples>1?GL_TEXTURE_2D_MULTISAMPLE:GL_TEXTURE_2D;
  glBindTexture(target,t);
  if(samples>1) {
    if (proc_storage) {
      auto storage = reinterpret_cast<PFNGLTEXSTORAGE2DMULTISAMPLEPROC>(
          eglGetProcAddress("glTexStorage2DMultisample"));
      if (!storage) { LOG("glTexStorage2DMultisample proc unavailable"); return t; }
      storage(target,samples,format,8,8,GL_TRUE);
    } else glTexStorage2DMultisample(target,samples,format,8,8,GL_TRUE);
  }
  else glTexStorage2D(target,1,format,8,8);
  return t;
}
extern "C" void ANativeActivity_onCreate(ANativeActivity* activity,void*,size_t) {
  EGLDisplay d=eglGetDisplay(EGL_DEFAULT_DISPLAY); EGLint major,minor; eglInitialize(d,&major,&minor);
  EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT_KHR,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
  EGLConfig config; EGLint n; eglChooseConfig(d,attrs,&config,1,&n);
  EGLint pb[]={EGL_WIDTH,8,EGL_HEIGHT,8,EGL_NONE}; EGLSurface s=eglCreatePbufferSurface(d,config,pb);
  EGLint ca[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE}; EGLContext c=eglCreateContext(d,config,EGL_NO_CONTEXT,ca);
  if(!eglMakeCurrent(d,s,s,c)){LOG("EGL setup failed=0x%x",eglGetError());ANativeActivity_finish(activity);return;}
  LOG("renderer=%s version=%s",glGetString(GL_RENDERER),glGetString(GL_VERSION));
  for (bool use_proc: {false, true}) {
  proc_storage = use_proc;
  LOG("storage_entrypoint=%s", use_proc ? "eglGetProcAddress" : "linked");
  for(int samples: {1,4}) {
    GLuint f[2];glGenFramebuffers(2,f);
    GLuint color=texture(samples,GL_RGBA8),depth=texture(samples,GL_DEPTH_COMPONENT32F),out=texture(1,GL_RGBA8);
    GLenum target=samples>1?GL_TEXTURE_2D_MULTISAMPLE:GL_TEXTURE_2D;
    glBindFramebuffer(GL_FRAMEBUFFER,f[0]);
    const GLenum attach_target = use_proc ? GL_DRAW_FRAMEBUFFER : GL_FRAMEBUFFER;
    LOG("attachment_target=0x%x", attach_target);
    glFramebufferTexture2D(attach_target,GL_COLOR_ATTACHMENT0,target,color,0);
    glFramebufferTexture2D(attach_target,GL_DEPTH_ATTACHMENT,target,depth,0);
    LOG("samples=%d render_status=0x%x error=0x%x",samples,glCheckFramebufferStatus(GL_FRAMEBUFFER),glGetError());
    glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    LOG("samples=%d clear_error=0x%x",samples,glGetError());
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,f[1]);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,out,0);
    LOG("samples=%d resolve_read=0x%x resolve_draw=0x%x",samples,glCheckFramebufferStatus(GL_READ_FRAMEBUFFER),glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER));
    glBlitFramebuffer(0,0,8,8,0,0,8,8,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    LOG("samples=%d blit_error=0x%x",samples,glGetError());
    glBindFramebuffer(GL_READ_FRAMEBUFFER,f[1]); unsigned char pixel[4]={};
    glReadPixels(4,4,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    LOG("samples=%d resolved_pixel=%u,%u,%u,%u read_error=0x%x",samples,pixel[0],pixel[1],pixel[2],pixel[3],glGetError());
    // An incomplete DRAW framebuffer must not invalidate a complete READ framebuffer.
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,f[0]);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,out,0);
    LOG("samples=%d separate_read=0x%x separate_draw=0x%x",samples,glCheckFramebufferStatus(GL_READ_FRAMEBUFFER),glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER));
    pixel[0]=pixel[1]=pixel[2]=pixel[3]=0;
    glReadPixels(4,4,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    LOG("samples=%d separate_pixel=%u,%u,%u,%u read_error=0x%x",samples,pixel[0],pixel[1],pixel[2],pixel[3],glGetError());
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,f[1]);
    pixel[0]=pixel[1]=pixel[2]=pixel[3]=0;
    glReadPixels(4,4,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    LOG("samples=%d repaired_pixel=%u,%u,%u,%u read_error=0x%x",samples,pixel[0],pixel[1],pixel[2],pixel[3],glGetError());
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,f[0]);
    LOG("samples=%d restored_draw=0x%x",samples,glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER));
    glDeleteFramebuffers(2,f);GLuint ts[]={color,depth,out};glDeleteTextures(3,ts);
  }
  }
  eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(d,c);eglDestroySurface(d,s);eglTerminate(d);
  ANativeActivity_finish(activity);
}
