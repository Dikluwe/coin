// Visual dependency reproducer. Same executable for baseline, before and fixed.
#include <Inventor/SoDB.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/bundles/SoTextureCoordinateBundle.h>
#include <Inventor/caches/SoGLRenderCache.h>
#include <Inventor/elements/SoCacheElement.h>
#include <Inventor/elements/SoComplexityElement.h>
#include <Inventor/misc/SoState.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoShaderProgram.h>
#include <Inventor/nodes/SoVertexShader.h>
#include <Inventor/nodes/SoFragmentShader.h>
#include <Inventor/system/gl.h>
#include <cstdio>
#include <string>

struct Cleanup { ~Cleanup() { SoDB::finish(); } };

class DemoShape : public SoFaceSet {
public:
  DemoShape() : cache(NULL), warmed(false), builds(0), calls(0), bbox(0) {}
  ~DemoShape() override { if (cache) cache->unref(); }
  SoGLRenderCache * cache;
  bool warmed;
  int builds, calls, bbox;

  void computeBBox(SoAction * action, SbBox3f & box, SbVec3f & center) override
  {
    ++bbox;
    // Deliberately state-dependent custom shape, matching the API regression.
    const float width = SoComplexityElement::get(action->getState()) > 0.5f ? 2.0f : 1.0f;
    box.setBounds(SbVec3f(0,0,0), SbVec3f(width,1,0));
    center = box.getCenter();
  }

  void GLRender(SoGLRenderAction * action) override
  {
    SoState * state = action->getState();
    if (builds + calls == 0)
      std::fprintf(stderr, "GL_RENDERER=%s\nGL_VERSION=%s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
    if (!warmed) {
      SoTextureCoordinateBundle warm(action, TRUE);
      warmed = true;
    }
    const bool valid = cache && cache->isValid(state);
    if (cache && !valid) {
      const SoElement * cause = cache->getInvalidElement(state);
      std::printf("invalid_element=%s\n", cause ? cause->getTypeId().getName().getString() : "explicit");
    }
    if (valid) { cache->call(state); ++calls; }
    else {
      if (cache) cache->unref(state);
      const SbBool invalid = SoCacheElement::setInvalid(FALSE);
      state->push();
      cache = new SoGLRenderCache(state);
      cache->ref();
      SoCacheElement::set(state, cache);
      cache->open(state);
      // A descendant has an empty cache level, while the parent stays open.
      state->push();
      SoCacheElement::set(state, NULL);
      {
        SoTextureCoordinateBundle bundle(action, TRUE);
        const SbVec3f points[] = {SbVec3f(0,0,0), SbVec3f(1,0,0),
                                 SbVec3f(1,1,0), SbVec3f(0,1,0)};
        glBegin(GL_QUADS);
        for (const auto & point : points) {
          const SbVec4f uv = bundle.get(point, SbVec3f(0,0,1));
          glTexCoord4fv(uv.getValue());
          glVertex3fv(point.getValue());
        }
        glEnd();
      }
      state->pop();
      cache->close();
      state->pop();
      SoCacheElement::setInvalid(invalid);
      ++builds;
    }
    glFinish();
    std::printf("builds=%d calls=%d bbox=%d reused_render_cache=%d gl_error=%u\n",
                builds, calls, bbox, valid, unsigned(glGetError()));
  }
};

static bool save(SoOffscreenRenderer & renderer, SoNode * root, const std::string & path)
{
  if (!renderer.render(root)) return false;
  FILE * out = std::fopen(path.c_str(), "wb");
  if (!out) return false;
  std::fprintf(out, "P6\n512 512\n255\n");
  const auto * pixels = renderer.getBuffer();
  for (int y = 511; y >= 0; --y) std::fwrite(pixels + y * 512 * 3, 1, 512 * 3, out);
  return std::fclose(out) == 0;
}

int main(int argc, char ** argv)
{
  if (argc != 2) return 2;
  SoDB::init();
  Cleanup cleanup;
  SoSeparator * root = new SoSeparator;
  root->ref();
  root->renderCaching = SoSeparator::OFF;
  root->boundingBoxCaching = SoSeparator::OFF;
  SoOrthographicCamera * camera = new SoOrthographicCamera;
  camera->position.setValue(0.5f,0.5f,3);
  camera->height = 1.16f;
  root->addChild(camera);
  SoComplexity * complexity = new SoComplexity;
  complexity->value = 0.25f;
  root->addChild(complexity);
  SoTexture2 * texture = new SoTexture2;
  const unsigned char pixel[] = {255,255,255};
  texture->image.setValue(SbVec2s(1,1), 3, pixel);
  root->addChild(texture);
  SoShaderProgram * shader = new SoShaderProgram;
  SoVertexShader * vertex = new SoVertexShader;
  vertex->sourceType = SoShaderObject::GLSL_PROGRAM;
  vertex->sourceProgram = "void main(){gl_TexCoord[0]=gl_MultiTexCoord0;gl_Position=ftransform();}";
  SoFragmentShader * fragment = new SoFragmentShader;
  fragment->sourceType = SoShaderObject::GLSL_PROGRAM;
  // Asymmetric checkerboard: square size and colored axis stripes expose UVs.
  fragment->sourceProgram =
    "void main(){vec2 uv=gl_TexCoord[0].st;"
    "float odd=mod(floor(uv.x*8.0)+floor(uv.y*8.0),2.0);"
    "vec3 c=mix(vec3(0.08,0.18,0.30),vec3(0.85,0.91,0.95),odd);"
    "if(uv.x<0.08)c=vec3(1.0,0.53,0.12);"
    "if(uv.y<0.08)c=vec3(0.08,0.75,0.68);"
    "gl_FragColor=vec4(c,1.0);}";
  shader->shaderObject.set1Value(0,vertex);
  shader->shaderObject.set1Value(1,fragment);
  root->addChild(shader);
  root->addChild(new SoCoordinate3);
  DemoShape * shape = new DemoShape;
  root->addChild(shape);
  SoOffscreenRenderer renderer(SbViewportRegion(512,512));
  renderer.setComponents(SoOffscreenRenderer::RGB);
  renderer.setBackgroundColor(SbColor(0.035f,0.047f,0.07f));
  const std::string prefix(argv[1]);
  bool ok = save(renderer,root,prefix+"-initial.ppm");
  ok = save(renderer,root,prefix+"-unchanged.ppm") && ok;
  ok = save(renderer,root,prefix+"-unchanged.ppm") && ok;
  ok = save(renderer,root,prefix+"-unchanged.ppm") && ok;
  complexity->value = 0.75f;
  ok = save(renderer,root,prefix+"-edited.ppm") && ok;
  shape->cache->invalidate();
  ok = save(renderer,root,prefix+"-reference.ppm") && ok;
  root->unref();
  return ok ? 0 : 1;
}
