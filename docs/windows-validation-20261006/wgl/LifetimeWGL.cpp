#include <cstdio>
#include <cstring>
#include <thread>
#include "WGLSupport.h"

#include <Inventor/SoDB.h>
#include <Inventor/C/glue/gl.h>
#include <Inventor/misc/SoContextHandler.h>
#include "glue/glp.h"
#if defined(__SANITIZE_ADDRESS__) || defined(COIN_WGL_ASAN)
#include <sanitizer/asan_interface.h>
#endif
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#c); return 1; } } while (0)
typedef WGLNative NativeContext;
struct Borrow {
  const cc_glglue * glue; const void * owned[6];
  explicit Borrow(int id) : glue(cc_glglue_instance(id)) {
    // Force ownership of a dynamic-library handle as well as extension cache.
    (void) cc_glglue_getprocaddress(glue,"glGetString");
    (void) cc_glglue_glext_supported(glue,"GL_ARB_multitexture");
    owned[0]=glue->versionstr; owned[1]=glue->vendorstr; owned[2]=glue->rendererstr;
    owned[3]=glue->extensionsstr; owned[4]=glue->glextdict; owned[5]=glue->dl_handle;
  }
  bool released() const {
#if defined(__SANITIZE_ADDRESS__) || defined(COIN_WGL_ASAN)
    if (!__asan_address_is_poisoned(glue)) return false;
    for (const void * pointer:owned) if (pointer && !__asan_address_is_poisoned(pointer)) return false;
#endif
    return true;
  }
};
struct Probe {
  uint32_t id; const cc_glglue * glue; int callbacks; bool valid;
  Probe(uint32_t id) : id(id),glue(NULL),callbacks(0),valid(true) { }
  static void callback(uint32_t id,void * userdata) {
    Probe * self=static_cast<Probe *>(userdata); if (self->id!=id) return;
    ++self->callbacks;
    const cc_glglue * current=cc_glglue_instance(id);
    unsigned int major,minor,release; cc_glglue_glversion(current,&major,&minor,&release);
    if (current!=self->glue || major!=43) self->valid=false;
  }
};
static bool current(void * display,NativeContext & native) {
  return native.select();
}
static int cycle(void * display,NativeContext & native,Probe & probe,int count) {
  if (!current(display,native)) return 1;
  for (int i=0;i<count;++i) {
    Borrow borrow(probe.id); probe.glue=borrow.glue;
    if (borrow.glue->version.major==43) return 2; // A reused ID must get fresh metadata.
    const_cast<cc_glglue *>(borrow.glue)->version.major=43;
    SoContextHandler::destructingContext(probe.id);
    if (!probe.valid || !borrow.released()) return 3;
  }
  releaseWGL(); return 0;
}
int main(int argc,char ** argv) {
  const bool raw=argc>1 && std::strcmp(argv[1],"--no-callbacks")==0;
  void * display=NULL; NativeContext native[2];
  for (NativeContext & n:native) CHECK(n.create());
  if (!raw) SoDB::init();
  CHECK(current(display,native[0]));
  std::printf("WGL vendor=%s renderer=%s version=%s\n", glGetString(GL_VENDOR), glGetString(GL_RENDERER), glGetString(GL_VERSION));
  // Unknown IDs must not require a previously created callback registry.
  SoContextHandler::destructingContext(9998);
  if (raw) {
    Borrow borrow(9999); SoContextHandler::destructingContext(9999); CHECK(borrow.released());
  }
  else {
    Probe a(7001),b(7002);
    SoContextHandler::addContextDestructionCallback(Probe::callback,&a);
    SoContextHandler::addContextDestructionCallback(Probe::callback,&b);
    CHECK(cycle(display,native[0],a,32)==0 && a.callbacks==32);
    // Distinct contexts may be used and destroyed concurrently. Each context's
    // callers serialize borrowing and destruction within its owning thread.
    int resultA=-1,resultB=-1;
    std::thread threadA([&] { resultA=cycle(display,native[0],a,16); });
    std::thread threadB([&] { resultB=cycle(display,native[1],b,16); });
    threadA.join(); threadB.join();
    CHECK(resultA==0 && resultB==0 && a.callbacks==48 && b.callbacks==16);
    Probe high(0x80000001U),maximum(0xffffffffU);
    SoContextHandler::addContextDestructionCallback(Probe::callback,&high);
    SoContextHandler::addContextDestructionCallback(Probe::callback,&maximum);
    CHECK(cycle(display,native[0],high,4)==0 && high.callbacks==4);
    CHECK(cycle(display,native[0],maximum,4)==0 && maximum.callbacks==4);
    SoContextHandler::removeContextDestructionCallback(Probe::callback,&high);
    SoContextHandler::removeContextDestructionCallback(Probe::callback,&maximum);
    SoContextHandler::removeContextDestructionCallback(Probe::callback,&a);
    SoContextHandler::removeContextDestructionCallback(Probe::callback,&b);
    CHECK(current(display,native[0]));
  std::printf("WGL vendor=%s renderer=%s version=%s\n", glGetString(GL_VENDOR), glGetString(GL_RENDERER), glGetString(GL_VERSION));
    // A still-live cached record is released during Coin shutdown too.
    Borrow active(7100); SoDB::finish(); CHECK(active.released());
  }
  releaseWGL();
  for (NativeContext & n:native) n.destroy();

  std::puts(raw ? "Glue released without registered callbacks." : "Glue: callbacks, owned allocations, 72 generations, two threads and shutdown passed.");
  return 0;
}