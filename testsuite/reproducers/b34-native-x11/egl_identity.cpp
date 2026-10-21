#include "CoinRenderEglLifetimeFixture.h"
int main(){
 if(!XInitThreads())return 2;
 Display*d=XOpenDisplay(nullptr);if(!d)return 3;
 egl_lifetime::Api a;egl_lifetime::Context c(a,d);
 if(!a.ready()||!c.initialize(false)||!c.makeCurrent())return 4;
 auto text=reinterpret_cast<const unsigned char*(*)(unsigned)>(a.GetProcAddress("glGetString"));
 std::cout<<"EGL vendor="<<a.QueryString(c.display,0x3053)<<" GL vendor="<<text(0x1f00)<<" renderer="<<text(0x1f01)<<" version="<<text(0x1f02)<<std::endl;
 bool ok=c.close()&&a.ReleaseThread();XCloseDisplay(d);return ok?0:5;
}
