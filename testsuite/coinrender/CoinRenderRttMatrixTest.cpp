#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <Inventor/SoDB.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/elements/SoMultiTextureMatrixElement.h>
#include <Inventor/elements/SoViewportRegionElement.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSceneTexture2.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoSubNode.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoTextureMatrixTransform.h>
#include <Inventor/nodes/SoTextureUnit.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/system/gl.h>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
class RttViewport : public SoNode {
  SO_NODE_HEADER(RttViewport);

public:
  static void initClass() { SO_NODE_INIT_CLASS(RttViewport, SoNode, "Node"); }
  RttViewport() { SO_NODE_CONSTRUCTOR(RttViewport); }
  SbViewportRegion viewport;
  void callback(SoCallbackAction *a) override {
    SoViewportRegionElement::set(a->getState(), viewport);
  }
  void GLRender(SoGLRenderAction *a) override {
    SoViewportRegionElement::set(a->getState(), viewport);
  }

protected:
  ~RttViewport() override = default;
};
SO_NODE_SOURCE(RttViewport);
void externalViewport(SoSeparator *r, int target, int extent) {
  auto *v = new RttViewport;
  v->viewport = SbViewportRegion(target, target);
  v->viewport.setViewportPixels(-8, -8, extent, extent);
  r->addChild(v);
}
class MatrixWitness : public SoNode {
  SO_NODE_HEADER(MatrixWitness);

public:
  static void initClass() { SO_NODE_INIT_CLASS(MatrixWitness, SoNode, "Node"); }
  MatrixWitness() { SO_NODE_CONSTRUCTOR(MatrixWitness); }
  SbMatrix captured, native, capturedThird, nativeThird;
  int framebuffer = -1;
  float physical[16] = {};
  void callback(SoCallbackAction *a) override {
    captured = SoMultiTextureMatrixElement::get(a->getState(), 0);
    capturedThird = SoMultiTextureMatrixElement::get(a->getState(), 3);
  }
  void GLRender(SoGLRenderAction *a) override {
    native = SoMultiTextureMatrixElement::get(a->getState(), 0);
    nativeThird = SoMultiTextureMatrixElement::get(a->getState(), 3);
    glGetIntegerv(0x8ca6, &framebuffer);
    glGetFloatv(GL_TEXTURE_MATRIX, physical);
  }

protected:
  ~MatrixWitness() override = default;
};
SO_NODE_SOURCE(MatrixWitness);
class TextureWitness : public SoNode {
  SO_NODE_HEADER(TextureWitness);

public:
  static void initClass() {
    SO_NODE_INIT_CLASS(TextureWitness, SoNode, "Node");
  }
  TextureWitness() { SO_NODE_CONSTRUCTOR(TextureWitness); }
  int minFilter = 0, levelWidth = 0;
  void GLRender(SoGLRenderAction *) override {
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &minFilter);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 1, GL_TEXTURE_WIDTH, &levelWidth);
  }

protected:
  ~TextureWitness() override = default;
};
SO_NODE_SOURCE(TextureWitness);

void quad(SoSeparator *root, float uv = -1) {
  auto *c = new SoCoordinate3;
  const SbVec3f p[] = {{-1, -1, 0}, {1, -1, 0}, {1, 1, 0}, {-1, 1, 0}};
  c->point.setValues(0, 4, p);
  root->addChild(c);
  auto *t = new SoTextureCoordinate2;
  const SbVec2f v[] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
  t->point.setValues(0, 4, v);
  if (uv >= 0)
    for (int i = 0; i < 4; ++i)
      t->point.set1Value(i, SbVec2f(uv, .5f));
  root->addChild(t);
  auto *f = new SoIndexedFaceSet;
  const int32_t idx[] = {0, 1, 2, 3, -1};
  f->coordIndex.setValues(0, 5, idx);
  root->addChild(f);
}
SoSeparator *base() {
  auto *r = new SoSeparator;
  auto *c = new SoOrthographicCamera;
  c->height = 2;
  c->position.setValue(0, 0, 4);
  r->addChild(c);
  auto *l = new SoLightModel;
  l->model = SoLightModel::BASE_COLOR;
  r->addChild(l);
  auto *q = new SoComplexity;
  q->textureQuality = .3f;
  r->addChild(q);
  return r;
}
int runCase(int local, float quality, const std::string &mode, bool external) {
  const bool filterProbe = mode == "--filter-probe";
  auto *child = base();
  if (external)
    externalViewport(child, 32, 40);
  SbMatrix childTransform = SbMatrix::identity();
  childTransform[3][0] = (local ? .25f : 0.f);
  auto *localMatrix = new SoTextureMatrixTransform;
  localMatrix->matrix = childTransform;
  child->addChild(localMatrix);
  auto *w = new MatrixWitness;
  child->addChild(w);
  auto *texture = new SoTexture2;
  std::vector<uint8_t> pixels(8 * 8 * 3, 0);
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 8; ++x)
      pixels[(y * 8 + x) * 3 + (x < 4 ? 0 : 1)] = 255;
  texture->image.setValue(SbVec2s(8, 8), 3, pixels.data());
  texture->model = SoTexture2::REPLACE;
  child->addChild(texture);
  quad(child);
  SoSeparator *producer = child;
  if (local == 2) {
    producer = base();
    auto *localMatrix = new SoTextureMatrixTransform;
    localMatrix->matrix = childTransform;
    producer->addChild(localMatrix);
    auto *inner = new SoSceneTexture2;
    inner->scene = child;
    inner->size.setValue(32, 32);
    inner->transparencyFunction = SoSceneTexture2::NONE;
    producer->addChild(inner);
    quad(producer, .125f);
  }
  auto *root = base();
  if (external)
    externalViewport(root, 64, 80);
  root->ref();
  static_cast<SoComplexity *>(root->getChild(2))->textureQuality =
      filterProbe ? .5f : quality;
  auto *u3 = new SoTextureUnit;
  u3->unit = 3;
  root->addChild(u3);
  auto *m3 = new SoTextureMatrixTransform;
  SbMatrix third = SbMatrix::identity();
  third[3][1] = .4f;
  m3->matrix = third;
  root->addChild(m3);
  auto *u0 = new SoTextureUnit;
  u0->unit = 0;
  root->addChild(u0);
  auto *matrix = new SoTextureMatrixTransform;
  SbMatrix transform = SbMatrix::identity();
  transform[3][0] = .25f;
  matrix->matrix = transform;
  root->addChild(matrix);
  auto *rtt = new SoSceneTexture2;
  rtt->scene = producer;
  rtt->size.setValue(32, 32);
  rtt->transparencyFunction = SoSceneTexture2::NONE;
  root->addChild(rtt);
  auto *parentWitness = new MatrixWitness;
  root->addChild(parentWitness);
  quad(root, external ? 0.f : .125f);
  auto *filterWitness = new TextureWitness;
  root->addChild(filterWitness);

  if (mode == "--gl-probe" || mode == "--gl" || filterProbe) {
    SoOffscreenRenderer gl(SbViewportRegion(64, 64));
    gl.setComponents(SoOffscreenRenderer::RGB);
    gl.getGLRenderAction()->setTransparencyType(SoGLRenderAction::BLEND);
    if (!gl.render(root) || !gl.getBuffer())
      return 1;
    const auto *p = gl.getBuffer() + (32 * 64 + 32) * 3;
    std::cout << "RTT matrix GL framebuffer=" << w->framebuffer
              << " stateTx=" << w->native[3][0]
              << " physicalTx=" << w->physical[12] << " RGB=" << int(p[0])
              << "," << int(p[1]) << "," << int(p[2])
              << " minFilter=" << filterWitness->minFilter
              << " mip1Width=" << filterWitness->levelWidth << '\n';
    const char *disabled = std::getenv("COIN_DONT_USE_FBO");
    const bool pbuffer = disabled && std::string(disabled) == "1";
    const bool ok = mode == "--gl-probe" || filterProbe ||
                    ((pbuffer ? w->framebuffer == 0 : w->framebuffer != 0) &&
                     w->native == childTransform &&
                     w->nativeThird == SbMatrix::identity() &&
                     w->physical[12] == (local ? .25f : 0.f) &&
                     (local ? p[1] > (local == 2 ? 150 : 200) && p[0] < 10
                            : p[0] > 200 && p[1] < 10) &&
                     parentWitness->native == transform &&
                     parentWitness->nativeThird == third &&
                     parentWitness->physical[12] == .25f &&
                     filterWitness->minFilter == GL_LINEAR &&
                     filterWitness->levelWidth == 0);
    root->unref();
    return ok ? 0 : 1;
  }
  if (mode.empty()) {
    CoinRenderAction capture(SbViewportRegion(64, 64));
    capture.apply(root);
    const bool ok = capture.getLastStatus() == CoinRenderAction::SUCCESS &&
                    w->captured == childTransform &&
                    w->capturedThird == SbMatrix::identity() &&
                    parentWitness->captured == transform &&
                    parentWitness->capturedThird == third;
    root->unref();
    return ok ? 0 : 1;
  }
  const bool direct = mode == "--direct", gpu = mode == "--gpu" || direct;
  for (int backend = direct ? 1 : 0; backend < (gpu ? 2 : 1); ++backend) {
    CoinRenderOptions options;
    if (direct)
      options.sceneTexture = COIN_RENDER_SCENE_TEXTURE_DIRECT;
    std::unique_ptr<CoinRenderTarget> target(
        CoinRenderTarget::createOffscreen(SbVec2i32(64, 64), options));
    if (!backend)
      target->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);
    CoinRenderAction action(SbViewportRegion(64, 64));
    action.setRenderTarget(target.get());
    action.setTransparencyType(CoinRenderAction::BLEND);
    action.apply(root);
    std::vector<uint8_t> out;
    target->readbackRGBA(out);
    bool ok = action.getLastStatus() == CoinRenderAction::SUCCESS &&
              out.size() == 64 * 64 * 4 && w->captured == childTransform &&
              w->capturedThird == SbMatrix::identity() &&
              parentWitness->captured == transform &&
              parentWitness->capturedThird == third &&
              (local ? out[(32 * 64 + 32) * 4 + 1] > (local == 2 ? 150 : 200) &&
                           out[(32 * 64 + 32) * 4] < 10
                     : out[(32 * 64 + 32) * 4] > 200 &&
                           out[(32 * 64 + 32) * 4 + 1] < 10);
    std::cout << "RTT matrix backend=" << backend
              << " status=" << action.getLastStatus()
              << " Tx=" << w->captured[3][0]
              << " error=" << action.getLastError().getString() << '\n';
    action.setRenderTarget(nullptr);
    if (!ok)
      return 1;
  }
  root->unref();
  return 0;
}

int main(int argc, char **argv) {
  SoDB::init();
  CoinRenderAction::initClass();
  MatrixWitness::initClass();
  TextureWitness::initClass();
  RttViewport::initClass();
  const std::string mode = argc > 1 ? argv[1] : "";
  if (mode == "--gl" && !std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE"))
    return 77;
  for (float quality : {.1f, .3f, .5f})
    for (int local : {0, 1, 2})
      if (runCase(local, quality, mode, argc > 2 && std::string(argv[2]) == "--external"))
        return 1;
  return 0;
}
