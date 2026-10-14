// Diagnostic campaign only: differences are measurements, never relaxed gates.
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/system/gl.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/SoPrimitiveVertex.h>
#include <Inventor/nodes/SoShape.h>
#include <Inventor/nodes/SoSubNode.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoTransform.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoSphere.h>
#include <Inventor/nodes/SoCone.h>
#include <Inventor/nodes/SoDrawStyle.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinrender/CoinRenderPolygonStyleCore.h"
#include "rendering/coinrender/CoinRenderStrokeCore.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

struct StudyPolygons {
  std::vector<std::vector<CoinRenderVertexSnapshot>> rings;
  std::vector<CoinRenderVertexSnapshot> pending;
  static void triangle(void * data, SoCallbackAction * action, const SoPrimitiveVertex * a,
                       const SoPrimitiveVertex * b, const SoPrimitiveVertex * c) {
    auto & self=*static_cast<StudyPolygons *>(data);
    auto convert=[](const SoPrimitiveVertex * source) {
      CoinRenderVertexSnapshot v;
      for(int i=0;i<3;++i) v.position[i]=source->getPoint()[i];
      return v;
    };
    bool sphere=action->getCurPathTail()->getTypeId()==SoSphere::getClassTypeId();
    bool pole=false;
    if(sphere) for(auto * v:{a,b,c})
      pole|=v->getPoint()[0]==0 && v->getPoint()[2]==0;
    if(sphere && !pole) {
      if(self.pending.empty()) self.pending={convert(a),convert(b),convert(c)};
      else {
        if(SbVec3f(self.pending[0].position)!=a->getPoint() ||
           SbVec3f(self.pending[2].position)!=b->getPoint())
          throw std::runtime_error("sphere quad reconstruction failed");
        self.pending.push_back(convert(c)); self.rings.push_back(self.pending); self.pending.clear();
      }
    } else self.rings.push_back({convert(a),convert(b),convert(c)});
  }
};

class RasterWitness : public SoNode {
  SO_NODE_HEADER(RasterWitness);
public:
  static void initClass() { SO_NODE_INIT_CLASS(RasterWitness, SoNode, "Node"); }
  RasterWitness() { SO_NODE_CONSTRUCTOR(RasterWitness); }
  int side = 80;
  const CoinRenderFramePlan * replay = nullptr;
  std::vector<float> depth;
  std::vector<uint8_t> color;
  const StudyPolygons * polygons=nullptr;
  const CoinRenderFramePlan * sourcePlan=nullptr;
  CoinRenderTarget * cpuTarget=nullptr;
  std::string polygonPath;
  float lineWidth=4;
  void probePolygons();
  void GLRender(SoGLRenderAction *) override {
    if (replay) {
      glPushAttrib(GL_ALL_ATTRIB_BITS);
      glDisable(GL_LIGHTING); glDisable(GL_TEXTURE_2D); glDisable(GL_BLEND);
      glDisable(GL_LINE_SMOOTH); glDisable(GL_POLYGON_SMOOTH); glDisable(GL_SCISSOR_TEST);
      glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
      glDepthMask(GL_TRUE); glClearColor(0,0,0,1); glClearDepth(1);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      glMatrixMode(GL_PROJECTION); glPushMatrix();
      glMatrixMode(GL_MODELVIEW); glPushMatrix();
      for (const auto & draw : replay->draws) {
        if (draw.topology != CoinRenderPrimitiveTopology::TRIANGLE_LIST)
          throw std::runtime_error("replay requires resolved triangles");
        const auto & s = replay->renderStates[draw.renderStateSlot];
        const auto & vp = replay->viewports[s.viewportSlot];
        glViewport(vp.x, vp.y, vp.width, vp.height);
        if (s.depthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        glDepthMask(s.depthWrite); glDepthFunc(GL_LESS);
        glDepthRange(s.depthRange[0], s.depthRange[1]);
        glDisable(GL_CULL_FACE); // All study source scenes and plans disable culling.
        glMatrixMode(GL_PROJECTION); glLoadMatrixf(s.projectionCoin.getValue()[0]);
        const SbMatrix mv = s.model * s.view;
        glMatrixMode(GL_MODELVIEW); glLoadMatrixf(mv.getValue()[0]);
        glColor3f(1,1,1); glBegin(GL_TRIANGLES);
        for (uint32_t j=0; j<draw.geometry.indexCount; ++j) {
          const auto & v = replay->vertices[replay->indices[draw.geometry.firstIndex+j]];
          const float w = v.screenSpaceW;
          glVertex4f(v.position[0]*w, v.position[1]*w, v.position[2]*w, w);
        }
        glEnd();
      }
      glMatrixMode(GL_MODELVIEW); glPopMatrix();
      glMatrixMode(GL_PROJECTION); glPopMatrix(); glPopAttrib();
    }
    GLint bits=0, samples=0, subpixel=0;
    glGetIntegerv(GL_DEPTH_BITS,&bits); glGetIntegerv(GL_SAMPLES,&samples);
    glGetIntegerv(GL_SUBPIXEL_BITS,&subpixel);
    std::cout << "GL vendor=" << glGetString(GL_VENDOR) << " renderer=" << glGetString(GL_RENDERER)
              << " version=" << glGetString(GL_VERSION) << " depth_bits=" << bits
              << " samples=" << samples << " subpixel_bits=" << subpixel << '\n';
    color.resize(side*side*4); std::vector<uint8_t> bottomColor(side*side*4);
    glReadPixels(0,0,side,side,GL_RGBA,GL_UNSIGNED_BYTE,bottomColor.data());
    for(int y=0;y<side;++y) std::copy_n(bottomColor.data()+(side-1-y)*side*4,side*4,color.data()+y*side*4);
    depth.resize(side*side); std::vector<float> bottom(side*side);
    glReadPixels(0,0,side,side,GL_DEPTH_COMPONENT,GL_FLOAT,bottom.data());
    for (int y=0;y<side;++y)
      std::copy_n(bottom.data()+(side-1-y)*side,side,depth.data()+y*side);
    if (glGetError()!=GL_NO_ERROR) throw std::runtime_error("GL witness error");
    if(polygons && !replay) probePolygons();
  }
protected:
  ~RasterWitness() override = default;
};
SO_NODE_SOURCE(RasterWitness);

namespace {
class StudyCapture : public CoinRenderCpuReferenceBackend {
public:
  CoinRenderFramePlan plan;
  CoinRenderSubmitResult submit(const CoinRenderFramePlan & p, CoinRenderTargetP & t) override {
    plan=p; return CoinRenderCpuReferenceBackend::submit(p,t);
  }
};
struct Image { std::vector<uint8_t> rgba; std::vector<float> depth; };
void save(const Image & image, int side, const std::string & stem) {
  if (image.rgba.size()!=size_t(side*side*4) || image.depth.size()!=size_t(side*side))
    throw std::runtime_error("missing full color/depth readback");
  std::ofstream rgb(stem+".ppm",std::ios::binary), depth(stem+".depth-f32",std::ios::binary);
  rgb << "P6\n" << side << ' ' << side << "\n255\n";
  for (int p=0;p<side*side;++p) rgb.write(reinterpret_cast<const char*>(image.rgba.data()+p*4),3);
  depth.write(reinterpret_cast<const char*>(image.depth.data()),image.depth.size()*sizeof(float));
  if (!rgb || !depth) throw std::runtime_error("cannot save study images");
}
Image readTarget(CoinRenderTarget & target) {
  Image image; target.readbackRGBA(image.rgba); target.readbackDepth(image.depth); return image;
}
Image native(SoSeparator * root, RasterWitness * witness, int side) {
  SoOffscreenRenderer gl(SbViewportRegion(side,side));
  gl.setComponents(SoOffscreenRenderer::RGB); gl.setBackgroundColor(SbColor(0,0,0));
  if (!gl.render(root) || !gl.getBuffer()) throw std::runtime_error("CoinGL unavailable");
  Image image; image.rgba=witness->color; image.depth=witness->depth;
  return image;
}
void compare(const Image & a, const Image & b, int side, const std::string & label,
             const std::string & directory) {
  size_t coverage=0, over3=0, shared=0, depthOver=0; double sum=0, maxDepth=0;
  std::ofstream pixels(directory+"/"+label+".csv");
  pixels << "x,y,a_covered,b_covered,a_depth,b_depth,depth_delta,rgb_max\n" << std::setprecision(10);
  for (int p=0;p<side*side;++p) {
    bool ac=a.rgba[p*4]!=0,bc=b.rgba[p*4]!=0; int maximum=0;
    for(int c=0;c<3;++c) { int d=std::abs(int(a.rgba[p*4+c])-int(b.rgba[p*4+c])); sum+=d; maximum=std::max(maximum,d); }
    coverage+=ac!=bc; over3+=maximum>3;
    double delta=std::abs(double(a.depth[p])-b.depth[p]);
    if(ac&&bc) { ++shared; maxDepth=std::max(maxDepth,delta); depthOver+=delta>1e-5; }
    if(maximum || ((ac||bc)&&delta>1e-5))
      pixels<<p%side<<','<<p/side<<','<<ac<<','<<bc<<','<<a.depth[p]<<','<<b.depth[p]<<','<<delta<<','<<maximum<<'\n';
  }
  if (!pixels) throw std::runtime_error("cannot save pixel diagnostics");
  std::cout<<label<<" rgb_mae="<<sum/(side*side*3)<<" pixels_over3="<<over3
           <<" coverage_xor="<<coverage<<" shared="<<shared<<" shared_depth_over1e-5="<<depthOver
           <<" shared_depth_max="<<maxDepth<<'\n';
}
}
namespace {
// Controlled white scenes; camera geometry matches the camera-reference fixture.
SoSeparator * scene(const std::string & kind, int frame, float width, bool filled) {
  auto * root=new SoSeparator; root->ref();
  if (kind=="camera") {
    auto * camera=new SoPerspectiveCamera;
    camera->position.setValue(.6f*std::sin(frame*.7f),.4f*std::cos(frame*.8f),28-frame*.25f);
    camera->orientation.setValue(SbVec3f(0,1,0),.035f*frame);
    camera->heightAngle=.78f+.01f*frame; camera->nearDistance=1+.03f*frame; camera->farDistance=80;
    root->addChild(camera);
    auto * transform=new SoTransform; transform->translation.setValue(.3f,-.2f,.4f);
    transform->rotation.setValue(SbVec3f(0,1,0),.15f); root->addChild(transform);
  } else {
    auto * camera=new SoOrthographicCamera; camera->position.setValue(0,0,4); camera->height=3;
    root->addChild(camera);
  }
  auto * lm=new SoLightModel; lm->model=SoLightModel::BASE_COLOR; root->addChild(lm);
  auto * material=new SoMaterial; material->diffuseColor.setValue(1,1,1); root->addChild(material);
  auto * style=new SoDrawStyle; style->style=filled?SoDrawStyle::FILLED:SoDrawStyle::LINES;
  style->lineWidth=width; root->addChild(style);
  if (kind=="camera") {
    auto * cube=new SoCube;
    for(int y=0;y<16;++y) for(int x=0;x<16;++x) {
      auto * group=new SoSeparator; auto * t=new SoTransform;
      t->translation.setValue(x-7.5f,y-7.5f,.15f*((x+y)%3));
      t->scaleFactor.setValue(.32f,.37f,.45f); t->rotation.setValue(SbVec3f(1,2,3),.05f*(x%4));
      group->addChild(t); group->addChild(cube); root->addChild(group);
    }
  } else if(kind=="sphere") {
    auto * shape=new SoSphere; shape->radius=.8f; root->addChild(shape);
  } else if(kind=="cone") {
    auto * shape=new SoCone; shape->height=1.4f; shape->bottomRadius=.7f; root->addChild(shape);
  } else throw std::runtime_error("unknown scene");
  return root;
}
// Pretransforming constant-color diagnostic triangles is intentionally separate
// from production lowering. Every draw gets private vertices and one identity state.
CoinRenderFramePlan bake(const CoinRenderFramePlan & source, const std::string & mode,
                         int side, const std::string & directory) {
  if(mode=="native") return source;
  CoinRenderFramePlan p=source; p.vertices.clear(); p.indices.clear();
  std::ofstream vertices(directory+"/projected.csv");
  vertices<<"draw,source_vertex,x_float,y_float,x_double,y_double,z_double,w_double\n"<<std::setprecision(15);
  for(size_t di=0;di<p.draws.size();++di) {
    auto & d=p.draws[di]; const auto range=d.geometry;
    auto s=source.renderStates[d.renderStateSlot];
    // Coin convention: row-vector model * view * projection, z in [-1,1].
    SbMatrix m=s.model*s.view*s.projectionCoin;
    double md[4][4]={},mv[4][4]={};
    for(int i=0;i<4;++i) for(int j=0;j<4;++j)
      for(int k=0;k<4;++k) mv[i][j]+=double(s.model[i][k])*s.view[k][j];
    for(int i=0;i<4;++i) for(int j=0;j<4;++j)
      for(int k=0;k<4;++k) md[i][j]+=mv[i][k]*s.projectionCoin[k][j];
    d.geometry.firstVertex=uint32_t(p.vertices.size()); d.geometry.firstIndex=uint32_t(p.indices.size());
    d.geometry.vertexCount=range.indexCount;
    for(uint32_t j=0;j<range.indexCount;++j) {
      uint32_t index=source.indices[range.firstIndex+j]; auto v=source.vertices[index];
      double cd[4]={}; float cf[4]={};
      for(int c=0;c<4;++c) for(int k=0;k<4;++k) {
        const float a=k==3?1:v.position[k]; cf[c]+=a*m[k][c]; cd[c]+=double(a)*md[k][c];
      }
      if(cf[3]<=0 || cd[3]<=0) throw std::runtime_error("bake only supports positive-W study scenes");
      vertices<<di<<','<<index<<','<<(cf[0]/cf[3]+1)*side*.5<<','<<(1-cf[1]/cf[3])*side*.5
              <<','<<(cd[0]/cd[3]+1)*side*.5<<','<<(1-cd[1]/cd[3])*side*.5<<','<<cd[2]/cd[3]<<','<<cd[3]<<'\n';
      for(int c=0;c<3;++c) v.position[c]=mode=="float"?cf[c]/cf[3]:float(cd[c]/cd[3]);
      v.screenSpaceW=mode=="float"?cf[3]:float(cd[3]);
      if(mode=="snap8") {
        float x=(v.position[0]+1)*side*.5f,y=(1-v.position[1])*side*.5f;
        x=std::round(x*256)/256; y=std::round(y*256)/256;
        v.position[0]=2*x/side-1; v.position[1]=1-2*y/side;
      }
      p.indices.push_back(uint32_t(p.vertices.size())); p.vertices.push_back(v);
    }
    s.model=s.view=s.projectionCoin=SbMatrix::identity();
    d.renderStateSlot=uint32_t(p.renderStates.size()); p.renderStates.push_back(s);
  }
  ++p.revision;
  if(!vertices) throw std::runtime_error("cannot save projected vertices");
  return p;
}
}
int main(int argc,char ** argv) {
  if(argc!=7 && argc!=8) {
    std::cerr<<"Usage: CoinRenderRasterStudy camera|sphere|cone frame width filled|lines native|float|double|snap8 output-directory [--polygons]\n";
    return 2;
  }
  try {
    const std::string kind=argv[1],mode=argv[5],directory=argv[6];
    if(mode!="native" && mode!="float" && mode!="double" && mode!="snap8") throw std::runtime_error("unknown projection mode");
    int side=kind=="camera"?256:80;
    SoDB::init(); CoinRenderAction::initClass(); RasterWitness::initClass();
    CoinRenderCapabilities caps{};
    if(coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,&caps,sizeof(caps)) || !caps.gpu_available)
      throw std::runtime_error("physical GPU required");
    std::cout<<"backend="<<caps.backend<<" renderer="<<caps.renderer<<" vendor_id="<<caps.vendor_id
             <<" device_id="<<caps.device_id<<" adapter="<<caps.adapter_name<<'\n';
    std::cout<<"scene="<<kind<<" frame="<<argv[2]<<" width="<<argv[3]<<" style="<<argv[4]<<" transform="<<mode<<'\n';
    auto * root=scene(kind,std::stoi(argv[2]),std::stof(argv[3]),std::string(argv[4])=="filled");
    auto * witness=new RasterWitness; witness->side=side; root->addChild(witness);
    std::unique_ptr<CoinRenderTarget> cpu(CoinRenderTarget::createOffscreen(SbVec2i32(side,side)));
    std::unique_ptr<CoinRenderTarget> gpu(CoinRenderTarget::createOffscreen(SbVec2i32(side,side)));
    auto * capture=new StudyCapture; cpu->getPimpl()->backend.reset(capture);
    CoinRenderAction action(SbViewportRegion(side,side)); action.setRenderTarget(cpu.get());
    action.setFastPathEnabled(FALSE); action.setBackgroundColor(SbColor4f(0,0,0,1));
    action.apply(root);
    if(action.getLastStatus()!=CoinRenderAction::SUCCESS) throw std::runtime_error(action.getLastError().getString());
    CoinRenderFramePlan p=capture->plan;
    std::ofstream stateFile(directory+"/source-state.json");
    const auto & source=p.renderStates.front();
    const auto mvp=source.model*source.view*source.projectionCoin;
    const auto & viewport=p.viewports[source.viewportSlot];
    stateFile<<std::setprecision(10)<<"{\"mvp\":[";
    for(int row=0;row<4;++row) {
      if(row) stateFile<<','; stateFile<<'[';
      for(int col=0;col<4;++col) { if(col) stateFile<<','; stateFile<<mvp[row][col]; }
      stateFile<<']';
    }
    stateFile<<"],\"viewport\":["<<viewport.x<<','<<viewport.y<<','<<viewport.width<<','<<viewport.height<<"]}\n";
    if(!stateFile) throw std::runtime_error("cannot save source matrices");
    // Disable culling identically in native scenes (default UNKNOWN_FACE_TYPE)
    // and study plans to keep replay coverage comparisons on the same input.
    for(auto & s:p.renderStates) s.cullMode=CoinRenderCullMode::NONE;
    p=bake(p,mode,side,directory); ++p.revision;
    for (auto * target : {cpu.get(), gpu.get()}) {
      auto result=target->getPimpl()->executeFrame(p);
      if(result.status!=CoinRenderBackendStatus::SUCCESS) throw std::runtime_error(result.diagnostic);
    }
    if(cpu->getStatus()!=CoinRenderTarget::TARGET_READY || gpu->getStatus()!=CoinRenderTarget::TARGET_READY)
      throw std::runtime_error("frame execution failed");
    StudyPolygons polygons;
    if(argc==8) {
      if(std::string(argv[7])!="--polygons" || kind=="camera" || std::string(argv[4])!="lines" || mode!="native")
        throw std::runtime_error("polygon study requires native sphere/cone lines");
      SoCallbackAction callback;
      callback.addTriangleCallback(SoShape::getClassTypeId(),StudyPolygons::triangle,&polygons);
      callback.apply(root);
      if(!polygons.pending.empty()) throw std::runtime_error("incomplete sphere contour");
      witness->polygons=&polygons; witness->sourcePlan=&p; witness->cpuTarget=cpu.get();
      witness->lineWidth=std::stof(argv[3]); witness->polygonPath=directory+"/polygons.csv";
    }
    Image c=readTarget(*cpu),g=readTarget(*gpu),n=native(root,witness,side);
    witness->polygons=nullptr;
    witness->replay=&p; Image r=native(root,witness,side); witness->replay=nullptr;
    save(c,side,directory+"/cpu"); save(g,side,directory+"/gpu");
    save(n,side,directory+"/coin-gl"); save(r,side,directory+"/replay-gl");
    compare(c,g,side,"cpu-gpu",directory); compare(c,n,side,"cpu-coin",directory);
    compare(g,n,side,"gpu-coin",directory); compare(c,r,side,"cpu-replay",directory);
    compare(g,r,side,"gpu-replay",directory);
    for(auto point:{SbVec2s(33,20),SbVec2s(26,21),SbVec2s(26,23)}) {
      int i=point[1]*side+point[0];
      std::cout<<"witness x="<<point[0]<<" y="<<point[1]<<std::setprecision(10)
               <<" cpu="<<c.depth[i]<<" gpu="<<g.depth[i]<<" coin="<<n.depth[i]<<" replay="<<r.depth[i]<<'\n';
    }
    action.setRenderTarget(nullptr); root->unref();
    std::cout<<"Diagnostic completed; no visual gate was applied or waived.\n"; return 0;
  } catch(const std::exception & error) { std::cerr<<error.what()<<'\n'; return 1; }
}

void RasterWitness::probePolygons() {
  std::ofstream log(polygonPath), edges(polygonPath+".edges"), vertices(polygonPath+".vertices");
  edges<<"polygon,edge,x,y,common_covered,common_depth,canonical_covered,canonical_depth,authored_covered,authored_depth,reversed_covered,reversed_depth\n"<<std::setprecision(10);
  vertices<<"polygon,vertex,x,y,z\n"<<std::setprecision(10);
  log << "polygon,x,y,common_covered,common_depth,polygon_covered,polygon_depth,line_covered,line_depth\n" << std::setprecision(10);
  glPushAttrib(GL_ALL_ATTRIB_BITS);
  glMatrixMode(GL_PROJECTION); glPushMatrix();
  glMatrixMode(GL_MODELVIEW); glPushMatrix();
  glDisable(GL_LIGHTING); glDisable(GL_TEXTURE_2D); glDisable(GL_BLEND);
  glDisable(GL_CULL_FACE); glDisable(GL_SCISSOR_TEST); glDisable(GL_LINE_SMOOTH);
  glDisable(GL_POLYGON_SMOOTH); glDisable(GL_LINE_STIPPLE);
  glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE);
  glClearColor(0,0,0,1); glClearDepth(1); glColor3f(1,1,1); glLineWidth(lineWidth);
  const auto sourceState=sourcePlan->renderStates.front();
  glMatrixMode(GL_PROJECTION); glLoadMatrixf(sourceState.projectionCoin.getValue()[0]);
  const auto mv=sourceState.model*sourceState.view;
  glMatrixMode(GL_MODELVIEW); glLoadMatrixf(mv.getValue()[0]);
  glDepthRange(sourceState.depthRange[0],sourceState.depthRange[1]);
  const SbVec2s points[]={SbVec2s(33,20),SbVec2s(26,21),SbVec2s(26,23)};
  for(size_t i=0;i<polygons->rings.size();++i) {
    const auto & ring=polygons->rings[i];
    for(size_t j=0;j<ring.size();++j) vertices<<i<<','<<j<<','<<ring[j].position[0]<<','<<ring[j].position[1]<<','<<ring[j].position[2]<<'\n';
    CoinRenderFramePlan plan=*sourcePlan;
    plan.draws.clear(); plan.vertices.clear(); plan.indices.clear(); plan.renderStates.clear();
    CoinRenderPolygonStyleResult result; std::string error;
    if(!coin_render_prepare_polygon_style(ring,sourceState,plan.materials,
        plan.lightingStates[sourceState.lightingSlot],CoinRenderPolygonStyle::LINES,result,error,
        plan.viewports[sourceState.viewportSlot])) throw std::runtime_error(error);
    plan.renderStates.push_back(result.state);
    for(const auto & v:result.vertices) plan.vertices.push_back(v.vertex);
    plan.indices=result.indices;
    CoinRenderDrawPacket draw; draw.topology=result.topology;
    draw.geometry.vertexCount=uint32_t(plan.vertices.size()); draw.geometry.indexCount=uint32_t(plan.indices.size());
    if(!plan.indices.empty()) plan.draws.push_back(draw);
    if(!coin_render_expand_strokes(plan,error)) throw std::runtime_error(error);
    plan.revision=sourcePlan->revision+i+1;
    auto execution=cpuTarget->getPimpl()->executeFrame(plan);
    if(execution.status!=CoinRenderBackendStatus::SUCCESS) throw std::runtime_error(execution.diagnostic);
    auto common=readTarget(*cpuTarget);
    bool hit[2][3]={}; float values[2][3]={};
    for(int mode=0;mode<2;++mode) {
      glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
      glPolygonMode(GL_FRONT_AND_BACK,GL_LINE);
      glBegin(mode==0?(ring.size()==4?GL_QUADS:GL_TRIANGLES):GL_LINE_LOOP);
      for(const auto & v:ring) glVertex3fv(v.position);
      glEnd();
      for(int p=0;p<3;++p) {
        uint8_t rgb[4]={}; int x=points[p][0],y=side-1-points[p][1];
        glReadPixels(x,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,rgb);
        glReadPixels(x,y,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,&values[mode][p]); hit[mode][p]=rgb[0]!=0;
      }
    }
    for(size_t e=0;e<result.indices.size();e+=2) {
      auto isolated=plan; isolated.draws.clear(); isolated.vertices.clear(); isolated.indices={0,1};
      isolated.renderStates={result.state};
      auto a=result.vertices[result.indices[e]].vertex, b=result.vertices[result.indices[e+1]].vertex;
      const auto authoredA=a, authoredB=b;
      if(std::lexicographical_compare(b.position,b.position+3,a.position,a.position+3)) std::swap(a,b);
      isolated.vertices={a,b}; CoinRenderDrawPacket edge;
      edge.topology=CoinRenderPrimitiveTopology::LINE_LIST; edge.geometry.vertexCount=2; edge.geometry.indexCount=2;
      isolated.draws.push_back(edge);
      if(!coin_render_expand_strokes(isolated,error)) throw std::runtime_error(error);
      isolated.revision=sourcePlan->revision+10000+i*16+e;
      auto edgeResult=cpuTarget->getPimpl()->executeFrame(isolated);
      if(edgeResult.status!=CoinRenderBackendStatus::SUCCESS) throw std::runtime_error(edgeResult.diagnostic);
      auto ec=readTarget(*cpuTarget);
      bool edgeHits[3][3]={}; float edgeDepths[3][3]={};
      for(int mode=0;mode<3;++mode) {
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT); glBegin(GL_LINES);
        glVertex3fv((mode==0?a:mode==1?authoredA:authoredB).position);
        glVertex3fv((mode==0?b:mode==1?authoredB:authoredA).position); glEnd();
        for(int p=0;p<3;++p) {
          uint8_t rgba[4]={};
          glReadPixels(points[p][0],side-1-points[p][1],1,1,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
          glReadPixels(points[p][0],side-1-points[p][1],1,1,GL_DEPTH_COMPONENT,GL_FLOAT,&edgeDepths[mode][p]);
          edgeHits[mode][p]=rgba[0]!=0;
        }
      }
      for(int p=0;p<3;++p) {
        int pixel=points[p][1]*side+points[p][0]; bool covered=ec.rgba[pixel*4]!=0;
        if(covered || edgeHits[0][p] || edgeHits[1][p] || edgeHits[2][p]) {
          edges<<i<<','<<e/2<<','<<points[p][0]<<','<<points[p][1]<<','<<covered<<','<<ec.depth[pixel];
          for(int mode=0;mode<3;++mode) edges<<','<<edgeHits[mode][p]<<','<<edgeDepths[mode][p];
          edges<<'\n';
        }
      }
    }
    for(int p=0;p<3;++p) {
      int pixel=points[p][1]*side+points[p][0]; bool covered=common.rgba[pixel*4]!=0;
      if(covered || hit[0][p] || hit[1][p])
        log<<i<<','<<points[p][0]<<','<<points[p][1]<<','<<covered<<','<<common.depth[pixel]
           <<','<<hit[0][p]<<','<<values[0][p]<<','<<hit[1][p]<<','<<values[1][p]<<'\n';
    }
  }
  glMatrixMode(GL_MODELVIEW); glPopMatrix(); glMatrixMode(GL_PROJECTION); glPopMatrix(); glPopAttrib();
  if(!log || !edges || !vertices || glGetError()!=GL_NO_ERROR) throw std::runtime_error("polygon probe failed");
  // The witness preserved normal color/depth before isolated probes modified GL.
}
