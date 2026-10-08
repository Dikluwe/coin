// This executable deliberately links no Coin library or Coin headers.
#include "egl_context.h"
#include "raw_gl.h"
int main(int argc,char **argv) {
  if(argc!=4) {std::cerr<<"Usage: pure-egl-sampling OUTPUT_PREFIX QUALITY OFFSET\n";return 2;}
  EglContext context;
  if(!context.init()) {std::cerr<<"EGL unavailable "<<eglGetError()<<'\n';return 77;}
  bool ok=true;std::cout<<std::setprecision(9);
  for(const char *route : {"fixed","implicit","explicit-lod","fetch","fetch-round256","query-lod","boundary-scan","boundary-fetch"}) {
    Probe p{std::stof(argv[2]),std::stof(argv[3]),route,argv[1],true};
    glViewport(0,0,64,64);glClearColor(.1f,.1f,.1f,1);glClearDepth(1);
    glDepthMask(GL_TRUE);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    drawRaw(p);
    std::vector<uint8_t> bottom(64*64*3),top(bottom.size());
    glPixelStorei(GL_PACK_ALIGNMENT,1);glReadPixels(0,0,64,64,GL_RGB,GL_UNSIGNED_BYTE,bottom.data());
    for(int y=0;y<64;++y)std::copy(bottom.begin()+(63-y)*64*3,bottom.begin()+(64-y)*64*3,top.begin()+y*64*3);
    std::ofstream out(p.output+"-"+p.route+".rgb",std::ios::binary);
    out.write(reinterpret_cast<const char*>(top.data()),top.size());ok &= p.ok && bool(out) && glGetError()==GL_NO_ERROR;
  }
  return ok?0:1;
}
