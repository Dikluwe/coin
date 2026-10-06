#include <Inventor/SoDB.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoMarkerSet.h>
#include <Inventor/nodes/SoCallback.h>
#include <Inventor/elements/SoViewingMatrixElement.h>
#include <Inventor/elements/SoProjectionMatrixElement.h>
#include <Inventor/system/gl.h>
#include <iostream>
#include <iomanip>
struct Probe { float x,y, raw[2]; double window[4]; };
void callback(void * p, SoAction * a) {
  if (!a->isOfType(SoGLRenderAction::getClassTypeId())) return;
  auto & q=*static_cast<Probe*>(p); glGetDoublev(GL_CURRENT_RASTER_POSITION,q.window);
  SbVec3f point(q.x-96,q.y-80,0);
  SbMatrix m=SoViewingMatrixElement::get(a->getState())*SoProjectionMatrixElement::get(a->getState());
  m.multVecMatrix(point,point); q.raw[0]=(point[0]+1)*.5f*192-4; q.raw[1]=(point[1]+1)*.5f*160-4;
}
int main() {
  SoDB::init(); SoOffscreenRenderer renderer(SbViewportRegion(192,160)); renderer.setComponents(SoOffscreenRenderer::RGB);
  auto * root=new SoSeparator; root->ref(); auto * camera=new SoOrthographicCamera;
  camera->height=160; camera->position.setValue(0,0,10); camera->nearDistance=1; camera->farDistance=40; root->addChild(camera);
  auto * coords=new SoCoordinate3; root->addChild(coords); auto * markers=new SoMarkerSet; markers->markerIndex=SoMarkerSet::SQUARE_FILLED_9_9; root->addChild(markers);
  Probe probe{}; auto * cb=new SoCallback; cb->setCallback(callback,&probe); root->addChild(cb);
  std::cout<<std::setprecision(12);
  for (float x: {16.f,32.f,48.f,64.f,80.f,96.f,112.f,128.f,144.f,160.f,15.75f,15.999f,15.99999f,16.00001f,16.001f,16.25f,16.75f,15.12345f,15.9997f,15.9995f,15.9993f,16.0003f,16.0005f,16.0007f,32.12345f,64.12345f,96.12345f,128.12345f,144.12345f}) {
    probe.x=x; probe.y=x; coords->point.setValue(x-96,x-80,0);
    if (!renderer.render(root)) return 1;
    auto * b=renderer.getBuffer(); int minx=192,miny=160;
    for(int y=0;y<160;++y)for(int xx=0;xx<192;++xx){auto i=(y*192+xx)*3;if(b[i]||b[i+1]||b[i+2]){if(xx<minx)minx=xx;if(y<miny)miny=y;}}
    std::cout<<"center="<<x<<" raw="<<probe.raw[0]<<","<<probe.raw[1]<<" window="<<probe.window[0]<<","<<probe.window[1]<<" covered_lower_left="<<minx<<","<<miny<<"\n";
  }
  root->unref();
}
