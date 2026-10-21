#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/glx.h>
#include <X11/Xlib.h>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <string>
#include <Inventor/SoSceneManager.h>
#include <Inventor/nodes/SoCallback.h>
#include <Inventor/sensors/SoSensorManager.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoBumpMap.h>
#include <Inventor/nodes/SoBumpMapCoordinate.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include "TestAdapter.h"
#include "shapenodes/soshape_bumprender.cpp"

namespace {
GLboolean isProgram(GLuint id) {
  PFNGLISPROGRAMARBPROC func = (PFNGLISPROGRAMARBPROC) glXGetProcAddressARB((const GLubyte *) "glIsProgramARB");
  CHECK(func); return func(id);
}
void bindProgram(GLenum target, GLuint id) {
  PFNGLBINDPROGRAMARBPROC func = (PFNGLBINDPROGRAMARBPROC) glXGetProcAddressARB((const GLubyte *) "glBindProgramARB");
  CHECK(func); func(target, id);
}
void getProgramiv(GLenum target, GLenum pname, GLint * value) {
  PFNGLGETPROGRAMIVARBPROC func = (PFNGLGETPROGRAMIVARBPROC) glXGetProcAddressARB((const GLubyte *) "glGetProgramivARB");
  CHECK(func); func(target, pname, value);
}

struct GLContext {
  Display * display;
  GLXContext context;
  Colormap colormap;
  Window window;
  GLContext(Display * d, XVisualInfo * v) : display(d) {
    context = glXCreateContext(d, v, NULL, True); CHECK(context);
    colormap = XCreateColormap(d, RootWindow(d, v->screen), v->visual, AllocNone);
    XSetWindowAttributes attr = {}; attr.colormap = colormap;
    window = XCreateWindow(d, RootWindow(d, v->screen), 0, 0, 16, 16, 0,
                            v->depth, InputOutput, v->visual, CWColormap, &attr);
  }
  void current(uint32_t id) {
    CHECK(glXMakeCurrent(display, window, context)); BumpTestCacheContext::current() = id;
  }
  ~GLContext() {
    glXMakeCurrent(display, None, NULL); glXDestroyContext(display, context);
    XDestroyWindow(display, window); XFreeColormap(display, colormap);
  }
};
void check_lengths(const cc_glglue * glue, const CoinBumpTestRenderer::spec_programidx & p) {
  const GLenum targets[] = { GL_FRAGMENT_PROGRAM_ARB, GL_VERTEX_PROGRAM_ARB, GL_VERTEX_PROGRAM_ARB };
  const GLuint ids[] = { p.fragment, p.dirlight, p.pointlight };
  for (int i = 0; i < 3; ++i) {
    CHECK(isProgram(ids[i])); bindProgram(targets[i], ids[i]);
    GLint length = 0; cc_glglue_glGetProgramiv(glue, targets[i], GL_PROGRAM_LENGTH_ARB, &length);
    CHECK(length > 0);
  }
  bindProgram(GL_FRAGMENT_PROGRAM_ARB, 0); bindProgram(GL_VERTEX_PROGRAM_ARB, 0);
  CHECK(glGetError() == GL_NO_ERROR);
}
struct ListTrace {
  ListTrace() : listed(0) { }
  int listed;
};
void trace_list(void * closure, SoAction * action) {
  if (action->isOfType(SoGLRenderAction::getClassTypeId())) {
    GLint list = 0; glGetIntegerv(GL_LIST_INDEX, &list);
    if (list != 0) ++((ListTrace *) closure)->listed;
  }
}
void redraw_scene(void * closure, SoSceneManager * manager) {
  ++*(int *) closure;
  manager->render();
}
void read_pixel(GLubyte * pixel) { glReadPixels(8, 8, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel); }
void save_visual_frame(const char * stage) {
  const char * prefix = std::getenv("COIN_BUMP_VISUAL_PREFIX");
  if (!prefix || !*prefix) return;
  GLubyte pixels[16 * 16 * 3];
  glReadPixels(0, 0, 16, 16, GL_RGB, GL_UNSIGNED_BYTE, pixels);
  std::ofstream output(std::string(prefix) + "-" + stage + ".ppm", std::ios::binary);
  CHECK(output.good());
  output << "P6\n16 16\n255\n";
  for (int y = 15; y >= 0; --y)
    output.write(reinterpret_cast<const char *>(pixels + y * 16 * 3), 16 * 3);
  CHECK(output.good());
}
void drain_redraws() {
  // Process the viewer's queued redraw, not another explicit action.apply().
  for (int i = 0; i < 4 && SoDB::getSensorManager()->isDelaySensorPending(); ++i) {
    SoDB::getSensorManager()->processDelayQueue(TRUE);
  }
}
void bump_coordinate_oracle(GLContext & context) {
  context.current(75);
  SoSeparator * root = new SoSeparator; root->ref();
  root->renderCaching = SoSeparator::OFF;
  SoOrthographicCamera * camera = new SoOrthographicCamera;
  camera->position.setValue(0,0,5); camera->height = 3;
  camera->nearDistance = 1; camera->farDistance = 10;
  root->addChild(camera);
  SoDirectionalLight * light = new SoDirectionalLight;
  root->addChild(light);
  SoMaterial * material = new SoMaterial;
  material->diffuseColor.setValue(.8f,.8f,.8f);
  root->addChild(material);
  SoCoordinate3 * coordinates = new SoCoordinate3;
  const SbVec3f points[] = {SbVec3f(-1,-1,0), SbVec3f(1,-1,0),
                            SbVec3f(-1,1,0), SbVec3f(1,1,0)};
  coordinates->point.setValues(0,4,points);
  root->addChild(coordinates);
  SoTextureCoordinate2 * baseCoords = new SoTextureCoordinate2;
  const SbVec2f uv[] = {SbVec2f(.125f,.125f), SbVec2f(.875f,.125f),
                        SbVec2f(.125f,.875f), SbVec2f(.875f,.875f)};
  baseCoords->point.setValues(0,4,uv);
  root->addChild(baseCoords);
  SoBumpMapCoordinate * bumpCoords = new SoBumpMapCoordinate;
  bumpCoords->point.setValues(0,4,uv);
  root->addChild(bumpCoords);
  SoBumpMap * bump = new SoBumpMap;
  const unsigned char normals[] = {128,128,255, 240,128,128,
                                   128,240,128, 32,128,128};
  bump->image.setValue(SbVec2s(2,2),3,normals);
  root->addChild(bump);
  SoIndexedFaceSet * face = new SoIndexedFaceSet;
  const int32_t indices[] = {0,1,2,-1, 1,3,2,-1};
  face->coordIndex.setValues(0,8,indices);
  face->textureCoordIndex.setValues(0,8,indices);
  root->addChild(face);
  SoGLRenderAction action(SbViewportRegion(16,16));
  action.setCacheContext(75);
  SoSceneManager manager;
  manager.setGLRenderAction(&action);
  manager.setViewportRegion(SbViewportRegion(16,16));
  manager.setSceneGraph(root);
  manager.setBackgroundColor(SbColor(0,0,0));
  auto capture = [&]() {
    manager.render(); manager.render(); drain_redraws();
    GLubyte pixels[16*16*3];
    glReadPixels(0,0,16,16,GL_RGB,GL_UNSIGNED_BYTE,pixels);
    return std::string(reinterpret_cast<const char *>(pixels),sizeof(pixels));
  };
  const std::string withoutBase = capture();
  const SbVec2f uniform[] = {uv[0],uv[0],uv[0],uv[0]};
  bumpCoords->point.setValues(0,4,uniform);
  face->touch();
  const std::string withoutBaseUniform = capture();
  size_t absentDifference = 0;
  for (size_t i=0;i<withoutBase.size();++i)
    absentDifference += withoutBase[i] != withoutBaseUniform[i];
  const SbVec2f changedFirst[] = {uv[3],uv[0],uv[0],uv[0]};
  bumpCoords->point.setValues(0,4,changedFirst);
  face->touch();
  const std::string withoutBaseFirst = capture();
  size_t firstDifference = 0;
  for (size_t i=0;i<withoutBaseFirst.size();++i)
    firstDifference += withoutBaseFirst[i] != withoutBaseUniform[i];
  bumpCoords->point.setValues(0,4,uv);
  face->touch();
  SoTexture2 * base = new SoTexture2;
  const unsigned char white[] = {255,255,255};
  base->image.setValue(SbVec2s(1,1),3,white);
  root->insertChild(base,root->findChild(face));
  const std::string withBase = capture();
  bumpCoords->point.setValues(0,4,uniform);
  face->touch();
  const std::string withBaseUniform = capture();
  material->ambientColor.setValue(.9f,.1f,.1f);
  material->emissiveColor.setValue(.3f,.4f,.5f);
  const std::string ambientEmissionChanged = capture();
  size_t ambientEmissionDifference = 0;
  for (size_t i=0;i<withBaseUniform.size();++i)
    ambientEmissionDifference += withBaseUniform[i] != ambientEmissionChanged[i];
  std::printf("BUMP_BASE_ORACLE ambient_emission_changed_bytes=%zu\n",
              ambientEmissionDifference);
  CHECK(ambientEmissionDifference == 0);
  material->ambientColor.setValue(.2f,.2f,.2f);
  material->emissiveColor.setValue(0,0,0);
  size_t activeDifference = 0;
  for (size_t i=0;i<withBase.size();++i)
    activeDifference += withBase[i] != withBaseUniform[i];
  light->direction.setValue(0,0,1);
  const std::string reversedLight = capture();
  size_t lightDifference = 0;
  for (size_t i=0;i<withBaseUniform.size();++i)
    lightDifference += withBaseUniform[i] != reversedLight[i];
  const size_t center = (8 * 16 + 8) * 3;
  const unsigned int frontCenter = static_cast<unsigned char>(withBaseUniform[center]) +
    static_cast<unsigned char>(withBaseUniform[center + 1]) +
    static_cast<unsigned char>(withBaseUniform[center + 2]);
  const unsigned int backCenter = static_cast<unsigned char>(reversedLight[center]) +
    static_cast<unsigned char>(reversedLight[center + 1]) +
    static_cast<unsigned char>(reversedLight[center + 2]);
  std::printf("BUMP_UV_ORACLE without_base_other_uv_bytes=%zu without_base_first_uv_bytes=%zu with_base_other_uv_bytes=%zu\n",
              absentDifference,firstDifference,activeDifference);
  std::printf("BUMP_LIGHT_ORACLE reversed_direction_bytes=%zu front_center=%u back_center=%u\n",
              lightDifference, frontCenter, backCenter);
  CHECK(absentDifference == 0 && firstDifference > 0 && activeDifference > 0);
  CHECK(lightDifference > 0 && frontCenter > backCenter);
  // Keep the complete CoinGL specular composition observable without channel
  // saturation. The normal pass can contribute fixed-function specular too;
  // compare roughness at the same light angle, with a no-spec baseline.
  material->ambientColor.setValue(0,0,0);
  material->diffuseColor.setValue(0,0,0);
  material->specularColor.setValue(.1f,.1f,.1f);
  material->shininess = .5f;
  const unsigned char black[] = {0,0,0};
  base->image.setValue(SbVec2s(1,1),3,black);
  const unsigned char flatNormal[] = {128,128,255};
  bump->image.setValue(SbVec2s(1,1),3,flatNormal);
  light->direction.setValue(0,0,-1);
  const std::string specFront = capture();
  light->direction.setValue(1,0,0);
  const std::string specAngled = capture();
  material->shininess = .05f;
  const std::string specBroad = capture();
  light->direction.setValue(0,0,1);
  const std::string specReverse = capture();
  material->specularColor.setValue(0,0,0);
  light->direction.setValue(1,0,0);
  const std::string noSpecAngled = capture();
  const auto centerRed = [center](const std::string & pixels) {
    return static_cast<unsigned int>(static_cast<unsigned char>(pixels[center]));
  };
  std::printf("BUMP_SPECULAR_ORACLE front=%u angled=%u broad=%u reverse=%u no_spec_angled=%u\n",
              centerRed(specFront),centerRed(specAngled),
              centerRed(specBroad),centerRed(specReverse),centerRed(noSpecAngled));
  CHECK(centerRed(specBroad) > centerRed(specAngled) + 3 &&
        centerRed(specAngled) > centerRed(noSpecAngled) &&
        centerRed(specReverse) == 0);
  manager.setSceneGraph(NULL);
  root->unref();
  SoContextHandler::destructingContext(75);
}
void integration_scene(GLContext & a, GLContext & b) {
  SoSeparator * root = new SoSeparator; root->ref();
  root->renderCaching = SoSeparator::OFF;
  SoOrthographicCamera * camera = new SoOrthographicCamera;
  camera->position.setValue(0, 0, 5); camera->height = 3;
  camera->nearDistance = 1; camera->farDistance = 10; root->addChild(camera);
  root->addChild(new SoDirectionalLight);
  SoSeparator * scene = new SoSeparator;
  scene->renderCaching = SoSeparator::ON;
  root->addChild(scene);
  SoMaterial * material = new SoMaterial;
  material->diffuseColor.setValue(0.1f, 0.1f, 0.1f);
  material->specularColor.setValue(0.8f, 0.8f, 0.8f); scene->addChild(material);
  SoBumpMap * bump = new SoBumpMap;
  const unsigned char pixels[] = {128,128,255,128,128,255,128,128,255,128,128,255};
  bump->image.setValue(SbVec2s(2,2), 3, pixels); scene->addChild(bump);
  ListTrace trace;
  SoCallback * callback = new SoCallback;
  callback->setCallback(trace_list, &trace); scene->addChild(callback);
  scene->addChild(new SoCube);
  {
    SoGLRenderAction action(SbViewportRegion(16,16));
    SoSceneManager manager;
    manager.setGLRenderAction(&action); manager.setViewportRegion(SbViewportRegion(16,16));
    manager.setRedrawPriority(0); // Must not reenter an action inside its display list.
    manager.setSceneGraph(root); manager.setBackgroundColor(SbColor(0,0,0));
    int redraws = 0;
    manager.setRenderCallback(redraw_scene, &redraws); manager.activate();
    a.current(73); action.setCacheContext(73);
    manager.render(); manager.render(); manager.render(); drain_redraws();
    CHECK(trace.listed > 0);
    GLubyte expected[3]; read_pixel(expected); CHECK(expected[0] > 0);
    save_visual_frame("neutral");
    GLubyte neutralFrame[16 * 16 * 3];
    glReadPixels(0, 0, 16, 16, GL_RGB, GL_UNSIGNED_BYTE, neutralFrame);
    GLubyte neutralPixel[3]; std::memcpy(neutralPixel, expected, sizeof(neutralPixel));
    const unsigned char tilted[] = {192,128,238,192,128,238,192,128,238,192,128,238};
    bump->image.setValue(SbVec2s(2,2), 3, tilted);
    manager.render(); manager.render(); drain_redraws();
    GLubyte tiltedPixel[3]; read_pixel(tiltedPixel);
    save_visual_frame("tilted");
    std::printf("BUMP_VISUAL neutral=%u,%u,%u tilted=%u,%u,%u\n",
                expected[0], expected[1], expected[2],
                tiltedPixel[0], tiltedPixel[1], tiltedPixel[2]);
    const unsigned char heights[] = {0,64,128,0,64,128,0,64,128};
    bump->image.setValue(SbVec2s(3,3), 1, heights);
    manager.render(); manager.render(); drain_redraws();
    GLubyte heightPixel[3]; read_pixel(heightPixel);
    save_visual_frame("height");
    std::printf("BUMP_VISUAL height=%u,%u,%u\n",
                heightPixel[0], heightPixel[1], heightPixel[2]);
    bump->image.setValue(SbVec2s(2,2), 3, pixels);
    manager.render(); manager.render(); drain_redraws();
    read_pixel(expected);
    save_visual_frame("restored");
    GLubyte restoredFrame[16 * 16 * 3];
    glReadPixels(0, 0, 16, 16, GL_RGB, GL_UNSIGNED_BYTE, restoredFrame);
    CHECK(std::memcmp(neutralFrame, restoredFrame, sizeof(neutralFrame)) == 0);
    std::printf("BUMP_VISUAL restored=%u,%u,%u\n",
                expected[0], expected[1], expected[2]);
    for (int i = 0; i < 3; ++i) CHECK(expected[i] == neutralPixel[i]);
    CHECK(neutralPixel[0] > tiltedPixel[0] + 32);
    CHECK(tiltedPixel[0] > heightPixel[0] + 16);
    GLint fragmenta = 0, vertexa = 0;
    getProgramiv(GL_FRAGMENT_PROGRAM_ARB, GL_PROGRAM_BINDING_ARB, &fragmenta);
    getProgramiv(GL_VERTEX_PROGRAM_ARB, GL_PROGRAM_BINDING_ARB, &vertexa);
    CHECK(fragmenta != 0 && vertexa != 0);

    // The separator is already warm, but B has no programs. Its first visit
    // really compiles a list; then the scene-manager notification must render
    // again without any user input, initializing outside the list.
    b.current(74); action.setCacheContext(74);
    const int listed = trace.listed, before = redraws;
    manager.render(); CHECK(trace.listed > listed);
    drain_redraws(); CHECK(redraws > before);
    GLubyte actual[3]; read_pixel(actual);
    for (int i = 0; i < 3; ++i) CHECK(std::abs((int) actual[i] - (int) expected[i]) <= 2);
    GLint fragmentb = 0, vertexb = 0;
    getProgramiv(GL_FRAGMENT_PROGRAM_ARB, GL_PROGRAM_BINDING_ARB, &fragmentb);
    getProgramiv(GL_VERTEX_PROGRAM_ARB, GL_PROGRAM_BINDING_ARB, &vertexb);
    CHECK(fragmentb != 0 && vertexb != 0);
    GLint length = 0;
    getProgramiv(GL_FRAGMENT_PROGRAM_ARB, GL_PROGRAM_LENGTH_ARB, &length); CHECK(length > 0);
    getProgramiv(GL_VERTEX_PROGRAM_ARB, GL_PROGRAM_LENGTH_ARB, &length); CHECK(length > 0);
    const int settled = redraws; manager.render(); drain_redraws(); CHECK(redraws == settled);
    manager.deactivate(); manager.setSceneGraph(NULL); root->unref();
    SoSeparator * empty = new SoSeparator; empty->ref();
    action.apply(empty); empty->unref();
    CHECK(!isProgram((GLuint) fragmentb) && !isProgram((GLuint) vertexb));
    SoContextHandler::destructingContext(74);
    a.current(73); SoContextHandler::destructingContext(73);
    CHECK(!isProgram((GLuint) fragmenta) && !isProgram((GLuint) vertexa));
  }
  std::puts("PASS: real deferred initialization in a second context, automatic redraw and cleanup.");
}

}
int main() {
  Display * display = XOpenDisplay(NULL);
  if (!display) { std::puts("SKIP: no X display"); return 77; }
  int attr[] = { GLX_RGBA, GLX_DEPTH_SIZE, 24, None };
  XVisualInfo * visual = glXChooseVisual(display, DefaultScreen(display), attr);
  // PRIME NVIDIA exposes the compatible RGBA visual with double buffering.
  if (!visual) {
    int buffered[] = { GLX_RGBA, GLX_DOUBLEBUFFER, GLX_DEPTH_SIZE, 24, None };
    visual = glXChooseVisual(display, DefaultScreen(display), buffered);
  }
  if (!visual) { std::puts("SKIP: no compatible RGBA/depth GLX visual"); XCloseDisplay(display); return 77; }
  SoDB::init();
  {
    GLContext a(display, visual), b(display, visual); a.current(71);
    const char * extensions = (const char *) glGetString(GL_EXTENSIONS);
    if (!extensions || !std::strstr(extensions, "GL_ARB_fragment_program") ||
        !std::strstr(extensions, "GL_ARB_vertex_program")) {
      std::puts("SKIP: ARB vertex/fragment programs unavailable"); return 77;
    }
    std::printf("GL renderer: %s\n", glGetString(GL_RENDERER));
    const cc_glglue * gluea = cc_glglue_instance(71);
    CoinBumpTestRenderer * renderer = new CoinBumpTestRenderer;
    CoinBumpTestRenderer::spec_programidx pa, pb;
    CHECK(renderer->ensurePrograms(gluea, NULL, pa)); check_lengths(gluea, pa);

    // Both default bindings and pre-existing valid bindings must defer uploads.
    for (int prebound = 0; prebound < 2; ++prebound) {
      CoinBumpTestRenderer deferred;
      if (prebound) {
        bindProgram(GL_FRAGMENT_PROGRAM_ARB, pa.fragment);
        bindProgram(GL_VERTEX_PROGRAM_ARB, pa.dirlight);
      }
      GLuint list = glGenLists(1); glNewList(list, GL_COMPILE);
      CoinBumpTestRenderer::spec_programidx p;
      CHECK(!deferred.ensurePrograms(gluea, NULL, p));
      CHECK(!deferred.ensurePrograms(gluea, NULL, p));
      glEndList(); CHECK(glGetError() == GL_NO_ERROR);
      glCallList(list); CHECK(glGetError() == GL_NO_ERROR);
      CHECK(deferred.programcache->contexts[71].spec.fragment == 0);
      BumpTestCacheContext::flush(71);
      CHECK(deferred.ensurePrograms(gluea, NULL, p));
      GLint binding = 0; getProgramiv(GL_FRAGMENT_PROGRAM_ARB, GL_PROGRAM_BINDING_ARB, &binding);
      CHECK(binding == (prebound ? (GLint) pa.fragment : 0));
      getProgramiv(GL_VERTEX_PROGRAM_ARB, GL_PROGRAM_BINDING_ARB, &binding);
      CHECK(binding == (prebound ? (GLint) pa.dirlight : 0));
      check_lengths(gluea, p); glDeleteLists(list, 1);
    }
    BumpTestCacheContext::flush(71);
    b.current(72); const cc_glglue * glueb = cc_glglue_instance(72);
    CHECK(renderer->ensurePrograms(glueb, NULL, pb)); check_lengths(glueb, pb);
    a.current(71); CoinBumpTestRenderer::spec_programidx reused;
    CHECK(renderer->ensurePrograms(gluea, NULL, reused)); CHECK(reused.fragment == pa.fragment);
    SoContextHandler::destructingContext(71);
    CHECK(!isProgram(pa.fragment) && !isProgram(pa.dirlight) && !isProgram(pa.pointlight));
    b.current(72); delete renderer; BumpTestCacheContext::flush(72);
    CHECK(!isProgram(pb.fragment) && !isProgram(pb.dirlight) && !isProgram(pb.pointlight));
    SoContextHandler::destructingContext(72);
    CHECK(CoinBumpTestRenderer::ProgramCache::registry().entries.empty());
    CHECK(BumpTestCacheContext::queue().empty());
    integration_scene(a, b);
    bump_coordinate_oracle(a);
  }
  SoDB::finish(); XFree(visual); XCloseDisplay(display);
  std::puts("Bump GLX display-list and two-context lifecycle tests passed.");
}
