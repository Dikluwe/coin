#include <Inventor/SoDB.h>
#include <Inventor/annex/Profiler/SoProfiler.h>
#include <Inventor/annex/Profiler/nodekits/SoNodeVisualize.h>
#include <Inventor/nodekits/SoShapeKit.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoSwitch.h>
#include <Inventor/nodes/SoTransform.h>

int main(void)
{
  SoDB::init();
  SoProfiler::init();

  SoNode * nodes[] = {
    new SoMaterial,
    new SoCube,
    new SoSeparator,
    new SoShapeKit,
    new SoSwitch,
    new SoTransform
  };
  for (int i = 0; i < 6; ++i) nodes[i]->ref();
  for (int repeat = 0; repeat < 2; ++repeat) {
    for (int i = 0; i < 6; ++i) {
      SoNodeVisualize * visualizer = SoNodeVisualize::visualizeTree(nodes[i], 0);
      visualizer->ref();
      visualizer->unref();
    }
  }
  for (int i = 0; i < 6; ++i) nodes[i]->unref();
  SoNodeVisualize::cleanClass();
  return 0;
}
