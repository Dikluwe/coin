#include <Inventor/SoDB.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/SbBox3f.h>
#include <Inventor/SbViewportRegion.h>
#include <Inventor/actions/SoGetBoundingBoxAction.h>
#include <Inventor/nodes/SoArray.h>
#include <Inventor/nodes/SoBaseColor.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>

#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <sys/resource.h>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Scene {
  SoSeparator * root;
  int physicalArrays;
};

double
secondsSince(const Clock::time_point & start)
{
  return std::chrono::duration<double>(Clock::now() - start).count();
}

long
maxRssKiB()
{
  struct rusage usage;
  return getrusage(RUSAGE_SELF, &usage) == 0 ? usage.ru_maxrss : -1;
}

bool
parseSide(const char * text, int & side)
{
  errno = 0;
  char * end = NULL;
  const long value = std::strtol(text, &end, 10);
  if (errno || end == text || *end != '\0' || value <= 0 ||
      value > std::numeric_limits<int>::max()) {
    return false;
  }
  side = static_cast<int>(value);
  return true;
}

std::vector<int>
factorsFor(const int count)
{
  if (count <= 30000) return std::vector<int>(1, count);
  if ((count % 1000) == 0 && (count / 1000) <= 30000) {
    std::vector<int> factors;
    factors.push_back(1000);
    factors.push_back(count / 1000);
    return factors;
  }
  std::fprintf(stderr, "side=%d cannot be represented by safe SoSFShort factors\n", count);
  std::exit(2);
}

SoSeparator *
makeHexagonalRing()
{
  const float outerRadius = 1.0f;
  const float innerRadius = 0.82f;
  const float pi = 3.14159265358979323846f;

  SoSeparator * tile = new SoSeparator;
  tile->renderCaching = SoSeparator::ON;
  tile->boundingBoxCaching = SoSeparator::ON;

  SoCoordinate3 * coordinates = new SoCoordinate3;
  coordinates->point.setNum(12);
  for (int i = 0; i < 6; ++i) {
    const float angle = pi * float(i) / 3.0f;
    coordinates->point.set1Value(i,
      SbVec3f(outerRadius * std::cos(angle), outerRadius * std::sin(angle), 0.0f));
    coordinates->point.set1Value(6 + i,
      SbVec3f(innerRadius * std::cos(angle), innerRadius * std::sin(angle), 0.0f));
  }

  SoIndexedFaceSet * faces = new SoIndexedFaceSet;
  faces->coordIndex.setNum(30);
  int cursor = 0;
  for (int i = 0; i < 6; ++i) {
    const int next = (i + 1) % 6;
    faces->coordIndex.set1Value(cursor++, i);
    faces->coordIndex.set1Value(cursor++, next);
    faces->coordIndex.set1Value(cursor++, 6 + next);
    faces->coordIndex.set1Value(cursor++, 6 + i);
    faces->coordIndex.set1Value(cursor++, SO_END_FACE_INDEX);
  }

  tile->addChild(coordinates);
  tile->addChild(faces);
  return tile;
}

SoNode *
wrapArrays(SoNode * child,
           const std::vector<int> & factors,
           const SbVec3f & basis,
           int & physicalArrays)
{
  int64_t stride = 1;
  for (std::vector<int>::const_iterator it = factors.begin();
       it != factors.end(); ++it) {
    SoArray * array = new SoArray;
    array->origin = SoArray::FIRST;
    array->numElements1 = static_cast<short>(*it);
    array->numElements2 = 1;
    array->numElements3 = 1;
    array->separation1 = basis * static_cast<float>(stride);
    array->addChild(child);
    child = array;
    stride *= *it;
    ++physicalArrays;
  }
  return child;
}

Scene
makeScene(const int side)
{
  const SbVec3f xBasis(2.10f, 0.0f, 0.0f);
  const SbVec3f yBasis(1.05f, 1.82f, 0.0f);
  const std::vector<int> factors = factorsFor(side);

  int physicalArrays = 0;
  SoNode * instances = makeHexagonalRing();
  instances = wrapArrays(instances, factors, xBasis, physicalArrays);
  instances = wrapArrays(instances, factors, yBasis, physicalArrays);

  SoSeparator * root = new SoSeparator;
  root->ref();
  root->renderCaching = SoSeparator::OFF;
  root->boundingBoxCaching = SoSeparator::OFF;

  SoOrthographicCamera * camera = new SoOrthographicCamera;
  const float width = 3.15f * static_cast<float>(side);
  const float height = 1.82f * static_cast<float>(side);
  camera->position = SbVec3f(width * 0.5f, height * 0.5f, 10.0f);
  camera->height = width;
  root->addChild(camera);

  SoLightModel * lightModel = new SoLightModel;
  lightModel->model = SoLightModel::BASE_COLOR;
  root->addChild(lightModel);

  SoBaseColor * color = new SoBaseColor;
  color->rgb = SbColor(0.82f, 0.58f, 0.12f);
  root->addChild(color);
  root->addChild(instances);

  Scene scene = { root, physicalArrays };
  return scene;
}

int
runConstruct(const int side)
{
  const Clock::time_point start = Clock::now();
  Scene scene = makeScene(side);
  const double elapsed = secondsSince(start);
  const uint64_t holes = uint64_t(side) * uint64_t(side);
  std::printf("mode=construct side=%d holes=%llu arrays=%d seconds=%.6f max_rss_kib=%ld\n",
              side, static_cast<unsigned long long>(holes), scene.physicalArrays,
              elapsed, maxRssKiB());
  scene.root->unref();
  return 0;
}

int
runBoundingBox(const int side)
{
  Scene scene = makeScene(side);
  SoGetBoundingBoxAction action(SbViewportRegion(256, 256));
  const Clock::time_point start = Clock::now();
  action.apply(scene.root);
  const double elapsed = secondsSince(start);
  const SbBox3f box = action.getBoundingBox();
  const SbVec3f min = box.getMin();
  const SbVec3f max = box.getMax();
  std::printf("mode=bbox side=%d seconds=%.6f max_rss_kib=%ld "
              "min=(%.3f,%.3f,%.3f) max=(%.3f,%.3f,%.3f)\n",
              side, elapsed, maxRssKiB(), min[0], min[1], min[2],
              max[0], max[1], max[2]);
  scene.root->unref();
  return 0;
}

int
runRender(const int side)
{
  Scene scene = makeScene(side);
  SoOffscreenRenderer renderer(SbViewportRegion(256, 256));
  renderer.setComponents(SoOffscreenRenderer::RGB);
  const Clock::time_point start = Clock::now();
  const SbBool rendered = renderer.render(scene.root);
  const double elapsed = secondsSince(start);
  if (rendered) (void) renderer.getBuffer();
  std::printf("mode=render side=%d rendered=%d seconds=%.6f max_rss_kib=%ld\n",
              side, rendered, elapsed, maxRssKiB());
  scene.root->unref();
  return rendered ? 0 : 1;
}

} // namespace

int
main(int argc, char ** argv)
{
  if (argc != 3) {
    std::fprintf(stderr, "usage: %s construct|bbox|render side\n", argv[0]);
    return 2;
  }
  int side = 0;
  if (!parseSide(argv[2], side)) {
    std::fprintf(stderr, "invalid side: %s\n", argv[2]);
    return 2;
  }

  SoDB::init();
  const std::string mode(argv[1]);
  int result = 2;
  if (mode == "construct") result = runConstruct(side);
  else if (mode == "bbox") result = runBoundingBox(side);
  else if (mode == "render") result = runRender(side);
  else std::fprintf(stderr, "unknown mode: %s\n", mode.c_str());
  SoDB::finish();
  return result;
}
