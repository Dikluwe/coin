#include <Inventor/SoDB.h>
#include <Inventor/SoInput.h>
#include <Inventor/SbMatrix.h>
#include <Inventor/SbViewVolume.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/SbViewportRegion.h>
#include <iostream>
#include <iomanip>

int main(int argc, char **argv) {
  if (argc != 2) return 2;
  SoDB::init();
  SoInput input;
  if (!input.openFile(argv[1])) return 2;
  SoSeparator *imported = SoDB::readAll(&input);
  if (!imported) return 2;
  SoSeparator *root = new SoSeparator;
  root->ref();
  SoPerspectiveCamera *camera = new SoPerspectiveCamera;
  camera->orientation.setValue(SbRotation(SbVec3f(0,0,-1),SbVec3f(-.5f,-.35f,-1)));
  root->addChild(camera);
  SoLightModel *model = new SoLightModel;
  model->model = SoLightModel::BASE_COLOR;
  root->addChild(model);
  root->addChild(imported);
  camera->viewAll(root,SbViewportRegion(128,128),1.15f);
  const SbMatrix matrix = camera->getViewVolume(1.0f).getMatrix();
  std::cout << std::setprecision(12);
  std::cout << "camera " << camera->position.getValue()[0] << ' ' << camera->position.getValue()[1] << ' ' << camera->position.getValue()[2] << '\n';
  for (int i : {0,24,30,39}) {
    SoSeparator *quad = static_cast<SoSeparator*>(imported->getChild(i));
    SoCoordinate3 *coords = static_cast<SoCoordinate3*>(quad->getChild(1));
    for (int v=0;v<4;v++) {
      SbVec3f p=coords->point[v];
      SbVec4f projected;
      matrix.multVecMatrix(SbVec4f(p[0],p[1],p[2],1),projected);
      const double x=(double(projected[0])/projected[3]+1)*64;
      const double y=(1-double(projected[1])/projected[3])*64;
      std::cout << i << ' ' << v << ' ' << x << ' ' << y << ' ' << projected[3] << '\n';
    }
  }
  root->unref();
}
