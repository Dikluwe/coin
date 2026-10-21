#ifndef COIN_RENDER_TERRAIN_FIXTURE_H
#define COIN_RENDER_TERRAIN_FIXTURE_H

#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <cmath>
#include <vector>

// Same authored coordinates, bands, palette, lights and camera as the archived
// linux-visual-20261010 terrain-million fixture. Ownership belongs to the root.
inline SoSeparator* coinRenderTerrainFixture(SoOrthographicCamera*& camera) {
  auto* root=new SoSeparator;
  camera=new SoOrthographicCamera;
  camera->height=90; camera->nearDistance=.1f; camera->farDistance=600;
  camera->position=SbVec3f(180*std::sin(.65f),120,180*std::cos(.65f));
  camera->pointAt(SbVec3f(0,0,0)); root->addChild(camera);
  auto* model=new SoLightModel; model->model=SoLightModel::PHONG; root->addChild(model);
  auto* sun=new SoDirectionalLight; sun->direction=SbVec3f(-.5f,-1,-.3f);
  sun->intensity=.85f; root->addChild(sun);
  auto* fill=new SoDirectionalLight; fill->direction=SbVec3f(.6f,-.3f,.7f);
  fill->intensity=.35f; root->addChild(fill);
  auto* coord=new SoCoordinate3;
  std::vector<SbVec3f> points; points.reserve(1001*501);
  for(int z=0;z<=500;++z) for(int x=0;x<=1000;++x) {
    float px=(x-500)*.09f,pz=(z-250)*.14f;
    float y=5*std::sin(px*.13f)*std::cos(pz*.11f)+2*std::sin((px+pz)*.3f);
    points.emplace_back(px,y,pz);
  }
  coord->point.setValues(0,int(points.size()),points.data()); root->addChild(coord);
  const SbColor palette[]={SbColor(.12f,.42f,.70f),SbColor(.16f,.66f,.72f),
    SbColor(.92f,.48f,.16f),SbColor(.76f,.22f,.22f),SbColor(.45f,.25f,.70f),
    SbColor(.72f,.72f,.77f),SbColor(.22f,.55f,.29f),SbColor(.85f,.68f,.24f)};
  for(int band=0;band<20;++band) {
    auto* sep=new SoSeparator; auto* material=new SoMaterial;
    material->diffuseColor=palette[band%8]; sep->addChild(material);
    std::vector<int32_t> indices; indices.reserve(25000*5);
    for(int z=0;z<500;++z) for(int x=band*50;x<(band+1)*50;++x) {
      int a=z*1001+x; indices.insert(indices.end(),{a,a+1001,a+1002,a+1,-1});
    }
    auto* faces=new SoIndexedFaceSet;
    faces->coordIndex.setValues(0,int(indices.size()),indices.data());
    sep->addChild(faces); root->addChild(sep);
  }
  return root;
}
#endif
