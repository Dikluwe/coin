#include "CoinRenderPbrCadTools.h"
#include <Inventor/SoDB.h>
#include <Inventor/SoInteraction.h>
#include <cstdio>
#define CHECK(x,m) do{if(!(x)){std::fprintf(stderr,"FAIL: %s (%d)\n",m,__LINE__);return 1;}}while(0)
static int edgeCount(SoNode * source) {
  SoSeparator * mesh=coin_pbr_feature_edges(source,SbViewportRegion(64,64));mesh->ref();
  auto * lines=static_cast<SoIndexedLineSet *>(mesh->getChild(1));const int count=lines->coordIndex.getNum()/3;mesh->unref();return count;
}
int main() {
  SoDB::init();SoInteraction::init();SoPBRMaterial::initClass();
  auto * cube=new SoSeparator;cube->ref();coin_pbr_fixture::box(cube,"cube",SbVec3f(3,2,1),SbVec3f(2,2,2),SbColor(.5f,.5f,.5f));
  CHECK(edgeCount(cube)==12,"cube exposes twelve geometric edges, without triangle diagonals");
  auto * ring=new SoSeparator;ring->ref();coin_pbr_fixture::ring(ring,"ring",SbVec3f(0,0,0),.5f,2,1,.1f,SbColor(.5f,.5f,.5f),1,.3f);
  CHECK(edgeCount(ring)==512,"beveled ring retains eight profile creases and suppresses coplanar/smooth tessellation edges");
  auto * plane=new SoSeparator;plane->ref();auto * coords=new SoCoordinate3;const SbVec3f xy[]={SbVec3f(-1,-1,0),SbVec3f(1,-1,0),SbVec3f(1,1,0),SbVec3f(-1,1,0)};coords->point.setValues(0,4,xy);plane->addChild(coords);
  auto * faces=new SoIndexedFaceSet;const int32_t indices[]={0,1,2,3,-1};faces->coordIndex.setValues(0,5,indices);plane->addChild(faces);
  CHECK(edgeCount(plane)==4,"planar quad has four boundary edges");
  CoinRenderPbrCadMetrics metrics;CHECK(coin_pbr_convert_import(cube,.8f,.4f,metrics)&&metrics.convertedMaterials==1,"explicit import conversion replaces one material");
  auto * object=static_cast<SoSeparator *>(cube->getChild(0));auto * pbr=static_cast<SoPBRMaterial *>(object->getChild(0));
  CHECK(pbr->isOfType(SoPBRMaterial::getClassTypeId())&&std::abs(pbr->baseColor.getValue()[0]-.21404114f)<1e-6,"display diffuse is explicitly decoded to linear");
  CHECK(pbr->metallic.getValue()==.8f&&pbr->roughness.getValue()==.4f&&edgeCount(cube)==12,"converted factors preserve geometric identity");
  auto * bad=new SoSeparator;bad->ref();auto * transparent=new SoMaterial;transparent->transparency=.1f;bad->addChild(transparent);bad->addChild(new SoCube);
  CHECK(!coin_pbr_convert_import(bad,.8f,.4f,metrics)&&bad->getChild(0)==transparent,"transparent import is refused without partial replacement");
  auto * root=new SoSeparator;root->ref();SbVec3f center;const SbVec2i32 size(800,600);auto * camera=coin_pbr_fixture::camera(cube,"iso-perspective",size,center);root->addChild(camera);
  auto * selection=new SoSelection;selection->addChild(cube);root->addChild(selection);
  std::printf("object name=%s\n",object->getName().getString());
  CHECK(coin_pbr_add_cad_tools(root,selection,cube,size,metrics),"CAD tools select a geometric face through native Coin picking");
  std::printf("selected count=%d name=%s diagonal=%g\n",selection->getNumSelected(),metrics.selectedName.getString(),metrics.selectedDiagonal);
  CHECK(selection->getNumSelected()==1&&metrics.selectedName=="cube"&&std::abs(metrics.selectedDiagonal-std::sqrt(12.f))<1e-5,"selection and diagonal correspond to transformed native geometry");
  root->unref();bad->unref();plane->unref();ring->unref();cube->unref();
  std::printf("PBR CAD tools: native feature edges, chamfer preservation, explicit import, selection and measure PASS\n");return 0;
}
