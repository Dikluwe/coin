#include <EGL/egl.h>
#include <GL/gl.h>
#include <Inventor/SoDB.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoSceneTexture2.h>
#include <cstdio>
int main(){
 EGLDisplay d=eglGetDisplay(EGL_DEFAULT_DISPLAY);EGLint major,minor;
 if(!eglInitialize(d,&major,&minor)||!eglBindAPI(EGL_OPENGL_API)) return 1;
 EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_DEPTH_SIZE,24,EGL_NONE};EGLConfig config;EGLint n=0;
 if(!eglChooseConfig(d,attrs,&config,1,&n)||!n)return 2;
 EGLint dims[]={EGL_WIDTH,32,EGL_HEIGHT,32,EGL_NONE};auto surface=eglCreatePbufferSurface(d,config,dims);auto context=eglCreateContext(d,config,EGL_NO_CONTEXT,nullptr);
 if(!eglMakeCurrent(d,surface,surface,context))return 3;
 SoDB::init();auto*root=new SoSeparator;root->ref();root->addChild(new SoOrthographicCamera);auto*model=new SoLightModel;model->model=SoLightModel::BASE_COLOR;root->addChild(model);
 auto*producer=static_cast<SoSeparator*>(root->copy(TRUE));producer->addChild(new SoCube);auto*rtt=new SoSceneTexture2;rtt->size=SbVec2s(32,32);rtt->scene=producer;root->addChild(rtt);root->addChild(new SoCube);
 SoGLRenderAction action(SbViewportRegion(32,32));action.apply(root);
 const GLenum error=glGetError();unsigned char px[4];glReadPixels(16,16,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px);
 printf("EGL %d.%d renderer=%s GL_error=%x RGB=%d,%d,%d\n",major,minor,glGetString(GL_RENDERER),error,px[0],px[1],px[2]);
 const bool ok=error==GL_NO_ERROR&&(px[0]+px[1]+px[2]>0);root->unref();return ok?0:4;
}
