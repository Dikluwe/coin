#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "CoinRenderPortableApiFixture.h"
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoDrawStyle.h>
#include <Inventor/nodes/SoLineSet.h>
#include <Inventor/nodes/SoPointSet.h>
#include <array>

#if !defined(COIN_RENDER_ACTION_OPTIONS_VERSION) || COIN_RENDER_ACTION_OPTIONS_VERSION < 1
int main(){std::cout<<"portable smoothing API absent\n";return 78;}
#else
using namespace portable_api;
namespace {
struct Point {double x,y;};
using Polygon=std::vector<Point>;
Polygon clip(Polygon source,int axis,double boundary,bool greater) {
  Polygon out;if(source.empty())return out;
  const auto coordinate=[&](Point p){return axis?p.y:p.x;};
  Point previous=source.back();bool previousInside=greater?coordinate(previous)>=boundary:coordinate(previous)<=boundary;
  for(Point current:source) {
    const bool currentInside=greater?coordinate(current)>=boundary:coordinate(current)<=boundary;
    if(currentInside!=previousInside) {
      const double t=(boundary-coordinate(previous))/(coordinate(current)-coordinate(previous));
      out.push_back({previous.x+t*(current.x-previous.x),previous.y+t*(current.y-previous.y)});
    }
    if(currentInside)out.push_back(current);previous=current;previousInside=currentInside;
  }
  return out;
}
double polygonArea(const Polygon &polygon) {
  double sum=0;if(polygon.size()<3)return 0;
  for(size_t i=0;i<polygon.size();++i)sum+=polygon[i].x*polygon[(i+1)%polygon.size()].y-polygon[i].y*polygon[(i+1)%polygon.size()].x;
  return std::abs(sum)*.5;
}
double rectangleCoverage(Point a,Point b,double width,int x,int y) {
  const double length=std::hypot(b.x-a.x,b.y-a.y),dx=-(b.y-a.y)*width*.5/length,dy=(b.x-a.x)*width*.5/length;
  Polygon p={{a.x+dx,a.y+dy},{b.x+dx,b.y+dy},{b.x-dx,b.y-dy},{a.x-dx,a.y-dy}};
  p=clip(p,0,x,true);p=clip(p,0,x+1,false);p=clip(p,1,y,true);p=clip(p,1,y+1,false);
  return std::min(1.,polygonArea(p));
}
// Deterministic independent quadrature over the disk's vertical chord. The
// product does not receive these samples; it receives one point and its size.
double diskCoverage(Point center,double diameter,int x,int y) {
  const double radius=diameter*.5,left=std::max(double(x),center.x-radius),right=std::min(double(x+1),center.x+radius);
  if(right<=left)return 0;
  const auto chord=[&](double at) {
    const double half=std::sqrt(std::max(0.,radius*radius-(at-center.x)*(at-center.x)));
    return std::max(0.,std::min(double(y+1),center.y+half)-std::max(double(y),center.y-half));
  };
  const int intervals=2048;const double step=(right-left)/intervals;
  double sum=chord(left)+chord(right);
  for(int i=1;i<intervals;++i)sum+=(i%2?4:2)*chord(left+i*step);
  return std::min(1.,sum*step/3);
}
struct Stroke {
  SoSeparator *root=new SoSeparator;
  SoCoordinate3 *coordinates=new SoCoordinate3;
  SoMaterial *material=new SoMaterial;
  SoDrawStyle *style=new SoDrawStyle;
  SoDepthBuffer *depth=new SoDepthBuffer;
  bool point;
  Point a,b;double width;
  Stroke(bool isPoint,Point first,Point second,double size):point(isPoint),a(first),b(second),width(size) {
    root->ref();root->addChild(material);root->addChild(style);root->addChild(depth);root->addChild(coordinates);
    material->diffuseColor.setValue(.25f,.75f,.5f);material->transparency=.25f;
    style->lineWidth=float(width);style->pointSize=float(width);
    const SbVec3f points[]={SbVec3f(float(a.x/16-1),float(a.y/12-1),0),SbVec3f(float(b.x/16-1),float(b.y/12-1),0)};
    coordinates->point.setValues(0,isPoint?1:2,points);
    if(isPoint){auto *shape=new SoPointSet;shape->numPoints=1;root->addChild(shape);}
    else {auto *shape=new SoLineSet;shape->numVertices=2;root->addChild(shape);}
  }
  ~Stroke(){root->unref();}
  double coverage(int x,int y) const {return point?diskCoverage(a,width,x,y):rectangleCoverage(a,b,width,x,y);}
};
bool verify(const CoinRenderTarget &target,const Stroke &stroke,double alpha,const std::array<double,3> &color,bool write,const std::string &label) {
  auto image=read(target);if(!check(image.rgba.size()==32*24*4&&image.depth.size()==32*24,label+" complete readback"))return false;
  unsigned bad=0,partial=0,full=0;
  for(int y=0;y<24;++y)for(int x=0;x<32;++x) {
    const double coverage=stroke.coverage(x,y),effectiveAlpha=alpha*coverage;
    const size_t p=(23-y)*32+x;
    for(unsigned c=0;c<3;++c)bad+=std::abs(double(image.rgba[p*4+c])-255*color[c]*effectiveAlpha)>2;
    bad+=std::abs(double(image.rgba[p*4+3])-255*effectiveAlpha)>2;
    if(coverage>.02)bad+=std::abs(image.depth[p]-(write?.5f:1.f))>1e-5f;
    if(coverage==0)bad+=image.depth[p]!=1;
    partial+=coverage>.05&&coverage<.95;full+=coverage>.999;
  }
  std::cout<<"smooth oracle="<<label<<" wrong="<<bad<<" partial="<<partial<<" full="<<full<<'\n';
  return check(!bad&&partial>0&&full>0,label+" geometric area/color/alpha/depth oracle");
}
void one(Fixture &f,bool point,Point a,Point b,double width) {
  Scene scene;Stroke stroke(point,a,b,width);scene.root->addChild(stroke.root);
  f.action.setSmoothing(FALSE);f.apply(scene.root,"aliased initial");const auto aliased=read(*f.target);
  f.action.setSmoothing(TRUE);check(f.action.isSmoothing(),"smoothing enabled getter");f.apply(scene.root,"smooth current");
  verify(*f.target,stroke,.75,{{.25,.75,.5}},true,point?"disk":"line rectangle");
  const auto smooth=read(*f.target);check(smooth.rgba!=aliased.rgba,"smoothing changes actual subpixel coverage");
  stroke.depth->write=FALSE;stroke.material->transparency=.625f;stroke.material->diffuseColor.setValue(.8f,.2f,.4f);
  f.apply(scene.root,"smooth authored alpha/depth off");verify(*f.target,stroke,.375,{{.8,.2,.4}},false,"authored alpha/depth");
  stroke.depth->write=TRUE;stroke.material->transparency=.25f;stroke.material->diffuseColor.setValue(.25f,.75f,.5f);
  f.action.setSmoothing(FALSE);f.apply(scene.root,"aliased restored");const auto restored=read(*f.target);
  check(restored.rgba==aliased.rgba&&restored.depth==aliased.depth,"flag FALSE restores exact A/B/A old raster path");
  f.action.setSmoothing(TRUE);f.apply(scene.root,"smooth recovery");const auto recovered=read(*f.target);
  check(recovered.rgba==smooth.rgba&&recovered.depth==smooth.depth,"smooth A/B/A independent of previous alpha/depth state");
  f.action.setNumPasses(0);f.action.apply(scene.root);check(f.action.getLastStatus()!=CoinRenderAction::SUCCESS,"invalid smooth candidate rejects");
  unchanged(*f.target,recovered,"smooth rollback");f.action.setNumPasses(1);
}
}
int main(int argc,char **argv) {
  const bool gpu=argc==2&&std::string(argv[1])=="--gpu";
  if(argc!=2||(!gpu&&std::string(argv[1])!="--cpu"))return 2;
  SoDB::init();CoinRenderAction::initClass();Fixture f(gpu);if(!f.target)return gpu&&good()?77:1;
  check(!f.action.isSmoothing(),"smoothing default FALSE");f.action.setTransparencyType(CoinRenderAction::BLEND);f.action.setBackgroundColor(SbColor4f(0,0,0,0));
  one(f,false,{5.2,8.35},{25.7,8.35},2.6);
  one(f,false,{7.4,4.25},{24.1,19.6},3.2);
  one(f,false,{-3.2,12.35},{35.1,12.35},2.6);
  one(f,true,{15.35,11.7},{15.35,11.7},5.6);
  // Filled triangles retain their exact previous coverage and depth.
  Scene triangle;Quad quad;triangle.root->addChild(quad.root);
  f.action.setSmoothing(FALSE);f.apply(triangle.root,"filled initial");const auto old=read(*f.target);
  f.action.setSmoothing(TRUE);f.apply(triangle.root,"filled smoothing flag");const auto current=read(*f.target);
  check(current.rgba==old.rgba&&current.depth==old.depth,"smoothing flag does not smooth filled triangles");
  // A six-pixel aliased point has square corners; a smooth point is a disk.
  Scene points;Stroke point(true,{16,12},{16,12},6);points.root->addChild(point.root);
  point.material->diffuseColor.setValue(1,0,0);point.material->transparency=0;
  f.action.setSmoothing(FALSE);f.apply(points.root,"square point");const auto square=read(*f.target);
  f.action.setSmoothing(TRUE);f.apply(points.root,"disk point");const auto disk=read(*f.target);const size_t corner=((23-9)*32+13)*4;
  check(square.rgba.size()==32*24*4&&disk.rgba.size()==square.rgba.size()&&square.rgba[corner]>disk.rgba[corner]+64,
    "point smoothing uses disk instead of alpha-scaled square");
  Scene occlusion;Quad occluder(-1,0,-1,1,1);occluder.material->diffuseColor.setValue(0,0,1);
  Stroke behind(false,{5.2,12.35},{26.7,12.35},2.6);occlusion.root->addChild(occluder.root);occlusion.root->addChild(behind.root);
  f.apply(occlusion.root,"smoothed line behind opaque depth");const auto hidden=read(*f.target);unsigned bad=0;
  if(check(hidden.rgba.size()==32*24*4&&hidden.depth.size()==32*24,"smoothed occlusion image complete"))
    for(int y=0;y<24;++y)for(int x=0;x<32;++x) {
      const size_t p=(23-y)*32+x;
      if(x<16)bad+=hidden.rgba[p*4]!=0||hidden.rgba[p*4+1]!=0||hidden.rgba[p*4+2]!=255||std::abs(hidden.depth[p]-.25f)>1e-5f;
      else {
        const double coverage=behind.coverage(x,y),alpha=.75*coverage;
        const double color[]={.25,.75,.5};for(unsigned c=0;c<3;++c)bad+=std::abs(hidden.rgba[p*4+c]-255*color[c]*alpha)>2;
        bad+=std::abs(hidden.rgba[p*4+3]-255*alpha)>2;
        if(coverage>.02)bad+=std::abs(hidden.depth[p]-.5f)>1e-5f;
      }
    }
  check(!bad,"geometric smoothing preserves authored depth test against closer opaque surface");
  return good()?0:1;
}
#endif
