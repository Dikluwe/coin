#ifndef COIN_RENDER_PBR_CAD_TOOLS_H
#define COIN_RENDER_PBR_CAD_TOOLS_H
#include "CoinRenderPbrFixtures.h"
#include <Inventor/SoPickedPoint.h>
#include <Inventor/SoFullPath.h>
#include <Inventor/SoPrimitiveVertex.h>
#include <Inventor/SbBSPTree.h>
#include <algorithm>
#include <Inventor/actions/SoRayPickAction.h>
#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/elements/SoClipPlaneElement.h>
#include <Inventor/actions/SoSearchAction.h>
#include <Inventor/nodes/SoAnnotation.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoDrawStyle.h>
#include <Inventor/nodes/SoFont.h>
#include <Inventor/nodes/SoIndexedLineSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoPickStyle.h>
#include <Inventor/nodes/SoPolygonOffset.h>
#include <Inventor/nodes/SoSelection.h>
#include <Inventor/nodes/SoText3.h>
#include <Inventor/rendering/SoPBRMaterial.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <vector> // Public readback contract.
struct CoinRenderPbrCadMetrics {
  int convertedMaterials=0, depthProbes=0, depthEdges=0;
  SbString selectedName;
  SbVec3f selectedPoint, selectedNormal;
  float selectedDiagonal=0;
};
inline bool coin_pbr_convert_import(SoSeparator * geometry,float metallic,float roughness,
  CoinRenderPbrCadMetrics & metrics) {
  SoSearchAction search;search.setType(SoMaterial::getClassTypeId());search.setInterest(SoSearchAction::ALL);
  search.setSearchingAll(TRUE);search.apply(geometry);
  struct Entry {SoGroup * parent;SoMaterial * material;};SbList<Entry> entries;
  for(int i=0;i<search.getPaths().getLength();++i) {
    SoPath * path=search.getPaths()[i];if(path->getLength()<2)return false;
    auto * material=static_cast<SoMaterial *>(path->getTail());
    if(material->diffuseColor.getNum()!=1||material->transparency.getNum()!=1||material->transparency[0]!=0||
       material->emissiveColor.getNum()!=1)return false;
    auto * parent=path->getNode(path->getLength()-2);if(!parent->isOfType(SoGroup::getClassTypeId()))return false;
    entries.append({static_cast<SoGroup *>(parent),material});
  }
  if(!entries.getLength())return false;
  for(int i=0;i<entries.getLength();++i) {
    const auto & entry=entries[i];const int child=entry.parent->findChild(entry.material);if(child<0)continue;
    auto * pbr=new SoPBRMaterial;SbColor color=entry.material->diffuseColor[0];
    // Explicit viewer policy: imported diffuse values are display sRGB.
    for(int c=0;c<3;++c)color[c]=color[c]<=.04045f?color[c]/12.92f:std::pow((color[c]+.055f)/1.055f,2.4f);
    pbr->baseColor=color;pbr->metallic=metallic;pbr->roughness=roughness;
    pbr->emissive=entry.material->emissiveColor[0];pbr->setName(entry.material->getName());
    pbr->setOverride(entry.material->isOverride());entry.parent->replaceChild(child,pbr);++metrics.convertedMaterials;
  }
  return true;
}
inline SoAnnotation * coin_pbr_annotation(const SbColor & color,SbBool depthTest=FALSE) {
  auto * a=new SoAnnotation;auto * unpickable=new SoPickStyle;unpickable->style=SoPickStyle::UNPICKABLE;a->addChild(unpickable);
  auto * light=new SoLightModel;light->model=SoLightModel::BASE_COLOR;light->setOverride(TRUE);a->addChild(light);
  auto * material=new SoMaterial;material->diffuseColor=color;material->transparency=0;material->setOverride(TRUE);a->addChild(material);
  auto * depth=new SoDepthBuffer;depth->test=depthTest;depth->write=FALSE;depth->function=SoDepthBuffer::LEQUAL;a->addChild(depth);
  return a;
}
struct CoinRenderPbrCadEdges {
  struct Edge {int a,b;SbVec3f normal;};
  SbBSPTree points;SbList<Edge> edges;float epsilon;
  explicit CoinRenderPbrCadEdges(float e):epsilon(e){}
};
inline void coin_pbr_edge_triangle(void * data,SoCallbackAction * action,const SoPrimitiveVertex * a,
  const SoPrimitiveVertex * b,const SoPrimitiveVertex * c) {
  auto & mesh=*static_cast<CoinRenderPbrCadEdges *>(data);const SoPrimitiveVertex * vertices[]={a,b,c};
  SbVec3f positions[3];int ids[3];
  for(int i=0;i<3;++i) {
    action->getModelMatrix().multVecMatrix(vertices[i]->getPoint(),positions[i]);
    for(int j=0;j<3;++j)positions[i][j]=std::round(positions[i][j]/mesh.epsilon)*mesh.epsilon;
    ids[i]=mesh.points.addPoint(positions[i]);
  }
  SbVec3f normal=(positions[1]-positions[0]).cross(positions[2]-positions[0]);if(normal.normalize()==0)return;
  for(int i=0;i<3;++i)if(ids[i]!=ids[(i+1)%3])mesh.edges.append({std::min(ids[i],ids[(i+1)%3]),std::max(ids[i],ids[(i+1)%3]),normal});
}
inline SoSeparator * coin_pbr_feature_edges(SoNode * geometry,const SbViewportRegion & viewport) {
  SoGetBoundingBoxAction bounds(viewport);bounds.apply(geometry);
  CoinRenderPbrCadEdges mesh(std::max(1e-6f,bounds.getBoundingBox().getSize().length()*1e-7f));
  SoCallbackAction action;action.addTriangleCallback(SoShape::getClassTypeId(),coin_pbr_edge_triangle,&mesh);action.apply(geometry);
  if(mesh.edges.getLength()>1)std::sort(&mesh.edges[0],&mesh.edges[0]+mesh.edges.getLength(),
    [](const CoinRenderPbrCadEdges::Edge & a,const CoinRenderPbrCadEdges::Edge & b){return a.a<b.a||(a.a==b.a&&a.b<b.b);});
  SbList<int32_t> indices;
  for(int i=0;i<mesh.edges.getLength();) {
    const auto edge=mesh.edges[i];int end=i+1;bool sharp=false;
    while(end<mesh.edges.getLength()&&mesh.edges[end].a==edge.a&&mesh.edges[end].b==edge.b) {
      if(edge.normal.dot(mesh.edges[end].normal)<.9396926f)sharp=true;++end;
    }
    if(sharp||end==i+1){indices.append(edge.a);indices.append(edge.b);indices.append(-1);}i=end;
  }
  auto * result=new SoSeparator;auto * coords=new SoCoordinate3;
  coords->point.setValues(0,mesh.points.numPoints(),mesh.points.getPointsArrayPtr());result->addChild(coords);
  auto * lines=new SoIndexedLineSet;lines->coordIndex.setValues(0,indices.getLength(),indices.getArrayPtr());result->addChild(lines);
  return result;
}
inline bool coin_pbr_add_cad_tools(SoSeparator * root,SoSelection * selection,SoSeparator * geometry,
  const SbVec2i32 & size,CoinRenderPbrCadMetrics & metrics,SbBool clipped=FALSE) {
  const SbViewportRegion viewport(size[0],size[1]);SoRayPickAction pick(viewport);pick.setRadius(0);
  SoPath * chosen=nullptr;
  for(float y:{.6f,.4f,.7f,.3f,.5f})for(float x:{.6f,.4f,.7f,.3f,.5f}) {
    if(chosen)break;pick.setNormalizedPoint(SbVec2f(x,y));pick.apply(root);
    if(const auto * hit=pick.getPickedPoint()) {
      chosen=hit->getPath()->copy();chosen->ref();metrics.selectedPoint=hit->getPoint();metrics.selectedNormal=hit->getNormal();
      const auto * full=static_cast<const SoFullPath *>(hit->getPath());
      for(int i=0;i<full->getLength();++i)if(full->getNodeFromTail(i)->getName().getLength()) {
        metrics.selectedName=full->getNodeFromTail(i)->getName().getString();break;
      }
    }
  }
  if(!chosen)return false;
  selection->select(chosen);SoGetBoundingBoxAction bounds(viewport);bounds.apply(chosen);
  const SbBox3f box=bounds.getBoundingBox();chosen->unref();if(box.isEmpty())return false;
  const SbVec3f extent=box.getMax()-box.getMin();metrics.selectedDiagonal=extent.length();
  auto * edges=coin_pbr_annotation(SbColor(.08f,.12f,.16f),TRUE);
  auto * style=new SoDrawStyle;style->lineWidth=1;edges->addChild(style);
  if(clipped){auto * plane=new SoClipPlane;plane->plane=SbPlane(SbVec3f(1,0,0),0);edges->addChild(plane);}
  auto * offset=new SoPolygonOffset;offset->factor=-1;offset->units=-1;offset->styles=SoPolygonOffset::LINES;edges->addChild(offset);
  edges->addChild(coin_pbr_feature_edges(geometry,viewport));root->addChild(edges);
  auto * highlight=coin_pbr_annotation(SbColor(1,.7f,.1f));
  auto * width=new SoDrawStyle;width->lineWidth=2;highlight->addChild(width);
  SbVec3f corners[8];for(int i=0;i<8;++i)corners[i].setValue((i&1)?box.getMax()[0]:box.getMin()[0],
    (i&2)?box.getMax()[1]:box.getMin()[1],(i&4)?box.getMax()[2]:box.getMin()[2]);
  auto * coordinates=new SoCoordinate3;coordinates->point.setValues(0,8,corners);highlight->addChild(coordinates);
  const int32_t indices[]={0,1,-1,2,3,-1,4,5,-1,6,7,-1,0,2,-1,1,3,-1,4,6,-1,5,7,-1,0,4,-1,1,5,-1,2,6,-1,3,7,-1,0,7,-1};
  auto * lines=new SoIndexedLineSet;lines->coordIndex.setValues(0,39,indices);highlight->addChild(lines);root->addChild(highlight);
  auto * label=coin_pbr_annotation(SbColor(1,.95f,.7f));auto * move=new SoTranslation;move->translation=box.getCenter()+SbVec3f(0,0,extent.length()*.1f);label->addChild(move);
  auto * font=new SoFont;font->size=extent.length()*.025f;label->addChild(font);
  SbString text;text.sprintf("diag %.3f fixture-unit",double(metrics.selectedDiagonal));auto * glyphs=new SoText3;glyphs->justification=SoText3::CENTER;glyphs->string=text;label->addChild(glyphs);root->addChild(label);
  return true;
}
struct CoinRenderPbrCadTriangle {
  SbVec3f a,b,c;SbList<SbPlane> planes;bool cullBack=false;
};
inline void coin_pbr_oracle_triangle(void * data,SoCallbackAction * action,const SoPrimitiveVertex * a,
  const SoPrimitiveVertex * b,const SoPrimitiveVertex * c) {
  if(action->getPickStyle()==SoPickStyle::UNPICKABLE)return;
  CoinRenderPbrCadTriangle tri;const auto & matrix=action->getModelMatrix();
  matrix.multVecMatrix(a->getPoint(),tri.a);matrix.multVecMatrix(b->getPoint(),tri.b);matrix.multVecMatrix(c->getPoint(),tri.c);
  tri.cullBack=action->getShapeType()==SoShapeHints::SOLID&&action->getVertexOrdering()!=SoShapeHints::UNKNOWN_ORDERING;
  if(action->getVertexOrdering()==SoShapeHints::CLOCKWISE)std::swap(tri.b,tri.c);
  const auto * clipping=SoClipPlaneElement::getInstance(action->getState());
  for(int i=0;i<clipping->getNum();++i)tri.planes.append(clipping->get(i,TRUE));
  static_cast<SbList<CoinRenderPbrCadTriangle> *>(data)->append(tri);
}
inline bool coin_pbr_mesh_ray(const SbList<CoinRenderPbrCadTriangle> & triangles,const SbVec3f & begin,
  const SbVec3f & end,SbVec3f & nearest) {
  const SbVec3f direction=end-begin;float distance=1.000001f;bool found=false;
  for(int i=0;i<triangles.getLength();++i) {
    const auto & t=triangles.getArrayPtr()[i];const SbVec3f e1=t.b-t.a,e2=t.c-t.a;
    const SbVec3f p=direction.cross(e2);const double determinant=e1.dot(p);
    if(std::abs(determinant)<1e-12||(t.cullBack&&determinant<=0))continue;
    const double inverse=1/determinant;const SbVec3f delta=begin-t.a;
    const double u=delta.dot(p)*inverse;if(u<0||u>1)continue;
    const SbVec3f q=delta.cross(e1);const double v=direction.dot(q)*inverse;if(v<0||u+v>1)continue;
    const double ray=e2.dot(q)*inverse;if(ray<0||ray>distance)continue;
    const SbVec3f point=begin+direction*float(ray);bool clipped=false;
    for(int j=0;j<t.planes.getLength();++j)if(t.planes.getArrayPtr()[j].getDistance(point)<-1e-6f)clipped=true;
    if(clipped)continue;nearest=point;distance=float(ray);found=true;
  }
  return found;
}
inline bool coin_pbr_depth_oracle(SoNode * root,SoCamera * camera,CoinRenderTarget * target,
  const SbVec2i32 & size,CoinRenderPbrCadMetrics & metrics) {
  std::vector<float> depth;target->readbackDepth(depth);if(depth.size()!=size_t(size[0])*size[1])return false;
  metrics.depthProbes=metrics.depthEdges=0;const auto volume=camera->getViewVolume(float(size[0])/size[1]);
  SbList<CoinRenderPbrCadTriangle> triangles;SoCallbackAction capture;
  capture.addTriangleCallback(SoShape::getClassTypeId(),coin_pbr_oracle_triangle,&triangles);capture.apply(root);
  for(int gy=0;gy<12;++gy)for(int gx=0;gx<16;++gx) {
    const int x=(gx*2+1)*size[0]/32,y=(gy*2+1)*size[1]/24;
    const bool covered=depth[y*size[0]+x]<.999999f;bool edge=false;
    for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx) {
      const int xx=std::max(0,std::min(size[0]-1,x+dx)),yy=std::max(0,std::min(size[1]-1,y+dy));
      if((depth[yy*size[0]+xx]<.999999f)!=covered)edge=true;
    }
    if(edge){++metrics.depthEdges;continue;}
    // Public readback is top-to-bottom; Coin projection y is bottom-up.
    SbVec3f begin,end,point;volume.projectPointToLine(SbVec2f((x+.5f)/size[0],1.f-(y+.5f)/size[1]),begin,end);
    const bool hit=coin_pbr_mesh_ray(triangles,begin,end,point);
    if(hit!=covered) {
      std::fprintf(stderr,"CAD coverage oracle mismatch (%d,%d) mesh-ray=%d depth=%g\n",x,y,hit,depth[y*size[0]+x]);return false;
    }
    if(hit) {SbVec3f projected;volume.projectToScreen(point,projected);
      if(std::abs(projected[2]-depth[y*size[0]+x])>.002f) {
        std::fprintf(stderr,"CAD depth oracle mismatch (%d,%d) mesh-ray=%g depth=%g\n",x,y,projected[2],depth[y*size[0]+x]);return false;
      }
    }
    ++metrics.depthProbes;
  }
  return metrics.depthProbes>100;
}
#endif
