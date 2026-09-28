#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/nodes/SoDrawStyle.h>
#include <Inventor/nodes/SoPolygonOffset.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoEnvironment.h>
#include <Inventor/nodes/SoTexture2Transform.h>
#include "rendering/coinrender/CoinRenderFrameReuseCore.h"
#include "rendering/coinrender/CoinRenderStateCore.h"
#include "rendering/coinrender/CoinRenderStrokeCore.h"
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoGroup.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoShape.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoIndexedLineSet.h>
#include <Inventor/nodes/SoLineSet.h>
#include <Inventor/nodes/SoPointSet.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoRotation.h>
#include <Inventor/nodes/SoSphere.h>
#include <Inventor/nodes/SoCylinder.h>
#include <Inventor/nodes/SoCone.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/SbViewVolume.h>
#include "rendering/coinrender/CoinRenderPolygonStyleCore.h"
#include <algorithm>
#include <cmath>
#include <Inventor/nodes/SoClipPlane.h>
#include <Inventor/nodes/SoTextureUnit.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include "rendering/coinrender/CoinRenderFramePlanBuilder.h"
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <cstdlib>
#include <iostream>
#include <memory>

namespace {
bool check(bool condition, const char * message) {
  if (!condition) std::cerr << "CoinRenderDrawStyleTest: " << message << '\n';
  return condition;
}
void triangle(void * p, SoCallbackAction * a, const SoPrimitiveVertex * x,
              const SoPrimitiveVertex * y, const SoPrimitiveVertex * z) {
  static_cast<CoinRenderFramePlanBuilder *>(p)->addTriangle(a,x,y,z);
}
void line(void * p, SoCallbackAction * a, const SoPrimitiveVertex * x, const SoPrimitiveVertex * y) {
  static_cast<CoinRenderFramePlanBuilder *>(p)->addLine(a,x,y);
}
void point(void * p, SoCallbackAction * a, const SoPrimitiveVertex * x) {
  static_cast<CoinRenderFramePlanBuilder *>(p)->addPoint(a,x);
}
SoDrawStyle * style(int value, bool overrideState = false) {
  SoDrawStyle * node = new SoDrawStyle;
  node->style = value; node->setOverride(overrideState ? TRUE : FALSE);
  return node;
}
SoCoordinate3 * coordinates() {
  SoCoordinate3 * node = new SoCoordinate3;
  const SbVec3f points[] = {SbVec3f(-.8f,-.8f,0), SbVec3f(.8f,-.8f,0),
                          SbVec3f(.8f,.8f,0), SbVec3f(-.8f,.8f,0)};
  node->point.setValues(0,4,points);
  return node;
}
SoIndexedFaceSet * faces() {
  SoIndexedFaceSet * node = new SoIndexedFaceSet;
  const int32_t indices[] = {0,1,2,3,-1}; node->coordIndex.setValues(0,5,indices);
  return node;
}
bool capture(SoNode * root, CoinRenderFramePlan & frame) {
  CoinRenderFramePlanBuilder builder;
  builder.beginFrame(SbColor4f(0,0,0,1), SbViewportRegion(64,64));
  CoinRenderAction action(SbViewportRegion(64,64));
  action.addTriangleCallback(SoShape::getClassTypeId(),triangle,&builder);
  action.addLineSegmentCallback(SoShape::getClassTypeId(),line,&builder);
  action.addPointCallback(SoShape::getClassTypeId(),point,&builder);
  action.apply(root);
  std::string error;
  return builder.build(frame,&error);
}
bool stateContract() {
  SoSeparator * root = new SoSeparator; root->ref();
  root->addChild(new SoCube);
  SoSeparator * hidden = new SoSeparator; root->addChild(hidden);
  hidden->addChild(style(SoDrawStyle::INVISIBLE));
  // A hidden shape does not consume the unsupported clipping state.
  for (int i=0;i<9;++i) hidden->addChild(new SoClipPlane);
  hidden->addChild(new SoCube); hidden->addChild(coordinates());
  SoLineSet * lines = new SoLineSet; lines->numVertices=4; hidden->addChild(lines);
  SoPointSet * points = new SoPointSet; points->numPoints=4; hidden->addChild(points);
  hidden->addChild(faces());
  SoIndexedLineSet * indexedLines = new SoIndexedLineSet;
  const int32_t indices[]={0,1,2,3,-1};indexedLines->coordIndex.setValues(0,5,indices);
  hidden->addChild(indexedLines);
  root->addChild(new SoCube);
  CoinRenderFramePlan frame;
  bool ok=check(capture(root,frame) && frame.draws.size()==2 && frame.vertices.size()==72,
                "INVISIBLE suppresses polygon, line and point callbacks; Separator restores visibility");
  root->unref();

  root=new SoSeparator;root->ref();
  SoGroup * group=new SoGroup;root->addChild(group);
  group->addChild(style(SoDrawStyle::INVISIBLE));group->addChild(new SoCube);
  // State continues after the invisible shape and beyond a plain Group.
  group->addChild(style(SoDrawStyle::FILLED));
  SoMaterial * red=new SoMaterial;red->diffuseColor=SbColor(1,0,0);group->addChild(red);
  root->addChild(new SoCube);
  ok=check(capture(root,frame) && frame.draws.size()==1 && frame.materials.size()==1 &&
           frame.materials[0].diffuse[0]==1 && frame.materials[0].diffuse[1]==0,
           "invisible shape does not interrupt subsequent state traversal") && ok;
  root->unref();

  root=new SoSeparator;root->ref();
  root->addChild(style(SoDrawStyle::INVISIBLE,true));
  root->addChild(style(SoDrawStyle::FILLED));root->addChild(new SoCube);
  ok=check(capture(root,frame) && frame.draws.empty() && frame.vertices.empty() &&
           frame.materials.empty() && frame.renderStates.empty(),
           "effective invisible override emits no geometry or captured resources") && ok;
  root->unref();

  root=new SoSeparator;root->ref();root->addChild(style(SoDrawStyle::FILLED,true));
  root->addChild(style(SoDrawStyle::INVISIBLE));root->addChild(new SoCube);
  ok=check(capture(root,frame) && frame.draws.size()==1,"filled override keeps shape visible") && ok;
  root->unref();

  root=new SoSeparator;root->ref();SoDrawStyle * ignored=style(SoDrawStyle::INVISIBLE);
  ignored->style.setIgnored(TRUE);root->addChild(ignored);root->addChild(new SoCube);
  ok=check(capture(root,frame) && frame.draws.size()==1,"ignored style preserves inherited visibility") && ok;
  root->unref();return ok;
}
bool polygonCoreContract() {
  CoinRenderRenderStateSnapshot state;state.lightModel=CoinRenderLightModel::BASE_COLOR;
  state.cullMode=CoinRenderCullMode::NONE;
  std::vector<CoinRenderMaterialSnapshot> materials(2);
  materials[0].diffuse[3]=.2f;materials[1].diffuse[3]=.8f;
  std::vector<CoinRenderVertexSnapshot> ring(4);
  const float xy[4][2]={{-.8f,-.8f},{.8f,-.8f},{.8f,.8f},{-.8f,.8f}};
  for(int i=0;i<4;++i) {
    ring[i].position[0]=xy[i][0];ring[i].position[1]=xy[i][1];
    ring[i].materialSlot=i==1 || i==2 ? 1 : 0;ring[i].texcoord[0]=i==1 || i==2 ? 1 : 0;
  }
  CoinRenderLightingSnapshot lighting;
  CoinRenderPolygonStyleResult resolved;std::string error;
  bool ok=check(coin_render_prepare_polygon_style(ring,state,materials,lighting,
    CoinRenderPolygonStyle::LINES,resolved,error,CoinRenderViewportSnapshot{}) && resolved.vertices.size()==4 && resolved.indices.size()==8,
    "quad produces four original edges without a tessellation diagonal");
  state.clipPlanesWorld.push_back(SbPlane(SbVec3f(1,0,0),0));
  ok=check(coin_render_prepare_polygon_style(ring,state,materials,lighting,
    CoinRenderPolygonStyle::POINTS,resolved,error,CoinRenderViewportSnapshot{}) && resolved.vertices.size()==4 && resolved.indices.size()==4 &&
    resolved.state.clipPlanesWorld.empty(),"clipped quad has four points, including new cut corners") && ok;
  int cut=0;
  for(const auto & item:resolved.vertices)if(std::abs(item.vertex.position[0])<1e-6f) {
    ++cut;ok=check(std::abs(item.vertex.texcoord[0]-.5f)<1e-6f &&
      std::abs(item.material.diffuse[3]-.5f)<1e-6f,"clipping interpolates original UV and alpha") && ok;
  }
  ok=check(cut==2,"one continuous cut edge has exactly two corners") && ok;
  state.clipPlanesWorld.clear();state.cullMode=CoinRenderCullMode::BACK;
  std::reverse(ring.begin(),ring.end());
  ok=check(coin_render_prepare_polygon_style(ring,state,materials,lighting,CoinRenderPolygonStyle::LINES,
    resolved,error,CoinRenderViewportSnapshot{}) && resolved.vertices.empty(),"culling precedes line expansion") && ok;
  state.frontFace=CoinRenderFrontFace::CW;
  ok=check(coin_render_prepare_polygon_style(ring,state,materials,lighting,CoinRenderPolygonStyle::LINES,
    resolved,error,CoinRenderViewportSnapshot{}) && resolved.vertices.size()==4,"effective front-face winding is preserved") && ok;
  state.frontFace=CoinRenderFrontFace::CCW;state.model.setScale(SbVec3f(-1,1,1));
  ok=check(coin_render_prepare_polygon_style(ring,state,materials,lighting,CoinRenderPolygonStyle::LINES,
    resolved,error,CoinRenderViewportSnapshot{}) && resolved.vertices.size()==4,"mirrored model changes facing before stroke expansion") && ok;
  state.model=SbMatrix::identity();state.cullMode=CoinRenderCullMode::NONE;
  std::reverse(ring.begin(),ring.end());
  state.lightModel=CoinRenderLightModel::PHONG;lighting.ambientIntensity=0;
  CoinRenderLightSourceSnapshot light;light.type=CoinRenderLightType::POINT;light.position[0]=1;
  light.position[2]=1;lighting.lights.push_back(light);
  state.clipPlanesWorld.push_back(SbPlane(SbVec3f(1,0,0),0));
  const float expected=(coin_render_shade_vertex(materials[0],SbVec3f(ring[0].position),SbVec3f(0,0,1),lighting,state)[0]+
    coin_render_shade_vertex(materials[1],SbVec3f(ring[1].position),SbVec3f(0,0,1),lighting,state)[0])*.5f;
  ok=check(coin_render_prepare_polygon_style(ring,state,materials,lighting,CoinRenderPolygonStyle::LINES,
    resolved,error,CoinRenderViewportSnapshot{}),"PHONG polygon resolves before clipping") && ok;
  for(const auto & item:resolved.vertices)if(std::abs(item.vertex.position[0])<1e-6f && item.vertex.position[1]<0)
    ok=check(std::abs(item.material.diffuse[0]-expected)<1e-6f,"cut color interpolates already lit endpoints") && ok;
  state.clipPlanesWorld.clear();state.lightModel=CoinRenderLightModel::BASE_COLOR;
  ring[1].position[0]=1.5f;ring[2].position[0]=1.5f;
  ok=check(coin_render_prepare_polygon_style(ring,state,materials,lighting,CoinRenderPolygonStyle::LINES,
    resolved,error,CoinRenderViewportSnapshot{}) && resolved.vertices.size()==4,"view-volume clipping creates complete contour") && ok;
  for(const auto & item:resolved.vertices)ok=check(item.vertex.position[0]<=1.000001f,"viewport clipping bounds") && ok;
  SbViewVolume volume;volume.perspective(.785398163f,1,.1f,10);
  SbMatrix affine;volume.getMatrices(affine,state.projectionCoin);
  for(auto & vertex:ring){vertex.position[0]*=.2f;vertex.position[1]*=.2f;vertex.position[2]=-1;}
  ring[0].position[2]=.2f;
  ok=check(coin_render_prepare_polygon_style(ring,state,materials,lighting,CoinRenderPolygonStyle::LINES,
    resolved,error,CoinRenderViewportSnapshot{}) && !resolved.vertices.empty(),"polygon crossing eye/near plane survives homogeneous clipping") && ok;
  state=CoinRenderRenderStateSnapshot{};state.cullMode=CoinRenderCullMode::NONE;
  std::vector<CoinRenderVertexSnapshot> concave(5);
  const float concaveXY[5][2]={{-.8f,-.8f},{.8f,-.8f},{0,0},{.8f,.8f},{-.8f,.8f}};
  for(int i=0;i<5;++i) {concave[i].position[0]=concaveXY[i][0];concave[i].position[1]=concaveXY[i][1];}
  ok=check(!coin_render_prepare_polygon_style(concave,state,materials,lighting,CoinRenderPolygonStyle::LINES,
    resolved,error,CoinRenderViewportSnapshot{}),"concave contours are rejected instead of joined incorrectly") && ok;
  state=CoinRenderRenderStateSnapshot{};state.cullMode=CoinRenderCullMode::NONE;
  state.linePattern=0xaaaau;
  ok=check(!coin_render_prepare_polygon_style(ring,state,materials,lighting,CoinRenderPolygonStyle::LINES,
    resolved,error,CoinRenderViewportSnapshot{}),"unqualified polygon stipple is explicitly rejected") && ok;
  return ok;
}
bool slopeCoreContract() {
  CoinRenderRenderStateSnapshot state;
  state.lightModel=CoinRenderLightModel::BASE_COLOR;state.cullMode=CoinRenderCullMode::NONE;
  state.polygonOffsetEnabled=true;state.polygonOffsetStyles=6;
  std::vector<CoinRenderMaterialSnapshot> materials(1);
  std::vector<CoinRenderVertexSnapshot> ring(4);
  const float xy[4][2]={{-.8f,-.8f},{.8f,-.8f},{.8f,.8f},{-.8f,.8f}};
  for(int i=0;i<4;++i) {
    ring[i].position[0]=xy[i][0];ring[i].position[1]=xy[i][1];
    ring[i].position[2]=.2f*xy[i][0]+.1f*xy[i][1];
  }
  CoinRenderLightingSnapshot lighting;CoinRenderPolygonStyleResult result;std::string error;
  bool ok=true;
  for(auto style:{CoinRenderPolygonStyle::LINES,CoinRenderPolygonStyle::POINTS})
    for(float factor:{-2.0f,2.0f})for(int width:{64,128})for(int clipped:{0,1})for(int projective:{0,1}) {
      CoinRenderViewportSnapshot viewport;viewport.width=width;viewport.height=32;
      state.projectionCoin=SbMatrix::identity();state.projectionCoin[2][3]=projective ? .5f : 0;
      state.polygonOffsetFactor=factor;state.depthRange[0]=.2f;state.depthRange[1]=.8f;
      state.clipPlanesWorld.clear();if(clipped)state.clipPlanesWorld.push_back(SbPlane(SbVec3f(1,0,0),0));
      // z_ndc=.2*x+.1*y, hence window slopes .6*.2/width and .6*.1/height.
      const float expected=factor*std::max(.12f/width,.06f/32);
      ok=check(coin_render_prepare_polygon_style(ring,state,materials,lighting,style,result,error,viewport) &&
        std::abs(result.state.polygonOffsetSlopeBias-expected)<1e-8f && result.state.polygonOffsetFactor==0,
        "original planar slope uses viewport, depth range, sign and survives clipping") && ok;
    }
  state.projectionCoin=SbMatrix::identity();
  state.clipPlanesWorld.clear();state.depthRange[0]=0;state.depthRange[1]=1;
  state.polygonOffsetStyles=1;
  ok=check(coin_render_prepare_polygon_style(ring,state,materials,lighting,CoinRenderPolygonStyle::LINES,
    result,error,CoinRenderViewportSnapshot{}) && result.state.polygonOffsetSlopeBias==0,
    "inactive style mask leaves slope disabled") && ok;
  state.polygonOffsetStyles=6;ring[2].position[2]+=.1f;
  ok=check(!coin_render_prepare_polygon_style(ring,state,materials,lighting,CoinRenderPolygonStyle::LINES,
    result,error,CoinRenderViewportSnapshot{}) && error.find("planar original face")!=std::string::npos,
    "nonplanar original faces reject ambiguous slope even after clipping") && ok;
  ring[2].position[2]-=.1f;
  state.model.setScale(SbVec3f(0,1,1));
  ok=check(!coin_render_prepare_polygon_style(ring,state,materials,lighting,CoinRenderPolygonStyle::LINES,
    result,error,CoinRenderViewportSnapshot{}) && error.find("zero projected area")!=std::string::npos,
    "edge-on slope is explicitly unsupported") && ok;
  CoinRenderFramePlan before,after;before.renderStates.push_back(state);after=before;
  after.renderStates[0].polygonOffsetSlopeBias=.01f;
  ok=check(!coin_render_same_state_except_camera(before.renderStates[0],after.renderStates[0]),
    "resolved bias participates in state equality") && ok;
  return ok;
}
bool polygonCaptureContract() {
  bool ok=true;
  for(int mode:{SoDrawStyle::LINES,SoDrawStyle::POINTS}) {
    SoSeparator * root=new SoSeparator;root->ref();
    root->addChild(style(mode));root->addChild(coordinates());root->addChild(faces());
    CoinRenderFramePlan frame;
    ok=check(capture(root,frame) && frame.draws.size()==1 && frame.draws[0].geometry.vertexCount==16 &&
             frame.draws[0].geometry.indexCount==24,
             "face quad expands four edges or four points exactly once") && ok;
    root->unref();
    root=new SoSeparator;root->ref();root->addChild(style(mode));root->addChild(coordinates());
    SoFaceSet * face=new SoFaceSet;face->numVertices=4;root->addChild(face);
    ok=check(capture(root,frame) && frame.draws.size()==1 && frame.draws[0].geometry.vertexCount==16,
             "non-indexed face retains its original quad contour") && ok;root->unref();
    for(int count:{3,5}) {
      root=new SoSeparator;root->ref();root->addChild(style(mode));
      SoCoordinate3 * polygon=new SoCoordinate3;
      for(int i=0;i<count;++i) {
        const float angle=float(i)*6.283185307f/count;
        polygon->point.set1Value(i,SbVec3f(.5f*std::cos(angle),.5f*std::sin(angle),0));
      }
      root->addChild(polygon);face=new SoFaceSet;face->numVertices=count;root->addChild(face);
      ok=check(capture(root,frame) && frame.draws.size()==1 &&
        frame.draws[0].geometry.vertexCount==static_cast<uint32_t>(count*4),
        "triangles and convex pentagons preserve original contour multiplicity") && ok;
      root->unref();
    }
    root=new SoSeparator;root->ref();root->addChild(style(mode));
    SoRotation * rotation=new SoRotation;rotation->rotation=SbRotation(SbVec3f(1,2,3),.3f);root->addChild(rotation);
    SoCube * cube=new SoCube;cube->width=.7f;cube->height=.7f;cube->depth=.7f;root->addChild(cube);
    ok=check(capture(root,frame) && frame.draws.size()==6,"Cube retains six quad contours") && ok;
    for(const auto & draw:frame.draws)ok=check(draw.geometry.vertexCount==16,"Cube does not expose quad diagonals or duplicate quad points") && ok;
    root->unref();
    root=new SoSeparator;root->ref();root->addChild(style(mode));
    cube=new SoCube;cube->width=.7f;cube->height=.7f;cube->depth=.7f;root->addChild(cube);
    ok=check(capture(root,frame) && frame.draws.size()==6,
      "edge-on projected faces retain boundaries when polygon culling is disabled") && ok;
    root->unref();
    for(int shape=0;shape<3;++shape) {
      root=new SoSeparator;root->ref();root->addChild(style(mode));
      SoComplexity * complexity=new SoComplexity;complexity->value=.2f;root->addChild(complexity);
      rotation=new SoRotation;rotation->rotation=SbRotation(SbVec3f(1,2,3),.3f);root->addChild(rotation);
      if(shape==0) {SoSphere * sphere=new SoSphere;sphere->radius=.25f;root->addChild(sphere);}
      if(shape==1) {SoCylinder * cylinder=new SoCylinder;cylinder->radius=.25f;cylinder->height=.5f;root->addChild(cylinder);}
      if(shape==2) {SoCone * cone=new SoCone;cone->bottomRadius=.25f;cone->height=.5f;root->addChild(cone);}
      ok=check(capture(root,frame) && !frame.draws.empty(),"procedural sphere/cylinder/cone contour capture") && ok;
      size_t triangles=0,quads=0;
      for(const auto & draw:frame.draws) {
        triangles+=draw.geometry.vertexCount==12;quads+=draw.geometry.vertexCount==16;
        ok=check(draw.geometry.vertexCount==12 || draw.geometry.vertexCount==16,
          "procedural triangles and quads retain original primitive boundaries") && ok;
      }
      ok=check(triangles>0 && (shape==2 || quads>0),"procedural caps and quad strips are distinguished") && ok;
      root->unref();
    }
    root=new SoSeparator;root->ref();root->addChild(style(mode));root->addChild(coordinates());
    SoLineSet * lines=new SoLineSet;lines->numVertices=2;root->addChild(lines);
    SoPointSet * points=new SoPointSet;points->numPoints=1;root->addChild(points);
    ok=check(capture(root,frame) && frame.draws.size()==2 &&
      frame.renderStates[frame.draws[0].renderStateSlot].polygonOffsetPrimitiveStyle==2 &&
      frame.renderStates[frame.draws[1].renderStateSlot].polygonOffsetPrimitiveStyle==4,
      "polygon mode leaves native line and point topologies intact") && ok;
    root->unref();
  }
  return ok;
}
bool pixel(const std::vector<uint8_t> & image, int red, int blue) {
  const size_t offset=(32*64+32)*4;
  return image.size()==64*64*4 && std::abs(int(image[offset])-red)<=2 &&
    image[offset+1]<=2 && std::abs(int(image[offset+2])-blue)<=2;
}
bool sample(const std::vector<uint8_t> & image,int x,int y,int r,int g,int b) {
  const size_t offset=(y*64+x)*4;
  if(image.size()!=64*64*4)return false;
  return std::abs(int(image[offset])-r)<=2 && std::abs(int(image[offset+1])-g)<=2 &&
    std::abs(int(image[offset+2])-b)<=2;
}
bool polygonActionContract(bool cpu) {
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(64,64)));
  if(cpu)target->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);
#ifdef HAVE_COIN_BGFX
  target->setDepthReadbackEnabled(FALSE);
#endif
  SoSeparator * root=new SoSeparator;root->ref();
  SoOrthographicCamera * camera=new SoOrthographicCamera;camera->height=2;camera->position=SbVec3f(0,0,3);
  camera->nearDistance=.1f;camera->farDistance=10;root->addChild(camera);
  SoLightModel * lightModel=new SoLightModel;lightModel->model=SoLightModel::BASE_COLOR;root->addChild(lightModel);
  root->addChild(new SoDirectionalLight);
  SoMaterial * blue=new SoMaterial;blue->diffuseColor=SbColor(0,0,1);blue->ambientColor=SbColor(0,0,0);root->addChild(blue);
  SoCoordinate3 * background=coordinates();
  for(int i=0;i<4;++i) {SbVec3f position=background->point[i];position[2]=-.5f;background->point.set1Value(i,position);}
  root->addChild(background);root->addChild(faces());
  SoSeparator * foreground=new SoSeparator;root->addChild(foreground);
  SoDrawStyle * drawStyle=style(SoDrawStyle::LINES);drawStyle->lineWidth=4;drawStyle->pointSize=6;foreground->addChild(drawStyle);
  SoClipPlane * clip=new SoClipPlane;clip->on=FALSE;foreground->addChild(clip);
  SoMaterial * red=new SoMaterial;red->diffuseColor=SbColor(1,0,0);red->ambientColor=SbColor(0,0,0);foreground->addChild(red);
  foreground->addChild(coordinates());foreground->addChild(faces());
  // Draw the far filled face again afterwards: depth written by the strokes
  // must prevent this later draw from covering the red boundary/points.
  root->addChild(faces());
  CoinRenderAction action(SbViewportRegion(64,64));action.setRenderTarget(target.get());
  action.setBackgroundColor(SbColor4f(0,0,0,1));bool ok=true;
  for(int fast=0;fast<2;++fast)for(int lighting=0;lighting<2;++lighting) {
    action.setFastPathEnabled(fast ? TRUE : FALSE);
    lightModel->model=lighting ? SoLightModel::PHONG : SoLightModel::BASE_COLOR;
    for(int mode:{SoDrawStyle::LINES,SoDrawStyle::POINTS})for(int clipped=0;clipped<2;++clipped) {
      drawStyle->style=mode;clip->on=clipped ? TRUE : FALSE;
      action.apply(root);std::vector<uint8_t> image;target->readbackRGBA(image);
      ok=check(action.getLastStatus()==CoinRenderAction::SUCCESS,"polygon style action CPU/GPU succeeds") && ok;
      ok=check(sample(image,6,6,clipped ? 0 : 255,0,clipped ? 255 : 0),
               "original left corner follows polygon clipping") && ok;
      const bool cutLine=clipped && mode==SoDrawStyle::LINES;
      ok=check(sample(image,32,32,cutLine ? 255 : 0,0,cutLine ? 0 : 255),
               "wireframe has no tessellation diagonal; clipping creates the new cut edge") && ok;
      if(clipped)ok=check(sample(image,32,6,255,0,0),"cut corner is emitted once in POINTS and retained in LINES") && ok;
      const bool leftLine=!clipped && mode==SoDrawStyle::LINES;
      ok=check(sample(image,6,32,leftLine ? 255 : 0,0,leftLine ? 0 : 255),
               "POINTS emits corners only, while LINES retains boundary and foreground depth") && ok;
    }
  }
  // Unsupported patterned polygons preserve the last publication, then recover.
  drawStyle->style=SoDrawStyle::LINES;drawStyle->linePattern=0xaaaau;
  const uint64_t serial=target->getLastSubmissionSerial();std::vector<uint8_t> before,image;
  target->readbackRGBA(before);action.apply(root);target->readbackRGBA(image);
  ok=check(action.getLastStatus()==CoinRenderAction::UNSUPPORTED && serial==target->getLastSubmissionSerial() &&
           image==before,"unsupported polygon pattern does not publish") && ok;
  drawStyle->linePattern=0xffffu;action.apply(root);
  ok=check(action.getLastStatus()==CoinRenderAction::SUCCESS,"polygon style recovers after unsupported pattern") && ok;
  SoSeparator * textured=new SoSeparator;foreground->addChild(textured);
  SoTexture2 * texture=new SoTexture2;const unsigned char white[]={255,255,255};
  texture->image.setValue(SbVec2s(1,1),3,white);textured->addChild(texture);
  SoTextureCoordinate2 * uv=new SoTextureCoordinate2;
  const SbVec2f corners[]={SbVec2f(0,0),SbVec2f(1,0),SbVec2f(1,1),SbVec2f(0,1)};
  uv->point.setValues(0,4,corners);textured->addChild(uv);textured->addChild(faces());
  action.apply(root);
  ok=check(action.getLastStatus()==CoinRenderAction::SUCCESS,
    "textured polygon styles use the shared stroke payload") && ok;
  foreground->removeChild(textured);action.apply(root);
  ok=check(action.getLastStatus()==CoinRenderAction::SUCCESS,"polygon remains valid after removing a texture") && ok;
  root->unref();return ok;
}
bool homogeneousStrokeContract(bool cpu) {
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(64,64)));
  if(cpu)target->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);
#ifdef HAVE_COIN_BGFX
  target->setDepthReadbackEnabled(FALSE);
#endif
  bool ok=true;
  for(auto topology:{CoinRenderPrimitiveTopology::LINE_LIST,CoinRenderPrimitiveTopology::POINT_LIST})
    for(int clipped:{0,1})for(int matrix:{0,1})for(int model=0;model<4;++model)for(int fog=1;fog<=3;++fog) {
      CoinRenderFramePlan plan;plan.lightingStates.push_back(CoinRenderLightingSnapshot{});
      plan.cameras.push_back(CoinRenderCameraSnapshot{});
      CoinRenderViewportSnapshot viewport;viewport.width=viewport.height=64;plan.viewports.push_back(viewport);
      CoinRenderMaterialSnapshot material;material.diffuse[0]=material.diffuse[1]=material.diffuse[2]=1;
      plan.materials.push_back(material);
      CoinRenderTextureImageSnapshot texture;texture.width=16;texture.height=1;
      for(int i=0;i<16;++i) {texture.pixelsRgba.push_back(i*17);texture.pixelsRgba.push_back(0);
        texture.pixelsRgba.push_back(0);texture.pixelsRgba.push_back(255);}
      // Different payloads must have different digests; this image is constant across the matrix.
      texture.contentDigest=1234567;plan.textures.push_back(texture);
      CoinRenderSamplerSnapshot sampler;sampler.filter=CoinRenderTextureFilter::NEAREST;
      sampler.wrapS=sampler.wrapT=CoinRenderTextureWrap::CLAMP;plan.samplers.push_back(sampler);
      CoinRenderRenderStateSnapshot state;state.lightModel=CoinRenderLightModel::BASE_COLOR;
      state.cullMode=CoinRenderCullMode::NONE;state.lineWidth=6;state.pointSize=8;
      SbViewVolume volume;volume.perspective(1.570796327f,1,.1f,10);
      SbMatrix affine;volume.getMatrices(affine,state.projectionCoin);
      state.hasTexture=true;state.textureModel=static_cast<CoinRenderTextureModel>(model);
      state.textureBlendColor[0]=state.textureBlendColor[1]=state.textureBlendColor[2]=0;
      if(matrix)state.textureMatrix.setTranslate(SbVec3f(.1f,0,0));
      state.fogMode=static_cast<CoinRenderFogMode>(fog);state.fogColor[0]=state.fogColor[1]=0;
      state.fogColor[2]=1;state.fogStart=0;state.fogEnd=10;
      if(clipped)state.clipPlanesWorld.push_back(SbPlane(SbVec3f(1,0,0),0));
      plan.renderStates.push_back(state);plan.vertices.resize(2);
      plan.vertices[0].position[0]=-.8f;plan.vertices[0].position[2]=-1;plan.vertices[0].texcoord[0]=.25f;
      plan.vertices[1].position[0]=3.2f;plan.vertices[1].position[2]=-4;plan.vertices[1].texcoord[0]=1;
      if(clipped && topology==CoinRenderPrimitiveTopology::POINT_LIST) {
        plan.vertices[0].position[0]=0;plan.vertices[0].position[2]=-1.6f;
        plan.vertices[0].texcoord[0]=.4f;
      }
      plan.indices={0,1};CoinRenderDrawPacket draw;draw.topology=topology;
      draw.geometry.vertexCount=2;draw.geometry.indexCount=2;plan.draws.push_back(draw);
      std::string diagnostic;
      ok=check(coin_render_expand_strokes(plan,diagnostic),"common stroke expansion succeeds") && ok;
      const auto result=target->getPimpl()->executeFrame(plan);
      std::vector<uint8_t> image;target->readbackRGBA(image);
      // Perspective-correct t at the sample: clip W is 1 and 4. Point clipping
      // creates a corner at object t=.2; an uncut point keeps t=0.
      const bool line=topology==CoinRenderPrimitiveTopology::LINE_LIST;
      const double screenT=(32.5-6.4)/51.2;
      const double t=line ? (screenT/4)/((1-screenT)+screenT/4) : (clipped ? .2 : 0);
      const double eye=1+3*t,uv=.25+.75*t+(matrix ? .1 : 0);
      const double texel=std::floor(uv*16)*17/255;
      const double factor=fog==1 ? 1-eye/10 : (fog==2 ? std::exp(-5.545*eye/10) :
        std::exp(-std::pow(2.35*eye/10,2)));
      const double r=model==3 ? 1-texel : texel;
      const double g=model==3 ? 1 : 0,b=model==3 ? 1 : 0;
      const int x=line || clipped ? 32 : 6;
      const size_t at=(32*64+x)*4;
      const bool matches=image.size()==64*64*4 &&
        std::abs(int(image[at])-int(std::lround(255*r*factor)))<=4 &&
        std::abs(int(image[at+1])-int(std::lround(255*g*factor)))<=4 &&
        std::abs(int(image[at+2])-int(std::lround(255*(b*factor+1-factor))))<=4;
      if(!matches && image.size()==64*64*4)std::cerr<<"stroke "<<cpu<<','<<line<<','<<clipped<<','<<matrix<<','<<model<<','<<fog
        <<" pixel "<<int(image[at])<<','<<int(image[at+1])<<','<<int(image[at+2])<<" eye "<<eye<<" uv "<<uv<<'\n';
      ok=check(result.status==CoinRenderBackendStatus::SUCCESS && matches,
        "homogeneous UV, texture model/matrix and fragment fog match analytic perspective reference") && ok;
    }
  return ok;
}
bool polygonAttributesActionContract(bool cpu) {
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(64,64)));
  if(cpu)target->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);
#ifdef HAVE_COIN_BGFX
  target->setDepthReadbackEnabled(FALSE);
#endif
  SoSeparator * root=new SoSeparator;root->ref();
  SoOrthographicCamera * camera=new SoOrthographicCamera;
  camera->height=2;camera->position=SbVec3f(0,0,3);camera->nearDistance=.1f;camera->farDistance=10;
  root->addChild(camera);
  SoEnvironment * environment=new SoEnvironment;environment->fogColor=SbColor(0,0,1);
  environment->fogVisibility=6;root->addChild(environment);
  SoLightModel * lightModel=new SoLightModel;root->addChild(lightModel);root->addChild(new SoDirectionalLight);
  SoMaterial * material=new SoMaterial;material->diffuseColor=SbColor(1,1,1);root->addChild(material);
  SoDrawStyle * drawStyle=style(SoDrawStyle::LINES);drawStyle->lineWidth=4;drawStyle->pointSize=6;
  root->addChild(drawStyle);
  SoClipPlane * clip=new SoClipPlane;root->addChild(clip);
  SoTexture2 * texture=new SoTexture2;texture->blendColor=SbColor(0,0,0);
  texture->wrapS=SoTexture2::CLAMP;texture->wrapT=SoTexture2::CLAMP;
  unsigned char gradient[16*3];for(int i=0;i<16;++i) {gradient[i*3]=i*17;gradient[i*3+1]=gradient[i*3+2]=0;}
  texture->image.setValue(SbVec2s(16,1),3,gradient);root->addChild(texture);
  SoTexture2Transform * transform=new SoTexture2Transform;root->addChild(transform);
  SoTextureCoordinate2 * uv=new SoTextureCoordinate2;
  const SbVec2f corners[]={SbVec2f(.25f,0),SbVec2f(1,0),SbVec2f(1,1),SbVec2f(.25f,1)};
  uv->point.setValues(0,4,corners);root->addChild(uv);root->addChild(coordinates());root->addChild(faces());
  CoinRenderAction action(SbViewportRegion(64,64));action.setRenderTarget(target.get());bool ok=true;
  const bool compareGl=std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE")!=nullptr;
  SoOffscreenRenderer gl(SbViewportRegion(64,64));gl.setComponents(SoOffscreenRenderer::RGB);
  for(int fast:{0,1})for(int lighting:{0,1})for(int mode:{SoDrawStyle::LINES,SoDrawStyle::POINTS})
    for(int model=0;model<4;++model)for(int fog=1;fog<=3;++fog)for(int matrix:{0,1}) {
      action.setFastPathEnabled(fast ? TRUE : FALSE);
      lightModel->model=lighting ? SoLightModel::PHONG : SoLightModel::BASE_COLOR;
      const SoTexture2::Model models[]={SoTexture2::MODULATE,SoTexture2::REPLACE,SoTexture2::DECAL,SoTexture2::BLEND};
      drawStyle->style=mode;texture->model=models[model];environment->fogType=fog;
      transform->translation=SbVec2f(matrix ? .1f : 0,0);
      action.apply(root);std::vector<uint8_t> image;target->readbackRGBA(image);
      if(action.getLastStatus()!=CoinRenderAction::SUCCESS) {
        std::cerr << "polygon attrs diagnostic: " << action.getLastError().getString() << "\n";
        root->unref();return false;
      }
      const double u=(mode==SoDrawStyle::LINES ? .63232421875 : .625)+(matrix ? .1 : 0);
      const double texel=(u*16-.5)*17/255;
      const double factor=fog==1 ? .5 : (fog==2 ? std::exp(-5.545*.5) : std::exp(-std::pow(2.35*.5,2)));
      const double r=model==3 ? 1-texel : texel,g=model==3 ? 1 : 0,b=model==3 ? 1 : 0;
      const size_t at=(6*64+32)*4;
      const bool matches=image.size()==64*64*4 &&
        std::abs(int(image[at])-int(std::lround(255*r*factor)))<=4 &&
        std::abs(int(image[at+1])-int(std::lround(255*g*factor)))<=4 &&
        std::abs(int(image[at+2])-int(std::lround(255*(b*factor+1-factor))))<=4;
      if(!matches && image.size()==64*64*4)std::cerr<<"polygon attr "<<cpu<<','<<mode<<','<<model<<','<<fog
        <<" pixel "<<int(image[at])<<','<<int(image[at+1])<<','<<int(image[at+2])<<'\n';
      ok=check(action.getLastStatus()==CoinRenderAction::SUCCESS && matches,
        "polygon style captures UV, matrix, models and fragment fog after Coin clipping and PHONG") && ok;
      if(compareGl) {
        if(!check(gl.render(root) && gl.getBuffer()!=nullptr,"required Coin/GL reference renders")) {
          root->unref();return false;
        }
        const unsigned char * reference=gl.getBuffer();const size_t glAt=(6*64+32)*3;
        const bool same=std::abs(int(reference[glAt])-int(image[at]))<=4 &&
          std::abs(int(reference[glAt+1])-int(image[at+1]))<=4 &&
          std::abs(int(reference[glAt+2])-int(image[at+2]))<=4;
        if(!same)std::cerr<<"GL polygon "<<mode<<','<<model<<','<<fog<<','<<matrix
          <<" GL "<<int(reference[glAt])<<','<<int(reference[glAt+1])<<','<<int(reference[glAt+2])
          <<" Core "<<int(image[at])<<','<<int(image[at+1])<<','<<int(image[at+2])<<'\n';
        ok=check(same,"Coin/GL polygon texture/fog sample matches common Core reference") && ok;
      }
    }
  if(compareGl && ok)std::cout<<"Coin/GL polygon attributes reference passed\n";
  root->unref();return ok;
}
bool slopeActionContract(bool cpu) {
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(64,64)));
  if(cpu)target->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);
#ifdef HAVE_COIN_BGFX
  target->setDepthReadbackEnabled(FALSE);
#endif
  SoSeparator * root=new SoSeparator;root->ref();
  SoOrthographicCamera * camera=new SoOrthographicCamera;
  camera->height=2;camera->position=SbVec3f(0,0,3);camera->nearDistance=.1f;camera->farDistance=10;
  root->addChild(camera);
  SoDepthBuffer * depth=new SoDepthBuffer;root->addChild(depth);
  SoLightModel * lighting=new SoLightModel;lighting->model=SoLightModel::BASE_COLOR;root->addChild(lighting);
  auto slanted=[](float extent) {
    SoCoordinate3 * node=new SoCoordinate3;
    const float xy[4][2]={{-1,-1},{1,-1},{1,1},{-1,1}};
    for(int i=0;i<4;++i)node->point.set1Value(i,SbVec3f(extent*xy[i][0],extent*xy[i][1],
      extent*(.2f*xy[i][0]+.1f*xy[i][1])));
    return node;
  };
  SoMaterial * blue=new SoMaterial;blue->diffuseColor=SbColor(0,0,1);root->addChild(blue);
  root->addChild(slanted(.95f));root->addChild(faces());
  SoSeparator * overlay=new SoSeparator;root->addChild(overlay);
  SoDrawStyle * drawStyle=style(SoDrawStyle::LINES);
  drawStyle->lineWidth=6;drawStyle->pointSize=8;overlay->addChild(drawStyle);
  SoPolygonOffset * offset=new SoPolygonOffset;offset->units=0;
  offset->styles=SoPolygonOffset::LINES|SoPolygonOffset::POINTS;overlay->addChild(offset);
  SoClipPlane * clip=new SoClipPlane;clip->on=FALSE;overlay->addChild(clip);
  SoMaterial * red=new SoMaterial;red->diffuseColor=SbColor(1,0,0);overlay->addChild(red);
  SoCoordinate3 * foreground=slanted(.6f);overlay->addChild(foreground);overlay->addChild(faces());
  // Later coplanar fill must also respect the depth written by the offset stroke.
  root->addChild(faces());
  CoinRenderAction action(SbViewportRegion(64,64));action.setRenderTarget(target.get());
  bool ok=true;
  for(int fast:{0,1})for(int mode:{SoDrawStyle::LINES,SoDrawStyle::POINTS})
    for(int range:{0,1})for(int clipped:{0,1})for(float factor:{-4.0f,4.0f,-2000.0f,2000.0f}) {
      action.setFastPathEnabled(fast ? TRUE : FALSE);drawStyle->style=mode;
      depth->range=range ? SbVec2f(.2f,.8f) : SbVec2f(0,1);
      clip->on=clipped ? TRUE : FALSE;offset->factor=factor;
      action.apply(root);std::vector<uint8_t> image;target->readbackRGBA(image);
      const int x=clipped ? 32 : 13;
      ok=check(action.getLastStatus()==CoinRenderAction::SUCCESS &&
        sample(image,x,13,factor<0 ? 255 : 0,0,factor<0 ? 0 : 255),
        "original slope offset controls coplanar stroke oclusion, range, clipping and depth writes") && ok;
    }
  // A non-planar contour cannot silently use a gradient from one tessellation triangle.
  std::vector<uint8_t> before,image;target->readbackRGBA(before);
  const uint64_t serial=target->getLastSubmissionSerial();
  SbVec3f position=foreground->point[2];position[2]+=.15f;foreground->point.set1Value(2,position);
  action.apply(root);target->readbackRGBA(image);
  ok=check(action.getLastStatus()==CoinRenderAction::UNSUPPORTED && image==before &&
    target->getLastSubmissionSerial()==serial,"unsupported non-planar slope preserves publication") && ok;
  position[2]-=.15f;foreground->point.set1Value(2,position);
  offset->factor=-4;action.apply(root);
  ok=check(action.getLastStatus()==CoinRenderAction::SUCCESS,"slope action recovers after rejection") && ok;
  if(!cpu) {
    // Units and slope can oppose each other; the sum decides coplanar occlusion.
    for(int sign:{-1,1}) {
      offset->factor=sign>0 ? -4 : 4;offset->units=float(sign)*200000.5f;
      action.apply(root);std::vector<uint8_t> combined;target->readbackRGBA(combined);
      ok=check(action.getLastStatus()==CoinRenderAction::SUCCESS &&
        sample(combined,32,13,sign<0 ? 255 : 0,0,sign<0 ? 0 : 255),
        "fractional units combine with opposing original slope on GPU") && ok;
    }
#if !defined(HAVE_COIN_BGFX)
    offset->factor=-4;offset->units=0;action.apply(root);
    std::vector<float> baseline,biased;target->readbackDepth(baseline);
    offset->units=16.5f;action.apply(root);target->readbackDepth(biased);
    const size_t at=13*64+32;
    // The original contour's depths lie in [.25,.5), so its D32 quantum is 2^-25.
    const float quantum=std::ldexp(1.0f,-25);
    ok=check(action.getLastStatus()==CoinRenderAction::SUCCESS && baseline.size()==64*64 &&
      biased.size()==baseline.size() && std::abs((biased[at]-baseline[at])-16.5f*quantum)<=quantum,
      "D32 readback resolves fractional units with the original contour exponent") && ok;
#endif
  }
  root->unref();return ok;
}
bool actionContract(bool cpu) {
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(64,64)));
  if(cpu) target->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);
#ifdef HAVE_COIN_BGFX
  target->setDepthReadbackEnabled(FALSE);
#endif
  SoSeparator * root=new SoSeparator;root->ref();
  SoOrthographicCamera * camera=new SoOrthographicCamera;camera->height=2;
  camera->position=SbVec3f(0,0,3);camera->nearDistance=.1f;camera->farDistance=10;root->addChild(camera);
  SoLightModel * light=new SoLightModel;light->model=SoLightModel::BASE_COLOR;root->addChild(light);
  SoMaterial * blue=new SoMaterial;blue->diffuseColor=SbColor(0,0,1);blue->ambientColor=SbColor(0,0,0);root->addChild(blue);
  root->addChild(coordinates());root->addChild(faces());
  SoSeparator * nested=new SoSeparator;root->addChild(nested);
  SoDrawStyle * visibility=style(SoDrawStyle::INVISIBLE);nested->addChild(visibility);
  SoMaterial * red=new SoMaterial;red->diffuseColor=SbColor(1,0,0);nested->addChild(red);
  SoCube * cube=new SoCube;cube->width=1;cube->height=1;cube->depth=1;nested->addChild(cube);
  nested->addChild(faces());
  SoIndexedLineSet * lines=new SoIndexedLineSet;const int32_t ix[]={0,2,-1};
  lines->coordIndex.setValues(0,3,ix);nested->addChild(lines);
  SoPointSet * points=new SoPointSet;points->startIndex=0;points->numPoints=4;nested->addChild(points);
  CoinRenderAction action(SbViewportRegion(64,64));action.setRenderTarget(target.get());
  action.setBackgroundColor(SbColor4f(0,0,0,1));
  bool ok=true;
  for(int fast=0;fast<2;++fast) {
    action.setFastPathEnabled(fast ? TRUE : FALSE);
    for(int step=0;step<3;++step) {
      visibility->style=step==1 ? SoDrawStyle::FILLED : SoDrawStyle::INVISIBLE;
      action.apply(root);std::vector<uint8_t> image;target->readbackRGBA(image);
      ok=check(action.getLastStatus()==CoinRenderAction::SUCCESS &&
               pixel(image,step==1 ? 255 : 0,step==1 ? 0 : 255),
               "CPU/GPU fast and callback paths update visibility without stale cached geometry") && ok;
    }
  }
  root->unref();
  // An all-invisible frame must publish the clear, replacing the previous image.
  root=new SoSeparator;root->ref();visibility=style(SoDrawStyle::INVISIBLE);root->addChild(visibility);
  SoTextureUnit * unit=new SoTextureUnit;unit->unit=8;root->addChild(unit);
  SoTexture2 * texture=new SoTexture2;const unsigned char white[]={255,255,255};
  texture->image.setValue(SbVec2s(1,1),3,white);root->addChild(texture);
  for(int i=0;i<9;++i)root->addChild(new SoClipPlane);
  root->addChild(new SoCube);root->addChild(coordinates());root->addChild(faces());
  root->addChild(lines=new SoIndexedLineSet);lines->coordIndex.setValues(0,3,ix);
  const uint64_t serial=target->getLastSubmissionSerial();
  action.apply(root);std::vector<uint8_t> image;target->readbackRGBA(image);
  ok=check(action.getLastStatus()==CoinRenderAction::SUCCESS && pixel(image,0,0) &&
           target->getLastSubmissionSerial()>serial,"empty invisible frame publishes a fresh clear") && ok;
  const uint64_t hiddenSerial=target->getLastSubmissionSerial();
  visibility->style=SoDrawStyle::FILLED;action.apply(root);target->readbackRGBA(image);
  ok=check(action.getLastStatus()==CoinRenderAction::UNSUPPORTED && pixel(image,0,0) &&
           target->getLastSubmissionSerial()==hiddenSerial,
           "unsupported resources become errors when the shape becomes visible") && ok;
  visibility->style=SoDrawStyle::INVISIBLE;action.apply(root);
  ok=check(action.getLastStatus()==CoinRenderAction::SUCCESS &&
           target->getLastSubmissionSerial()>hiddenSerial,
           "invisible frame recovers after visible unsupported resources") && ok;
  root->unref();return ok;
}
}
int main() {
  SoDB::init();CoinRenderAction::initClass();
  if(!stateContract() || !polygonCoreContract() || !slopeCoreContract() || !polygonCaptureContract() || !polygonActionContract(true) || !slopeActionContract(true) || !homogeneousStrokeContract(true) || !polygonAttributesActionContract(true) || !actionContract(true))return 1;
  if(!CoinRenderAction::isGpuBackendAvailable()) {
    std::cerr<<"[SKIP] GPU adapter unavailable\n";return 77;
  }
  if(!polygonActionContract(false) || !slopeActionContract(false) || !homogeneousStrokeContract(false) || !polygonAttributesActionContract(false) || !actionContract(false))return 1;
  std::cout<<"DrawStyle polygon/Core/INVISIBLE CPU/GPU contracts passed\n";return 0;
}
