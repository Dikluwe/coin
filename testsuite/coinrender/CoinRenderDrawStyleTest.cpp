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
#include <Inventor/nodes/SoMaterialBinding.h>
#include <Inventor/nodes/SoNormalBinding.h>
#include <Inventor/nodes/SoNormal.h>
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
#include "rendering/coinrender/CoinRenderLineStippleCore.h"
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
class DerivedStyleCube : public SoCube {
  SO_NODE_HEADER(DerivedStyleCube);
public:
  DerivedStyleCube() { SO_NODE_CONSTRUCTOR(DerivedStyleCube); }
  static void initClass() { SO_NODE_INIT_CLASS(DerivedStyleCube, SoCube, "Cube"); }
};
SO_NODE_SOURCE(DerivedStyleCube);
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
  ok=check(coin_render_prepare_polygon_style(ring,state,materials,lighting,CoinRenderPolygonStyle::LINES,
    resolved,error,CoinRenderViewportSnapshot{}) && resolved.state.polygonLinePattern,"polygon pattern retains original contour ownership") && ok;
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
        std::abs(result.state.polygonOffsetSlopeBias-expected)<1e-8f && result.state.polygonOffsetFactor==0 &&
        std::abs(result.state.polygonOffsetMaxDepth-(.5f+.072f/(projective ? 1.12f : 1.f)))<1e-6f,
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
  drawStyle->style=SoDrawStyle::LINES;drawStyle->linePattern=0xaaaau;
  action.apply(root);
  ok=check(action.getLastStatus()==CoinRenderAction::SUCCESS,"polygon patterns share the Core expansion") && ok;
  drawStyle->linePattern=0xffffu;action.apply(root);
  ok=check(action.getLastStatus()==CoinRenderAction::SUCCESS,"polygon style switches back to solid") && ok;
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
bool polygonUnsupportedActionContract(bool cpu) {
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(64,64)));
  if(cpu)target->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);
#ifdef HAVE_COIN_BGFX
  target->setDepthReadbackEnabled(FALSE);
#endif
  SoSeparator* root=new SoSeparator;root->ref();
  SoOrthographicCamera* camera=new SoOrthographicCamera;camera->height=2;camera->position=SbVec3f(0,0,3);
  camera->nearDistance=.1f;camera->farDistance=10;root->addChild(camera);
  SoLightModel* lighting=new SoLightModel;lighting->model=SoLightModel::BASE_COLOR;root->addChild(lighting);
  SoMaterial* material=new SoMaterial;material->diffuseColor=SbColor(1,0,0);root->addChild(material);
  SoDrawStyle* drawStyle=style(SoDrawStyle::FILLED);root->addChild(drawStyle);
  SoGroup* geometry=new SoGroup;root->addChild(geometry);
  DerivedStyleCube* cube=new DerivedStyleCube;cube->width=cube->height=cube->depth=.5f;geometry->addChild(cube);
  CoinRenderAction action(SbViewportRegion(64,64));action.setRenderTarget(target.get());bool ok=true;
  for(int shape=0;shape<2;++shape) {
    if(shape) {
      geometry->removeAllChildren();SoCoordinate3* coords=new SoCoordinate3;
      const SbVec3f concave[]={SbVec3f(-.8f,-.8f,0),SbVec3f(.8f,-.8f,0),SbVec3f(0,0,0),
        SbVec3f(.8f,.8f,0),SbVec3f(-.8f,.8f,0)};
      coords->point.setValues(0,5,concave);geometry->addChild(coords);
      SoFaceSet* face=new SoFaceSet;face->numVertices=5;geometry->addChild(face);
    }
    drawStyle->style=SoDrawStyle::FILLED;action.apply(root);
    ok=check(action.getLastStatus()==CoinRenderAction::SUCCESS,"unsupported style contour remains valid when FILLED") && ok;
    std::vector<uint8_t> before,image;target->readbackRGBA(before);const uint64_t serial=target->getLastSubmissionSerial();
    for(int mode:{SoDrawStyle::LINES,SoDrawStyle::POINTS}) {
      drawStyle->style=mode;action.apply(root);target->readbackRGBA(image);
      ok=check(action.getLastStatus()==CoinRenderAction::UNSUPPORTED && serial==target->getLastSubmissionSerial() &&
        image==before,"subclasses without recoverable face details and concave contours reject without publication") && ok;
    }
    drawStyle->style=SoDrawStyle::INVISIBLE;action.apply(root);
    ok=check(action.getLastStatus()==CoinRenderAction::SUCCESS,"INVISIBLE needs no contour recovery") && ok;
    drawStyle->style=SoDrawStyle::FILLED;action.apply(root);
    ok=check(action.getLastStatus()==CoinRenderAction::SUCCESS,"valid style recovers after unsupported contour") && ok;
  }
  root->unref();return ok;
}
bool polygonStippleCapCoreContract() {
  CoinRenderFramePlan plan;
  CoinRenderViewportSnapshot viewport;
  viewport.width = viewport.height = 64;
  plan.viewports.push_back(viewport);
  plan.materials.resize(2);
  plan.materials[0].diffuse[0] = .2f;
  plan.materials[0].diffuse[3] = .5f;
  plan.materials[1].diffuse[0] = .8f;
  plan.materials[1].diffuse[3] = .8f;
  CoinRenderRenderStateSnapshot state;
  state.lightModel = CoinRenderLightModel::BASE_COLOR;
  state.projectionCoin[2][3] = -.5f;
  state.polygonLinePattern = true;
  state.linePattern = 1;
  state.linePatternScaleFactor = 256;
  state.fogMode = CoinRenderFogMode::HAZE;
  plan.renderStates.push_back(state);
  plan.vertices.resize(2);
  plan.vertices[0].position[0] = -.796875f * 1.25f;
  plan.vertices[0].position[1] = -.796875f * 1.25f;
  plan.vertices[0].position[2] = -.5f;
  plan.vertices[0].texcoord[0] = .25f;
  plan.vertices[1].position[0] = -.203125f * 1.75f;
  plan.vertices[1].position[1] = -.796875f * 1.75f;
  plan.vertices[1].position[2] = -1.5f;
  plan.vertices[1].texcoord[0] = .9f;
  plan.vertices[1].materialSlot = 1;
  plan.indices = {0, 1};
  CoinRenderDrawPacket draw;
  draw.topology = CoinRenderPrimitiveTopology::LINE_LIST;
  draw.geometry.vertexCount = 2;
  draw.geometry.indexCount = 2;
  plan.draws.push_back(draw);
  std::string diagnostic;
  bool ok = check(coin_render_expand_strokes(plan, diagnostic) && plan.draws.size() == 1,
                  "stipple cap expansion succeeds");
  if (!ok)
    return false;
  const auto& expanded = plan.draws[0];
  bool capFound = false, bodyFound = false;
  for (uint32_t i = 0; i < expanded.geometry.vertexCount; ++i) {
    const auto& vertex = plan.vertices[expanded.geometry.firstVertex + i];
    if (vertex.position[0] < -.796875f) {
      capFound = true;
      ok = check(vertex.screenSpaceW == 1.25f && vertex.fogEyeDepth == .5f &&
                     vertex.texcoord[0] == .25f && vertex.materialSlot == 0 &&
                     vertex.position[2] == -.4f,
                 "cap retains exact endpoint W, UV, color, alpha, fog and unbiased depth") && ok;
    } else if (vertex.position[0] < -.203125f) {
      bodyFound = true;
      const double screenT = (vertex.position[0] + .796875) / (.796875 - .203125);
      const double inverseW = (1 - screenT) / 1.25 + screenT / 1.75;
      const double t = (screenT / 1.75) / inverseW;
      const auto& material = plan.materials[vertex.materialSlot];
      ok = check(std::abs(vertex.screenSpaceW - 1 / inverseW) < 1e-6 &&
                     std::abs(vertex.texcoord[0] - (.25 + .65 * t)) < 1e-6 &&
                     std::abs(vertex.fogEyeDepth - (.5 + t)) < 1e-6 &&
                     std::abs(material.diffuse[0] - (.2 + .6 * t)) < 1e-6 &&
                     std::abs(material.diffuse[3] - (.5 + .3 * t)) < 1e-6 &&
                     std::abs(vertex.position[2] - (-.4 + (-1.5 / 1.75 + .4) * screenT)) < 1e-6,
                 "original interval keeps independent perspective attributes and linear window depth") && ok;
    }
  }
  ok = check(capFound && bodyFound, "raster cap covers the start cell while retaining the original interval") && ok;
  return ok;
}
bool polygonStippleCoreContract() {
  bool ok=true;uint32_t phase=0;std::vector<std::pair<float,float>> spans;
  const double points[5][2]={{6.5,6.5},{15.5,6.5},{15.5,14.5},{6.5,14.5},{6.5,6.5}};
  const uint32_t expected[]={9,1,10,2};
  for(int edge=0;edge<4;++edge) {
    ok=check(coin_render_line_stipple(points[edge][0],points[edge][1],points[edge+1][0],
      points[edge+1][1],0x000fu,1,phase,spans) && phase==expected[edge],
      "stipple counter continues across original polygon edges") && ok;
    if(edge==1)ok=check(spans.size()==1 && spans[0].first>.8f,
      "second edge consumes the previous edge's invisible cells") && ok;
  }
  phase=0;ok=check(coin_render_line_stipple(6.5,6.5,15.5,14.5,0xaaaau,1,phase,spans) && phase==9,
    "diagonal stipple counts fragments along the major axis, rather than Euclidean length") && ok;
  for(int repeat:{1,2,256,999}) {
    phase=0;ok=check(coin_render_line_stipple(6.5,6.5,15.5,6.5,0x0001u,repeat,phase,spans) &&
      phase==9 && spans.size()==1 && std::abs(spans[0].second-std::min(8.5f,std::min(repeat,256)-.5f)/9)<1e-6f,
      "stipple repeat is clamped to Coin/GL's 1..256 range") && ok;
  }
  phase=0;ok=check(coin_render_line_stipple(6.5,6.5,15.5,6.5,0,1,phase,spans) &&
    spans.empty() && phase==9,"invisible pattern still advances the Core counter") && ok;
  ok=check(!coin_render_line_stipple(0,0,70000,0,1,1,phase,spans),"stipple work budget is explicit") && ok;
  return ok;
}

bool nativeStippleActionContract(bool cpu) {
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(64, 64)));
  if (cpu)
    target->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);
#ifdef HAVE_COIN_BGFX
  target->setDepthReadbackEnabled(FALSE);
#endif
  SoSeparator* root = new SoSeparator;
  root->ref();
  SoOrthographicCamera* camera = new SoOrthographicCamera;
  camera->height = 2;
  camera->position = SbVec3f(0, 0, 3);
  camera->nearDistance = .1f;
  camera->farDistance = 10;
  root->addChild(camera);
  SoLightModel* lighting = new SoLightModel;
  root->addChild(lighting);
  root->addChild(new SoDirectionalLight);
  SoMaterial* material = new SoMaterial;
  for (int i = 0; i < 8; ++i)
    material->diffuseColor.set1Value(i, SbColor(1, 1, 1));
  root->addChild(material);
  SoMaterialBinding* mb = new SoMaterialBinding;
  root->addChild(mb);
  SoNormalBinding* nb = new SoNormalBinding;
  root->addChild(nb);
  SoNormal* normals = new SoNormal;
  root->addChild(normals);
  SoDrawStyle* drawStyle = style(SoDrawStyle::LINES);
  root->addChild(drawStyle);
  SoCoordinate3* coords = new SoCoordinate3;
  root->addChild(coords);
  // Two strips share an endpoint. A repeated interior vertex produces no fragment.
  const int xy[][2] = {{6, 6}, {25, 6}, {25, 6}, {25, 25}, {25, 25}, {44, 25}, {44, 44}};
  for (int i = 0; i < 7; ++i)
    coords->point.set1Value(i, SbVec3f((xy[i][0] + .5f) / 32 - 1, (xy[i][1] + .5f) / 32 - 1, 0));
  SoGroup* geometry = new SoGroup;
  root->addChild(geometry);
  CoinRenderAction action(SbViewportRegion(64, 64));
  action.setRenderTarget(target.get());
  action.setBackgroundColor(SbColor4f(0, 0, 0, 1));
  const bool compareGl = std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE") != nullptr;
  SoOffscreenRenderer gl(SbViewportRegion(64, 64));
  gl.setComponents(SoOffscreenRenderer::RGB);
  bool ok = true;
  for (int indexed : {0, 1})
    for (int separateNodes : {0, 1}) {
      geometry->removeAllChildren();
      if (indexed) {
        SoIndexedLineSet* first = new SoIndexedLineSet;
        // PER_PART_INDEXED needs one valid material index per segment,
        // including the degenerate pair; coordIndex separators are not indices.
        const int32_t materialIndices[] = {0, 1, 2, 3, 4};
        first->materialIndex.setValues(0, separateNodes ? 3 : 5, materialIndices);
        const int32_t both[] = {0, 1, 2, 3, -1, 4, 5, 6, -1};
        first->coordIndex.setValues(0, separateNodes ? 5 : 9, both);
        geometry->addChild(first);
        if (separateNodes) {
          SoIndexedLineSet* second = new SoIndexedLineSet;
          second->materialIndex.setValues(0, 2, materialIndices);
          const int32_t next[] = {4, 5, 6, -1};
          second->coordIndex.setValues(0, 4, next);
          geometry->addChild(second);
        }
      } else {
        SoLineSet* first = new SoLineSet;
        const int32_t sizes[] = {4, 3};
        first->numVertices.setValues(0, separateNodes ? 1 : 2, sizes);
        geometry->addChild(first);
        if (separateNodes) {
          SoLineSet* second = new SoLineSet;
          second->startIndex = 4;
          second->numVertices = 3;
          geometry->addChild(second);
        }
      }
      for (int binding = 0; binding < 8; ++binding) {
        // Different material slots split the captured packets within a strip.
        // All colors stay above the visibility threshold used below.
        for (int i = 0; i < 8; ++i)
          material->diffuseColor.set1Value(
              i, binding == 2 ? SbColor(.65f + .03f * i, .65f + .03f * i, .65f + .03f * i)
                              : SbColor(1, 1, 1));
        mb->value = binding == 1   ? SoMaterialBinding::PER_FACE
                    : binding == 2 ? SoMaterialBinding::PER_VERTEX
                    : binding == 3 ? SoMaterialBinding::PER_PART
                    : binding == 4 ? SoMaterialBinding::PER_PART_INDEXED
                                   : SoMaterialBinding::OVERALL;
        nb->value = binding >= 5 ? SoNormalBinding::PER_PART : SoNormalBinding::OVERALL;
        lighting->model = binding >= 6 ? SoLightModel::PHONG : SoLightModel::BASE_COLOR;
        normals->vector.setNum(binding == 7 ? 0 : 8);
        if (binding != 7)
          for (int i = 0; i < 8; ++i)
            normals->vector.set1Value(i, SbVec3f(0, 0, 1));
        const bool independent = binding == 3 || binding == 4 || binding == 6;
        for (int fast : {0, 1})
          for (int width : {1, 3})
            for (int repeat : {1, 2, 5, 256})
              for (uint32_t pattern : {0u, 0xffffu, 0x000fu, 0xaaaau, 0x9249u}) {
                action.setFastPathEnabled(fast ? TRUE : FALSE);
                drawStyle->lineWidth = width;
                drawStyle->linePattern = pattern;
                drawStyle->linePatternScaleFactor = repeat;
                action.apply(root);
                std::vector<uint8_t> image;
                target->readbackRGBA(image);
                if (!check(action.getLastStatus() == CoinRenderAction::SUCCESS &&
                               image.size() == 64 * 64 * 4,
                           "native patterned strips render")) {
                  root->unref();
                  return false;
                }
                const unsigned char* reference = nullptr;
                if (compareGl) {
                  if (!check(gl.render(root) && gl.getBuffer(),
                             "required Coin/GL native stipple renders")) {
                    root->unref();
                    return false;
                  }
                  reference = gl.getBuffer();
                }
                auto sample = [&](int x, int y, int counter) {
                  const bool expected =
                      (pattern & (1u << ((counter / std::min(repeat, 256)) & 15))) != 0;
                  const bool actual = image[((63 - y) * 64 + x) * 4] > 127;
                  const bool same = !reference || (reference[(y * 64 + x) * 3] > 127) == actual;
                  if (actual != expected || !same)
                    std::cerr << "native stipple " << cpu << ',' << indexed << ',' << separateNodes
                              << ',' << binding << ',' << fast << ',' << width << ',' << repeat
                              << ',' << pattern << " at " << x << ',' << y << " count " << counter
                              << " Core " << actual << " GL "
                              << (reference ? int(reference[(y * 64 + x) * 3]) : -1) << '\n';
                  ok = check(actual == expected && same,
                             "native stipple counts fragments, preserves strip phase and resets "
                             "per binding/node") &&
                       ok;
                };
                for (int t = 4; t <= 14; ++t) {
                  sample(6 + t, 6, t);
                  sample(25, 6 + t, (independent ? 0 : 19) + t);
                  sample(25 + t, 25, t);
                  sample(44, 25 + t, (independent ? 0 : 19) + t);
                }
                if (!ok) {
                  root->unref();
                  return false;
                }
              }
      }
    }
  if (compareGl)
    std::cout << "Coin/GL native stipple reference passed\n";
  root->unref();
  return ok;
}

bool polygonStippleActionContract(bool cpu) {
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(64, 64)));
  if (cpu)
    target->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);
#ifdef HAVE_COIN_BGFX
  target->setDepthReadbackEnabled(FALSE);
#endif
  SoSeparator* root = new SoSeparator;
  root->ref();
  SoOrthographicCamera* camera = new SoOrthographicCamera;
  camera->height = 2;
  camera->position = SbVec3f(0, 0, 3);
  camera->nearDistance = .1f;
  camera->farDistance = 10;
  root->addChild(camera);
  SoLightModel* lighting = new SoLightModel;
  lighting->model = SoLightModel::BASE_COLOR;
  root->addChild(lighting);
  SoMaterial* material = new SoMaterial;
  material->diffuseColor = SbColor(1, 1, 1);
  root->addChild(material);
  SoDrawStyle* drawStyle = style(SoDrawStyle::LINES);
  root->addChild(drawStyle);
  SoCoordinate3* coords=new SoCoordinate3;
  root->addChild(coords);SoGroup* geometry=new SoGroup;root->addChild(geometry);
  CoinRenderAction action(SbViewportRegion(64,64));action.setRenderTarget(target.get());
  const bool compareGl=std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE")!=nullptr;
  SoOffscreenRenderer gl(SbViewportRegion(64,64));gl.setComponents(SoOffscreenRenderer::RGB);
  bool ok=true;
  for(int count:{3,4,5})for(int indexed:{0,1}) {
    const int triangleXY[3][2]={{6,6},{25,6},{6,20}};
    const int quadXY[4][2]={{6,6},{25,6},{25,20},{6,20}};
    const int pentagonXY[5][2]={{6,6},{25,6},{25,13},{16,20},{6,20}};
    const int (*xy)[2]=count==3 ? triangleXY : (count==4 ? quadXY : pentagonXY);
    coords->point.setNum(count*2);
    for(int copy=0;copy<2;++copy)for(int i=0;i<count;++i)
      coords->point.set1Value(copy*count+i,SbVec3f((xy[i][0]+copy*32+.5f)/32-1,(xy[i][1]+.5f)/32-1,0));
    geometry->removeAllChildren();
    if(indexed) {
      SoIndexedFaceSet* shape=new SoIndexedFaceSet;std::vector<int32_t> indices;
      for(int copy=0;copy<2;++copy) {for(int i=0;i<count;++i)indices.push_back(copy*count+i);indices.push_back(-1);}
      shape->coordIndex.setValues(0,indices.size(),indices.data());geometry->addChild(shape);
    } else {
      SoFaceSet* shape=new SoFaceSet;const int32_t sizes[]={count,count};shape->numVertices.setValues(0,2,sizes);
      geometry->addChild(shape);
    }
  for(int fast:{0,1})for(int width:{1,3,6})for(int repeat:{1,2,256})
    for(uint32_t pattern:{0u,0xffffu,0x000fu,0xaaaau,0x9249u}) {
      action.setFastPathEnabled(fast ? TRUE : FALSE);drawStyle->lineWidth=width;
      drawStyle->linePattern=pattern;drawStyle->linePatternScaleFactor=repeat;
      action.apply(root);std::vector<uint8_t> image;target->readbackRGBA(image);
      if(!check(action.getLastStatus()==CoinRenderAction::SUCCESS && image.size()==64*64*4,
        "patterned original polygons render through shared Core")) {root->unref();return false;}
      const unsigned char* reference=nullptr;
      if(compareGl) {
        if(!check(gl.render(root) && gl.getBuffer(),"required Coin/GL polygon stipple reference renders")) {
          root->unref();return false;
        }
        reference=gl.getBuffer();
      }
      auto pixel=[&](int x,int y,int counter) {
        const bool expected=(pattern & (1u<<((counter/repeat)&15)))!=0;
        const bool actual=image[((63-y)*64+x)*4]>127;
        if(actual!=expected)std::cerr<<"stipple "<<cpu<<','<<width<<','<<repeat<<','<<pattern
          <<" at "<<x<<','<<y<<" counter "<<counter<<" got "<<actual<<'\n';
        ok=check(actual==expected,"pattern phase and reset agree with independent fragment counts") && ok;
        if(reference) {
          const bool same=(reference[(y*64+x)*3]>127)==actual;
          if(!same)std::cerr<<"GL stipple "<<count<<','<<indexed<<','<<width<<','<<repeat<<','<<pattern<<" at "<<x<<','<<y
            <<" GL "<<int(reference[(y*64+x)*3])<<" Core "<<int(image[((63-y)*64+x)*4])<<'\n';
          ok=check(same,"Coin/GL polygon stipple matches shared Core away from corner raster ties") && ok;
        }
      };
      // Interior samples avoid the GL implementation's permitted endpoint raster variation.
      for(int shift:{0,32}) {
        for(int x=10;x<=(count==3 ? 17 : 21);++x)pixel(x+shift,6,(count==5 ? 0 : 14)+x-6);
        if(count==4)for(int x=10;x<=21;++x)pixel(x+shift,20,47+25-x);
        if(count==5)for(int x=10;x<=12;++x)pixel(x+shift,20,35+16-x);
        for(int y=10;y<=(count==3 ? 14 : 16);++y)pixel(6+shift,y,(count==5 ? 45 : 0)+20-y);
        if(count==4)for(int y=10;y<=16;++y)pixel(25+shift,y,33+y-6);
      }
      if(!ok) {root->unref();return false;}
    }
  }
  if(compareGl)std::cout<<"Coin/GL polygon stipple reference passed\n";
  root->unref();return ok;
}
bool polygonClippedStippleActionContract(bool cpu) {
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(64, 64)));
  if (cpu)
    target->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);
#ifdef HAVE_COIN_BGFX
  target->setDepthReadbackEnabled(FALSE);
#endif
  SoSeparator* root = new SoSeparator;
  root->ref();
  SoOrthographicCamera* camera = new SoOrthographicCamera;
  camera->height = 2;
  camera->position = SbVec3f(0, 0, 3);
  camera->nearDistance = .1f;
  camera->farDistance = 10;
  root->addChild(camera);
  SoDepthBuffer* depth = new SoDepthBuffer;
  depth->function = SoDepthBuffer::LEQUAL;
  root->addChild(depth);
  SoLightModel* lighting = new SoLightModel;
  lighting->model = SoLightModel::BASE_COLOR;
  root->addChild(lighting);
  SoMaterial* material = new SoMaterial;
  material->diffuseColor = SbColor(1, 1, 1);
  root->addChild(material);
  SoDrawStyle* drawStyle = style(SoDrawStyle::LINES);
  root->addChild(drawStyle);
  SoClipPlane* firstClip = new SoClipPlane;
  root->addChild(firstClip);
  SoClipPlane* secondClip = new SoClipPlane;
  root->addChild(secondClip);
  SoCoordinate3* coords = new SoCoordinate3;
  root->addChild(coords);
  root->addChild(faces());
  CoinRenderAction action(SbViewportRegion(64, 64));
  action.setRenderTarget(target.get());
  bool ok = true;
  const bool compareGl = std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE") != nullptr;
  SoOffscreenRenderer gl(SbViewportRegion(64, 64));
  gl.setComponents(SoOffscreenRenderer::RGB);
  for (int clip = 0; clip < 8; ++clip) {
    const int xy[4][2] = {{6, 6}, {45, 6}, {45, 31}, {6, 31}};
    camera->nearDistance = clip >= 6 ? 1 : .1f;
    for (int i = 0; i < 4; ++i) {
      const float z =
          clip == 6 ? 2.5f - .05f * (xy[i][0] - 6) : (clip == 7 ? -5.f - .1f * (xy[i][0] - 6) : 0);
      coords->point.set1Value(i, SbVec3f((xy[i][0] + .5f) / 32 - 1, (xy[i][1] + .5f) / 32 - 1, z));
    }
    firstClip->on = clip > 0 && clip < 6 ? TRUE : FALSE;
    secondClip->on = clip == 5 ? TRUE : FALSE;
    if (clip == 1 || clip == 5)
      firstClip->plane = SbPlane(SbVec3f(1, 0, 0), 16.5f / 32 - 1);
    if (clip == 2)
      firstClip->plane = SbPlane(SbVec3f(-1, 0, 0), -(35.5f / 32 - 1));
    if (clip == 3)
      firstClip->plane = SbPlane(SbVec3f(0, 1, 0), 14.5f / 32 - 1);
    if (clip == 4)
      firstClip->plane = SbPlane(SbVec3f(0, -1, 0), -(24.5f / 32 - 1));
    secondClip->plane = SbPlane(SbVec3f(0, 1, 0), 14.5f / 32 - 1);
    const int left = clip == 1 || clip == 5 || clip == 6 ? 16 : 6,
              right = clip == 2 ? 35 : (clip == 7 ? 26 : 45);
    const int bottom = clip == 3 || clip == 5 ? 14 : 6, top = clip == 4 ? 24 : 31;
    const int width = right - left, height = top - bottom;
    struct GlSample {
      int counter, repeat, edge;
      uint32_t pattern;
      bool visible;
    };
    std::vector<GlSample> glSamples;
    size_t frustumCutDifferences = 0;
    for (int mode : {CoinRenderAction::BLEND, CoinRenderAction::DELAYED_BLEND,
                     CoinRenderAction::SORTED_OBJECT_BLEND})
      for (float transparency : {0.0f, .5f})
        for (int fast : {0, 1})
          for (int strokeWidth : {1, 3, 6})
            for (int repeat : {1, 2, 5})
              for (uint32_t pattern : {0u, 0xffffu, 0x000fu, 0xaaaau, 0x9249u}) {
                action.setTransparencyType(static_cast<CoinRenderAction::TransparencyType>(mode));
                gl.getGLRenderAction()->setTransparencyType(
                    static_cast<SoGLRenderAction::TransparencyType>(mode));
                material->transparency = transparency;
                action.setFastPathEnabled(fast ? TRUE : FALSE);
                drawStyle->lineWidth = strokeWidth;
                drawStyle->linePattern = pattern;
                drawStyle->linePatternScaleFactor = repeat;
                action.apply(root);
                std::vector<uint8_t> image;
                target->readbackRGBA(image);
                if (!check(action.getLastStatus() == CoinRenderAction::SUCCESS &&
                               image.size() == 64 * 64 * 4,
                           "clipped patterned contour renders")) {
                  root->unref();
                  return false;
                }
                const unsigned char* reference = nullptr;
                if (compareGl) {
                  if (!check(gl.render(root) && gl.getBuffer(),
                             "required Coin/GL clipped stipple renders")) {
                    root->unref();
                    return false;
                  }
                  reference = gl.getBuffer();
                }
                auto sample = [&](int x, int y, int counter, int edge) {
                  const bool expected = (pattern & (1u << ((counter / repeat) & 15))) != 0;
                  const bool actual = image[((63 - y) * 64 + x) * 4] > 40;
                  if (actual != expected)
                    std::cerr << "clipped stipple " << clip << ',' << strokeWidth << ',' << repeat
                              << ',' << pattern << " at " << x << ',' << y << " counter " << counter
                              << " Core " << actual << '\n';
                  ok = check(actual == expected &&
                                 std::abs(int(image[((63 - y) * 64 + x) * 4]) -
                                          (expected ? (transparency == 0 ? 255 : 128) : 0)) <= 2,
                             "clipped contour keeps its continuous counter and single source-over "
                             "coverage") &&
                       ok;
                  if (reference) {
                    if (pattern == 0 || pattern == 0xffffu || clip == 0) {
                      const bool same = (reference[(y * 64 + x) * 3] > 40) == actual;
                      const bool frustumCut = (clip == 6 && edge == 2) || (clip == 7 && edge == 3);
                      // The local Mesa reference can omit a newly created depth-plane
                      // edge. CoinRender keeps the clipped polygon's boundary as
                      // required by GL 2.1 section 2.12. Record this driver difference;
                      // qualify all user-plane cuts and retained edges exactly.
                      if (frustumCut && !same)
                        ++frustumCutDifferences;
                      else {
                        if (!same)
                          std::cerr << "GL clip geometry " << clip << ',' << strokeWidth << ','
                                    << pattern << " xy " << x << ',' << y << " GL "
                                    << int(reference[(y * 64 + x) * 3]) << " Core "
                                    << int(image[((63 - y) * 64 + x) * 4]) << '\n';
                        ok = check(same,
                                   "Coin/GL retained geometry and user-plane boundaries agree") &&
                             ok;
                      }
                    }
                    glSamples.push_back(
                        {counter, repeat, edge, pattern, reference[(y * 64 + x) * 3] > 40});
                  }
                };
                for (int x = left + 4; x < right - 3; ++x) {
                  sample(x, bottom, (clip == 4 ? width + height : height) + x - left, 0);
                  sample(x, top, (clip == 4 ? 0 : height + width + height) + right - x, 1);
                }
                for (int y = bottom + 4; y < top - 3; ++y) {
                  sample(left, y, (clip == 4 ? width : 0) + top - y, 2);
                  sample(right, y,
                         (clip == 4 ? width + height + width : height + width) + y - bottom, 3);
                }
                if (strokeWidth == 1 && pattern != 0xffffu) {
                  // Every original unit-width fragment, including the start pixel,
                  // belongs to one edge; terminal endpoints are excluded.
                  const int cornerPhase[4] = {clip == 4 ? width + height : height,
                                              clip == 4 ? width + height + width : height + width,
                                              clip == 4 ? 0 : height + width + height,
                                              clip == 4 ? width : 0};
                  const int cornerXY[4][2] = {
                      {left, bottom}, {right, bottom}, {right, top}, {left, top}};
                  for (int i = 0; i < 4; ++i) {
                    const bool expected = (pattern & (1u << ((cornerPhase[i] / repeat) & 15))) != 0;
                    const bool actual =
                        image[((63 - cornerXY[i][1]) * 64 + cornerXY[i][0]) * 4] > 40;
                    if (actual != expected)
                      std::cerr << "stipple corner " << clip << ',' << pattern << ',' << repeat
                                << " corner " << i << " counter " << cornerPhase[i] << " got "
                                << actual << '\n';
                    ok = check(
                             actual == expected &&
                                 std::abs(
                                     int(image[((63 - cornerXY[i][1]) * 64 + cornerXY[i][0]) * 4]) -
                                     (expected ? (transparency == 0 ? 255 : 128) : 0)) <= 2,
                             "stipple caps retain corner fragments without double blending") &&
                         ok;
                  }
                }
                if (!ok) {
                  root->unref();
                  return false;
                }
              }
    if (compareGl) {
      // GL allows an indeterminate initial phase on clipped segments.
      // Newly created cut edges may also be split by the driver's polygon
      // mechanism. Qualify retained edges by periodicity, with one phase per
      // edge across all masks/repeats/widths. Core continuity is checked above.
      for (int edge = 0; edge < 4; ++edge) {
        const bool cut = (edge == 2 && (clip == 1 || clip == 5 || clip == 6)) ||
                         (edge == 3 && (clip == 2 || clip == 7)) ||
                         (edge == 0 && (clip == 3 || clip == 5)) || (edge == 1 && clip == 4);
        if (cut)
          continue;
        bool periodic = false;
        int found = 0;
        const int perimeter = 2 * (width + height);
        for (int displacement = -perimeter; displacement <= perimeter && !periodic;
             ++displacement) {
          if (clip == 0 && displacement != 0)
            continue;
          bool matches = true;
          for (const auto& item : glSamples)
            if (item.edge == edge) {
              const int period = 16 * item.repeat;
              const int phase = ((item.counter + displacement) % period + period) % period;
              if (((item.pattern & (1u << (phase / item.repeat))) != 0) != item.visible) {
                matches = false;
                break;
              }
            }
          if (matches) {
            periodic = true;
            found = displacement;
          }
        }
        if (!periodic)
          std::cerr << "GL clipped pattern has no retained-edge phase for clip " << clip << " edge "
                    << edge << '\n';
        ok =
            check(periodic, "Coin/GL retained edge keeps pattern periodicity under clipping") && ok;
        if (periodic)
          std::cout << "Coin/GL clip " << clip << " edge " << edge << " phase displacement "
                    << found << '\n';
      }
    }
    if (compareGl && frustumCutDifferences)
      std::cout << "Coin/GL depth-plane cut " << clip << " differs in " << frustumCutDifferences
                << " samples; normative Core boundary tested separately\n";
    if (!ok) {
      root->unref();
      return false;
    }
  }
  const uint64_t serial = target->getLastSubmissionSerial();
  firstClip->on = TRUE;
  firstClip->plane = SbPlane(SbVec3f(1, 0, 0), 2);
  secondClip->on = FALSE;
  action.apply(root);
  std::vector<uint8_t> clear;
  target->readbackRGBA(clear);
  bool empty = clear.size() == 64 * 64 * 4;
  for (size_t i = 0; i + 3 < clear.size(); i += 4)
    empty = empty && clear[i] == 0 && clear[i + 1] == 0 && clear[i + 2] == 0;
  ok = check(action.getLastStatus() == CoinRenderAction::SUCCESS && empty &&
                 target->getLastSubmissionSerial() > serial,
             "fully clipped patterned polygon publishes a fresh clear") &&
       ok;
  firstClip->on = FALSE;
  action.apply(root);
  ok = check(action.getLastStatus() == CoinRenderAction::SUCCESS,
             "patterned polygon recovers after complete clipping") &&
       ok;
  if (compareGl)
    std::cout << "Coin/GL clipped geometry and retained-edge stipple reference passed\n";
  root->unref();
  return ok;
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
  SoDB::init();DerivedStyleCube::initClass();CoinRenderAction::initClass();
  if(!nativeStippleActionContract(true) || !polygonStippleCapCoreContract() || !polygonStippleCoreContract() || !stateContract() || !polygonCoreContract() || !slopeCoreContract() || !polygonCaptureContract() || !polygonUnsupportedActionContract(true) || !polygonClippedStippleActionContract(true) || !polygonStippleActionContract(true) || !polygonActionContract(true) || !slopeActionContract(true) || !homogeneousStrokeContract(true) || !polygonAttributesActionContract(true) || !actionContract(true))return 1;
  if(!CoinRenderAction::isGpuBackendAvailable()) {
    std::cerr<<"[SKIP] GPU adapter unavailable\n";return 77;
  }
  if(!nativeStippleActionContract(false) || !polygonUnsupportedActionContract(false) || !polygonClippedStippleActionContract(false) || !polygonStippleActionContract(false) || !polygonActionContract(false) || !slopeActionContract(false) || !homogeneousStrokeContract(false) || !polygonAttributesActionContract(false) || !actionContract(false))return 1;
  std::cout<<"DrawStyle polygon/Core/INVISIBLE CPU/GPU contracts passed\n";return 0;
}
