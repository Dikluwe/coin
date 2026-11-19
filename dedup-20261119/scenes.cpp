// Local before/after scenes. All image pixels are produced by Coin's legacy
// SoOffscreenRenderer. No custom GL geometry or synthetic image replacement.
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <Inventor/SoDB.h>
#include <Inventor/SoOutput.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/SoPickedPoint.h>
#include <Inventor/SoPrimitiveVertex.h>
#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/actions/SoRayPickAction.h>
#include <Inventor/actions/SoWriteAction.h>
#include <Inventor/elements/SoGLVBOElement.h>
#include <Inventor/nodes/SoCallback.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoDrawStyle.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoIndexedLineSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoNormal.h>
#include <Inventor/nodes/SoNormalBinding.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoScale.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoShapeHints.h>
#include <Inventor/nodes/SoSphere.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoCone.h>
#include <Inventor/nodes/SoCylinder.h>
#include <Inventor/VRMLnodes/SoVRMLBox.h>
#include <Inventor/VRMLnodes/SoVRMLCone.h>
#include <Inventor/VRMLnodes/SoVRMLCylinder.h>
#include <Inventor/VRMLnodes/SoVRMLSphere.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCubeMap.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoTextureCoordinate3.h>
#include <Inventor/nodes/SoTextureCoordinateCube.h>
#include <Inventor/nodes/SoTextureCoordinateCylinder.h>
#include <Inventor/nodes/SoTextureCoordinateSphere.h>
#include <Inventor/nodes/SoTextureCoordinateDefault.h>
#include <Inventor/nodes/SoTextureCoordinateNormalMap.h>
#include <Inventor/nodes/SoTextureCoordinateEnvironment.h>
#include <Inventor/nodes/SoTextureCoordinateReflectionMap.h>
#include <Inventor/nodes/SoTextureCoordinateObject.h>
#include <Inventor/nodes/SoTextureUnit.h>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

constexpr int W = 480, H = 320;
struct Observation {
  std::string renderer, version;
  bool positionVBO = false, textureVBO = false, equalVBO = false;
  int units = 0;
  bool texgen[2] = {false, false};
  std::vector<SbVec3f> visiblePoints;
  std::vector<SbVec4f> visibleUV;
};
void collect(void * data, SoCallbackAction *, const SoPrimitiveVertex * a,
             const SoPrimitiveVertex * b, const SoPrimitiveVertex * c) {
  Observation & o = *static_cast<Observation *>(data);
  for (const auto * v : {a,b,c}) {
    const auto & p = v->getPoint();
    if (p[2] == 0 && std::fabs(p[1]) < 10) {
      o.visiblePoints.push_back(p); o.visibleUV.push_back(v->getTextureCoords());
    }
  }
}
void observe(void * data, SoAction * action) {
  if (!action->isOfType(SoGLRenderAction::getClassTypeId())) return;
  Observation & o = *static_cast<Observation *>(data);
  const char * renderer = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
  const char * version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
  o.renderer = renderer ? renderer : "unknown";
  o.version = version ? version : "unknown";
  glGetIntegerv(GL_MAX_TEXTURE_UNITS, &o.units);
  GLint active; glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
  for (int i = 0; i < 2 && i < o.units; ++i) {
    glActiveTexture(GL_TEXTURE0 + i);
    o.texgen[i] = glIsEnabled(GL_TEXTURE_GEN_S);
  }
  glActiveTexture(active);
  const auto * vbo = SoGLVBOElement::getInstance(action->getState());
  const auto * p = vbo->getVertexVBO();
  const auto * t = vbo->getTexCoordVBO(0);
  o.positionVBO = p != nullptr; o.textureVBO = t != nullptr;
  o.equalVBO = p && p == t;
}
void unit(SoSeparator * root, int n) {
  auto * u = new SoTextureUnit; u->unit = n; root->addChild(u);
}
void texture(SoSeparator * root, bool white = false, bool stripes = false, bool shifted = false) {
  constexpr int N = 64;
  std::vector<unsigned char> data(N*N*3);
  for (int y = 0; y < N; ++y) for (int x = 0; x < N; ++x) {
    const int offset = shifted ? 4 : 0;
    const bool cell = (((x+offset)/8) + ((y+offset)/8)) & 1;
    unsigned char * p = &data[(y*N+x)*3];
    if (white) { p[0] = p[1] = p[2] = 255; }
    else if (stripes) { p[0] = x*255/(N-1); p[1] = 100; p[2] = 255-p[0]; }
    else { p[0] = cell ? 245 : 35; p[1] = cell ? 190 : 105; p[2] = cell ? 40 : 225; }
  }
  auto * t = new SoTexture2;
  t->image.setValue(SbVec2s(N,N), 3, data.data());
  t->wrapS = SoTexture2::CLAMP; t->wrapT = SoTexture2::CLAMP;
  t->model = SoTexture2::MODULATE; root->addChild(t);
}
void cubemap(SoSeparator * root) {
  auto * t = new SoTextureCubeMap;
  SoSFImage * faces[] = {&t->imagePosX, &t->imageNegX, &t->imagePosY,
                        &t->imageNegY, &t->imagePosZ, &t->imageNegZ};
  const unsigned char colors[6][3] = {{240,65,50},{40,190,210},{65,210,85},
                                     {225,80,190},{55,100,245},{240,190,40}};
  for (int f = 0; f < 6; ++f) {
    std::vector<unsigned char> data(32*32*3);
    for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) {
      const float level = ((x/8 + y/8)&1) ? 1.0f : .55f;
      for (int c = 0; c < 3; ++c) data[(y*32+x)*3+c] = colors[f][c]*level;
    }
    faces[f]->setValue(SbVec2s(32,32),3,data.data());
  }
  root->addChild(t);
}
void diagnosticTexture(SoSeparator * root) {
  constexpr int n=128;
  std::vector<unsigned char> bytes(n*n*3);
  for(int y=0;y<n;++y) for(int x=0;x<n;++x) {
    bytes[(y*n+x)*3]=24+208*x/(n-1);
    bytes[(y*n+x)*3+1]=24+208*y/(n-1);
    bytes[(y*n+x)*3+2]=255-(24+208*x/(n-1))/2;
  }
  auto * t=new SoTexture2; t->image.setValue(SbVec2s(n,n),3,bytes.data());
  t->wrapS=SoTexture2::CLAMP; t->wrapT=SoTexture2::CLAMP;
  root->addChild(t);
}
void quad(SoSeparator * root) {
  auto * c = new SoCoordinate3;
  const SbVec3f points[] = {{-1.6f,-1.1f,0},{1.6f,-1.1f,0},
                           {1.6f,1.1f,0},{-1.6f,1.1f,0}};
  c->point.setValues(0,4,points); root->addChild(c);
  auto * f = new SoFaceSet; f->numVertices.set1Value(0,4); root->addChild(f);
}
void triangle(SoSeparator * root, bool extreme) {
  auto * c = new SoCoordinate3;
  const SbVec3f points[] = {{0,0,0},{1,0,0},{0,1,0}};
  c->point.setValues(0,3,points); root->addChild(c);
  auto * tc = new SoTextureCoordinate2;
  const float m = extreme ? std::ldexp(1.0f,100) : 1.0f;
  const SbVec2f uv[] = {{m,.5f},{1,.5f},{-2*m,.5f}};
  tc->point.setValues(0,3,uv); root->addChild(tc);
  auto * f = new SoFaceSet; f->numVertices.set1Value(0,3); root->addChild(f);
}
void vboGrid(SoSeparator * root, Observation & o) {
  auto * hints = new SoShapeHints;
  hints->vertexOrdering = SoShapeHints::COUNTERCLOCKWISE; root->addChild(hints);
  std::vector<SbVec3f> p, uv;
  for (int row = 0; row < 2; ++row) for (int col = 0; col < 11; ++col) {
    float x = col/10.0f, y = float(row);
    p.push_back(SbVec3f(-1.6f+3.2f*x, -1.1f+2.2f*y, 0));
    uv.push_back(SbVec3f(x,y,0));
  }
  auto * c = new SoCoordinate3; c->point.setValues(0,p.size(),p.data()); root->addChild(c);
  auto * t = new SoTextureCoordinate3; t->point.setValues(0,uv.size(),uv.data()); root->addChild(t);
  auto * cb = new SoCallback; cb->setCallback(observe,&o); root->addChild(cb);
  std::vector<int32_t> indices;
  for (int col = 0; col < 10; ++col) for (int v : {col,col+1,col+12,col+11,-1}) indices.push_back(v);
  auto * f = new SoIndexedFaceSet; f->coordIndex.setValues(0,indices.size(),indices.data()); root->addChild(f);
}
void line(SoSeparator * root) {
  auto * draw = new SoDrawStyle; draw->lineWidth = 12; root->addChild(draw);
  auto * c = new SoCoordinate3;
  const SbVec3f p[] = {{-1.6f,0,0},{1.6f,0,0}};
  c->point.setValues(0,2,p); root->addChild(c);
  auto * t = new SoTextureCoordinate2;
  const SbVec2f uv[] = {{0,.5f},{1,.5f}};
  t->point.setValues(0,2,uv); root->addChild(t);
  auto * s = new SoIndexedLineSet;
  const int32_t idx[] = {0,1,-1}; s->coordIndex.setValues(0,3,idx); root->addChild(s);
}
void meshSphere(SoSeparator * root, float radius = 1) {
  // Standard Coin FaceSet, rather than SoSphere's specialized GL/pick paths.
  // The same geometry and normals are fed to both libraries.
  std::vector<SbVec3f> points, normals;
  constexpr int slices = 40, stacks = 24;
  const double pi = std::acos(-1.0);
  auto emit = [&](int x, int y) {
    const double latitude = -pi/2 + pi*y/stacks;
    const double longitude = 2*pi*x/slices;
    SbVec3f n(float(std::cos(latitude)*std::sin(longitude)),
              float(std::sin(latitude)),float(std::cos(latitude)*std::cos(longitude)));
    normals.push_back(n); points.push_back(n*radius);
  };
  for (int y=0;y<stacks;++y) for (int x=0;x<slices;++x) {
    emit(x,y); emit(x+1,y); emit(x+1,y+1);
    emit(x,y); emit(x+1,y+1); emit(x,y+1);
  }
  auto * coords = new SoCoordinate3;
  coords->point.setValues(0,points.size(),points.data()); root->addChild(coords);
  auto * binding = new SoNormalBinding; binding->value = SoNormalBinding::PER_VERTEX; root->addChild(binding);
  auto * n = new SoNormal; n->vector.setValues(0,normals.size(),normals.data()); root->addChild(n);
  std::vector<int32_t> counts(points.size()/3,3);
  auto * shape = new SoFaceSet; shape->numVertices.setValues(0,counts.size(),counts.data()); root->addChild(shape);
}
void wideBoxMesh(SoSeparator * root) {
  // Actual off-screen faces extend a finite bounding box beyond Float's
  // representable size. No synthetic bounding-box override is used.
  const float h = 2e38f;
  const SbVec3f points[] = {{-1.6f,-1.1f,0},{1.6f,-1.1f,0},{1.6f,1.1f,0},{-1.6f,1.1f,0},
                           {-1,h,-1},{1,h,-1},{0,h,1},
                           {-1,-h,-1},{1,-h,-1},{0,-h,1}};
  auto * coords = new SoCoordinate3; coords->point.setValues(0,10,points); root->addChild(coords);
  auto * bind = new SoNormalBinding; bind->value=SoNormalBinding::OVERALL; root->addChild(bind);
  auto * n = new SoNormal; n->vector.set1Value(0,SbVec3f(0,0,1)); root->addChild(n);
  const int32_t counts[] = {4,3,3};
  auto * shape = new SoFaceSet; shape->numVertices.setValues(0,3,counts); root->addChild(shape);
}
void number(std::ostream & os, float x) {
  if (std::isfinite(x)) os << x;
  else if (std::isnan(x)) os << "\"NaN\"";
  else os << (x>0 ? "\"Infinity\"" : "\"-Infinity\"");
}
void vec(std::ostream & os, const SbVec4f & v) {
  os << '[';
  for (int i=0;i<4;++i) { if(i)os<<',';number(os,v[i]); }
  os << ']';
}
int main(int argc, char ** argv) {
  if (argc != 3) return 2;
  const std::string scene = argv[1], out = argv[2];
  const bool native = scene.size()>7 && scene.substr(scene.size()-7)=="-native";
  const bool twod = scene.size()>3 && scene.substr(scene.size()-3)=="-2d";
  const bool fallback = scene.size()>9 && scene.substr(scene.size()-9)=="-fallback";
  const std::string kind = native ? scene.substr(0,scene.size()-7) : twod ? scene.substr(0,scene.size()-3) : fallback ? scene.substr(0,scene.size()-9) : scene;
  SoDB::init();
  Observation o;
  auto * root = new SoSeparator; root->ref();
  root->renderCaching = SoSeparator::OFF;
  auto * camera = new SoOrthographicCamera;
  camera->position.setValue(0,0,6); camera->height = 3.0f;
  camera->nearDistance = .1f; camera->farDistance = 20; root->addChild(camera);
  auto * light = new SoLightModel; light->model = SoLightModel::BASE_COLOR; root->addChild(light);
  auto * mat = new SoMaterial; mat->diffuseColor.setValue(1,1,1); root->addChild(mat);
  auto * complexity = new SoComplexity; complexity->value = .65f; root->addChild(complexity);
  auto * observer = new SoCallback; observer->setCallback(observe,&o);
  if (scene.find("transition-") == 0) {
    unit(root,0); texture(root,true);
    unit(root,1); texture(root);
    auto * prior = new SoTextureCoordinateObject;
    prior->factorS.setValue(0,0,0,.1f); prior->factorT.setValue(0,0,0,.1f);
    root->addChild(prior);
    if (scene == "transition-default") root->addChild(new SoTextureCoordinateDefault);
    else if (scene == "transition-cube") root->addChild(new SoTextureCoordinateCube);
    else if (scene == "transition-cylinder") root->addChild(new SoTextureCoordinateCylinder);
    else if (scene == "transition-sphere") root->addChild(new SoTextureCoordinateSphere);
    else return 2;
    root->addChild(observer); quad(root);
  }
  else if (scene == "vbo-coordinate3") {
    texture(root); vboGrid(root,o);
  }
  else if (scene.find("analytic-") == 0) {
    cubemap(root); root->addChild(new SoTextureCoordinateNormalMap);
    root->addChild(observer);
    if (scene=="analytic-cube") root->addChild(new SoCube);
    else if (scene=="analytic-cone") root->addChild(new SoCone);
    else if (scene=="analytic-cylinder") root->addChild(new SoCylinder);
    else if (scene=="analytic-vrmlsphere") root->addChild(new SoVRMLSphere);
    else if (scene=="analytic-vrmlbox") root->addChild(new SoVRMLBox);
    else if (scene=="analytic-vrmlcone") root->addChild(new SoVRMLCone);
    else if (scene=="analytic-vrmlcylinder") root->addChild(new SoVRMLCylinder);
    else return 2;
  }
  else if (scene == "bundle-normal") {
    texture(root); root->addChild(new SoTextureCoordinateDefault);
    root->addChild(observer); quad(root);
  }
  else if (scene == "legacy-default-native") {
    texture(root); root->addChild(observer); root->addChild(new SoSphere);
  }
  else if (scene == "bbox-cube" || scene == "bbox-cylinder" || scene == "bbox-bundle" ||
           scene == "bbox-bundle-gradient" || scene == "bbox-bundle-reference" || scene == "bbox-bundle-reference-checker") {
    if (scene == "bbox-bundle-gradient" || scene == "bbox-bundle-reference") diagnosticTexture(root);
    else texture(root,false,false,scene=="bbox-cube");
    if (scene == "bbox-cube") root->addChild(new SoTextureCoordinateCube);
    else if (scene == "bbox-cylinder") root->addChild(new SoTextureCoordinateCylinder);
    else if (scene == "bbox-bundle-reference" || scene == "bbox-bundle-reference-checker") {
      // Explicit analytic oracle, independent of Bundle's numerical algorithm.
      auto * tc=new SoTextureCoordinate2;
      const SbVec2f uv[]={{.5f,0},{.5f,1},{.5f,1},{.5f,0},
                          {.5f,.5f},{.5f,.5f},{.5f,.5f},{.5f,.5f},{.5f,.5f},{.5f,.5f}};
      tc->point.setValues(0,10,uv); root->addChild(tc);
    }
    else root->addChild(new SoTextureCoordinateDefault);
    root->addChild(observer); wideBoxMesh(root);
  }
  else if (scene == "line-picking") {
    texture(root,false,true); root->addChild(observer); line(root);
  }
  else if (scene == "triangle-picking") {
    camera->position.setValue(.5f,.5f,6); camera->height = 1.6f;
    texture(root,false,true); root->addChild(observer); triangle(root,true);
  }
  else if (kind == "normalmap" || kind == "reflectionmap" || kind == "environment") {
    auto * lit = new SoLightModel; lit->model = SoLightModel::PHONG; root->addChild(lit);
    root->addChild(new SoDirectionalLight);
    if (kind == "environment") { texture(root); root->addChild(new SoTextureCoordinateEnvironment); }
    else {
      if (twod) texture(root);
      else { if (fallback) texture(root,true); cubemap(root); }
      if (kind == "normalmap") root->addChild(new SoTextureCoordinateNormalMap);
      else root->addChild(new SoTextureCoordinateReflectionMap);
    }
    root->addChild(observer);
    if (native) root->addChild(new SoSphere); else meshSphere(root);
  }
  else if (kind == "sphere-huge" || kind == "sphere-tiny" || kind == "sphere-control") {
    const float radius = kind == "sphere-huge" ? 1e20f : kind == "sphere-tiny" ? 1e-30f : 1;
    auto * scale = new SoScale; scale->scaleFactor.setValue(1/radius,1/radius,1/radius); root->addChild(scale);
    texture(root); root->addChild(new SoTextureCoordinateSphere); root->addChild(observer);
    if (native) { auto * sphere = new SoSphere; sphere->radius = radius; root->addChild(sphere); }
    else meshSphere(root,radius);
  }
  else return 2;
  {
    SoWriteAction writer;
    if (!writer.getOutput()->openFile((out+".iv").c_str())) return 3;
    writer.apply(root); writer.getOutput()->closeFile();
  }
  {
    SoOffscreenRenderer renderer(SbViewportRegion(W,H));
    renderer.setComponents(SoOffscreenRenderer::RGB);
    renderer.setBackgroundColor(SbColor(.055f,.065f,.085f));
    if (!renderer.render(root)) { std::cerr << "Coin render failed\n"; return 4; }
    const unsigned char * pixels = renderer.getBuffer();
    std::ofstream image(out+".ppm",std::ios::binary);
    image << "P6\n" << W << ' ' << H << "\n255\n";
    for (int y=H-1;y>=0;--y) image.write(reinterpret_cast<const char *>(pixels+y*W*3),W*3);
  }
  if (scene.find("bbox-")==0) {
    SoCallbackAction action;
    action.addTriangleCallback(SoFaceSet::getClassTypeId(),collect,&o);
    action.apply(root);
  }
  std::ofstream data(out+".json"); data.precision(9);
  data << "{\"scene\":\""<<scene<<"\",\"renderer\":\""<<o.renderer<<"\",\"version\":\""<<o.version
       <<"\",\"texture_units\":"<<o.units<<",\"position_vbo\":"<<o.positionVBO
       <<",\"texture_vbo\":"<<o.textureVBO<<",\"same_vbo\":"<<o.equalVBO
       <<",\"texgen\":["<<o.texgen[0]<<','<<o.texgen[1]<<"],\"picks\":[";
  const bool isline = scene == "line-picking";
  const bool istriangle = scene == "triangle-picking";
  int n = kind == "sphere-huge" || kind == "sphere-tiny" || scene.find("bbox-")==0 ? 0 : isline ? 5 : 1;
  for (int i=0;i<n;++i) {
    const float t = .125f+i*.1875f;
    SbVec3f point = isline ? SbVec3f(-1.6f+3.2f*t,0,5) :
                    istriangle ? SbVec3f(.25f,.25f,5) : SbVec3f(.4f,.2f,5);
    SoRayPickAction pick(SbViewportRegion(W,H)); pick.enableCulling(FALSE);
    pick.setRay(point,SbVec3f(0,0,-1)); pick.apply(root);
    const SoPickedPoint * hit = pick.getPickedPoint();
    if(i)data<<',';
    data << "{\"ray_x\":"<<point[0]<<",\"ray_y\":"<<point[1]<<",\"hit\":"<<(hit?1:0);
    if(hit) {
      data << ",\"texture\":"; vec(data,hit->getTextureCoords());
      const auto p=hit->getPoint(); data<<",\"point\":["<<p[0]<<','<<p[1]<<','<<p[2]<<']';
    }
    data << '}';
  }
  data << "],\"visible_vertices\":[";
  for (size_t i=0;i<o.visibleUV.size();++i) {
    if(i)data<<',';
    const auto & p=o.visiblePoints[i];
    data << "{\"point\":["<<p[0]<<','<<p[1]<<','<<p[2]<<"],\"texture\":";
    vec(data,o.visibleUV[i]);data<<'}';
  }
  data << "]}\n";
  root->unref(); SoDB::finish();
  return 0;
}
